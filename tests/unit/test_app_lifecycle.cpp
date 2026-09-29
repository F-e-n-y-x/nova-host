/**
 * @file tests/unit/test_app_lifecycle.cpp
 * @brief Tests for Nova's running-app watcher (exit cleanup, idle timeout) and GET /nova/v1/running.
 */
// test includes
#include "../tests_common.h"

// standard includes
#include <chrono>

// local includes
#include <src/app_lifecycle.h>
#include <src/config.h>

using namespace std::chrono_literals;
using app_lifecycle::action_e;
using app_lifecycle::observation_t;
using app_lifecycle::tracker_t;

namespace {
  observation_t running_alone() {
    return {.app_running = true, .sessions = 0, .display_held = true, .busy = false};
  }

  observation_t streaming() {
    return {.app_running = true, .sessions = 1, .display_held = false, .busy = false};
  }
}  // namespace

TEST(AppLifecycle, DisconnectKeepsTheAppWithoutATimeout) {
  tracker_t tracker;
  const auto t0 = tracker_t::clock::time_point {} + 1h;
  EXPECT_EQ(tracker.tick(streaming(), t0, 0s), action_e::none);
  // The device left; days pass with app_idle_quit_hours = 0.
  for (auto t = t0; t < t0 + 72h; t += 1h) {
    EXPECT_EQ(tracker.tick(running_alone(), t, 0s), action_e::none);
  }
}

TEST(AppLifecycle, AppThatExitsWhileAloneStopsItsDisplay) {
  tracker_t tracker;
  const auto t0 = tracker_t::clock::time_point {} + 1h;
  EXPECT_EQ(tracker.tick(running_alone(), t0, 0s), action_e::none);
  auto gone = running_alone();
  gone.app_running = false;
  EXPECT_EQ(tracker.tick(gone, t0 + 5s, 0s), action_e::cleanup);
  // Once the display is stopped there is nothing left to do.
  gone.display_held = false;
  EXPECT_EQ(tracker.tick(gone, t0 + 10s, 0s), action_e::none);
}

TEST(AppLifecycle, MirrorAppThatExitsNeedsNoCleanup) {
  tracker_t tracker;
  observation_t gone {.app_running = false, .sessions = 0, .display_held = false, .busy = false};
  EXPECT_EQ(tracker.tick(gone, tracker_t::clock::time_point {} + 1h, 0s), action_e::none);
}

TEST(AppLifecycle, IdleQuitAfterTheConfiguredHours) {
  tracker_t tracker;
  const auto t0 = tracker_t::clock::time_point {} + 1h;
  EXPECT_EQ(tracker.tick(streaming(), t0, 2h), action_e::none);
  EXPECT_EQ(tracker.tick(running_alone(), t0 + 1min, 2h), action_e::none);
  ASSERT_TRUE(tracker.idle_since());
  EXPECT_EQ(*tracker.idle_since(), t0 + 1min);
  EXPECT_EQ(tracker.tick(running_alone(), t0 + 1min + 2h - 1s, 2h), action_e::none);
  EXPECT_EQ(tracker.tick(running_alone(), t0 + 1min + 2h, 2h), action_e::idle_quit);
}

TEST(AppLifecycle, AReconnectResetsTheIdleTimer) {
  tracker_t tracker;
  const auto t0 = tracker_t::clock::time_point {} + 1h;
  tracker.tick(running_alone(), t0, 1h);
  EXPECT_EQ(tracker.tick(streaming(), t0 + 50min, 1h), action_e::none);
  EXPECT_FALSE(tracker.idle_since());
  EXPECT_EQ(tracker.tick(running_alone(), t0 + 55min, 1h), action_e::none);
  EXPECT_EQ(tracker.tick(running_alone(), t0 + 1h + 50min, 1h), action_e::none) << "counted from the second disconnect";
  EXPECT_EQ(tracker.tick(running_alone(), t0 + 1h + 55min, 1h), action_e::idle_quit);
}

TEST(AppLifecycle, BusyLaunchOrResumeIsLeftAlone) {
  tracker_t tracker;
  observation_t launching {.app_running = false, .sessions = 0, .display_held = true, .busy = true};
  EXPECT_EQ(tracker.tick(launching, tracker_t::clock::time_point {} + 1h, 1h), action_e::none);
}

TEST(AppLifecycle, RunningJsonForAKeptApp) {
  app_lifecycle::running_t state;
  state.running = true;
  state.id = "0123456789abcdef";
  state.index = 3;
  state.appid = "1093255277";
  state.name = "Cyberpunk 2077";
  state.since = 1790000000;
  state.display = "virtual";
  state.connected_clients = 0;
  state.idle_quit_hours = 4;
  state.idle_quit_at = 1790014400;
  const auto json = app_lifecycle::running_json(state);
  EXPECT_EQ(json, nlohmann::json::parse(R"({
    "running": true,
    "app": {"id": "0123456789abcdef", "index": 3, "appid": "1093255277", "name": "Cyberpunk 2077"},
    "since": 1790000000,
    "display": "virtual",
    "connected_clients": 0,
    "idle_quit_hours": 4,
    "idle_quit_at": 1790014400
  })"));
}

TEST(AppLifecycle, RunningJsonWhenNothingRuns) {
  app_lifecycle::running_t state;
  state.connected_clients = 0;
  const auto json = app_lifecycle::running_json(state);
  EXPECT_EQ(json, nlohmann::json::parse(R"({
    "running": false, "app": null, "since": null, "display": null,
    "connected_clients": 0, "idle_quit_hours": 0, "idle_quit_at": null
  })"));
}

TEST(AppLifecycle, RunningJsonWithoutANovaId) {
  // An app whose entry changed since launch: GameStream id and name still identify it.
  app_lifecycle::running_t state;
  state.running = true;
  state.appid = "42";
  state.name = "Desktop";
  state.display = "mirror";
  state.connected_clients = 1;
  const auto json = app_lifecycle::running_json(state);
  EXPECT_TRUE(json["app"]["id"].is_null());
  EXPECT_TRUE(json["app"]["index"].is_null());
  EXPECT_EQ(json["app"]["appid"], "42");
  EXPECT_TRUE(json["since"].is_null());
  EXPECT_EQ(json["display"], "mirror");
  EXPECT_EQ(json["connected_clients"], 1);
}

TEST(AppLifecycle, IdleQuitHoursDefaultsToNeverAndIsBounded) {
  EXPECT_EQ(config::nova.app_idle_quit_hours, 0);
  config::apply_config_for_test("app_idle_quit_hours = 6\n");
  EXPECT_EQ(config::nova.app_idle_quit_hours, 6);
  config::apply_config_for_test("app_idle_quit_hours = -3\n");
  EXPECT_EQ(config::nova.app_idle_quit_hours, 6) << "out of range keeps the previous value";
  config::apply_config_for_test("app_idle_quit_hours = 0\n");
  EXPECT_EQ(config::nova.app_idle_quit_hours, 0);
}
