/**
 * @file tests/unit/test_display_follow.cpp
 * @brief Tests for Nova's session-level display-follow step.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <thread>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// local includes
#include <src/display_follow.h>

namespace fs = std::filesystem;

namespace {
  /**
   * @brief One recorded command invocation.
   */
  struct call_t {
    std::string cmd;  ///< Command path.
    std::string action;  ///< "set" or "restore".
    std::map<std::string, std::string> env;  ///< Extra environment.
  };

  /**
   * @brief Test fixture with a fake runner, a real script path and a temp marker.
   */
  class DisplayFollowTest: public ::testing::Test {
  protected:
    void SetUp() override {
      dir = fs::temp_directory_path() / ("nova-display-follow-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
      fs::create_directories(dir);
      script = (dir / "follow.sh").string();
      std::ofstream {script} << "#!/bin/sh\n";
      marker = dir / "state" / "display_follow.active";
    }

    void TearDown() override {
      std::error_code ec;
      fs::remove_all(dir, ec);
    }

    display_follow::controller_t make(int exit_code = 0) {
      return display_follow::controller_t {
        [this, exit_code](const std::string &cmd, const std::string &action, const display_follow::env_t &env) {
          call_t call {cmd, action, {}};
          for (const auto &[k, v] : env) {
            call.env[k] = v;
          }
          calls.push_back(call);
          return exit_code;
        },
        marker
      };
    }

    static display_follow::request_t phone(const std::string &mode = {}) {
      display_follow::request_t request;
      request.width = 3120;
      request.height = 1440;
      request.fps = 120;
      request.mode = mode;
      request.app_name = "Desktop (Virtual display)";
      request.client_name = "S25 Ultra";
      return request;
    }

    fs::path dir;  ///< Temp directory for this test.
    std::string script;  ///< Existing fake command path.
    fs::path marker;  ///< Marker path.
    std::vector<call_t> calls;  ///< Recorded invocations.
  };
}  // namespace

TEST_F(DisplayFollowTest, SwitchesWithClientModeAndEnv) {
  auto controller = make();
  EXPECT_EQ(controller.on_stream_request("virtual", script, phone(), false, false), display_follow::outcome_e::switched);
  ASSERT_EQ(calls.size(), 1u);
  EXPECT_EQ(calls[0].action, "set");
  EXPECT_EQ(calls[0].env["SUNSHINE_CLIENT_WIDTH"], "3120");
  EXPECT_EQ(calls[0].env["SUNSHINE_CLIENT_HEIGHT"], "1440");
  EXPECT_EQ(calls[0].env["SUNSHINE_CLIENT_FPS"], "120");
  EXPECT_EQ(calls[0].env["NOVA_DISPLAY_MODE"], "virtual");
  EXPECT_TRUE(controller.active());
  EXPECT_TRUE(fs::exists(marker));
}

TEST_F(DisplayFollowTest, ExplicitVirtualModeIsPassedThrough) {
  auto controller = make();
  controller.on_stream_request("virtual", script, phone("virtual"), false, false);
  ASSERT_EQ(calls.size(), 1u);
  EXPECT_EQ(calls[0].env["NOVA_DISPLAY_MODE"], "virtual");
}

TEST_F(DisplayFollowTest, OffOrEmptyCommandDoesNothing) {
  auto controller = make();
  EXPECT_EQ(controller.on_stream_request("off", script, phone(), false, false), display_follow::outcome_e::skipped_off);
  EXPECT_EQ(controller.on_stream_request("virtual", "", phone(), false, false), display_follow::outcome_e::skipped_off);
  EXPECT_TRUE(calls.empty());
}

TEST_F(DisplayFollowTest, MissingCommandIsSkipped) {
  auto controller = make();
  EXPECT_EQ(controller.on_stream_request("virtual", (dir / "missing.sh").string(), phone(), false, false), display_follow::outcome_e::skipped_missing_cmd);
  EXPECT_TRUE(calls.empty());
}

TEST_F(DisplayFollowTest, MirrorAlsoFollowsTheClientSize) {
  auto controller = make();
  EXPECT_EQ(controller.on_stream_request("virtual", script, phone("mirror"), false, false), display_follow::outcome_e::switched);
  ASSERT_EQ(calls.size(), 1u);
  EXPECT_TRUE(controller.active());
}

TEST_F(DisplayFollowTest, LegacyPrepAndBusyAreSkipped) {
  auto controller = make();
  EXPECT_EQ(controller.on_stream_request("virtual", script, phone(), true, false), display_follow::outcome_e::skipped_legacy);
  EXPECT_EQ(controller.on_stream_request("virtual", script, phone(), false, true), display_follow::outcome_e::skipped_busy);
  EXPECT_TRUE(calls.empty());
}

TEST_F(DisplayFollowTest, ResumeAfterReconnectSwitchesAgain) {
  auto controller = make();
  controller.on_stream_request("virtual", script, phone(), false, false);
  EXPECT_TRUE(controller.on_last_session_end(script));
  auto tablet = phone();
  tablet.width = 2560;
  tablet.height = 1600;
  tablet.fps = 60;
  EXPECT_EQ(controller.on_stream_request("virtual", script, tablet, false, false), display_follow::outcome_e::switched);
  ASSERT_EQ(calls.size(), 3u);
  EXPECT_EQ(calls[1].action, "restore");
  EXPECT_EQ(calls[2].env["SUNSHINE_CLIENT_WIDTH"], "2560");
}

TEST_F(DisplayFollowTest, RestoresOnlyWhenSwitched) {
  auto controller = make();
  EXPECT_FALSE(controller.on_last_session_end(script));
  EXPECT_TRUE(calls.empty());

  controller.on_stream_request("virtual", script, phone(), false, false);
  EXPECT_TRUE(controller.on_last_session_end(script));
  ASSERT_EQ(calls.size(), 2u);
  EXPECT_EQ(calls[1].action, "restore");
  EXPECT_FALSE(controller.active());
  EXPECT_FALSE(fs::exists(marker));
}

TEST_F(DisplayFollowTest, FailedSetIsNotActive) {
  auto controller = make(1);
  EXPECT_EQ(controller.on_stream_request("virtual", script, phone(), false, false), display_follow::outcome_e::failed);
  EXPECT_FALSE(controller.active());
  EXPECT_FALSE(controller.on_last_session_end(script));
  EXPECT_EQ(calls.size(), 1u);
}

TEST_F(DisplayFollowTest, RecoversAfterCrash) {
  {
    auto crashed = make();
    crashed.on_stream_request("virtual", script, phone(), false, false);
  }
  ASSERT_TRUE(fs::exists(marker));
  calls.clear();

  auto restarted = make();
  EXPECT_TRUE(restarted.recover(script));
  ASSERT_EQ(calls.size(), 1u);
  EXPECT_EQ(calls[0].action, "restore");
  EXPECT_EQ(calls[0].cmd, script);
  EXPECT_FALSE(fs::exists(marker));

  EXPECT_FALSE(restarted.recover(script));
  EXPECT_EQ(calls.size(), 1u);
}

TEST(DisplayFollowHelpers, LegacyPrepDetection) {
  const std::string cmd = "/usr/local/bin/sunshine-resolution.sh";
  EXPECT_TRUE(display_follow::is_legacy_prep("/usr/local/bin/sunshine-resolution.sh set", cmd));
  EXPECT_TRUE(display_follow::is_legacy_prep("/usr/local/bin/sunshine-resolution.sh virtual", cmd));
  EXPECT_FALSE(display_follow::is_legacy_prep("/usr/local/bin/sunshine-resolution.sh restore", cmd));
  EXPECT_FALSE(display_follow::is_legacy_prep("/usr/bin/other.sh set", cmd));
  EXPECT_FALSE(display_follow::is_legacy_prep("/usr/local/bin/sunshine-resolution.sh set", ""));
}

TEST(DisplayFollowHelpers, EnvDefaultsToVirtual) {
  display_follow::request_t request;
  request.width = 1920;
  request.height = 1080;
  request.fps = 60;
  std::map<std::string, std::string> env;
  for (const auto &[k, v] : display_follow::make_env(request)) {
    env[k] = v;
  }
  EXPECT_EQ(env["NOVA_DISPLAY_MODE"], "virtual");
  EXPECT_EQ(env["SUNSHINE_CLIENT_FPS"], "60");
}

namespace {
  /**
   * @brief Fake virtual-display backend that records what the controller asked for.
   */
  class fake_backend_t: public display_follow::virtual_backend_t {
  public:
    std::optional<virtual_display::target_t> start(const display_follow::request_t &request) override {
      log.push_back("start " + std::to_string(request.width) + "x" + std::to_string(request.height));
      if (fail_start) {
        return std::nullopt;
      }
      running = true;
      return virtual_display::target_t {":20", "/state/X20/xauthority", request.width, request.height, request.fps};
    }

    std::optional<virtual_display::target_t> resize(const display_follow::request_t &request) override {
      log.push_back("resize " + std::to_string(request.width) + "x" + std::to_string(request.height));
      if (fail_resize) {
        return std::nullopt;
      }
      return virtual_display::target_t {":20", "/state/X20/xauthority", request.width, request.height, request.fps};
    }

    void stop() override {
      log.push_back("stop");
      running = false;
    }

    bool recover() override {
      log.push_back("recover");
      return false;
    }

    bool set_scale(int percent) override {
      log.push_back("scale " + std::to_string(percent));
      return !fail_scale;
    }

    std::vector<std::string> log;  ///< Calls in order (shared with the hooks).
    bool running = false;  ///< Whether a display is up.
    bool fail_start = false;  ///< Make start() fail.
    bool fail_resize = false;  ///< Make resize() fail.
    bool fail_scale = false;  ///< Make set_scale() fail.
  };

  /**
   * @brief Fixture for Virtual display streams: fake runner, fake backend and recording hooks.
   */
  class DisplayFollowVirtualTest: public DisplayFollowTest {
  protected:
    /**
     * @brief Scheduler whose timer the test fires by hand.
     */
    display_follow::scheduler_t fake_scheduler() {
      return {
        [this](std::chrono::milliseconds delay, std::function<void()> fn) {
          scheduled_delay = delay;
          pending = std::move(fn);
          ++schedules;
        },
        [this]() {
          pending = nullptr;
          ++cancels;
        },
      };
    }

    /**
     * @brief Run the armed linger callback (as the timer would when it expires).
     */
    void fire() {
      ASSERT_TRUE(pending) << "no linger armed";
      auto fn = std::move(pending);
      pending = nullptr;
      fn();
    }

    display_follow::controller_t make_virtual() {
      backend = std::make_shared<fake_backend_t>();
      display_follow::virtual_hooks_t hooks;
      hooks.up = [this](const virtual_display::target_t &target) {
        backend->log.push_back("up " + target.display);
      };
      hooks.before_stop = [this]() {
        backend->log.push_back("end app");
      };
      hooks.down = [this]() {
        backend->log.push_back("down");
      };
      return display_follow::controller_t {
        [this](const std::string &cmd, const std::string &action, const display_follow::env_t &env) {
          call_t call {cmd, action, {}};
          for (const auto &[k, v] : env) {
            call.env[k] = v;
          }
          calls.push_back(call);
          return 0;
        },
        marker,
        backend,
        hooks,
        fake_scheduler()
      };
    }

    std::shared_ptr<fake_backend_t> backend;  ///< Backend of the last make_virtual().
    std::function<void()> pending;  ///< Armed linger callback.
    std::chrono::milliseconds scheduled_delay {0};  ///< Delay of the last schedule.
    int schedules = 0;  ///< schedule() calls.
    int cancels = 0;  ///< cancel() calls.
  };
}  // namespace

TEST_F(DisplayFollowVirtualTest, VirtualStartsItsOwnDisplayAndLeavesTheDesktopAlone) {
  auto controller = make_virtual();
  EXPECT_EQ(controller.on_stream_request("virtual", script, phone("virtual"), false, false, "headless_x"), display_follow::outcome_e::virtual_started);
  EXPECT_TRUE(calls.empty()) << "the desktop script must not run for a virtual display";
  ASSERT_TRUE(controller.virtual_target());
  EXPECT_EQ(controller.virtual_target()->display, ":20");
  EXPECT_EQ(controller.virtual_target()->width, 3120);
  EXPECT_EQ(controller.virtual_target()->fps, 120);
  EXPECT_EQ(backend->log, (std::vector<std::string> {"start 3120x1440", "up :20"}));
}

TEST_F(DisplayFollowVirtualTest, MirrorStillRunsTheScript) {
  auto controller = make_virtual();
  EXPECT_EQ(controller.on_stream_request("virtual", script, phone("mirror"), false, false, "headless_x"), display_follow::outcome_e::switched);
  ASSERT_EQ(calls.size(), 1u);
  EXPECT_EQ(calls[0].action, "set");
  EXPECT_EQ(calls[0].env["NOVA_DISPLAY_MODE"], "mirror");
  EXPECT_TRUE(backend->log.empty());
  EXPECT_FALSE(controller.virtual_target());
  EXPECT_TRUE(controller.on_last_session_end(script));
  EXPECT_EQ(calls.back().action, "restore");
}

TEST_F(DisplayFollowVirtualTest, NoModeOrBackendOffKeepsTheScriptBehaviour) {
  auto controller = make_virtual();
  EXPECT_EQ(controller.on_stream_request("virtual", script, phone(), false, false, "headless_x"), display_follow::outcome_e::switched);
  EXPECT_TRUE(controller.on_last_session_end(script));
  EXPECT_EQ(controller.on_stream_request("virtual", script, phone("virtual"), false, false, "off"), display_follow::outcome_e::switched);
  EXPECT_TRUE(backend->log.empty());
  EXPECT_EQ(calls.size(), 3u);
}

TEST_F(DisplayFollowVirtualTest, FeatureOffDoesNothing) {
  auto controller = make_virtual();
  EXPECT_EQ(controller.on_stream_request("off", script, phone("virtual"), false, false, "headless_x"), display_follow::outcome_e::skipped_off);
  EXPECT_TRUE(backend->log.empty());
}

TEST_F(DisplayFollowVirtualTest, LastDisconnectEndsTheAppThenStopsTheDisplay) {
  auto controller = make_virtual();
  controller.on_stream_request("virtual", script, phone("virtual"), false, false, "headless_x");
  EXPECT_TRUE(controller.on_last_session_end(script));
  EXPECT_EQ(backend->log, (std::vector<std::string> {"start 3120x1440", "up :20", "end app", "stop", "down"}));
  EXPECT_FALSE(controller.virtual_target());
  EXPECT_TRUE(calls.empty());
  EXPECT_FALSE(controller.on_last_session_end(script)) << "a second end is a no-op";
}

TEST_F(DisplayFollowVirtualTest, SecondDeviceJoinsTheRunningDisplay) {
  auto controller = make_virtual();
  controller.on_stream_request("virtual", script, phone("virtual"), false, false, "headless_x");
  auto tablet = phone("virtual");
  tablet.width = 2560;
  EXPECT_EQ(controller.on_stream_request("virtual", script, tablet, false, true, "headless_x"), display_follow::outcome_e::skipped_busy);
  EXPECT_EQ(backend->log.size(), 2u) << "no second display and no resize under a live stream";
  EXPECT_EQ(controller.virtual_target()->width, 3120);
}

TEST_F(DisplayFollowVirtualTest, ReconnectResizesTheRunningDisplay) {
  auto controller = make_virtual();
  controller.on_stream_request("virtual", script, phone("virtual"), false, false, "headless_x");
  auto tablet = phone("virtual");
  tablet.width = 2560;
  tablet.height = 1600;
  EXPECT_EQ(controller.on_stream_request("virtual", script, tablet, false, false, "headless_x"), display_follow::outcome_e::virtual_reused);
  EXPECT_EQ(controller.virtual_target()->width, 2560);
  EXPECT_EQ(backend->log.back(), "up :20");
  EXPECT_EQ(backend->log[2], "resize 2560x1600");
}

TEST_F(DisplayFollowVirtualTest, FailedResizeStartsAFreshDisplay) {
  auto controller = make_virtual();
  controller.on_stream_request("virtual", script, phone("virtual"), false, false, "headless_x");
  backend->fail_resize = true;
  EXPECT_EQ(controller.on_stream_request("virtual", script, phone("virtual"), false, false, "headless_x"), display_follow::outcome_e::virtual_started);
  EXPECT_EQ(backend->log, (std::vector<std::string> {"start 3120x1440", "up :20", "resize 3120x1440", "end app", "stop", "down", "start 3120x1440", "up :20"}));
}

TEST_F(DisplayFollowVirtualTest, FailedStartLeavesTheDesktopAlone) {
  auto controller = make_virtual();
  backend->fail_start = true;
  EXPECT_EQ(controller.on_stream_request("virtual", script, phone("virtual"), false, false, "headless_x"), display_follow::outcome_e::failed);
  EXPECT_TRUE(calls.empty()) << "no fallback resize of the desktop";
  EXPECT_FALSE(controller.virtual_target());
  EXPECT_FALSE(controller.on_last_session_end(script));
}

TEST_F(DisplayFollowVirtualTest, VirtualAfterMirrorPutsTheDesktopBackFirst) {
  auto controller = make_virtual();
  controller.on_stream_request("virtual", script, phone("mirror"), false, false, "headless_x");
  EXPECT_EQ(controller.on_stream_request("virtual", script, phone("virtual"), false, false, "headless_x"), display_follow::outcome_e::virtual_started);
  ASSERT_EQ(calls.size(), 2u);
  EXPECT_EQ(calls[1].action, "restore");
  EXPECT_FALSE(controller.active());
}

TEST_F(DisplayFollowVirtualTest, StartupRecoveryAsksTheBackend) {
  auto controller = make_virtual();
  EXPECT_FALSE(controller.recover(script));
  EXPECT_EQ(backend->log, (std::vector<std::string> {"recover"}));
}

// ---- Linger: a quick reconnect (Nebula's live resolution change) resumes the same display ----

using namespace std::chrono_literals;

TEST_F(DisplayFollowVirtualTest, LingerKeepsTheDisplayAndItsAppAfterTheLastDisconnect) {
  auto controller = make_virtual();
  controller.on_stream_request("virtual", script, phone("virtual"), false, false, "headless_x");
  EXPECT_TRUE(controller.on_last_session_end(script, 30s));
  EXPECT_TRUE(controller.lingering());
  EXPECT_EQ(scheduled_delay, 30s);
  EXPECT_EQ(backend->log, (std::vector<std::string> {"start 3120x1440", "up :20"})) << "nothing ended or stopped yet";
  ASSERT_TRUE(controller.virtual_target());
}

TEST_F(DisplayFollowVirtualTest, ResumeWithinTheLingerResizesTheSameDisplay) {
  auto controller = make_virtual();
  controller.on_stream_request("virtual", script, phone("virtual"), false, false, "headless_x");
  controller.on_last_session_end(script, 30s);
  auto landscape = phone("virtual");
  landscape.width = 2340;
  landscape.height = 1080;
  landscape.fps = 60;
  EXPECT_EQ(controller.on_stream_request("virtual", script, landscape, false, false, "headless_x"), display_follow::outcome_e::virtual_reused);
  EXPECT_FALSE(controller.lingering());
  EXPECT_EQ(cancels, 1);
  EXPECT_EQ(backend->log, (std::vector<std::string> {"start 3120x1440", "up :20", "resize 2340x1080", "up :20"}));
  EXPECT_EQ(controller.virtual_target()->width, 2340);
  EXPECT_EQ(controller.virtual_target()->fps, 60);
  // The timer callback of the cancelled linger must not tear the resumed display down.
  auto stale = std::move(pending);
  if (stale) {
    stale();
  }
  EXPECT_TRUE(controller.virtual_target());
  EXPECT_EQ(backend->log.size(), 4u);
}

TEST_F(DisplayFollowVirtualTest, StaleLingerCallbackIsIgnored) {
  auto controller = make_virtual();
  controller.on_stream_request("virtual", script, phone("virtual"), false, false, "headless_x");
  controller.on_last_session_end(script, 30s);
  auto first = pending;  // the timer thread may already hold it when a resume arrives
  controller.on_stream_request("virtual", script, phone("virtual"), false, false, "headless_x");
  first();
  EXPECT_TRUE(controller.virtual_target());
  EXPECT_EQ(std::count(backend->log.begin(), backend->log.end(), "stop"), 0);
}

TEST_F(DisplayFollowVirtualTest, LingerExpiryEndsTheAppAndStopsTheDisplay) {
  auto controller = make_virtual();
  controller.on_stream_request("virtual", script, phone("virtual"), false, false, "headless_x");
  controller.on_last_session_end(script, 30s);
  fire();
  EXPECT_FALSE(controller.lingering());
  EXPECT_FALSE(controller.virtual_target());
  EXPECT_EQ(backend->log, (std::vector<std::string> {"start 3120x1440", "up :20", "end app", "stop", "down"}));
}

TEST_F(DisplayFollowVirtualTest, QuitDuringTheLingerTearsDownAtOnce) {
  auto controller = make_virtual();
  controller.on_stream_request("virtual", script, phone("virtual"), false, false, "headless_x");
  controller.on_last_session_end(script, 30s);
  EXPECT_TRUE(controller.end_now(script));
  EXPECT_FALSE(controller.lingering());
  EXPECT_FALSE(controller.virtual_target());
  EXPECT_EQ(cancels, 1);
  EXPECT_EQ(backend->log, (std::vector<std::string> {"start 3120x1440", "up :20", "end app", "stop", "down"}));
  EXPECT_FALSE(pending);
}

TEST_F(DisplayFollowVirtualTest, ZeroLingerKeepsTheOldBehaviour) {
  auto controller = make_virtual();
  controller.on_stream_request("virtual", script, phone("virtual"), false, false, "headless_x");
  EXPECT_TRUE(controller.on_last_session_end(script, 0s));
  EXPECT_EQ(schedules, 0);
  EXPECT_FALSE(controller.virtual_target());
  EXPECT_EQ(backend->log.back(), "down");
}

TEST_F(DisplayFollowVirtualTest, NothingToKeepMeansNoLinger) {
  auto controller = make_virtual();
  EXPECT_FALSE(controller.on_last_session_end(script, 30s));
  EXPECT_EQ(schedules, 0);
  EXPECT_FALSE(controller.lingering());
}

TEST_F(DisplayFollowVirtualTest, MirrorResumeWithinTheLingerSwitchesWithoutRestoring) {
  auto controller = make_virtual();
  controller.on_stream_request("virtual", script, phone("mirror"), false, false, "headless_x");
  controller.on_last_session_end(script, 30s);
  EXPECT_EQ(calls.size(), 1u) << "no restore while waiting for a reconnect";
  auto small = phone("mirror");
  small.width = 1280;
  small.height = 720;
  EXPECT_EQ(controller.on_stream_request("virtual", script, small, false, false, "headless_x"), display_follow::outcome_e::switched);
  ASSERT_EQ(calls.size(), 2u);
  EXPECT_EQ(calls[1].action, "set") << "straight to the new mode, the desktop never flickers back";
  EXPECT_EQ(calls[1].env["SUNSHINE_CLIENT_WIDTH"], "1280");
  EXPECT_TRUE(controller.active());
  EXPECT_FALSE(controller.lingering());
}

TEST_F(DisplayFollowVirtualTest, MirrorLingerExpiryRestoresTheDesktop) {
  auto controller = make_virtual();
  controller.on_stream_request("virtual", script, phone("mirror"), false, false, "headless_x");
  controller.on_last_session_end(script, 30s);
  fire();
  ASSERT_EQ(calls.size(), 2u);
  EXPECT_EQ(calls[1].action, "restore");
  EXPECT_FALSE(controller.active());
  EXPECT_FALSE(fs::exists(marker));
}

TEST_F(DisplayFollowVirtualTest, MirrorQuitDuringTheLingerRestoresAtOnce) {
  auto controller = make_virtual();
  controller.on_stream_request("virtual", script, phone("mirror"), false, false, "headless_x");
  controller.on_last_session_end(script, 30s);
  EXPECT_TRUE(controller.end_now(script));
  ASSERT_EQ(calls.size(), 2u);
  EXPECT_EQ(calls[1].action, "restore");
}

TEST_F(DisplayFollowVirtualTest, MirrorAfterALingeringVirtualDisplayStopsIt) {
  auto controller = make_virtual();
  controller.on_stream_request("virtual", script, phone("virtual"), false, false, "headless_x");
  controller.on_last_session_end(script, 30s);
  EXPECT_EQ(controller.on_stream_request("virtual", script, phone("mirror"), false, false, "headless_x"), display_follow::outcome_e::switched);
  EXPECT_FALSE(controller.virtual_target());
  EXPECT_EQ(backend->log, (std::vector<std::string> {"start 3120x1440", "up :20", "end app", "stop", "down"}));
  EXPECT_EQ(calls.back().action, "set");
}

TEST_F(DisplayFollowVirtualTest, RealTimerFiresAfterTheDelay) {
  // The built-in timer (no injected scheduler) tears down after the linger.
  auto real_backend = std::make_shared<fake_backend_t>();
  display_follow::controller_t controller {
    [](const std::string &, const std::string &, const display_follow::env_t &) {
      return 0;
    },
    marker,
    real_backend
  };
  controller.on_stream_request("virtual", script, phone("virtual"), false, false, "headless_x");
  controller.on_last_session_end(script, 50ms);
  EXPECT_TRUE(controller.virtual_target());
  for (int i = 0; i < 100 && controller.virtual_target(); ++i) {
    std::this_thread::sleep_for(10ms);
  }
  EXPECT_FALSE(controller.virtual_target());
  EXPECT_FALSE(controller.lingering());
  // Re-armed and cancelled: nothing happens.
  controller.on_stream_request("virtual", script, phone("virtual"), false, false, "headless_x");
  controller.on_last_session_end(script, 50ms);
  controller.on_stream_request("virtual", script, phone("virtual"), false, false, "headless_x");
  std::this_thread::sleep_for(150ms);
  EXPECT_TRUE(controller.virtual_target());
}

// ---- Display scale (/display-scale) ----

TEST_F(DisplayFollowVirtualTest, ScaleNeedsAVirtualDisplay) {
  auto controller = make_virtual();
  EXPECT_EQ(controller.set_scale(150), display_follow::scale_result_e::not_virtual);
  controller.on_stream_request("virtual", script, phone("mirror"), false, false, "headless_x");
  EXPECT_EQ(controller.set_scale(150), display_follow::scale_result_e::not_virtual) << "Mirror never changes the desktop's scale";
  EXPECT_EQ(calls.size(), 1u);
}

TEST_F(DisplayFollowVirtualTest, ScaleIsValidatedAndApplied) {
  auto controller = make_virtual();
  controller.on_stream_request("virtual", script, phone("virtual"), false, false, "headless_x");
  EXPECT_EQ(controller.virtual_target()->scale, 100);
  EXPECT_EQ(controller.set_scale(130), display_follow::scale_result_e::invalid);
  EXPECT_EQ(controller.set_scale(0), display_follow::scale_result_e::invalid);
  EXPECT_EQ(controller.set_scale(150), display_follow::scale_result_e::applied);
  EXPECT_EQ(controller.virtual_target()->scale, 150);
  EXPECT_EQ(backend->log.back(), "scale 150");
  EXPECT_EQ(controller.set_scale(150), display_follow::scale_result_e::applied);
  EXPECT_EQ(backend->log.back(), "scale 150");
  EXPECT_EQ(std::count(backend->log.begin(), backend->log.end(), "scale 150"), 1) << "same scale is not re-applied";
  backend->fail_scale = true;
  EXPECT_EQ(controller.set_scale(200), display_follow::scale_result_e::failed);
  EXPECT_EQ(controller.virtual_target()->scale, 150);
}

TEST_F(DisplayFollowVirtualTest, StartAndResumeCarryTheDeviceScale) {
  auto controller = make_virtual();
  auto request = phone("virtual");
  request.scale = 175;
  controller.on_stream_request("virtual", script, request, false, false, "headless_x");
  EXPECT_EQ(controller.virtual_target()->scale, 175);
  controller.on_last_session_end(script, 30s);
  request.scale = 125;
  controller.on_stream_request("virtual", script, request, false, false, "headless_x");
  EXPECT_EQ(controller.virtual_target()->scale, 125);
  EXPECT_EQ(backend->log[3], "scale 125");
}

TEST(DisplayScaleStore, RoundTripsPerDevice) {
  const auto dir = fs::temp_directory_path() / ("nova-scale-store-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
  fs::remove_all(dir);
  const auto file = dir / "virtual-display-scale.json";
  EXPECT_EQ(display_follow::stored_scale(file, "aaaa"), 100);
  EXPECT_TRUE(display_follow::store_scale(file, "aaaa", 150));
  EXPECT_TRUE(display_follow::store_scale(file, "bbbb", 200));
  EXPECT_FALSE(display_follow::store_scale(file, "cccc", 110));
  EXPECT_FALSE(display_follow::store_scale(file, "", 150));
  EXPECT_EQ(display_follow::stored_scale(file, "aaaa"), 150);
  EXPECT_EQ(display_follow::stored_scale(file, "bbbb"), 200);
  EXPECT_EQ(display_follow::stored_scale(file, "cccc"), 100);
  EXPECT_EQ(fs::status(file).permissions() & fs::perms::all, fs::perms::owner_read | fs::perms::owner_write);
  std::ofstream {file} << "not json";
  EXPECT_EQ(display_follow::stored_scale(file, "aaaa"), 100);
  EXPECT_TRUE(display_follow::store_scale(file, "aaaa", 125)) << "a corrupt file is replaced";
  EXPECT_EQ(display_follow::stored_scale(file, "aaaa"), 125);
  fs::remove_all(dir);
}

TEST(DisplayScaleStore, DeviceKeyIsStable) {
  EXPECT_EQ(display_follow::device_key(""), "");
  const auto key = display_follow::device_key("-----BEGIN CERTIFICATE-----\nabc\n-----END CERTIFICATE-----\n");
  EXPECT_EQ(key.size(), 16u);
  EXPECT_EQ(key, display_follow::device_key("-----BEGIN CERTIFICATE-----\nabc\n-----END CERTIFICATE-----\n"));
  EXPECT_NE(key, display_follow::device_key("-----BEGIN CERTIFICATE-----\nabd\n-----END CERTIFICATE-----\n"));
  EXPECT_EQ(display_follow::device_key("a"), "af63dc4c8601ec8c");  // FNV-1a 64 test vector
}
