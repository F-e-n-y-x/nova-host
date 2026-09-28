/**
 * @file tests/unit/platform/linux/test_virtual_display_live.cpp
 * @brief Opt-in live test of Nova's virtual display on a real NVIDIA GPU (NOVA_VD_LIVE=1).
 *
 * Starts a headless X server on a spare display number through virtual_display::x_server_t, captures
 * it with Nova's own NvFBC path (display-name fallback and DISPLAY scoping), measures the frame cap on
 * glxgears, resizes it and stops it. Nothing is sent to any display: no input is injected anywhere.
 * Skipped unless NOVA_VD_LIVE=1, so CI and normal runs never start an X server.
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
#include <future>
#include <iostream>
#include <regex>
#include <string>
#include <thread>

// local includes
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

  std::string x_env(const vd::target_t &t) {
    return "DISPLAY=" + t.display + " XAUTHORITY=" + t.xauthority + " ";
  }

  std::string dimensions(const vd::target_t &t) {
    std::smatch m;
    const auto out = shell_output(x_env(t) + "xdpyinfo 2>/dev/null");
    static const std::regex re {R"(dimensions:\s+(\d+x\d+) pixels)"};
    return std::regex_search(out, m, re) ? m[1].str() : std::string {};
  }

  /**
   * @brief Run glxgears on the display for `seconds` with an environment and return its last FPS report.
   */
  double glxgears_fps(const vd::target_t &t, const vd::env_list_t &env, int seconds) {
    std::string cmd = "timeout " + std::to_string(seconds) + " env";
    for (const auto &[k, v] : env) {
      cmd += " '" + k + "=" + v + "'";
    }
    cmd += " glxgears 2>&1";
    const auto out = shell_output(cmd);
    static const std::regex re {R"(= ([0-9.]+) FPS)"};
    double last = -1;
    for (auto it = std::sregex_iterator(out.begin(), out.end(), re); it != std::sregex_iterator(); ++it) {
      last = std::stod((*it)[1].str());
    }
    return last;
  }

  /**
   * @brief Capture `frames` frames through Nova's NvFBC display and return the achieved rate.
   */
  double capture_rate(const std::shared_ptr<platf::display_t> &disp, int frames) {
    int pushed = 0;
    std::chrono::steady_clock::time_point first;
    auto push = [&](std::shared_ptr<platf::img_t> &&, bool captured) {
      if (captured && pushed++ == 0) {
        first = std::chrono::steady_clock::now();
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
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - first).count();
    if (status != platf::capture_e::ok || pushed < 2) {
      return -1;
    }
    return (pushed - 1) / secs;
  }
}  // namespace

TEST(VirtualDisplayLive, HeadlessNvidiaServerCaptureCapAndResize) {
  const char *live = std::getenv("NOVA_VD_LIVE");
  if (!live || std::string {live} != "1") {
    GTEST_SKIP() << "set NOVA_VD_LIVE=1 to start a real headless X server";
  }
  const fs::path state = fs::temp_directory_path() / "nova-vd-live";
  // The test harness captures stdout; the measurements also go to a report file for the record.
  std::ofstream report {fs::temp_directory_path() / "nova-vd-live-report.txt"};
  auto note = [&report](const std::string &line) {
    std::cout << "[live] " << line << std::endl;
    report << line << std::endl;
  };
  vd::x_server_t server {state, "openbox", vd::default_ops()};
  const auto started = std::chrono::steady_clock::now();
  auto target = server.start({2340, 1080, 120});
  ASSERT_TRUE(target) << "see " << (state / "Xorg.failed.log");
  note(target->display + " up in " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count()) + " ms");
  auto stop = util::fail_guard([&]() {
    vd::set_capture_target(std::nullopt);
    server.stop();
  });

  EXPECT_EQ(dimensions(*target), "2340x1080");
  note("xdpyinfo dimensions: " + dimensions(*target));
  note("glxinfo: " + shell_output(x_env(*target) + "glxinfo -B 2>/dev/null | grep -E 'direct rendering|renderer string' | tr '\\n' ' '"));
  note("xrandr: " + shell_output(x_env(*target) + "xrandr --current 2>/dev/null | head -1"));

  // Capture through Nova's NvFBC code with the DISPLAY scope and the headless name fallback.
  vd::set_capture_target(*target);
  const auto names = platf::nvfbc_display_names();
  ASSERT_EQ(names, (std::vector<std::string> {"0"}));

  // Something to capture while measuring: glxgears capped at the client rate (Nova's app env).
  const auto capped_env = vd::app_env(*target, 0, "", "", vd::mangohud_gl_preload());
  auto gears = std::async(std::launch::async, [&]() {
    return glxgears_fps(*target, capped_env, 12);
  });
  std::this_thread::sleep_for(std::chrono::seconds(1));

  video::config_t config {2340, 1080, 120, 12000, 20000, 1, 1, 1, 1, 0, 0, 0};
  auto disp = platf::nvfbc_display(platf::mem_type_e::cuda, names[0], config);
  ASSERT_TRUE(disp);
  EXPECT_EQ(disp->width, 2340);
  EXPECT_EQ(disp->height, 1080);
  const double rate = capture_rate(disp, 600);
  disp.reset();
  note("NvFBC display names on " + target->display + ": [" + names[0] + "]");
  note("NvFBC (Nova display_t) capture: " + std::to_string(rate) + " fps at 2340x1080 (600 frames)");
  EXPECT_GT(rate, 100.0);

  const double capped = gears.get();
  note("glxgears with Nova's frame cap (120): " + std::to_string(capped) + " FPS");
  EXPECT_GT(capped, 90.0);
  EXPECT_LT(capped, 135.0);

  const double uncapped = glxgears_fps(*target, {{"DISPLAY", target->display}, {"XAUTHORITY", target->xauthority}}, 6);
  note("glxgears uncapped: " + std::to_string(uncapped) + " FPS");

  // Reconnect at another size.
  ASSERT_TRUE(server.resize({3120, 1440, 60}));
  EXPECT_EQ(dimensions(*server.target()), "3120x1440");
  note("after resize: " + dimensions(*server.target()));
  vd::set_capture_target(*server.target());
  disp = platf::nvfbc_display(platf::mem_type_e::cuda, "0", {3120, 1440, 60, 12000, 20000, 1, 1, 1, 1, 0, 0, 0});
  ASSERT_TRUE(disp);
  EXPECT_EQ(disp->width, 3120);
  EXPECT_EQ(disp->height, 1440);
  note("NvFBC after resize: " + std::to_string(disp->width) + "x" + std::to_string(disp->height));
  disp.reset();

  const auto display = target->display;
  vd::set_capture_target(std::nullopt);
  server.stop();
  stop.disable();
  EXPECT_FALSE(fs::exists("/tmp/.X11-unix/X" + display.substr(1))) << "socket removed";
  EXPECT_FALSE(fs::exists(server.marker()));
  note("stopped; socket gone: " + std::string(fs::exists("/tmp/.X11-unix/X" + display.substr(1)) ? "no" : "yes"));
}
#endif
