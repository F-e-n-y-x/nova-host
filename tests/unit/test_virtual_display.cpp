/**
 * @file tests/unit/test_virtual_display.cpp
 * @brief Tests for Nova's headless X server lifecycle (fake processes), the NvFBC headless
 * display-name fallback and the scoped DISPLAY override.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

// local includes
#include <src/virtual_display.h>

namespace fs = std::filesystem;
namespace vd = virtual_display;

namespace {
  std::string join(const std::vector<std::string> &argv) {
    std::string out;
    for (const auto &a : argv) {
      out += (out.empty() ? "" : " ") + a;
    }
    return out;
  }

  std::string read_file(const fs::path &path) {
    std::ifstream in {path};
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  }

  /**
   * @brief Fake process table behind vd::ops_t.
   */
  struct fake_system_t {
    vd::ops_t ops() {
      vd::ops_t o;
      o.spawn_server = [this](const std::vector<std::string> &argv, const vd::env_list_t &env, const fs::path &log, int timeout) {
        server_argv = argv;
        server_env = env;
        server_log = log;
        server_timeout = timeout;
        if (server_fails) {
          std::ofstream {log} << "(EE) no screens found\n";
          return -1L;
        }
        const long pid = next_pid++;
        alive.insert(pid);
        cmdlines[pid] = join(argv);
        return pid;
      };
      o.spawn = [this](const std::vector<std::string> &argv, const vd::env_list_t &env, const fs::path &) {
        wm_argv = argv;
        wm_env = env;
        const long pid = next_pid++;
        alive.insert(pid);
        cmdlines[pid] = join(argv);
        return pid;
      };
      o.run = [this](const std::vector<std::string> &argv, const vd::env_list_t &env) {
        runs.push_back(join(argv));
        run_envs.push_back(env);
        return argv[0] == "/usr/bin/xrandr" && xrandr_fails ? 1 : 0;
      };
      o.alive = [this](long pid) {
        return alive.contains(pid);
      };
      o.signal = [this](long pid, int sig) {
        signals.emplace_back(pid, sig);
        if (sig == 9 || !ignores_term.contains(pid)) {
          alive.erase(pid);
        }
      };
      o.cmdline = [this](long pid) {
        return cmdlines.contains(pid) ? cmdlines[pid] : std::string {};
      };
      o.display_taken = [this](int n) {
        return taken.contains(n);
      };
      o.cookie = []() {
        return std::string(32, 'a');
      };
      o.gpu_bus_id = [this]() {
        return bus_id;
      };
      o.xorg_path = []() {
        return std::string {"/usr/lib/xorg/Xorg"};
      };
      o.sleep_ms = [](int) {
      };
      return o;
    }

    long next_pid = 1000;
    std::set<long> alive;
    std::set<long> ignores_term;
    std::map<long, std::string> cmdlines;
    std::set<int> taken;
    std::string bus_id = "PCI:8:0:0";
    bool server_fails = false;
    bool xrandr_fails = false;
    std::vector<std::string> server_argv;
    vd::env_list_t server_env;
    fs::path server_log;
    int server_timeout = 0;
    std::vector<std::string> wm_argv;
    vd::env_list_t wm_env;
    std::vector<std::string> runs;
    std::vector<vd::env_list_t> run_envs;
    std::vector<std::pair<long, int>> signals;
  };

  std::string env_value(const vd::env_list_t &env, const std::string &key) {
    for (const auto &[k, v] : env) {
      if (k == key) {
        return v;
      }
    }
    return "<unset>";
  }

  class VirtualDisplayTest: public ::testing::Test {
  protected:
    void SetUp() override {
      dir = fs::temp_directory_path() / ("nova-vd-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
      fs::remove_all(dir);
    }

    void TearDown() override {
      std::error_code ec;
      fs::remove_all(dir, ec);
    }

    vd::x_server_t make(const std::string &wm = "openbox") {
      return vd::x_server_t {dir, wm, sys.ops()};
    }

    fs::path dir;
    fake_system_t sys;
  };
}  // namespace

TEST(VirtualDisplayHelpers, BusIdFromPciSlot) {
  EXPECT_EQ(vd::bus_id_from_pci_slot("0000:08:00.0"), "PCI:8:0:0");
  EXPECT_EQ(vd::bus_id_from_pci_slot("0000:0a:1f.3"), "PCI:10:31:3");
  EXPECT_EQ(vd::bus_id_from_pci_slot("0001:41:00.0"), "PCI:65@1:0:0");
  EXPECT_EQ(vd::bus_id_from_pci_slot("garbage"), "");
  EXPECT_EQ(vd::bus_id_from_pci_slot("0000:zz:00.0"), "");
  EXPECT_EQ(vd::bus_id_from_pci_slot(""), "");
}

TEST(VirtualDisplayHelpers, XorgConfIsHeadlessAtTheClientSize) {
  const auto conf = vd::render_xorg_conf("PCI:8:0:0", 2340, 1080);
  EXPECT_NE(conf.find("Driver \"nvidia\""), std::string::npos);
  EXPECT_NE(conf.find("BusID \"PCI:8:0:0\""), std::string::npos);
  EXPECT_NE(conf.find("Option \"UseDisplayDevice\" \"none\""), std::string::npos) << "NoScanout: never touch the desktop's heads";
  EXPECT_NE(conf.find("Virtual 2340 1080"), std::string::npos);
  EXPECT_NE(conf.find("Option \"AutoAddGPU\" \"false\""), std::string::npos);
  EXPECT_NE(conf.find("Option \"AutoAddDevices\" \"true\""), std::string::npos) << "seat-nova input is hot-added";
  EXPECT_EQ(conf.find("ConnectedMonitor"), std::string::npos) << "never force a connector";
  EXPECT_EQ(conf.find("CustomEDID"), std::string::npos);
}

TEST(VirtualDisplayHelpers, XauthEntryFormat) {
  const auto entry = vd::xauth_entry(21, "00ff10203040506070800a0b0c0d0e0f");
  const std::string expected = std::string {"\xff\xff\x00\x00\x00\x02" "21" "\x00\x12" "MIT-MAGIC-COOKIE-1" "\x00\x10", 30} +
                               std::string {"\x00\xff\x10\x20\x30\x40\x50\x60\x70\x80\x0a\x0b\x0c\x0d\x0e\x0f", 16};
  EXPECT_EQ(entry, expected);
}

TEST(VirtualDisplayHelpers, PicksTheFirstFreeDisplayFrom20) {
  fake_system_t sys;
  EXPECT_EQ(vd::pick_display_number(sys.ops()), 20);
  sys.taken = {20, 21};
  EXPECT_EQ(vd::pick_display_number(sys.ops()), 22);
  for (int n = 20; n < 100; ++n) {
    sys.taken.insert(n);
  }
  EXPECT_EQ(vd::pick_display_number(sys.ops()), -1);
}

TEST_F(VirtualDisplayTest, StartsARootlessSeatedServerAndTheWindowManager) {
  sys.taken = {20};
  auto server = make();
  const auto target = server.start({2340, 1080, 120});
  ASSERT_TRUE(target);
  EXPECT_EQ(target->display, ":21");
  EXPECT_EQ(target->width, 2340);
  EXPECT_EQ(target->height, 1080);
  EXPECT_EQ(target->fps, 120);
  EXPECT_EQ(target->xauthority, (dir / "X21" / "xauthority").string());
  EXPECT_TRUE(server.running());

  const auto argv = join(sys.server_argv);
  EXPECT_EQ(sys.server_argv.at(0), "/usr/lib/xorg/Xorg") << "the real server, not the console-only setuid wrapper";
  EXPECT_EQ(sys.server_argv.at(1), ":21");
  EXPECT_NE(argv.find("-seat seat-nova"), std::string::npos);
  EXPECT_NE(argv.find("-nolisten tcp"), std::string::npos);
  EXPECT_NE(argv.find("-auth " + target->xauthority), std::string::npos);
  EXPECT_NE(argv.find("-configdir " + (dir / "X21" / "conf.d").string()), std::string::npos) << "the desktop's xorg.conf.d must not apply";
  EXPECT_EQ(argv.find(" -ac"), std::string::npos) << "access control stays on";
  EXPECT_EQ(sys.server_timeout, 5000);
  EXPECT_NE(read_file(dir / "X21" / "xorg.conf").find("Virtual 2340 1080"), std::string::npos);

  ASSERT_GE(sys.runs.size(), 1u);
  for (const auto &run : sys.runs) {
    EXPECT_EQ(run.find(std::string(32, 'a')), std::string::npos) << "the cookie must never be on a command line: " << run;
  }
  EXPECT_EQ(sys.runs[0], "/usr/bin/xrandr --fb 2340x1080");
  EXPECT_EQ(env_value(sys.run_envs[0], "DISPLAY"), ":21");
  EXPECT_EQ(read_file(target->xauthority), vd::xauth_entry(21, std::string(32, 'a')));
  EXPECT_EQ(fs::status(target->xauthority).permissions() & fs::perms::all, fs::perms::owner_read | fs::perms::owner_write);

  EXPECT_EQ(join(sys.wm_argv), "/bin/sh -c exec openbox");
  EXPECT_EQ(env_value(sys.wm_env, "DISPLAY"), ":21");
  EXPECT_EQ(env_value(sys.wm_env, "XAUTHORITY"), target->xauthority);

  const auto marker = read_file(server.marker());
  EXPECT_NE(marker.find("display=21"), std::string::npos);
  EXPECT_NE(marker.find("server=1000"), std::string::npos);
  EXPECT_NE(marker.find("wm=1001"), std::string::npos);
}

TEST_F(VirtualDisplayTest, NoWindowManagerWhenEmpty) {
  auto server = make("");
  ASSERT_TRUE(server.start({1920, 1080, 60}));
  EXPECT_TRUE(sys.wm_argv.empty());
}

TEST_F(VirtualDisplayTest, StopEndsTheWindowManagerThenTheServerAndCleansUp) {
  auto server = make();
  ASSERT_TRUE(server.start({1920, 1080, 60}));
  server.stop();
  ASSERT_EQ(sys.signals.size(), 2u);
  EXPECT_EQ(sys.signals[0], std::make_pair(1001L, 15));
  EXPECT_EQ(sys.signals[1], std::make_pair(1000L, 15));
  EXPECT_FALSE(server.running());
  EXPECT_FALSE(fs::exists(server.marker()));
  EXPECT_FALSE(fs::exists(dir / "X20"));
  server.stop();  // idempotent
  EXPECT_EQ(sys.signals.size(), 2u);
}

TEST_F(VirtualDisplayTest, StopKillsAServerThatIgnoresSigterm) {
  auto server = make("");
  ASSERT_TRUE(server.start({1920, 1080, 60}));
  sys.ignores_term.insert(1000);
  server.stop();
  ASSERT_EQ(sys.signals.size(), 2u);
  EXPECT_EQ(sys.signals[1], std::make_pair(1000L, 9));
  EXPECT_FALSE(sys.alive.contains(1000));
}

TEST_F(VirtualDisplayTest, FailedServerKeepsItsLogAndLeavesNothingRunning) {
  sys.server_fails = true;
  auto server = make();
  EXPECT_FALSE(server.start({1920, 1080, 60}));
  EXPECT_FALSE(server.running());
  EXPECT_TRUE(sys.wm_argv.empty());
  EXPECT_FALSE(fs::exists(server.marker()));
  EXPECT_NE(read_file(dir / "Xorg.failed.log").find("no screens found"), std::string::npos);
}

TEST_F(VirtualDisplayTest, RefusesWithoutAnNvidiaGpuOrASize) {
  sys.bus_id.clear();
  auto server = make();
  EXPECT_FALSE(server.start({1920, 1080, 60}));
  EXPECT_TRUE(sys.server_argv.empty());
  sys.bus_id = "PCI:8:0:0";
  EXPECT_FALSE(server.start({0, 1080, 60}));
  EXPECT_TRUE(sys.server_argv.empty());
}

TEST_F(VirtualDisplayTest, ResizeFollowsANewClient) {
  auto server = make("");
  ASSERT_TRUE(server.start({2340, 1080, 120}));
  EXPECT_TRUE(server.resize({3120, 1440, 60}));
  EXPECT_EQ(sys.runs.back(), "/usr/bin/xrandr --fb 3120x1440");
  EXPECT_EQ(server.target()->width, 3120);
  EXPECT_EQ(server.target()->fps, 60);
  const auto runs = sys.runs.size();
  EXPECT_TRUE(server.resize({3120, 1440, 120}));
  EXPECT_EQ(sys.runs.size(), runs) << "same size: nothing to run";
  sys.xrandr_fails = true;
  EXPECT_FALSE(server.resize({1280, 720, 60}));
  EXPECT_EQ(server.target()->width, 3120);
}

TEST_F(VirtualDisplayTest, RecoveryKillsTheLeftoverServerFromTheMarker) {
  {
    auto crashed = make();
    ASSERT_TRUE(crashed.start({1920, 1080, 60}));
    // Nova dies here: no stop().
  }
  ASSERT_TRUE(fs::exists(dir / "active"));
  auto restarted = make();
  EXPECT_TRUE(restarted.recover());
  EXPECT_FALSE(sys.alive.contains(1000)) << "leftover Xorg";
  EXPECT_FALSE(sys.alive.contains(1001)) << "leftover openbox";
  EXPECT_FALSE(fs::exists(dir / "active"));
  EXPECT_FALSE(fs::exists(dir / "X20"));
  EXPECT_FALSE(restarted.recover()) << "nothing left the second time";
}

TEST_F(VirtualDisplayTest, RecoveryNeverKillsARecycledPid) {
  {
    auto crashed = make("");
    ASSERT_TRUE(crashed.start({1920, 1080, 60}));
  }
  sys.cmdlines[1000] = "/usr/bin/some-other-program";
  auto restarted = make();
  EXPECT_TRUE(restarted.recover());
  EXPECT_TRUE(sys.signals.empty());
  EXPECT_TRUE(sys.alive.contains(1000));
}

TEST(VirtualDisplayEnv, AppGetsTheDisplayTheSinkAndAFrameCap) {
  const vd::target_t target {":20", "/s/X20/xauthority", 2340, 1080, 120};
  const auto env = vd::app_env(target, 0, "sink-sunshine-stereo", "", "/usr/$LIB/mangohud/libMangoHud_opengl.so");
  EXPECT_EQ(env_value(env, "DISPLAY"), ":20");
  EXPECT_EQ(env_value(env, "XAUTHORITY"), "/s/X20/xauthority");
  EXPECT_EQ(env_value(env, "PULSE_SINK"), "sink-sunshine-stereo");
  EXPECT_EQ(env_value(env, "DXVK_FRAME_RATE"), "120");
  EXPECT_EQ(env_value(env, "VKD3D_FRAME_RATE"), "120");
  EXPECT_EQ(env_value(env, "__GL_SYNC_TO_VBLANK"), "0");
  EXPECT_EQ(env_value(env, "MANGOHUD"), "1");
  EXPECT_EQ(env_value(env, "MANGOHUD_CONFIG"), "fps_limit=120,no_display");
  EXPECT_EQ(env_value(env, "LD_PRELOAD"), "/usr/$LIB/mangohud/libMangoHud_opengl.so");
  for (const auto &[key, value] : env) {
    EXPECT_TRUE(std::ranges::find(vd::app_env_keys(), key) != vd::app_env_keys().end()) << key << " must be restorable";
  }
}

TEST(VirtualDisplayEnv, CapChoicesAndPreloadKept) {
  const vd::target_t target {":20", "/x", 1920, 1080, 60};
  auto fixed = vd::app_env(target, 30, "", "/opt/steam/overlay.so", "/usr/$LIB/mangohud/libMangoHud_opengl.so");
  EXPECT_EQ(env_value(fixed, "DXVK_FRAME_RATE"), "30");
  EXPECT_EQ(env_value(fixed, "PULSE_SINK"), "<unset>") << "host audio: leave the app's output alone";
  EXPECT_EQ(env_value(fixed, "LD_PRELOAD"), "/opt/steam/overlay.so:/usr/$LIB/mangohud/libMangoHud_opengl.so");

  auto off = vd::app_env(target, -1, "s", "", "/usr/$LIB/mangohud/libMangoHud_opengl.so");
  EXPECT_EQ(env_value(off, "DXVK_FRAME_RATE"), "<unset>");
  EXPECT_EQ(env_value(off, "LD_PRELOAD"), "<unset>");

  auto no_mangohud = vd::app_env(target, 0, "s", "", "");
  EXPECT_EQ(env_value(no_mangohud, "DXVK_FRAME_RATE"), "60");
  EXPECT_EQ(env_value(no_mangohud, "MANGOHUD"), "<unset>");
  EXPECT_EQ(env_value(no_mangohud, "LD_PRELOAD"), "<unset>");
}

TEST(VirtualDisplayNvfbc, HeadlessScreenGetsOneDisplayName) {
  EXPECT_EQ(vd::nvfbc_names_with_headless_fallback({}, 0, 2340, 1080), (std::vector<std::string> {"0"}));
  EXPECT_TRUE(vd::nvfbc_names_with_headless_fallback({}, 0, 0, 0).empty()) << "no screen: still nothing to capture";
  EXPECT_EQ(vd::nvfbc_names_with_headless_fallback({"0", "1"}, 2, 3840, 1080), (std::vector<std::string> {"0", "1"}));
}

#ifndef _WIN32
TEST(VirtualDisplayScope, OverridesDisplayOnlyInsideTheScope) {
  setenv("DISPLAY", ":0", 1);
  setenv("XAUTHORITY", "/home/u/.Xauthority", 1);
  {
    vd::scoped_x_env_t scope {vd::target_t {":20", "/s/X20/xauthority"}};
    EXPECT_STREQ(std::getenv("DISPLAY"), ":20");
    EXPECT_STREQ(std::getenv("XAUTHORITY"), "/s/X20/xauthority");
  }
  EXPECT_STREQ(std::getenv("DISPLAY"), ":0");
  EXPECT_STREQ(std::getenv("XAUTHORITY"), "/home/u/.Xauthority");
  {
    vd::scoped_x_env_t scope {std::nullopt};
    EXPECT_STREQ(std::getenv("DISPLAY"), ":0");
  }
}

TEST(VirtualDisplayScope, RestoresUnsetVariables) {
  unsetenv("DISPLAY");
  unsetenv("XAUTHORITY");
  {
    vd::scoped_x_env_t scope {vd::target_t {":21", "/s/X21/xauthority"}};
    EXPECT_STREQ(std::getenv("DISPLAY"), ":21");
  }
  EXPECT_EQ(std::getenv("DISPLAY"), nullptr);
  EXPECT_EQ(std::getenv("XAUTHORITY"), nullptr);
}

TEST(VirtualDisplayScope, ConcurrentScopesNeverSeeEachOther) {
  setenv("DISPLAY", ":0", 1);
  std::atomic<int> wrong {0};
  auto worker = [&wrong](const std::string &display) {
    for (int i = 0; i < 2000; ++i) {
      vd::scoped_x_env_t scope {vd::target_t {display, "/x"}};
      std::this_thread::yield();
      if (display != std::getenv("DISPLAY")) {
        ++wrong;
      }
    }
  };
  std::thread a {worker, ":20"};
  std::thread b {worker, ":21"};
  std::thread c {[&wrong]() {
    for (int i = 0; i < 2000; ++i) {
      vd::scoped_x_env_t scope {std::nullopt};  // a desktop capture
      if (std::string {":0"} != std::getenv("DISPLAY")) {
        ++wrong;
      }
    }
  }};
  a.join();
  b.join();
  c.join();
  EXPECT_EQ(wrong.load(), 0);
  EXPECT_STREQ(std::getenv("DISPLAY"), ":0");
}

#endif

TEST(VirtualDisplayScope, CaptureTargetIsSetAndCleared) {
  EXPECT_FALSE(vd::capture_target());
  vd::set_capture_target(vd::target_t {":20", "/x", 2340, 1080, 120});
  ASSERT_TRUE(vd::capture_target());
  EXPECT_EQ(vd::capture_target()->display, ":20");
  vd::set_capture_target(std::nullopt);
  EXPECT_FALSE(vd::capture_target());
}

TEST(VirtualDisplaySeat, RuleDetection) {
  const auto dir = fs::temp_directory_path() / ("nova-vd-rules-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
  fs::create_directories(dir);
  EXPECT_FALSE(vd::seat_rule_installed({dir}));
  std::ofstream {dir / "61-nova-host-vd-seat.rules"} << "\n";
  EXPECT_TRUE(vd::seat_rule_installed({dir}));
  fs::remove_all(dir);
  EXPECT_EQ(vd::input_name_suffix, " (Nova VD)");
}

TEST_F(VirtualDisplayTest, DesktopSessionGetsItsStateDirAndEnvironment) {
  vd::x_server_t server {dir, "'/usr/lib/nova-host/nova-vd-session'", sys.ops(), {{"NOVA_VD_SCALE", "150"}, {"NOVA_VD_WM", "openbox"}}};
  const auto target = server.start({2340, 1080, 60});
  ASSERT_TRUE(target);
  EXPECT_EQ(join(sys.wm_argv), "/bin/sh -c exec '/usr/lib/nova-host/nova-vd-session'");
  EXPECT_EQ(env_value(sys.wm_env, "DISPLAY"), ":20");
  EXPECT_EQ(env_value(sys.wm_env, "NOVA_VD_DIR"), (dir / "X20").string());
  EXPECT_EQ(env_value(sys.wm_env, "NOVA_VD_SCALE"), "150");
  EXPECT_EQ(env_value(sys.wm_env, "WAYLAND_DISPLAY"), "");
  // A helper run against the display (refresh after a resize, a scale change) sees the same.
  EXPECT_EQ(server.run_on_display({"/usr/lib/nova-host/nova-vd-session", "refresh"}), 0);
  EXPECT_EQ(sys.runs.back(), "/usr/lib/nova-host/nova-vd-session refresh");
  EXPECT_EQ(env_value(sys.run_envs.back(), "NOVA_VD_DIR"), (dir / "X20").string());
  EXPECT_EQ(env_value(sys.run_envs.back(), "XAUTHORITY"), target->xauthority);
  server.stop();
  EXPECT_EQ(server.run_on_display({"/bin/true"}), -1) << "nothing to run against once stopped";
}

TEST(VirtualDisplayScale, ValidScalesAndTheirEnvironment) {
  for (const int ok : {100, 125, 150, 175, 200}) {
    EXPECT_TRUE(vd::valid_scale(ok)) << ok;
  }
  for (const int bad : {0, 99, 110, 250, -100}) {
    EXPECT_FALSE(vd::valid_scale(bad)) << bad;
  }
  EXPECT_TRUE(vd::scale_env(100).empty());
  EXPECT_TRUE(vd::scale_env(130).empty());
  const auto s150 = vd::scale_env(150);
  EXPECT_EQ(env_value(s150, "GDK_SCALE"), "1");
  EXPECT_EQ(env_value(s150, "GDK_DPI_SCALE"), "1.5");
  EXPECT_EQ(env_value(s150, "QT_SCALE_FACTOR"), "1.5");
  const auto s200 = vd::scale_env(200);
  EXPECT_EQ(env_value(s200, "GDK_SCALE"), "2");
  EXPECT_EQ(env_value(s200, "GDK_DPI_SCALE"), "1");
  EXPECT_EQ(env_value(s200, "QT_SCALE_FACTOR"), "2");
}

TEST(VirtualDisplayScale, AppsLaunchedOnAScaledDisplayGetIt) {
  vd::target_t target {":20", "/x", 2340, 1080, 60};
  target.scale = 125;
  const auto env = vd::app_env(target, -1, "", "", "");
  EXPECT_EQ(env_value(env, "GDK_DPI_SCALE"), "1.25");
  EXPECT_EQ(env_value(env, "QT_SCALE_FACTOR"), "1.25");
  for (const auto &[key, value] : env) {
    EXPECT_TRUE(std::ranges::find(vd::app_env_keys(), key) != vd::app_env_keys().end()) << key << " must be restorable";
  }
  target.scale = 100;
  EXPECT_EQ(env_value(vd::app_env(target, -1, "", "", ""), "GDK_SCALE"), "<unset>");
}
