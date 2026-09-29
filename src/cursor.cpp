/**
 * @file src/cursor.cpp
 * @brief Local cursor: wire codec, per-session sender and the shared cursor watcher.
 *        See cursor.h for the protocol contract.
 */
// standard includes
#include <algorithm>
#include <array>
#include <string_view>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <thread>

// local includes
#include "config.h"
#include "cursor.h"
#include "globals.h"
#include "logging.h"
#include "virtual_display.h"

using namespace std::literals;

namespace cursor {

  namespace {
    void put_u16(std::vector<std::uint8_t> &out, std::size_t at, std::uint16_t v) {
      out[at] = v & 0xFF;
      out[at + 1] = (v >> 8) & 0xFF;
    }

    void put_u32(std::vector<std::uint8_t> &out, std::size_t at, std::uint32_t v) {
      for (int i = 0; i < 4; ++i) {
        out[at + i] = (v >> (8 * i)) & 0xFF;
      }
    }

    /**
     * @brief Build one packet header followed by @p chunk_size pixel bytes of space.
     */
    std::vector<std::uint8_t> header(std::uint8_t flags, std::uint32_t id, const image_t *image, std::uint32_t offset, std::uint16_t chunk_size) {
      std::vector<std::uint8_t> out(kHeaderSize + chunk_size, 0);
      out[0] = kWireVersion;
      out[1] = flags;
      put_u16(out, 2, kHeaderSize);
      put_u32(out, 4, id);
      if (image) {
        put_u16(out, 8, image->width);
        put_u16(out, 10, image->height);
        put_u16(out, 12, (std::uint16_t) image->hotspot_x);
        put_u16(out, 14, (std::uint16_t) image->hotspot_y);
        put_u32(out, 16, (std::uint32_t) image->bgra.size());
        put_u32(out, 20, offset);
      }
      put_u16(out, 24, chunk_size);
      // 26..27 reserved, zero
      return out;
    }

    bool valid_shape(const image_t &image) {
      return image.width >= 1 && image.width <= kMaxDimension && image.height >= 1 && image.height <= kMaxDimension &&
             image.hotspot_x >= 0 && image.hotspot_x < image.width && image.hotspot_y >= 0 && image.hotspot_y < image.height &&
             image.bgra.size() == (std::size_t) image.width * image.height * 4;
    }
  }  // namespace

  std::optional<mode_e> parse_mode_request(const std::uint8_t *data, std::size_t size) {
    // V+ sends exactly {version, mode, 0, 0}; accept a longer payload from a newer client.
    if (!data || size < 2 || data[0] != kWireVersion) {
      return std::nullopt;
    }
    switch (data[1]) {
      case 0:
        return mode_e::video;
      case 1:
        return mode_e::local;
      default:
        return std::nullopt;
    }
  }

  std::optional<image_t> from_premultiplied_argb(const std::uint32_t *argb, int width, int height, int hotspot_x, int hotspot_y) {
    if (!argb || width <= 0 || height <= 0 || width > 4096 || height > 4096) {
      return std::nullopt;
    }
    image_t image;
    image.width = (std::uint16_t) width;
    image.height = (std::uint16_t) height;
    image.hotspot_x = (std::int16_t) std::clamp(hotspot_x, 0, width - 1);
    image.hotspot_y = (std::int16_t) std::clamp(hotspot_y, 0, height - 1);
    image.bgra.resize((std::size_t) width * height * 4);
    for (std::size_t i = 0; i < (std::size_t) width * height; ++i) {
      std::uint32_t p = argb[i];
      std::uint32_t a = (p >> 24) & 0xFF;
      std::uint32_t r = (p >> 16) & 0xFF;
      std::uint32_t g = (p >> 8) & 0xFF;
      std::uint32_t b = p & 0xFF;
      if (a == 0) {
        r = g = b = 0;
      } else if (a < 255) {
        r = std::min<std::uint32_t>(255, (r * 255 + a / 2) / a);
        g = std::min<std::uint32_t>(255, (g * 255 + a / 2) / a);
        b = std::min<std::uint32_t>(255, (b * 255 + a / 2) / a);
      }
      auto *o = &image.bgra[i * 4];
      o[0] = (std::uint8_t) b;
      o[1] = (std::uint8_t) g;
      o[2] = (std::uint8_t) r;
      o[3] = (std::uint8_t) a;
    }
    return image;
  }

  image_t fallback_arrow(int height) {
    // The classic 12 x 19 arrow: '#' outline, '.' fill, ' ' transparent.
    static constexpr std::array<std::string_view, 19> rows {
      "#           ",
      "##          ",
      "#.#         ",
      "#..#        ",
      "#...#       ",
      "#....#      ",
      "#.....#     ",
      "#......#    ",
      "#.......#   ",
      "#........#  ",
      "#.........# ",
      "#......#####",
      "#...#..#    ",
      "#..# #..#   ",
      "#.#  #..#   ",
      "##    #..#  ",
      "#     #..#  ",
      "       #..# ",
      "       ###  ",
    };
    image_t base;
    base.width = (std::uint16_t) rows[0].size();
    base.height = (std::uint16_t) rows.size();
    base.bgra.resize((std::size_t) base.width * base.height * 4, 0);
    for (std::size_t y = 0; y < rows.size(); ++y) {
      for (std::size_t x = 0; x < rows[y].size(); ++x) {
        const char c = rows[y][x];
        if (c == ' ') {
          continue;
        }
        const std::uint8_t v = c == '#' ? 0 : 255;
        auto *o = &base.bgra[(y * base.width + x) * 4];
        o[0] = o[1] = o[2] = v;
        o[3] = 255;
      }
    }
    const int want = std::clamp(height > 0 ? height : 24, 8, (int) kMaxDimension);
    // The arrow fills about 80% of a themed cursor's box, as in the usual left_ptr.
    return fit(base, want * 0.8 / base.height);
  }

  bool is_blank(const image_t &image) {
    for (std::size_t i = 3; i < image.bgra.size(); i += 4) {
      if (image.bgra[i] != 0) {
        return false;
      }
    }
    return true;
  }

  image_t fit(const image_t &image, double scale) {
    if (!(scale > 0) || !std::isfinite(scale)) {
      scale = 1.0;
    }
    if (image.width == 0 || image.height == 0) {
      return image;
    }
    scale = std::min({scale, (double) kMaxDimension / image.width, (double) kMaxDimension / image.height});
    int dw = std::clamp((int) std::lround(image.width * scale), 1, (int) kMaxDimension);
    int dh = std::clamp((int) std::lround(image.height * scale), 1, (int) kMaxDimension);
    if (dw == image.width && dh == image.height) {
      return image;
    }

    const double sx = (double) image.width / dw;
    const double sy = (double) image.height / dh;
    image_t out;
    out.width = (std::uint16_t) dw;
    out.height = (std::uint16_t) dh;
    out.hotspot_x = (std::int16_t) std::clamp((int) std::floor(image.hotspot_x / sx), 0, dw - 1);
    out.hotspot_y = (std::int16_t) std::clamp((int) std::floor(image.hotspot_y / sy), 0, dh - 1);
    out.bgra.resize((std::size_t) dw * dh * 4);

    for (int y = 0; y < dh; ++y) {
      const double y0 = y * sy;
      const double y1 = y0 + sy;
      for (int x = 0; x < dw; ++x) {
        const double x0 = x * sx;
        const double x1 = x0 + sx;
        double acc[4] = {0, 0, 0, 0};  // premultiplied b, g, r and alpha
        double area = 0;
        for (int iy = (int) y0; iy < std::min<int>(image.height, (int) std::ceil(y1)); ++iy) {
          const double wy = std::min<double>(iy + 1, y1) - std::max<double>(iy, y0);
          if (wy <= 0) {
            continue;
          }
          for (int ix = (int) x0; ix < std::min<int>(image.width, (int) std::ceil(x1)); ++ix) {
            const double wx = std::min<double>(ix + 1, x1) - std::max<double>(ix, x0);
            if (wx <= 0) {
              continue;
            }
            const double w = wx * wy;
            const auto *p = &image.bgra[((std::size_t) iy * image.width + ix) * 4];
            const double a = p[3] / 255.0;
            acc[0] += p[0] * a * w;
            acc[1] += p[1] * a * w;
            acc[2] += p[2] * a * w;
            acc[3] += p[3] * w;
            area += w;
          }
        }
        auto *o = &out.bgra[((std::size_t) y * dw + x) * 4];
        if (area <= 0 || acc[3] <= 0) {
          o[0] = o[1] = o[2] = o[3] = 0;
          continue;
        }
        const double alpha = acc[3] / area;  // 0..255
        const double straight = 255.0 / acc[3];  // undo premultiplication: sum(c*a) / sum(a)
        o[0] = (std::uint8_t) std::clamp(std::lround(acc[0] * straight), 0L, 255L);
        o[1] = (std::uint8_t) std::clamp(std::lround(acc[1] * straight), 0L, 255L);
        o[2] = (std::uint8_t) std::clamp(std::lround(acc[2] * straight), 0L, 255L);
        o[3] = (std::uint8_t) std::clamp(std::lround(alpha), 0L, 255L);
      }
    }
    return out;
  }

  std::uint32_t shape_id(const image_t &image) {
    // FNV-1a over the header fields and pixels, folded to 32 bits.
    std::uint64_t h = 0xcbf29ce484222325ULL;
    auto mix = [&h](std::uint8_t b) {
      h ^= b;
      h *= 0x100000001b3ULL;
    };
    for (std::uint16_t v : {image.width, image.height, (std::uint16_t) image.hotspot_x, (std::uint16_t) image.hotspot_y}) {
      mix(v & 0xFF);
      mix(v >> 8);
    }
    for (auto b : image.bgra) {
      mix(b);
    }
    auto id = (std::uint32_t) (h ^ (h >> 32));
    return id ? id : 1;
  }

  std::vector<std::vector<std::uint8_t>> encode_shape(std::uint32_t id, const image_t &image, bool visible, std::size_t chunk_bytes) {
    std::vector<std::vector<std::uint8_t>> packets;
    if (!valid_shape(image) || chunk_bytes == 0) {
      return packets;
    }
    chunk_bytes = std::min<std::size_t>(chunk_bytes, 0xFFFF);
    const std::uint8_t flags = kFlagShape | (visible ? kFlagVisible : 0);
    for (std::size_t offset = 0; offset < image.bgra.size(); offset += chunk_bytes) {
      auto n = (std::uint16_t) std::min(chunk_bytes, image.bgra.size() - offset);
      auto packet = header(flags, id, &image, (std::uint32_t) offset, n);
      std::copy_n(image.bgra.begin() + offset, n, packet.begin() + kHeaderSize);
      packets.push_back(std::move(packet));
    }
    return packets;
  }

  std::vector<std::uint8_t> encode_state(std::uint32_t id, bool visible) {
    return header(visible ? kFlagVisible : 0, id, nullptr, 0, 0);
  }

  // ---- sender_t ----------------------------------------------------------

  void sender_t::set_mode(mode_e mode) {
    if (mode == mode_e::local && mode_ != mode_e::local) {
      // The client may have evicted shapes while in video mode; start from a clean slate.
      known_.clear();
      known_index_.clear();
      known_bytes_ = 0;
      shown_id_.reset();
      shown_visible_.reset();
    }
    mode_ = mode;
    dirty_ = true;
  }

  bool sender_t::known(std::uint32_t id) {
    auto it = known_index_.find(id);
    if (it == known_index_.end()) {
      return false;
    }
    known_.splice(known_.begin(), known_, it->second);
    return true;
  }

  void sender_t::remember(std::uint32_t id, std::size_t bytes) {
    known_.emplace_front(id, bytes);
    known_index_[id] = known_.begin();
    known_bytes_ += bytes;
    while (known_bytes_ > kClientCacheBudget && known_.size() > 1) {
      known_bytes_ -= known_.back().second;
      known_index_.erase(known_.back().first);
      known_.pop_back();
    }
  }

  std::vector<std::vector<std::uint8_t>> sender_t::update(const snapshot_t &snapshot, double scale) {
    std::vector<std::vector<std::uint8_t>> out;
    if (mode_ != mode_e::local || snapshot.serial == 0) {
      return out;
    }
    if (!dirty_ && snapshot.serial == last_serial_ && scale == last_scale_) {
      return out;
    }
    dirty_ = false;
    last_serial_ = snapshot.serial;
    last_scale_ = scale;

    if (!snapshot.visible || !snapshot.image) {
      if (shown_visible_ != false) {
        out.push_back(encode_state(shown_id_.value_or(0), false));
        shown_visible_ = false;
      }
      return out;
    }

    if (snapshot.image != scaled_source_ || scale != scaled_for_) {
      scaled_ = fit(*snapshot.image, scale);
      scaled_id_ = shape_id(scaled_);
      scaled_source_ = snapshot.image;
      scaled_for_ = scale;
    }

    if (shown_id_ == scaled_id_ && shown_visible_ == true) {
      return out;
    }
    if (known(scaled_id_)) {
      out.push_back(encode_state(scaled_id_, true));
    } else {
      out = encode_shape(scaled_id_, scaled_, true);
      if (out.empty()) {
        return out;
      }
      remember(scaled_id_, scaled_.bgra.size());
    }
    shown_id_ = scaled_id_;
    shown_visible_ = true;
    return out;
  }

  // ---- shared watcher ------------------------------------------------------

  namespace {
    std::mutex state_mutex;  ///< Guards everything below.
    int refs = 0;  ///< Sessions in local mode.
    bool user_composite = true;  ///< The user's own cursor toggle.
    bool source_ok = false;  ///< The watcher is delivering shapes.
    snapshot_t snapshot;  ///< Latest published cursor.
    std::uint64_t serial_counter = 0;  ///< Monotonic, so a restart never repeats a serial.
    std::function<target_t()> target_provider;
    source_factory_t source_factory;
    std::jthread watcher;
    std::atomic<double> scale {1.0};

    /**
     * @brief Update the capture's cursor flag. The video keeps the cursor unless a session draws it
     *        locally and the watcher actually delivers shapes (so a failing source never leaves the
     *        user without any cursor).
     */
    void apply_composite_locked() {
      bool composite = user_composite && !(refs > 0 && source_ok);
      if (display_cursor != composite) {
        BOOST_LOG(info) << "cursor: "sv << (composite ? "drawing the cursor into the video"sv : "leaving the cursor out of the video (drawn by the client)"sv);
      }
      display_cursor = composite;
    }

    /**
     * @brief The display capture uses now: the session's Virtual display X server (`:N`) while
     *        one runs, else the desktop (`$DISPLAY`).
     */
    target_t capture_display() {
      if (auto vd = virtual_display::capture_target()) {
        return {vd->display, vd->xauthority};
      }
      return {};
    }

    target_t current_target_locked() {
      return target_provider ? target_provider() : capture_display();
    }

    std::unique_ptr<source_t> open_source(const target_t &target) {
      source_factory_t factory;
      {
        std::lock_guard lg {state_mutex};
        factory = source_factory;
      }
      return factory ? factory(target) : make_x11_source(target);
    }

    void publish(std::optional<image_t> image) {
      std::lock_guard lg {state_mutex};
      snapshot_t next;
      next.serial = ++serial_counter;
      if (image && !is_blank(*image)) {
        next.image = std::make_shared<const image_t>(std::move(*image));
        next.visible = true;
      }
      snapshot = std::move(next);
      if (!source_ok) {
        source_ok = true;
        apply_composite_locked();
      }
    }

    void set_broken() {
      std::lock_guard lg {state_mutex};
      if (source_ok) {
        source_ok = false;
        apply_composite_locked();
      }
      snapshot = {};
    }

    void watch(std::stop_token stop) {
      std::mutex wait_mutex;
      std::condition_variable_any wait_cv;
      auto sleep = [&](std::chrono::milliseconds d) {
        std::unique_lock ul {wait_mutex};
        wait_cv.wait_for(ul, stop, d, [] {
          return false;
        });
      };

      bool warned = false;
      while (!stop.stop_requested()) {
        target_t target;
        {
          std::lock_guard lg {state_mutex};
          target = current_target_locked();
        }
        auto source = open_source(target);
        if (!source) {
          if (!warned) {
            warned = true;
            BOOST_LOG(warning) << "cursor: can't read the cursor on display ["sv << (target.display.empty() ? "$DISPLAY"s : target.display) << "]; the video keeps it"sv;
          }
          set_broken();
          sleep(2s);
          continue;
        }
        warned = false;
        BOOST_LOG(info) << "cursor: watching cursor shapes on display ["sv << (target.display.empty() ? "$DISPLAY"s : target.display) << ']';

        auto last_target_check = std::chrono::steady_clock::now();
        while (!stop.stop_requested()) {
          bool broken = false;
          auto image = source->poll(100ms, broken);
          if (broken) {
            BOOST_LOG(warning) << "cursor: lost the X display; retrying"sv;
            set_broken();
            break;
          }
          if (image) {
            publish(std::move(image));
          }
          auto now = std::chrono::steady_clock::now();
          if (now - last_target_check >= 1s) {
            last_target_check = now;
            std::lock_guard lg {state_mutex};
            if (!(current_target_locked() == target)) {
              BOOST_LOG(info) << "cursor: capture moved to another display; following it"sv;
              break;
            }
          }
        }
      }
    }
  }  // namespace

#ifndef SUNSHINE_BUILD_X11
  std::unique_ptr<source_t> make_x11_source(const target_t &) {
    return nullptr;
  }
#endif

  bool available() {
    if (!config::input.local_cursor) {
      return false;
    }
    {
      std::lock_guard lg {state_mutex};
      if (source_ok) {
        return true;
      }
    }
    target_t target;
    {
      std::lock_guard lg {state_mutex};
      target = current_target_locked();
    }
    return open_source(target) != nullptr;
  }

  void acquire() {
    std::lock_guard lg {state_mutex};
    if (refs++ == 0) {
      watcher = std::jthread(watch);
    }
    apply_composite_locked();
  }

  void release() {
    std::jthread stopping;
    {
      std::lock_guard lg {state_mutex};
      if (refs == 0) {
        return;
      }
      if (--refs == 0) {
        stopping = std::move(watcher);
      }
      apply_composite_locked();
    }
    if (stopping.joinable()) {
      stopping.request_stop();
      stopping.join();
      std::lock_guard lg {state_mutex};
      if (refs == 0) {
        source_ok = false;
        snapshot = {};
        apply_composite_locked();
      }
    }
  }

  void release_all() {
    std::jthread stopping;
    {
      std::lock_guard lg {state_mutex};
      refs = 0;
      stopping = std::move(watcher);
    }
    if (stopping.joinable()) {
      stopping.request_stop();
      stopping.join();
    }
    std::lock_guard lg {state_mutex};
    source_ok = false;
    snapshot = {};
    apply_composite_locked();
  }

  bool local_active() {
    std::lock_guard lg {state_mutex};
    return refs > 0;
  }

  snapshot_t current() {
    std::lock_guard lg {state_mutex};
    return snapshot;
  }

  void set_video_scale(double s) {
    if (s > 0 && std::isfinite(s)) {
      scale.store(s, std::memory_order_relaxed);
    }
  }

  double video_scale() {
    return scale.load(std::memory_order_relaxed);
  }

  void toggle_user_composite() {
    std::lock_guard lg {state_mutex};
    user_composite = !user_composite;
    apply_composite_locked();
  }

  void set_target_provider(std::function<target_t()> provider) {
    std::lock_guard lg {state_mutex};
    target_provider = std::move(provider);
  }

  void set_source_factory(source_factory_t factory) {
    std::lock_guard lg {state_mutex};
    source_factory = std::move(factory);
  }

}  // namespace cursor
