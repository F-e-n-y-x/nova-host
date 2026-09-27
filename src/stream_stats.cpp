/**
 * @file src/stream_stats.cpp
 * @brief Definitions for live stream telemetry and session history.
 */
// class header include
#include "stream_stats.h"

// standard includes
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>

// local includes
#include "config.h"
#include "logging.h"

using namespace std::literals;

namespace stream_stats {
  namespace {
    /**
     * @brief Microseconds between two time points, clamped to the uint32 range.
     *
     * @param d Duration.
     * @return Microseconds.
     */
    std::uint64_t to_us(clock::duration d) {
      const auto us = std::chrono::duration_cast<std::chrono::microseconds>(d).count();
      return us < 0 ? 0 : static_cast<std::uint64_t>(us);
    }

    /**
     * @brief Round to one decimal place so JSON numbers stay short.
     *
     * @param v Value.
     * @return Rounded value.
     */
    double round1(double v) {
      return std::round(v * 10.0) / 10.0;
    }

    /**
     * @brief Registry of live sessions and the cached history.
     */
    struct registry_t {
      std::mutex mutex;  ///< Guards the members below.
      std::map<const session_stats_t *, std::weak_ptr<session_stats_t>> live;  ///< Live sessions.
      std::vector<history_entry_t> history;  ///< Ended sessions, newest first.
      bool history_loaded = false;  ///< Whether `history` was read from disk.
    };

    /**
     * @brief The process-wide registry.
     *
     * @return Registry.
     */
    registry_t &registry() {
      static registry_t r;
      return r;
    }

    /**
     * @brief Current Unix time in seconds.
     *
     * @return Seconds since the epoch.
     */
    std::int64_t unix_now() {
      return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    /**
     * @brief Load the history once (caller holds the registry lock).
     *
     * @param r Registry.
     */
    void ensure_history_loaded(registry_t &r) {
      if (!r.history_loaded) {
        r.history = load_history(history_path());
        r.history_loaded = true;
      }
    }
  }  // namespace

  frame_sample_t make_frame_sample(
    std::optional<clock::time_point> captured,
    std::optional<clock::time_point> encode_start,
    std::optional<clock::time_point> encode_done,
    clock::time_point sent,
    std::size_t bytes,
    std::size_t packets,
    std::size_t fec_packets
  ) {
    frame_sample_t f;
    f.sent = sent;
    f.bytes = static_cast<std::uint32_t>(std::min<std::size_t>(bytes, UINT32_MAX));
    f.packets = static_cast<std::uint16_t>(std::min<std::size_t>(packets, UINT16_MAX));
    f.fec_packets = static_cast<std::uint16_t>(std::min<std::size_t>(std::min(fec_packets, packets), UINT16_MAX));
    f.duplicate = !captured.has_value();
    if (captured && encode_start && encode_done && *captured <= *encode_start && *encode_start <= *encode_done && *encode_done <= sent) {
      constexpr std::uint64_t max_us = UINT32_MAX;
      f.capture_us = static_cast<std::uint32_t>(std::min(to_us(*encode_start - *captured), max_us));
      f.encode_us = static_cast<std::uint32_t>(std::min(to_us(*encode_done - *encode_start), max_us));
      f.send_us = static_cast<std::uint32_t>(std::min(to_us(sent - *encode_done), max_us));
      f.timed = true;
    }
    return f;
  }

  latency_stat_t summarize_us(std::vector<std::uint32_t> &values_us) {
    latency_stat_t out;
    if (values_us.empty()) {
      return out;
    }
    std::uint64_t sum = 0;
    for (auto v : values_us) {
      sum += v;
    }
    out.avg = round1(static_cast<double>(sum) / static_cast<double>(values_us.size()) / 1000.0);
    // Nearest-rank p95: the smallest value with at least 95% of values at or below it.
    const auto rank = static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(values_us.size())));
    const auto idx = std::clamp<std::size_t>(rank, 1, values_us.size()) - 1;
    std::nth_element(values_us.begin(), values_us.begin() + idx, values_us.end());
    out.p95 = round1(values_us[idx] / 1000.0);
    return out;
  }

  std::string codec_name(int video_format) {
    switch (video_format) {
      case 1:
        return "HEVC";
      case 2:
        return "AV1";
      default:
        return "H.264";
    }
  }

  session_stats_t::session_stats_t(session_info_t info, clock::time_point started):
      info_ {std::move(info)},
      started_ {started},
      bucket_start_ {started} {
  }

  void session_stats_t::roll_seconds_locked(clock::time_point now) {
    // Close every whole second that has elapsed since the bucket started.
    while (now - bucket_start_ >= 1s) {
      second_sample_t s;
      s.t = info_.started_at + std::chrono::duration_cast<std::chrono::seconds>(bucket_start_ - started_ + 1s).count();
      s.bitrate_kbps = round1(static_cast<double>(bucket_bytes_) * 8.0 / 1000.0);
      s.fps = bucket_frames_;
      s.latency_ms = bucket_timed_ ? round1(static_cast<double>(bucket_latency_us_) / bucket_timed_ / 1000.0) : 0.0;
      if (bucket_loss_reported_) {
        const auto sent = bucket_packets_ + bucket_lost_;
        s.loss_pct = sent ? round1(100.0 * static_cast<double>(bucket_lost_) / static_cast<double>(sent)) : 0.0;
      }
      seconds_[second_head_] = s;
      second_head_ = (second_head_ + 1) % SECOND_RING_SIZE;
      second_count_ = std::min(second_count_ + 1, SECOND_RING_SIZE);

      bucket_start_ += 1s;
      bucket_bytes_ = 0;
      bucket_frames_ = 0;
      bucket_latency_us_ = 0;
      bucket_timed_ = 0;
      bucket_packets_ = 0;
      bucket_lost_ = 0;
      bucket_loss_reported_ = false;

      // After a long gap, jump ahead instead of emitting a long run of empty seconds.
      if (now - bucket_start_ > std::chrono::seconds {SECOND_RING_SIZE}) {
        bucket_start_ = now - std::chrono::seconds {SECOND_RING_SIZE};
      }
    }
  }

  void session_stats_t::record_frame(const frame_sample_t &frame) {
    std::lock_guard lock {mutex_};
    roll_seconds_locked(frame.sent);

    frames_[frame_head_] = frame;
    frame_head_ = (frame_head_ + 1) % FRAME_RING_SIZE;
    frame_count_ = std::min(frame_count_ + 1, FRAME_RING_SIZE);

    ++frames_total_;
    frames_duplicated_ += frame.duplicate ? 1 : 0;
    packets_total_ += frame.packets;
    bytes_total_ += frame.bytes;

    bucket_bytes_ += frame.bytes;
    ++bucket_frames_;
    bucket_packets_ += frame.packets;
    if (frame.timed) {
      const std::uint64_t total = std::uint64_t {frame.capture_us} + frame.encode_us + frame.send_us;
      bucket_latency_us_ += total;
      ++bucket_timed_;
      latency_sum_us_ += total;
      ++timed_total_;
    }
  }

  void session_stats_t::record_loss(std::uint32_t lost_packets, clock::time_point when) {
    std::lock_guard lock {mutex_};
    roll_seconds_locked(when);
    losses_[loss_head_] = {when, lost_packets};
    loss_head_ = (loss_head_ + 1) % losses_.size();
    loss_count_ = std::min(loss_count_ + 1, losses_.size());
    loss_ever_reported_ = true;
    bucket_lost_ += lost_packets;
    bucket_loss_reported_ = true;
  }

  void session_stats_t::set_rtt(std::uint32_t rtt_ms) {
    std::lock_guard lock {mutex_};
    rtt_ms_ = rtt_ms;
  }

  void session_stats_t::set_end_reason(std::string_view reason) {
    std::lock_guard lock {mutex_};
    if (end_reason_.empty()) {
      end_reason_ = reason;
    }
  }

  std::optional<double> session_stats_t::loss_pct_locked(clock::time_point now) const {
    if (!loss_ever_reported_) {
      return std::nullopt;
    }
    std::uint64_t lost = 0;
    for (std::size_t i = 0; i < loss_count_; ++i) {
      const auto &[when, count] = losses_[i];
      if (now - when <= LOSS_WINDOW) {
        lost += count;
      }
    }
    std::uint64_t sent = 0;
    for (std::size_t i = 0; i < frame_count_; ++i) {
      if (now - frames_[i].sent <= LOSS_WINDOW) {
        sent += frames_[i].packets;
      }
    }
    if (sent + lost == 0) {
      return 0.0;
    }
    return round1(100.0 * static_cast<double>(lost) / static_cast<double>(sent + lost));
  }

  snapshot_t session_stats_t::snapshot(clock::time_point now, bool with_samples) const {
    snapshot_t out;
    out.info = info_;

    std::vector<std::uint32_t> capture, encode, send, total;
    capture.reserve(frame_count_);
    encode.reserve(frame_count_);
    send.reserve(frame_count_);
    total.reserve(frame_count_);

    std::lock_guard lock {mutex_};
    std::uint64_t rate_bytes = 0;
    std::uint32_t rate_frames = 0;
    std::uint64_t rate_packets = 0;
    std::uint64_t rate_fec = 0;
    for (std::size_t i = 0; i < frame_count_; ++i) {
      const auto &f = frames_[i];
      const auto age = now - f.sent;
      if (age < 0s) {
        continue;
      }
      if (age <= RATE_WINDOW) {
        rate_bytes += f.bytes;
        ++rate_frames;
        rate_packets += f.packets;
        rate_fec += f.fec_packets;
      }
      if (f.timed && age <= LATENCY_WINDOW) {
        capture.push_back(f.capture_us);
        encode.push_back(f.encode_us);
        send.push_back(f.send_us);
        total.push_back(f.capture_us + f.encode_us + f.send_us);
      }
    }
    out.fps_actual = rate_frames;
    out.bitrate_kbps = round1(static_cast<double>(rate_bytes) * 8.0 / 1000.0);
    out.fec_pct = rate_packets ? round1(100.0 * static_cast<double>(rate_fec) / static_cast<double>(rate_packets)) : 0.0;
    out.capture = summarize_us(capture);
    out.encode = summarize_us(encode);
    out.send = summarize_us(send);
    out.total = summarize_us(total);
    out.loss_pct = loss_pct_locked(now);
    out.rtt_ms = rtt_ms_;
    out.frames_total = frames_total_;
    out.frames_duplicated = frames_duplicated_;
    out.packets_total = packets_total_;
    out.bytes_total = bytes_total_;

    if (with_samples) {
      out.samples.reserve(second_count_);
      const auto first = (second_head_ + SECOND_RING_SIZE - second_count_) % SECOND_RING_SIZE;
      for (std::size_t i = 0; i < second_count_; ++i) {
        out.samples.push_back(seconds_[(first + i) % SECOND_RING_SIZE]);
      }
    }
    return out;
  }

  history_entry_t session_stats_t::finish(clock::time_point now, std::int64_t unix_now) const {
    history_entry_t e;
    e.id = info_.id;
    e.client_cert = info_.client_cert;
    e.client_name = info_.client_name;
    e.app_name = info_.app_name;
    e.started_at = info_.started_at;
    e.video_format = info_.video_format;
    e.width = info_.width;
    e.height = info_.height;
    e.hdr = info_.hdr;

    std::lock_guard lock {mutex_};
    const auto elapsed = std::chrono::duration<double>(now - started_).count();
    e.duration_s = std::max<std::int64_t>(0, unix_now - info_.started_at);
    if (elapsed > 0) {
      e.avg_fps = round1(static_cast<double>(frames_total_) / elapsed);
      e.avg_bitrate_kbps = round1(static_cast<double>(bytes_total_) * 8.0 / 1000.0 / elapsed);
    }
    if (timed_total_) {
      e.avg_latency_ms = round1(static_cast<double>(latency_sum_us_) / static_cast<double>(timed_total_) / 1000.0);
    }
    e.end_reason = end_reason_.empty() ? "ended" : end_reason_;
    return e;
  }

  std::shared_ptr<session_stats_t> start_session(session_info_t info) {
    if (info.started_at == 0) {
      info.started_at = unix_now();
    }
    auto stats = std::make_shared<session_stats_t>(std::move(info), clock::now());
    auto &r = registry();
    std::lock_guard lock {r.mutex};
    r.live[stats.get()] = stats;
    return stats;
  }

  void end_session(const std::shared_ptr<session_stats_t> &stats) {
    if (!stats) {
      return;
    }
    auto entry = stats->finish(clock::now(), unix_now());
    auto &r = registry();
    std::vector<history_entry_t> copy;
    {
      std::lock_guard lock {r.mutex};
      if (r.live.erase(stats.get()) == 0) {
        return;  // already ended; never record a session twice
      }
      ensure_history_loaded(r);
      r.history.insert(r.history.begin(), std::move(entry));
      if (r.history.size() > HISTORY_LIMIT) {
        r.history.resize(HISTORY_LIMIT);
      }
      copy = r.history;
    }
    if (!config::sunshine.flags[config::flag::FRESH_STATE]) {
      save_history(history_path(), copy);
    }
  }

  std::vector<snapshot_t> active_sessions(bool with_samples) {
    std::vector<std::shared_ptr<session_stats_t>> live;
    {
      auto &r = registry();
      std::lock_guard lock {r.mutex};
      for (auto it = r.live.begin(); it != r.live.end();) {
        if (auto p = it->second.lock()) {
          live.push_back(std::move(p));
          ++it;
        } else {
          it = r.live.erase(it);
        }
      }
    }
    std::vector<snapshot_t> out;
    out.reserve(live.size());
    const auto now = clock::now();
    for (const auto &s : live) {
      out.push_back(s->snapshot(now, with_samples));
    }
    std::sort(out.begin(), out.end(), [](const snapshot_t &a, const snapshot_t &b) {
      return a.info.started_at < b.info.started_at;
    });
    return out;
  }

  std::vector<history_entry_t> history() {
    auto &r = registry();
    std::lock_guard lock {r.mutex};
    ensure_history_loaded(r);
    return r.history;
  }

  std::filesystem::path history_path() {
    const std::filesystem::path state {config::nvhttp.file_state};
    return state.parent_path() / "session_history.json";
  }

  nlohmann::json history_to_file_json(const history_entry_t &e) {
    nlohmann::json j;
    j["id"] = e.id;
    j["client_cert"] = e.client_cert;
    j["client_name"] = e.client_name;
    j["app_name"] = e.app_name;
    j["started_at"] = e.started_at;
    j["duration_s"] = e.duration_s;
    j["video_format"] = e.video_format;
    j["width"] = e.width;
    j["height"] = e.height;
    j["hdr"] = e.hdr;
    j["avg_fps"] = e.avg_fps;
    j["avg_bitrate_kbps"] = e.avg_bitrate_kbps;
    j["avg_latency_ms"] = e.avg_latency_ms ? nlohmann::json(*e.avg_latency_ms) : nlohmann::json(nullptr);
    j["end_reason"] = e.end_reason;
    return j;
  }

  std::optional<history_entry_t> history_from_file_json(const nlohmann::json &j) {
    if (!j.is_object() || !j.contains("started_at") || !j["started_at"].is_number_integer()) {
      return std::nullopt;
    }
    try {
      history_entry_t e;
      e.id = j.value("id", 0u);
      e.client_cert = j.value("client_cert", ""s);
      e.client_name = j.value("client_name", ""s);
      e.app_name = j.value("app_name", ""s);
      e.started_at = j["started_at"].get<std::int64_t>();
      e.duration_s = std::max<std::int64_t>(0, j.value("duration_s", std::int64_t {0}));
      e.video_format = std::clamp(j.value("video_format", 0), 0, 2);
      e.width = std::max(0, j.value("width", 0));
      e.height = std::max(0, j.value("height", 0));
      e.hdr = j.value("hdr", false);
      e.avg_fps = j.value("avg_fps", 0.0);
      e.avg_bitrate_kbps = j.value("avg_bitrate_kbps", 0.0);
      if (j.contains("avg_latency_ms") && j["avg_latency_ms"].is_number()) {
        e.avg_latency_ms = j["avg_latency_ms"].get<double>();
      }
      e.end_reason = j.value("end_reason", "ended"s);
      return e;
    } catch (const nlohmann::json::exception &) {
      return std::nullopt;
    }
  }

  std::vector<history_entry_t> load_history(const std::filesystem::path &path) {
    std::vector<history_entry_t> out;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
      return out;
    }
    try {
      std::ifstream in {path};
      auto j = nlohmann::json::parse(in);
      const auto &list = j.contains("sessions") ? j["sessions"] : j;
      if (!list.is_array()) {
        return out;
      }
      for (const auto &item : list) {
        if (auto e = history_from_file_json(item)) {
          out.push_back(std::move(*e));
          if (out.size() == HISTORY_LIMIT) {
            break;
          }
        }
      }
    } catch (const std::exception &e) {
      BOOST_LOG(warning) << "Ignoring unreadable session history "sv << path.string() << ": "sv << e.what();
      out.clear();
    }
    return out;
  }

  bool save_history(const std::filesystem::path &path, const std::vector<history_entry_t> &entries) {
    nlohmann::json list = nlohmann::json::array();
    for (const auto &e : entries) {
      list.push_back(history_to_file_json(e));
    }
    const nlohmann::json doc {{"version", 1}, {"sessions", list}};

    std::error_code ec;
    if (path.has_parent_path()) {
      std::filesystem::create_directories(path.parent_path(), ec);
    }
    auto tmp = path;
    tmp += ".tmp";
    {
      std::ofstream out {tmp, std::ios::trunc};
      if (!out) {
        BOOST_LOG(warning) << "Couldn't write session history "sv << tmp.string();
        return false;
      }
      out << doc.dump(2);
      if (!out) {
        return false;
      }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
      BOOST_LOG(warning) << "Couldn't save session history "sv << path.string() << ": "sv << ec.message();
      std::filesystem::remove(tmp, ec);
      return false;
    }
    return true;
  }

  nlohmann::json snapshot_to_api_json(const snapshot_t &s, const client_identity_t &who, bool with_samples) {
    nlohmann::json j;
    j["id"] = s.info.id;
    j["client_uuid"] = who.uuid.empty() ? nlohmann::json(nullptr) : nlohmann::json(who.uuid);
    j["client_name"] = who.name.empty() ? s.info.client_name : who.name;
    j["app_name"] = s.info.app_name;
    j["started_at"] = s.info.started_at;
    j["resolution"] = {{"w", s.info.width}, {"h", s.info.height}};
    j["fps_requested"] = s.info.fps_requested;
    j["fps_actual"] = s.fps_actual;
    j["codec"] = codec_name(s.info.video_format);
    j["hdr"] = s.info.hdr;
    j["bitrate_kbps"] = s.bitrate_kbps;
    j["latency_ms"] = {
      {"capture", s.capture.avg},
      {"encode", s.encode.avg},
      {"send", s.send.avg},
      {"total", s.total.avg},
      {"p95", {{"capture", s.capture.p95}, {"encode", s.encode.p95}, {"send", s.send.p95}, {"total", s.total.p95}}},
    };
    j["loss_pct"] = s.loss_pct ? nlohmann::json(*s.loss_pct) : nlohmann::json(nullptr);
    j["fec_pct"] = s.fec_pct;
    j["rtt_ms"] = s.rtt_ms ? nlohmann::json(*s.rtt_ms) : nlohmann::json(nullptr);
    j["frames"] = {{"total", s.frames_total}, {"duplicated", s.frames_duplicated}};
    j["packets_total"] = s.packets_total;
    j["bytes_total"] = s.bytes_total;
    if (with_samples) {
      nlohmann::json samples = nlohmann::json::array();
      for (const auto &x : s.samples) {
        samples.push_back({
          {"t", x.t},
          {"bitrate_kbps", x.bitrate_kbps},
          {"fps", x.fps},
          {"latency_ms", x.latency_ms},
          {"loss_pct", x.loss_pct ? nlohmann::json(*x.loss_pct) : nlohmann::json(nullptr)},
        });
      }
      j["samples"] = std::move(samples);
    }
    return j;
  }

  nlohmann::json history_to_api_json(const history_entry_t &e, const client_identity_t &who) {
    nlohmann::json j;
    j["id"] = e.id;
    j["client_uuid"] = who.uuid.empty() ? nlohmann::json(nullptr) : nlohmann::json(who.uuid);
    j["client_name"] = who.name.empty() ? e.client_name : who.name;
    j["app_name"] = e.app_name;
    j["started_at"] = e.started_at;
    j["duration_s"] = e.duration_s;
    j["codec"] = codec_name(e.video_format);
    j["resolution"] = {{"w", e.width}, {"h", e.height}};
    j["hdr"] = e.hdr;
    j["avg_fps"] = e.avg_fps;
    j["avg_bitrate_kbps"] = e.avg_bitrate_kbps;
    j["avg_latency_ms"] = e.avg_latency_ms ? nlohmann::json(*e.avg_latency_ms) : nlohmann::json(nullptr);
    j["end_reason"] = e.end_reason;
    return j;
  }

#ifdef SUNSHINE_TESTS
  namespace test_support {
    void reset() {
      auto &r = registry();
      std::lock_guard lock {r.mutex};
      r.live.clear();
      r.history.clear();
      r.history_loaded = false;
    }
  }  // namespace test_support
#endif
}  // namespace stream_stats
