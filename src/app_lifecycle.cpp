/**
 * @file src/app_lifecycle.cpp
 * @brief Nova: watches the running app while no device streams (exit cleanup, idle timeout).
 */
// standard includes
#include <atomic>
#include <shared_mutex>
#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <thread>

// local includes
#include "app_lifecycle.h"
#include "config.h"
#include "display_follow.h"
#include "file_handler.h"
#include "library/library.h"
#include "logging.h"
#include "nova_client_api.h"
#include "process.h"
#include "rtsp.h"

using namespace std::literals;

namespace app_lifecycle {
  action_e tracker_t::tick(const observation_t &seen, const clock::time_point now, const std::chrono::seconds idle_quit) {
    if (seen.busy) {
      return action_e::none;
    }
    if (seen.sessions > 0) {
      idle_since_.reset();
      return action_e::none;
    }
    if (!seen.app_running) {
      idle_since_.reset();
      return seen.display_held ? action_e::cleanup : action_e::none;
    }
    if (!idle_since_) {
      idle_since_ = now;
    }
    if (idle_quit.count() > 0 && now - *idle_since_ >= idle_quit) {
      idle_since_.reset();
      return action_e::idle_quit;
    }
    return action_e::none;
  }

  std::optional<tracker_t::clock::time_point> tracker_t::idle_since() const {
    return idle_since_;
  }

  nlohmann::json running_json(const running_t &state) {
    nlohmann::json app = nullptr;
    if (state.running) {
      app = {
        {"id", state.id.empty() ? nlohmann::json(nullptr) : nlohmann::json(state.id)},
        {"index", state.index ? nlohmann::json(*state.index) : nlohmann::json(nullptr)},
        {"appid", state.appid},
        {"name", state.name},
      };
    }
    return {
      {"running", state.running},
      {"app", std::move(app)},
      {"since", state.running && state.since > 0 ? nlohmann::json(state.since) : nlohmann::json(nullptr)},
      {"display", state.running ? nlohmann::json(state.display) : nlohmann::json(nullptr)},
      {"connected_clients", state.connected_clients},
      {"idle_quit_hours", state.idle_quit_hours},
      {"idle_quit_at", state.running && state.idle_quit_at ? nlohmann::json(*state.idle_quit_at) : nlohmann::json(nullptr)},
    };
  }

  namespace {
    constexpr auto WATCH_INTERVAL = 5s;

    /**
     * @brief Shared by requests that change the app, taken exclusively (without waiting) by the watcher.
     */
    std::shared_mutex &busy_mutex() {
      static std::shared_mutex m;
      return m;
    }

    std::mutex &tracker_mutex() {
      static std::mutex m;
      return m;
    }

    tracker_t &tracker() {
      static tracker_t t;
      return t;
    }

    std::chrono::seconds idle_quit_timeout() {
      return std::chrono::hours {std::max(0, config::nova.app_idle_quit_hours)};
    }

    /**
     * @brief The watcher thread.
     */
    class watcher_t {
    public:
      void start() {
        std::lock_guard lg {mutex_};
        if (thread_.joinable()) {
          return;
        }
        thread_ = std::jthread {[this](std::stop_token stop) {
          std::mutex m;
          std::unique_lock lk {m};
          while (!stop.stop_requested()) {
            cv_.wait_for(lk, stop, WATCH_INTERVAL, [] {
              return false;  // only the interval or a stop request wakes it
            });
            if (stop.stop_requested()) {
              break;
            }
            check_now();
          }
        }};
      }

      void stop() {
        std::lock_guard lg {mutex_};
        if (thread_.joinable()) {
          thread_.request_stop();
          cv_.notify_all();
          thread_.join();
        }
      }

    private:
      std::mutex mutex_;
      std::condition_variable_any cv_;
      std::jthread thread_;
    };

    watcher_t &watcher() {
      static watcher_t w;
      return w;
    }
  }  // namespace

  busy_guard_t::busy_guard_t():
      lock_ {busy_mutex()} {
  }

  busy_guard_t::~busy_guard_t() = default;

  std::optional<std::int64_t> idle_quit_at() {
    const auto timeout = idle_quit_timeout();
    if (timeout.count() <= 0) {
      return std::nullopt;
    }
    std::optional<tracker_t::clock::time_point> since;
    {
      std::lock_guard lg {tracker_mutex()};
      since = tracker().idle_since();
    }
    if (!since) {
      return std::nullopt;
    }
    const auto left = *since + timeout - tracker_t::clock::now();
    const auto at = std::chrono::system_clock::now() + std::chrono::duration_cast<std::chrono::system_clock::duration>(left);
    return std::chrono::duration_cast<std::chrono::seconds>(at.time_since_epoch()).count();
  }

  running_t current() {
    running_t state;
    state.connected_clients = rtsp_stream::session_count();
    state.idle_quit_hours = config::nova.app_idle_quit_hours;
    const auto running_id = proc::proc.running();
    if (running_id <= 0) {
      return state;
    }
    state.running = true;
    state.appid = std::to_string(running_id);
    state.name = proc::proc.get_last_run_app_name();
    state.since = proc::proc.started_at();
    state.display = display_follow::virtual_target() ? "virtual" : "mirror";
    state.idle_quit_at = idle_quit_at();
    const auto &apps = proc::proc.get_apps();
    for (std::size_t i = 0; i < apps.size(); ++i) {
      if (apps[i].id == state.appid) {
        state.index = i;
        break;
      }
    }
    if (state.index) {
      try {
        std::scoped_lock lock(library::apps_file_mutex());
        const auto tree = nlohmann::json::parse(file_handler::read_file(config::stream.file_apps.c_str()));
        if (tree.contains("apps") && tree["apps"].is_array() && *state.index < tree["apps"].size()) {
          state.id = nova_api::app_id(tree["apps"][*state.index]);
        }
      } catch (const std::exception &) {
        // No Nova id then; the GameStream id and the name still identify the app.
      }
    }
    return state;
  }

  void check_now() {
    // Requests that change the app wait while this runs; when one is in flight, skip this round.
    std::unique_lock busy {busy_mutex(), std::try_to_lock};
    observation_t seen;
    seen.busy = !busy.owns_lock() || proc::proc.executing();
    if (!seen.busy) {
      seen.sessions = rtsp_stream::session_count();
      if (seen.sessions == 0) {
        // Also reaps the app and runs its undo commands when it exited.
        seen.app_running = proc::proc.running() > 0;
        seen.display_held = display_follow::held();
      }
    }

    action_e action;
    {
      std::lock_guard lg {tracker_mutex()};
      action = tracker().tick(seen, tracker_t::clock::now(), idle_quit_timeout());
    }

    switch (action) {
      case action_e::none:
        return;
      case action_e::cleanup:
        BOOST_LOG(info) << "App lifecycle: the app ended while no device was connected; stopping the display kept for it"sv;
        display_follow::app_closed();
        return;
      case action_e::idle_quit:
        if (rtsp_stream::session_count() > 0) {
          return;  // a device came back just now
        }
        BOOST_LOG(info) << "App lifecycle: no device connected for "sv << config::nova.app_idle_quit_hours << " h (app_idle_quit_hours); ending ["sv
                        << proc::proc.get_last_run_app_name() << ']';
        proc::proc.terminate();
        display_follow::app_closed();
        return;
    }
  }

  void start() {
    watcher().start();
  }

  void stop() {
    watcher().stop();
  }
}  // namespace app_lifecycle
