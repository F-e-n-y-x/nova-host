/**
 * @file tests/unit/test_display_follow.cpp
 * @brief Tests for Nova's session-level display-follow step.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <filesystem>
#include <fstream>
#include <map>
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
