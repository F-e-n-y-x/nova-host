/**
 * @file tests/unit/platform/linux/test_cursor_live.cpp
 * @brief Opt-in live test of the local cursor on a spare headless X server (NOVA_CURSOR_LIVE=1).
 *
 * Starts the Virtual display's headless NVIDIA X server (no scanout; the desktop's heads are never
 * touched), paints its root window one solid colour and then checks, on real hardware:
 *  - XFixes reports each new pointer shape set with xsetroot (a root-window property change, no
 *    input is sent anywhere);
 *  - the shared watcher reads the Virtual display (not the desktop) and, once shapes flow, clears
 *    the capture's cursor flag;
 *  - NvFBC frames contain the pointer with the flag set and no pointer pixels at all with it
 *    cleared, so a local-cursor client never sees two pointers.
 */
#if defined(__linux__) && defined(SUNSHINE_BUILD_CUDA) && defined(SUNSHINE_BUILD_X11)

  // test includes
  #include "../../../tests_common.h"

  // standard includes
  #include <algorithm>
  #include <array>
  #include <chrono>
  #include <cstdio>
  #include <cstdlib>
  #include <filesystem>
  #include <format>
  #include <fstream>
  #include <iostream>
  #include <string>
  #include <thread>
  #include <vector>

  // local includes
  #include <src/config.h>
  #include <src/cursor.h>
  #include <src/globals.h>
  #include <src/platform/common.h>
  #include <src/platform/linux/cuda.h>
  #include <src/utility.h>
  #include <src/video.h>
  #include <src/virtual_display.h>

  // Last: Xlib defines macros (None, Success, ...) that must not reach the headers above.
  #include "x_cursor_owner.h"

namespace platf {
  std::vector<std::string> nvfbc_display_names();
  std::shared_ptr<display_t> nvfbc_display(mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config);
}  // namespace platf

namespace {
  namespace fs = std::filesystem;
  namespace vd = virtual_display;
  using namespace std::literals;

  std::string shell_output(const std::string &cmd) {
    std::string out;
    if (FILE *pipe = popen(cmd.c_str(), "r")) {
      std::array<char, 512> buf {};
      while (fgets(buf.data(), buf.size(), pipe)) {
        out += buf.data();
      }
      pclose(pipe);
    }
    return out;
  }

  /**
   * @brief Pixels that differ from the solid background, and their bounding box.
   */
  struct diff_t {
    int count = 0;
    int min_x = 1 << 30, min_y = 1 << 30, max_x = -1, max_y = -1;
  };

  /**
   * @brief Capture a few frames with @p with_cursor and count the pixels that aren't background.
   */
  std::optional<diff_t> capture_diff(platf::display_t &disp, bool with_cursor, int width, int height, std::array<std::uint8_t, 3> bgr) {
    std::vector<std::shared_ptr<platf::img_t>> frames;
    auto push = [&](std::shared_ptr<platf::img_t> &&img, bool captured) {
      if (captured && img) {
        frames.push_back(std::move(img));
      }
      return frames.size() < 8;
    };
    auto pull = [&](std::shared_ptr<platf::img_t> &out) {
      out = disp.alloc_img();
      return out != nullptr;
    };
    bool cursor = with_cursor;
    if (disp.capture(push, pull, &cursor) != platf::capture_e::ok || frames.empty()) {
      return std::nullopt;
    }
    // The last frame: the capture has settled on the requested cursor mode by then.
    auto &img = *frames.back();
    std::vector<std::uint8_t> pixels((std::size_t) img.row_pitch * img.height);
    if (cuda::download_texture(cuda::img_texture(img, false), img.height, img.row_pitch, pixels.data())) {
      return std::nullopt;
    }
    diff_t d;
    for (int y = 0; y < std::min(height, img.height); ++y) {
      for (int x = 0; x < std::min(width, img.width); ++x) {
        const auto *p = &pixels[(std::size_t) y * img.row_pitch + x * 4];
        if (std::abs(p[0] - bgr[0]) > 2 || std::abs(p[1] - bgr[1]) > 2 || std::abs(p[2] - bgr[2]) > 2) {
          ++d.count;
          d.min_x = std::min(d.min_x, x);
          d.min_y = std::min(d.min_y, y);
          d.max_x = std::max(d.max_x, x);
          d.max_y = std::max(d.max_y, y);
        }
      }
    }
    return d;
  }

  template<class F>
  bool eventually(F &&f, std::chrono::milliseconds limit = 5s) {
    auto end = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < end) {
      if (f()) {
        return true;
      }
      std::this_thread::sleep_for(10ms);
    }
    return f();
  }

  video::config_t stream_config(int width, int height) {
    video::config_t config {};
    config.width = width;
    config.height = height;
    config.framerate = 60;
    config.bitrate = 20000;
    config.slicesPerFrame = 1;
    config.videoFormat = 0;
    return config;
  }
}  // namespace

TEST(CursorLive, VirtualDisplayShapesAndCursorFreeNvfbcFrames) {
  const char *live = std::getenv("NOVA_CURSOR_LIVE");
  if (!live || std::string {live} != "1") {
    GTEST_SKIP() << "set NOVA_CURSOR_LIVE=1 to start a real headless X server and read its cursor";
  }
  // With NOVA_CURSOR_LIVE_REPORT set, the measurements are also written there.
  std::ofstream report;
  if (const char *r = std::getenv("NOVA_CURSOR_LIVE_REPORT"); r && *r) {
    report.open(r);
  }
  auto note = [&report](const std::string &line) {
    std::cout << "[live] " << line << std::endl;
    report << line << std::endl;
  };
  constexpr int width = 1280;
  constexpr int height = 720;
  const fs::path dir = fs::temp_directory_path() / "nova-cursor-live";
  fs::remove_all(dir);
  fs::create_directories(dir);
  const auto desk_before = shell_output("DISPLAY=:0 xrandr --query 2>/dev/null");

  vd::x_server_t server {dir / "vd", "", vd::default_ops()};
  auto target = server.start({width, height, 60});
  ASSERT_TRUE(target) << "headless X server didn't start";
  ASSERT_NE(target->display, ":0");
  auto cleanup = util::fail_guard([&]() {
    cursor::release_all();
    vd::set_capture_target(std::nullopt);
    server.stop();
    display_cursor = true;
  });
  const auto on_vd = std::format("DISPLAY={} XAUTHORITY={} ", target->display, target->xauthority);

  // Solid background: BGR 0xa0 0x60 0x30 is #3060a0.
  const std::array<std::uint8_t, 3> bgr {0xa0, 0x60, 0x30};
  ASSERT_EQ(std::system((on_vd + "xsetroot -solid '#3060a0' -cursor_name left_ptr").c_str()), 0);

  // 1. XFixes on the Virtual display reports every new shape.
  auto source = cursor::make_x11_source({target->display, target->xauthority});
  ASSERT_TRUE(source) << "no XFixes on " << target->display;
  bool broken = false;
  auto first = source->poll(1s, broken);
  ASSERT_FALSE(broken);
  ASSERT_TRUE(first);
  note(std::format("first shape {}x{} hotspot {},{}", first->width, first->height, first->hotspot_x, first->hotspot_y));
  std::vector<std::uint32_t> ids;
  {
    // A live client owns the cursors, as applications do (see x_cursor_owner.h).
    x_cursor_owner_t owner {target->display, target->xauthority};
    ASSERT_TRUE(owner);
    for (unsigned int shape : {XC_hand2, XC_crosshair, XC_xterm, XC_left_ptr}) {
      owner.show(shape);
      std::optional<cursor::image_t> img;
      for (int i = 0; i < 30 && !img && !broken; ++i) {
        img = source->poll(100ms, broken);
      }
      ASSERT_FALSE(broken);
      ASSERT_TRUE(img) << "no shape change for cursor font glyph " << shape;
      EXPECT_FALSE(cursor::is_blank(*img));
      EXPECT_FALSE(cursor::encode_shape(cursor::shape_id(*img), cursor::fit(*img, 1.0), true).empty()) << shape;
      note(std::format("cursor font glyph {}: {}x{} hotspot {},{} id {:08x}", shape, img->width, img->height, img->hotspot_x, img->hotspot_y, cursor::shape_id(*img)));
      const auto id = cursor::shape_id(*img);
      EXPECT_EQ(std::count(ids.begin(), ids.end(), id), 0) << shape;
      ids.push_back(id);
    }
  }
  // The session's own root cursor (xsetroot, owner gone) comes back as the standard arrow.
  ASSERT_EQ(std::system((on_vd + "xsetroot -cursor_name left_ptr").c_str()), 0);
  {
    std::optional<cursor::image_t> img;
    for (int i = 0; i < 30 && !img && !broken; ++i) {
      img = source->poll(100ms, broken);
    }
    ASSERT_TRUE(img) << "no shape after xsetroot";
    EXPECT_FALSE(cursor::is_blank(*img));
    note(std::format("xsetroot left_ptr (owner gone): {}x{} stand-in arrow", img->width, img->height));
  }
  source.reset();

  // 2. The watcher follows the capture target (the Virtual display) and takes the cursor out of the video.
  vd::set_capture_target(*target);
  ASSERT_EQ(platf::nvfbc_display_names(), std::vector<std::string> {"0"});
  EXPECT_TRUE(cursor::available());
  display_cursor = true;
  cursor::acquire();
  ASSERT_TRUE(eventually([] {
    return cursor::current().serial != 0;
  })) << "the watcher published no shape";
  EXPECT_TRUE(cursor::current().visible);
  EXPECT_FALSE(display_cursor) << "local mode must leave the cursor out of the video";
  note(std::format("watcher on {}: shape {}x{} visible {}, capture cursor flag {}", target->display, cursor::current().image ? cursor::current().image->width : 0, cursor::current().image ? cursor::current().image->height : 0, cursor::current().visible, display_cursor));
  cursor::sender_t sender;
  sender.set_mode(cursor::mode_e::local);
  EXPECT_FALSE(sender.update(cursor::current(), 1.0).empty());
  cursor::release();
  EXPECT_TRUE(display_cursor);

  // 3. NvFBC: the pointer is in the frames with the cursor flag and absent without it.
  const auto saved_video = config::video;
  auto restore_config = util::fail_guard([&]() {
    config::video = saved_video;
  });
  config::video.capture = "nvfbc";
  auto platform = platf::init();
  ASSERT_TRUE(platform);
  // A capture session ends with its capture() call, so each mode gets its own display.
  auto with_disp = platf::nvfbc_display(platf::mem_type_e::cuda, "0", stream_config(width, height));
  ASSERT_TRUE(with_disp);
  auto with_cursor = capture_diff(*with_disp, true, width, height, bgr);
  ASSERT_TRUE(with_cursor);
  with_disp.reset();
  auto disp = platf::nvfbc_display(platf::mem_type_e::cuda, "0", stream_config(width, height));
  ASSERT_TRUE(disp);
  auto without_cursor = capture_diff(*disp, false, width, height, bgr);
  ASSERT_TRUE(without_cursor);
  note(std::format("NvFBC with cursor: {} pointer pixels in ({},{})-({},{}); without (local mode): {} pixels", with_cursor->count, with_cursor->min_x, with_cursor->min_y, with_cursor->max_x, with_cursor->max_y, without_cursor->count));
  EXPECT_GT(with_cursor->count, 20) << "the pointer should be drawn into the video with the cursor flag";
  EXPECT_LT(with_cursor->max_x - with_cursor->min_x, 128);
  EXPECT_LT(with_cursor->max_y - with_cursor->min_y, 128);
  EXPECT_EQ(without_cursor->count, 0) << "no pointer pixels may be in the video in local mode";

  disp.reset();
  EXPECT_EQ(shell_output("DISPLAY=:0 xrandr --query 2>/dev/null"), desk_before) << "the desktop's outputs changed";
}

#endif
