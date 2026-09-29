/**
 * @file src/app_lifecycle.h
 * @brief Nova: the running app outlives a disconnect; this watches it while no device streams.
 *
 * A disconnect never ends the app (Mirror or Virtual display). It keeps running until it is
 * quit (/cancel, "Close app", Nebula's Quit), exits by itself, or, when `app_idle_quit_hours` is
 * set, has had no device connected for that long. While no device streams nothing else polls the
 * app, so a small watcher notices when it exits (and stops the virtual display kept for it) and
 * applies the idle timeout.
 */
#pragma once

// standard includes
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <shared_mutex>
#include <string>

// lib includes
#include <nlohmann/json.hpp>

namespace app_lifecycle {
  /**
   * @brief What the watcher should do after one look at the host.
   */
  enum class action_e {
    none,  ///< Nothing to do.
    cleanup,  ///< The app is gone while a virtual display is still kept for it: stop the display.
    idle_quit,  ///< No device for app_idle_quit_hours: end the app (then stop its display).
  };

  /**
   * @brief One look at the host, as the watcher sees it.
   */
  struct observation_t {
    bool app_running = false;  ///< An app runs (proc::proc.running() > 0).
    int sessions = 0;  ///< Stream sessions, connected or starting.
    bool display_held = false;  ///< A virtual display is kept for the app with nobody connected.
    bool busy = false;  ///< A launch or resume is being handled: leave everything alone.
  };

  /**
   * @brief The watcher's decisions, without threads or globals (unit-tested).
   */
  class tracker_t {
  public:
    using clock = std::chrono::steady_clock;  ///< Clock for the idle timeout.

    /**
     * @brief Decide what to do now.
     *
     * The idle timer starts when an app runs with no session, and is reset by any session.
     *
     * @param seen What the host looks like.
     * @param now Current time.
     * @param idle_quit Idle timeout; zero never quits.
     * @return The action to take.
     */
    action_e tick(const observation_t &seen, clock::time_point now, std::chrono::seconds idle_quit);

    /**
     * @brief When the app was last left with no device.
     *
     * @return The time, or nullopt while a device streams or nothing runs.
     */
    std::optional<clock::time_point> idle_since() const;

  private:
    std::optional<clock::time_point> idle_since_;
  };

  /**
   * @brief Everything GET /nova/v1/running reports.
   */
  struct running_t {
    bool running = false;  ///< An app runs.
    std::string id;  ///< Nova app id (see nova_api::app_id()), empty when unknown.
    std::optional<std::size_t> index;  ///< Position in apps.json.
    std::string appid;  ///< GameStream app id (for /launch and /resume).
    std::string name;  ///< App name.
    std::int64_t since = 0;  ///< Unix time the app was started, 0 when unknown.
    std::string display;  ///< "virtual" or "mirror".
    int connected_clients = 0;  ///< Stream sessions right now.
    int idle_quit_hours = 0;  ///< app_idle_quit_hours (0 = never).
    std::optional<std::int64_t> idle_quit_at;  ///< Unix time the app will be idle-quit, when a timeout is counting.
    bool tracked = true;  ///< False when Nova can't see the app's processes (it detached): it runs until quit.
  };

  /**
   * @brief Body of GET /nova/v1/running.
   *
   * @param state The running app.
   * @return `{running, app: {id, index, appid, name} | null, since, display, connected_clients, idle_quit_hours, idle_quit_at, tracked}`.
   */
  nlohmann::json running_json(const running_t &state);

  /**
   * @brief The running app as the host sees it now (for /nova/v1/running and /api/apps/running).
   *
   * @return Current state.
   */
  running_t current();

  /**
   * @brief Unix time the app will be idle-quit, if a timeout is counting now.
   *
   * @return Unix seconds, or nullopt (no timeout set, a device streams, or nothing runs).
   */
  std::optional<std::int64_t> idle_quit_at();

  /**
   * @brief Held by a request that starts, resumes or ends the app (/launch, /resume, /cancel,
   * "Close app"): the watcher skips its step meanwhile, so it never stops a display about to be
   * used or looks at the app while the request changes it. Requests don't block each other.
   */
  class busy_guard_t {
  public:
    busy_guard_t();
    ~busy_guard_t();
    busy_guard_t(const busy_guard_t &) = delete;
    busy_guard_t &operator=(const busy_guard_t &) = delete;

  private:
    std::shared_lock<std::shared_mutex> lock_;
  };

  /**
   * @brief Start the watcher thread.
   */
  void start();

  /**
   * @brief Stop the watcher thread (Nova exits).
   */
  void stop();

  /**
   * @brief Run one watcher step now (also used by the watcher thread).
   */
  void check_now();
}  // namespace app_lifecycle
