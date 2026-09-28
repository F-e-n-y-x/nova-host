/**
 * @file tests/unit/platform/linux/test_portrait_live.cpp
 * @brief Opt-in live test of a portrait Virtual display on a spare headless X server (NOVA_PORTRAIT_LIVE=1).
 *
 * Starts a headless NVIDIA X server through display_follow::controller_t at a phone's portrait size
 * (1080x2340), captures portrait frames with Nova's NvFBC path, then turns it to landscape and back
 * the way Nebula's live rotation does (disconnect, linger, /resume at the swapped size). The desktop
 * (:0) is only read (`xrandr --query` before and after); nothing is rotated there and no input is
 * sent anywhere. Skipped unless NOVA_PORTRAIT_LIVE=1.
 */
#if defined(__linux__) && defined(SUNSHINE_BUILD_CUDA)

  // test includes
  #include "../../../tests_common.h"

  // standard includes
  #include <array>
  #include <chrono>
  #include <cstdio>
  #include <cstdlib>
  #include <filesystem>
  #include <fstream>
  #include <iostream>
  #include <regex>
  #include <string>

  // local includes
  #include <src/display_follow.h>
  #include <src/display_modeset.h>
  #include <src/utility.h>
  #include <src/video.h>
  #include <src/virtual_display.h>

namespace platf {
  std::vector<std::string> nvfbc_display_names();
  std::shared_ptr<display_t> nvfbc_display(mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config);
}  // namespace platf

namespace {
  namespace fs = std::filesystem;
  namespace vd = virtual_display;

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

  std::string dimensions(const vd::target_t &t) {
    std::smatch m;
    const auto out = shell_output("DISPLAY=" + t.display + " XAUTHORITY=" + t.xauthority + " xdpyinfo 2>/dev/null");
    static const std::regex re {R"(dimensions:\s+(\d+x\d+) pixels)"};
    return std::regex_search(out, m, re) ? m[1].str() : std::string {};
  }

  /**
   * @brief display_follow's virtual backend over a real x_server_t (like the one Nova uses).
   */
  class live_backend_t: public display_follow::virtual_backend_t {
  public:
    explicit live_backend_t(fs::path state):
        server {std::move(state), "", vd::default_ops()} {
    }

    std::optional<vd::target_t> start(const display_follow::request_t &r) override {
      return server.start({r.width, r.height, r.fps});
    }

    std::optional<vd::target_t> resize(const display_follow::request_t &r) override {
      if (!server.resize({r.width, r.height, r.fps})) {
        return std::nullopt;
      }
      return server.target();
    }

    void stop() override {
      server.stop();
    }

    bool recover() override {
      return false;
    }

    vd::x_server_t server;  ///< The spare headless server.
  };

  /**
   * @brief Capture frames with Nova's NvFBC display on the virtual display.
   *
   * @return "WxH of the display / WxH of the last frame / frames", or an error text.
   */
  std::string capture(const vd::target_t &t, int frames) {
    vd::set_capture_target(t);
    // As Nova does (video.cpp enumerates before it opens a display): this also loads CUDA and the
    // NvFBC entry points, which nvfbc_display() alone does not.
    if (platf::nvfbc_display_names() != std::vector<std::string> {"0"}) {
      return "no headless NvFBC screen";
    }
    auto disp = platf::nvfbc_display(platf::mem_type_e::cuda, "0", {t.width, t.height, 60, 12000, 20000, 1, 1, 1, 1, 0, 0, 0});
    if (!disp) {
      return "no display";
    }
    int pushed = 0;
    int img_w = 0;
    int img_h = 0;
    auto push = [&](std::shared_ptr<platf::img_t> &&img, bool captured) {
      if (captured && img) {
        ++pushed;
        img_w = img->width;
        img_h = img->height;
      }
      return pushed < frames;
    };
    std::vector<std::shared_ptr<platf::img_t>> pool;
    std::size_t next = 0;
    auto pull = [&](std::shared_ptr<platf::img_t> &out) {
      if (pool.size() < 4) {
        pool.push_back(disp->alloc_img());
      }
      out = pool[next++ % pool.size()];
      return out != nullptr;
    };
    bool cursor = true;
    const auto status = disp->capture(push, pull, &cursor);
    if (status != platf::capture_e::ok) {
      return "capture error";
    }
    return std::to_string(disp->width) + "x" + std::to_string(disp->height) + " / " + std::to_string(img_w) + "x" + std::to_string(img_h) + " / " + std::to_string(pushed);
  }
}  // namespace

TEST(PortraitLive, PortraitVirtualDisplayCapturesAndTurns) {
  const char *live = std::getenv("NOVA_PORTRAIT_LIVE");
  if (!live || std::string {live} != "1") {
    GTEST_SKIP() << "set NOVA_PORTRAIT_LIVE=1 to start a real headless X server";
  }
  const fs::path dir = fs::temp_directory_path() / "nova-portrait-live";
  fs::remove_all(dir);
  fs::create_directories(dir);
  std::ofstream report {fs::temp_directory_path() / "nova-portrait-live-report.txt"};
  auto note = [&report](const std::string &line) {
    std::cout << "[live] " << line << std::endl;
    report << line << std::endl;
  };
  const auto desk_before = shell_output("DISPLAY=:0 xrandr --query 2>/dev/null");

  auto backend = std::make_shared<live_backend_t>(dir / "vd");
  display_follow::controller_t controller {
    [](const std::string &, const std::string &, const display_follow::env_t &) {
      return 1;  // the display script must never run here
    },
    dir / "display_follow.active",
    backend,
    {}
  };
  auto cleanup = util::fail_guard([&]() {
    vd::set_capture_target(std::nullopt);
    controller.end_now({});
  });

  display_follow::request_t request;
  request.width = 1080;
  request.height = 2340;
  request.fps = 60;
  request.rotation = display_modeset::rotation_for_size(1080, 2340);
  request.mode = "virtual";
  request.client_name = "live test";
  ASSERT_EQ(controller.on_stream_request("virtual", "", request, false, false, "headless_x"), display_follow::outcome_e::virtual_started);
  auto target = controller.virtual_target();
  ASSERT_TRUE(target);
  EXPECT_EQ(dimensions(*target), "1080x2340");
  auto shot = capture(*target, 120);
  note(target->display + " portrait " + dimensions(*target) + ", NvFBC display / frame / frames: " + shot);
  EXPECT_EQ(shot, "1080x2340 / 1080x2340 / 120");

  // Nebula's live rotation: disconnect, reconnect within the linger at the swapped size.
  vd::set_capture_target(std::nullopt);
  controller.on_last_session_end({}, std::chrono::seconds {30});
  request.width = 2340;
  request.height = 1080;
  request.rotation = 0;
  ASSERT_EQ(controller.on_stream_request("virtual", "", request, false, false, "headless_x"), display_follow::outcome_e::virtual_reused);
  target = controller.virtual_target();
  EXPECT_EQ(dimensions(*target), "2340x1080");
  shot = capture(*target, 60);
  note("turned to landscape: " + dimensions(*target) + ", NvFBC: " + shot);
  EXPECT_EQ(shot, "2340x1080 / 2340x1080 / 60");

  vd::set_capture_target(std::nullopt);
  controller.on_last_session_end({}, std::chrono::seconds {30});
  request.width = 1080;
  request.height = 2340;
  ASSERT_EQ(controller.on_stream_request("virtual", "", request, false, false, "headless_x"), display_follow::outcome_e::virtual_reused);
  target = controller.virtual_target();
  EXPECT_EQ(dimensions(*target), "1080x2340");
  shot = capture(*target, 60);
  note("back to portrait: " + dimensions(*target) + ", NvFBC: " + shot);
  EXPECT_EQ(shot, "1080x2340 / 1080x2340 / 60");

  // Foundation /rotate-display on the virtual display resizes it.
  vd::set_capture_target(std::nullopt);
  ASSERT_TRUE(controller.rotate(0));
  EXPECT_EQ(dimensions(*controller.virtual_target()), "2340x1080");
  note("rotate-display 0: " + dimensions(*controller.virtual_target()));

  const auto display = target->display;
  controller.end_now({});
  cleanup.disable();
  EXPECT_FALSE(fs::exists("/tmp/.X11-unix/X" + display.substr(1)));
  const bool desk_same = shell_output("DISPLAY=:0 xrandr --query 2>/dev/null") == desk_before;
  EXPECT_TRUE(desk_same) << "the desktop must not change";
  note(std::string {"stopped; desktop unchanged: "} + (desk_same ? "yes" : "NO"));
  fs::remove_all(dir);
}
#endif
