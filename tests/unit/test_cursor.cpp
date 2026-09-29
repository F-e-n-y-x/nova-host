/**
 * @file tests/unit/test_cursor.cpp
 * @brief Local cursor (control message 21) tests: wire format against the V+ client's golden
 *        vectors and reassembly rules, image conversion and scaling, the per-session sender and
 *        the shared watcher (src/cursor.h).
 */
#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <string_view>
#include <thread>

#include "src/config.h"
#include "src/cursor.h"
#include "src/globals.h"
#include "src/virtual_display.h"

#include "../tests_common.h"

// Last: Xlib defines macros (None, Success, ...) that must not reach the headers above.
#include "platform/linux/x_cursor_owner.h"

using namespace std::literals;

namespace {

  /**
   * @brief Mirror of the client's reassembly (V+ moonlight-common-c `CursorStream.c`): the same
   *        checks, so every packet the host builds is one the client accepts.
   */
  struct client_t {
    struct update_t {
      std::uint8_t flags = 0;
      std::uint32_t id = 0;
      std::uint16_t width = 0, height = 0;
      std::int16_t hx = 0, hy = 0;
      std::vector<std::uint8_t> pixels;
    };

    std::vector<update_t> updates;
    int rejected = 0;

    void push(const std::vector<std::uint8_t> &p) {
      auto u16 = [&](std::size_t at) {
        return (std::uint16_t) (p[at] | (p[at + 1] << 8));
      };
      auto u32 = [&](std::size_t at) {
        return (std::uint32_t) p[at] | ((std::uint32_t) p[at + 1] << 8) | ((std::uint32_t) p[at + 2] << 16) | ((std::uint32_t) p[at + 3] << 24);
      };
      auto reject = [&] {
        rejected++;
        partial.reset();
      };
      if (p.size() < 28) {
        return;
      }
      std::uint8_t flags = p[1];
      if (p[0] != 1 || (flags & ~0x03) != 0) {
        return reject();
      }
      std::uint16_t header = u16(2), width = u16(8), height = u16(10), chunk = u16(24);
      std::int16_t hx = (std::int16_t) u16(12), hy = (std::int16_t) u16(14);
      std::uint32_t id = u32(4), total = u32(16), offset = u32(20);
      if (header != 28 || chunk != p.size() - header) {
        return reject();
      }
      if (!(flags & 0x01)) {
        if (width || height || total || offset || chunk) {
          return reject();
        }
        partial.reset();
        updates.push_back({flags, id});
        return;
      }
      if (width == 0 || width > 256 || height == 0 || height > 256 || hx < 0 || hx >= (std::int16_t) width || hy < 0 || hy >= (std::int16_t) height ||
          total == 0 || total > 256 * 256 * 4 || total != (std::uint32_t) width * height * 4 || chunk == 0 || offset > total || chunk > total - offset) {
        return reject();
      }
      if (offset == 0) {
        partial = update_t {flags, id, width, height, hx, hy, {}};
      }
      if (!partial || partial->id != id || partial->flags != flags || partial->width != width || partial->height != height || partial->hx != hx ||
          partial->hy != hy || partial->pixels.size() != offset) {
        return reject();
      }
      partial->pixels.insert(partial->pixels.end(), p.begin() + header, p.end());
      if (partial->pixels.size() == total) {
        updates.push_back(std::move(*partial));
        partial.reset();
      }
    }

    void push_all(const std::vector<std::vector<std::uint8_t>> &packets) {
      for (const auto &p : packets) {
        push(p);
      }
    }

  private:
    std::optional<update_t> partial;
  };

  cursor::image_t solid(int w, int h, std::uint8_t b, std::uint8_t g, std::uint8_t r, std::uint8_t a, int hx = 0, int hy = 0) {
    cursor::image_t img;
    img.width = (std::uint16_t) w;
    img.height = (std::uint16_t) h;
    img.hotspot_x = (std::int16_t) hx;
    img.hotspot_y = (std::int16_t) hy;
    for (int i = 0; i < w * h; ++i) {
      img.bgra.insert(img.bgra.end(), {b, g, r, a});
    }
    return img;
  }

  cursor::snapshot_t snap(std::uint64_t serial, std::optional<cursor::image_t> image) {
    cursor::snapshot_t s;
    s.serial = serial;
    if (image) {
      s.image = std::make_shared<const cursor::image_t>(std::move(*image));
      s.visible = true;
    }
    return s;
  }

}  // namespace

// ---- wire format ------------------------------------------------------------

TEST(CursorWire, VisibilityMatchesVPlusGoldenVector) {
  // V+ CursorStreamGoldenTest.c testVisibilityOnly
  const std::vector<std::uint8_t> golden {
    0x01, 0x02, 0x1c, 0x00, 0x44, 0x33, 0x22, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };
  EXPECT_EQ(cursor::encode_state(0x11223344, true), golden);

  auto hidden = cursor::encode_state(0x11223344, false);
  EXPECT_EQ(hidden[1], 0x00);
}

TEST(CursorWire, SingleChunkShapeMatchesVPlusGoldenVector) {
  // V+ CursorStreamGoldenTest.c testSingleChunkShape: 2x1, hotspot (1,0), not visible.
  const std::vector<std::uint8_t> golden {
    0x01, 0x01, 0x1c, 0x00, 0x04, 0x03, 0x02, 0x01, 0x02, 0x00, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
  };
  cursor::image_t img;
  img.width = 2;
  img.height = 1;
  img.hotspot_x = 1;
  img.hotspot_y = 0;
  img.bgra = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};

  auto packets = cursor::encode_shape(0x01020304, img, false);
  ASSERT_EQ(packets.size(), 1u);
  EXPECT_EQ(packets[0], golden);
}

TEST(CursorWire, MultiChunkShapeReassemblesLikeTheClient) {
  // Same shape as V+ testMultiChunkShape: 2x2, hotspot (0,1), visible.
  cursor::image_t img;
  img.width = 2;
  img.height = 2;
  img.hotspot_x = 0;
  img.hotspot_y = 1;
  for (std::uint8_t i = 0; i < 16; ++i) {
    img.bgra.push_back(i);
  }

  auto packets = cursor::encode_shape(0xaabbccdd, img, true, 6);
  ASSERT_EQ(packets.size(), 3u);  // 6 + 6 + 4 bytes
  // Second chunk header: offset 6, chunk size 6; flags and shape fields repeat.
  EXPECT_EQ(packets[1][1], 0x03);
  EXPECT_EQ(packets[1][20], 6);
  EXPECT_EQ(packets[1][24], 6);
  EXPECT_EQ(packets[2][20], 12);
  EXPECT_EQ(packets[2][24], 4);
  EXPECT_EQ(packets[2].size(), 28u + 4u);

  client_t client;
  client.push_all(packets);
  EXPECT_EQ(client.rejected, 0);
  ASSERT_EQ(client.updates.size(), 1u);
  EXPECT_EQ(client.updates[0].flags, 0x03);
  EXPECT_EQ(client.updates[0].id, 0xaabbccddu);
  EXPECT_EQ(client.updates[0].hy, 1);
  EXPECT_EQ(client.updates[0].pixels, img.bgra);
}

TEST(CursorWire, LargestShapeFitsControlFrames) {
  auto img = solid(256, 256, 1, 2, 3, 255, 255, 255);
  auto packets = cursor::encode_shape(7, img, true);
  EXPECT_EQ(packets.size(), 256u * 256u * 4u / cursor::kChunkBytes);
  for (const auto &p : packets) {
    EXPECT_LE(p.size(), 65500u - 4u);  // control header v2 is 4 bytes
  }
  client_t client;
  client.push_all(packets);
  EXPECT_EQ(client.rejected, 0);
  ASSERT_EQ(client.updates.size(), 1u);
  EXPECT_EQ(client.updates[0].pixels, img.bgra);
}

TEST(CursorWire, RejectsShapesTheClientWouldDrop) {
  EXPECT_TRUE(cursor::encode_shape(1, cursor::image_t {}, true).empty());
  EXPECT_TRUE(cursor::encode_shape(1, solid(257, 1, 0, 0, 0, 255), true).empty());
  EXPECT_TRUE(cursor::encode_shape(1, solid(4, 4, 0, 0, 0, 255, 4, 0), true).empty());  // hotspot outside
  auto short_pixels = solid(2, 2, 0, 0, 0, 255);
  short_pixels.bgra.pop_back();
  EXPECT_TRUE(cursor::encode_shape(1, short_pixels, true).empty());
  EXPECT_TRUE(cursor::encode_shape(1, solid(2, 2, 0, 0, 0, 255), true, 0).empty());
}

TEST(CursorWire, ParsesModeRequests) {
  const std::uint8_t local[] = {1, 1, 0, 0};
  const std::uint8_t video[] = {1, 0, 0, 0};
  const std::uint8_t bad_version[] = {2, 1, 0, 0};
  const std::uint8_t bad_mode[] = {1, 2, 0, 0};
  EXPECT_EQ(cursor::parse_mode_request(local, sizeof(local)), cursor::mode_e::local);
  EXPECT_EQ(cursor::parse_mode_request(video, sizeof(video)), cursor::mode_e::video);
  EXPECT_FALSE(cursor::parse_mode_request(bad_version, sizeof(bad_version)));
  EXPECT_FALSE(cursor::parse_mode_request(bad_mode, sizeof(bad_mode)));
  EXPECT_FALSE(cursor::parse_mode_request(local, 1));
  EXPECT_FALSE(cursor::parse_mode_request(nullptr, 4));
}

// ---- images -----------------------------------------------------------------

TEST(CursorImage, ConvertsPremultipliedArgbToStraightBgra) {
  const std::uint32_t argb[] = {0xFF102030, 0x80404040, 0x00FFFFFF, 0x40100000};
  auto img = cursor::from_premultiplied_argb(argb, 2, 2, 1, 1);
  ASSERT_TRUE(img);
  EXPECT_EQ(img->width, 2);
  EXPECT_EQ(img->hotspot_x, 1);
  const std::vector<std::uint8_t> expected {
    0x30, 0x20, 0x10, 0xFF,  // opaque: unchanged, reordered
    0x80, 0x80, 0x80, 0x80,  // 0x40 at alpha 0x80 is 0x80 straight
    0x00, 0x00, 0x00, 0x00,  // transparent: colour dropped
    0x00, 0x00, 0x40, 0x40,  // red 0x10 at alpha 0x40 is 0x40 straight
  };
  EXPECT_EQ(img->bgra, expected);
}

TEST(CursorImage, ClampsHotspotAndRejectsBadSizes) {
  const std::uint32_t px[4] = {};
  auto img = cursor::from_premultiplied_argb(px, 2, 2, 9, -3);
  ASSERT_TRUE(img);
  EXPECT_EQ(img->hotspot_x, 1);
  EXPECT_EQ(img->hotspot_y, 0);
  EXPECT_FALSE(cursor::from_premultiplied_argb(px, 0, 2, 0, 0));
  EXPECT_FALSE(cursor::from_premultiplied_argb(px, 5000, 1, 0, 0));
  EXPECT_FALSE(cursor::from_premultiplied_argb(nullptr, 1, 1, 0, 0));
}

TEST(CursorImage, FallbackArrowFollowsTheDesktopCursorSize) {
  auto normal = cursor::fallback_arrow(0);  // 24 px box
  EXPECT_FALSE(cursor::is_blank(normal));
  EXPECT_EQ(normal.hotspot_x, 0);
  EXPECT_EQ(normal.hotspot_y, 0);
  EXPECT_EQ(normal.height, 19);
  auto big = cursor::fallback_arrow(48);
  EXPECT_EQ(big.height, 38);
  EXPECT_GT(big.width, normal.width);
  EXPECT_FALSE(cursor::encode_shape(cursor::shape_id(big), big, true).empty());
  EXPECT_EQ(cursor::fallback_arrow(1000).height, 205);  // capped at 256 before the 80% box
}

TEST(CursorImage, BlankDetection) {
  EXPECT_TRUE(cursor::is_blank(solid(1, 1, 9, 9, 9, 0)));
  EXPECT_TRUE(cursor::is_blank(cursor::image_t {}));
  auto img = solid(3, 3, 0, 0, 0, 0);
  img.bgra[4 * 4 + 3] = 1;
  EXPECT_FALSE(cursor::is_blank(img));
}

TEST(CursorImage, FitScalesSizeAndHotspot) {
  auto img = solid(32, 32, 10, 20, 30, 255, 16, 8);
  auto half = cursor::fit(img, 0.5);
  EXPECT_EQ(half.width, 16);
  EXPECT_EQ(half.height, 16);
  EXPECT_EQ(half.hotspot_x, 8);
  EXPECT_EQ(half.hotspot_y, 4);
  EXPECT_EQ(half.bgra.size(), 16u * 16u * 4u);
  EXPECT_EQ(half.bgra[0], 10);
  EXPECT_EQ(half.bgra[2], 30);
  EXPECT_EQ(half.bgra[3], 255);

  auto doubled = cursor::fit(img, 2.0);
  EXPECT_EQ(doubled.width, 64);
  EXPECT_EQ(doubled.hotspot_x, 32);

  // A 4K desktop streamed at 1080p: 48 px cursor becomes 24 px.
  EXPECT_EQ(cursor::fit(solid(48, 48, 0, 0, 0, 255), 1920.0 / 3840.0).width, 24);

  EXPECT_EQ(cursor::fit(img, 1.0), img);
  EXPECT_EQ(cursor::fit(img, 0.0), img);
  EXPECT_EQ(cursor::fit(img, -2.0), img);
}

TEST(CursorImage, FitKeepsShapesWithinTheClientLimit) {
  auto big = cursor::fit(solid(300, 150, 0, 0, 0, 255, 299, 149), 1.0);
  EXPECT_EQ(big.width, 256);
  EXPECT_EQ(big.height, 128);
  EXPECT_LT(big.hotspot_x, 256);
  EXPECT_LT(big.hotspot_y, 128);
  EXPECT_FALSE(cursor::encode_shape(1, big, true).empty());

  auto upscaled = cursor::fit(solid(200, 200, 0, 0, 0, 255), 3.0);
  EXPECT_EQ(upscaled.width, 256);
}

TEST(CursorImage, FitAveragesAlphaWithoutDarkFringes) {
  // A white opaque pixel next to a transparent black one: the average is half-transparent white.
  cursor::image_t img;
  img.width = 2;
  img.height = 1;
  img.bgra = {255, 255, 255, 255, 0, 0, 0, 0};
  auto out = cursor::fit(img, 0.5);
  ASSERT_EQ(out.width, 1);
  EXPECT_EQ(out.bgra[0], 255);
  EXPECT_EQ(out.bgra[1], 255);
  EXPECT_EQ(out.bgra[2], 255);
  EXPECT_NEAR(out.bgra[3], 128, 1);
}

TEST(CursorImage, ShapeIdsFollowContent) {
  auto a = solid(8, 8, 1, 2, 3, 255);
  auto b = a;
  EXPECT_EQ(cursor::shape_id(a), cursor::shape_id(b));
  EXPECT_NE(cursor::shape_id(a), 0u);
  b.hotspot_x = 1;
  EXPECT_NE(cursor::shape_id(a), cursor::shape_id(b));
  auto c = a;
  c.bgra[5] ^= 1;
  EXPECT_NE(cursor::shape_id(a), cursor::shape_id(c));
}

// ---- per-session sender -----------------------------------------------------

TEST(CursorSender, SendsNothingInVideoModeOrBeforeACapture) {
  cursor::sender_t sender;
  EXPECT_TRUE(sender.update(snap(1, solid(4, 4, 0, 0, 0, 255)), 1.0).empty());
  sender.set_mode(cursor::mode_e::local);
  EXPECT_TRUE(sender.update(cursor::snapshot_t {}, 1.0).empty());
}

TEST(CursorSender, SendsShapeThenOnlyChanges) {
  cursor::sender_t sender;
  client_t client;
  sender.set_mode(cursor::mode_e::local);

  auto arrow = solid(16, 16, 0, 0, 0, 255, 1, 1);
  auto first = snap(1, arrow);
  client.push_all(sender.update(first, 1.0));
  ASSERT_EQ(client.updates.size(), 1u);
  EXPECT_EQ(client.updates[0].flags, cursor::kFlagShape | cursor::kFlagVisible);
  const auto arrow_id = client.updates[0].id;

  // Unchanged snapshot, and a new capture of the same shape: nothing to send.
  EXPECT_TRUE(sender.update(first, 1.0).empty());
  EXPECT_TRUE(sender.update(snap(2, arrow), 1.0).empty());

  // Hidden: one state packet keeps the id and clears the visible flag.
  client.push_all(sender.update(snap(3, std::nullopt), 1.0));
  ASSERT_EQ(client.updates.size(), 2u);
  EXPECT_EQ(client.updates[1].flags, 0);
  EXPECT_EQ(client.updates[1].id, arrow_id);
  EXPECT_TRUE(sender.update(snap(4, std::nullopt), 1.0).empty());

  // Shown again: the client has the pixels, so only the id goes out.
  client.push_all(sender.update(snap(5, arrow), 1.0));
  ASSERT_EQ(client.updates.size(), 3u);
  EXPECT_EQ(client.updates[2].flags, cursor::kFlagVisible);
  EXPECT_EQ(client.updates[2].id, arrow_id);

  // A new shape is sent in full; switching back to the arrow is by id.
  client.push_all(sender.update(snap(6, solid(8, 20, 255, 255, 255, 255, 4, 0)), 1.0));
  ASSERT_EQ(client.updates.size(), 4u);
  EXPECT_EQ(client.updates[3].flags, cursor::kFlagShape | cursor::kFlagVisible);
  EXPECT_EQ(client.updates[3].height, 20);
  client.push_all(sender.update(snap(7, arrow), 1.0));
  ASSERT_EQ(client.updates.size(), 5u);
  EXPECT_EQ(client.updates[4].flags, cursor::kFlagVisible);
  EXPECT_EQ(client.updates[4].id, arrow_id);
  EXPECT_EQ(client.rejected, 0);
}

TEST(CursorSender, ScalesToTheStream) {
  cursor::sender_t sender;
  client_t client;
  sender.set_mode(cursor::mode_e::local);
  auto s = snap(1, solid(32, 32, 0, 0, 0, 255, 10, 20));
  client.push_all(sender.update(s, 0.5));
  ASSERT_EQ(client.updates.size(), 1u);
  EXPECT_EQ(client.updates[0].width, 16);
  EXPECT_EQ(client.updates[0].hx, 5);
  EXPECT_EQ(client.updates[0].hy, 10);

  // The stream scale changed (new resolution): same capture, new shape.
  client.push_all(sender.update(s, 1.0));
  ASSERT_EQ(client.updates.size(), 2u);
  EXPECT_EQ(client.updates[1].width, 32);
}

TEST(CursorSender, ReenablingLocalModeResendsInFull) {
  cursor::sender_t sender;
  client_t client;
  sender.set_mode(cursor::mode_e::local);
  auto s = snap(1, solid(8, 8, 0, 0, 0, 255));
  client.push_all(sender.update(s, 1.0));
  sender.set_mode(cursor::mode_e::video);
  EXPECT_TRUE(sender.update(s, 1.0).empty());
  sender.set_mode(cursor::mode_e::local);
  client.push_all(sender.update(s, 1.0));  // same snapshot, but the client asked again
  ASSERT_EQ(client.updates.size(), 2u);
  EXPECT_EQ(client.updates[1].flags, cursor::kFlagShape | cursor::kFlagVisible);
}

TEST(CursorSender, ForgetsShapesBeyondTheClientCacheBudget) {
  cursor::sender_t sender;
  client_t client;
  sender.set_mode(cursor::mode_e::local);
  const auto per_shape = 256u * 256u * 4u;
  const auto count = (int) (cursor::kClientCacheBudget / per_shape) + 2;
  std::uint64_t serial = 0;
  for (int i = 0; i < count; ++i) {
    client.push_all(sender.update(snap(++serial, solid(256, 256, (std::uint8_t) i, 0, 0, 255)), 1.0));
  }
  ASSERT_EQ(client.updates.size(), (std::size_t) count);
  // The first shape fell out of the budget, so it goes out with pixels again.
  client.push_all(sender.update(snap(++serial, solid(256, 256, 0, 0, 0, 255)), 1.0));
  ASSERT_EQ(client.updates.size(), (std::size_t) count + 1);
  EXPECT_EQ(client.updates.back().flags, cursor::kFlagShape | cursor::kFlagVisible);
  // The most recent one is still cached.
  client.push_all(sender.update(snap(++serial, solid(256, 256, (std::uint8_t) (count - 1), 0, 0, 255)), 1.0));
  EXPECT_EQ(client.updates.back().flags, cursor::kFlagVisible);
}

// ---- shared watcher -----------------------------------------------------------

namespace {
  /**
   * @brief Scripted cursor source: tests push images, poll() hands them out.
   */
  struct fake_feed_t {
    std::mutex m;
    std::condition_variable cv;
    std::deque<cursor::image_t> images;
    std::vector<cursor::target_t> opened;
    bool fail_open = false;
    bool broken = false;

    void push(cursor::image_t img) {
      {
        std::lock_guard lg {m};
        images.push_back(std::move(img));
      }
      cv.notify_all();
    }
  };

  class fake_source_t: public cursor::source_t {
  public:
    explicit fake_source_t(std::shared_ptr<fake_feed_t> feed):
        feed_ {std::move(feed)} {
    }

    std::optional<cursor::image_t> poll(std::chrono::milliseconds timeout, bool &broken) override {
      std::unique_lock ul {feed_->m};
      feed_->cv.wait_for(ul, timeout, [&] {
        return !feed_->images.empty() || feed_->broken;
      });
      if (feed_->broken) {
        broken = true;
        return std::nullopt;
      }
      if (feed_->images.empty()) {
        return std::nullopt;
      }
      auto img = std::move(feed_->images.front());
      feed_->images.pop_front();
      return img;
    }

  private:
    std::shared_ptr<fake_feed_t> feed_;
  };

  template<class F>
  bool eventually(F &&f, std::chrono::milliseconds limit = 3s) {
    auto end = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < end) {
      if (f()) {
        return true;
      }
      std::this_thread::sleep_for(5ms);
    }
    return f();
  }

  class CursorWatcherTest: public ::testing::Test {
  protected:
    void SetUp() override {
      feed = std::make_shared<fake_feed_t>();
      cursor::set_source_factory([f = feed](const cursor::target_t &t) -> std::unique_ptr<cursor::source_t> {
        std::lock_guard lg {f->m};
        f->opened.push_back(t);
        if (f->fail_open) {
          return nullptr;
        }
        return std::make_unique<fake_source_t>(f);
      });
    }

    void TearDown() override {
      cursor::release_all();
      cursor::set_source_factory(nullptr);
      cursor::set_target_provider(nullptr);
      display_cursor = true;
    }

    std::shared_ptr<fake_feed_t> feed;
  };
}  // namespace

TEST_F(CursorWatcherTest, LocalModeTakesTheCursorOutOfTheVideoOnceShapesFlow) {
  EXPECT_TRUE(cursor::available());
  EXPECT_TRUE(display_cursor);
  cursor::acquire();
  EXPECT_TRUE(cursor::local_active());
  // No shape yet: the video keeps its cursor.
  EXPECT_TRUE(display_cursor);

  feed->push(solid(4, 4, 0, 0, 0, 255));
  ASSERT_TRUE(eventually([] {
    return cursor::current().serial != 0;
  }));
  EXPECT_TRUE(cursor::current().visible);
  EXPECT_FALSE(display_cursor);

  // A blank cursor (game hid it) publishes as hidden.
  auto before = cursor::current().serial;
  feed->push(solid(1, 1, 0, 0, 0, 0));
  ASSERT_TRUE(eventually([&] {
    return cursor::current().serial > before;
  }));
  EXPECT_FALSE(cursor::current().visible);
  EXPECT_FALSE(cursor::current().image);

  cursor::release();
  EXPECT_FALSE(cursor::local_active());
  EXPECT_TRUE(display_cursor);
  EXPECT_EQ(cursor::current().serial, 0u);
}

TEST_F(CursorWatcherTest, SharedBetweenSessions) {
  cursor::acquire();
  cursor::acquire();
  feed->push(solid(4, 4, 0, 0, 0, 255));
  ASSERT_TRUE(eventually([] {
    return !display_cursor;
  }));
  cursor::release();
  EXPECT_TRUE(cursor::local_active());
  EXPECT_FALSE(display_cursor);
  cursor::release();
  EXPECT_TRUE(display_cursor);
  cursor::release();  // extra releases are harmless
  EXPECT_FALSE(cursor::local_active());
}

TEST_F(CursorWatcherTest, BrokenSourcePutsTheCursorBackInTheVideo) {
  cursor::acquire();
  feed->push(solid(4, 4, 0, 0, 0, 255));
  ASSERT_TRUE(eventually([] {
    return !display_cursor;
  }));
  {
    std::lock_guard lg {feed->m};
    feed->broken = true;
    feed->fail_open = true;
  }
  feed->cv.notify_all();
  EXPECT_TRUE(eventually([] {
    return display_cursor;
  }));
  EXPECT_EQ(cursor::current().serial, 0u);
}

TEST_F(CursorWatcherTest, UnavailableWithoutASource) {
  feed->fail_open = true;
  EXPECT_FALSE(cursor::available());
  cursor::acquire();
  std::this_thread::sleep_for(50ms);
  EXPECT_TRUE(display_cursor);
}

TEST_F(CursorWatcherTest, TurnedOffInTheSettings) {
  const bool saved = config::input.local_cursor;
  config::input.local_cursor = false;
  EXPECT_FALSE(cursor::available());
  config::input.local_cursor = true;
  EXPECT_TRUE(cursor::available());
  config::input.local_cursor = saved;
}

TEST_F(CursorWatcherTest, UserToggleStillWorks) {
  cursor::toggle_user_composite();
  EXPECT_FALSE(display_cursor);
  cursor::toggle_user_composite();
  EXPECT_TRUE(display_cursor);
}

TEST_F(CursorWatcherTest, FollowsTheCaptureTarget) {
  std::mutex m;
  cursor::target_t target {":0", ""};
  cursor::set_target_provider([&] {
    std::lock_guard lg {m};
    return target;
  });
  cursor::acquire();
  ASSERT_TRUE(eventually([&] {
    std::lock_guard lg {feed->m};
    return feed->opened.size() == 1;
  }));
  {
    std::lock_guard lg {m};
    target = {":20", "/tmp/nova-vd-20.xauth"};
  }
  ASSERT_TRUE(eventually([&] {
    std::lock_guard lg {feed->m};
    return feed->opened.size() == 2;
  }));
  // The provider captures locals: stop the watcher before they go away.
  cursor::release_all();
  cursor::set_target_provider(nullptr);
  std::lock_guard lg {feed->m};
  EXPECT_EQ(feed->opened[0].display, ":0");
  EXPECT_EQ(feed->opened[1].display, ":20");
  EXPECT_EQ(feed->opened[1].xauthority, "/tmp/nova-vd-20.xauth");
}

TEST_F(CursorWatcherTest, ReadsTheVirtualDisplayWhileOneRuns) {
  virtual_display::target_t vd;
  vd.display = ":21";
  vd.xauthority = "/tmp/nova-vd-21.xauth";
  virtual_display::set_capture_target(vd);
  cursor::acquire();
  bool opened = eventually([&] {
    std::lock_guard lg {feed->m};
    return !feed->opened.empty();
  });
  cursor::release_all();
  virtual_display::set_capture_target(std::nullopt);
  ASSERT_TRUE(opened);
  std::lock_guard lg {feed->m};
  EXPECT_EQ(feed->opened[0].display, ":21");
  EXPECT_EQ(feed->opened[0].xauthority, "/tmp/nova-vd-21.xauth");
}

TEST_F(CursorWatcherTest, ReadsTheDesktopForMirror) {
  cursor::acquire();
  bool opened = eventually([&] {
    std::lock_guard lg {feed->m};
    return !feed->opened.empty();
  });
  cursor::release_all();
  ASSERT_TRUE(opened);
  std::lock_guard lg {feed->m};
  EXPECT_TRUE(feed->opened[0].display.empty());  // $DISPLAY
}

#ifdef SUNSHINE_BUILD_X11
/**
 * Live XFixes check against a spare headless X server, never the desktop: set
 * NOVA_TEST_CURSOR_DISPLAY (e.g. ":99" from `Xvfb :99`) to run it. A test client sets the root
 * window's cursor (no input is sent) and the source must report each new shape; a cursor left by a
 * client that exited (xsetroot) comes back as the standard arrow.
 */
TEST(CursorX11Live, ReportsShapeChangesOnAHeadlessServer) {
  const char *display = std::getenv("NOVA_TEST_CURSOR_DISPLAY");
  if (!display || !*display || std::string_view {display} == ":0") {
    GTEST_SKIP() << "set NOVA_TEST_CURSOR_DISPLAY to a spare headless X display";
  }
  auto source = cursor::make_x11_source({display, ""});
  ASSERT_TRUE(source) << "no XFixes on " << display;

  bool broken = false;
  auto first = source->poll(1s, broken);
  ASSERT_FALSE(broken);
  ASSERT_TRUE(first);  // the current cursor, at once

  auto next = [&]() {
    std::optional<cursor::image_t> img;
    for (int i = 0; i < 20 && !img && !broken; ++i) {
      img = source->poll(100ms, broken);
    }
    return img;
  };

  std::vector<std::uint32_t> seen;
  int last_height = 0;
  {
    x_cursor_owner_t owner {display, ""};
    ASSERT_TRUE(owner);
    for (unsigned int shape : {XC_hand2, XC_crosshair, XC_xterm, XC_left_ptr}) {
      owner.show(shape);
      auto img = next();
      ASSERT_FALSE(broken);
      ASSERT_TRUE(img) << "no shape change for cursor font glyph " << shape;
      EXPECT_FALSE(cursor::is_blank(*img));
      EXPECT_FALSE(cursor::encode_shape(cursor::shape_id(*img), cursor::fit(*img, 1.0), true).empty());
      const auto id = cursor::shape_id(*img);
      EXPECT_EQ(std::count(seen.begin(), seen.end(), id), 0) << shape;
      seen.push_back(id);
      last_height = img->height;
    }
  }

  // A root cursor whose owner exited: the server refuses its pixels, the arrow stands in.
  ASSERT_EQ(std::system(("DISPLAY="s + display + " xsetroot -cursor_name hand2").c_str()), 0);
  auto img = next();
  ASSERT_FALSE(broken);
  ASSERT_TRUE(img) << "no shape after xsetroot";
  EXPECT_FALSE(cursor::is_blank(*img));
  EXPECT_EQ(cursor::shape_id(*img), cursor::shape_id(cursor::fallback_arrow(last_height)));
}
#endif
