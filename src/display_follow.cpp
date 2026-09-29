/**
 * @file src/display_follow.cpp
 * @brief Definitions for Nova's session-level "follow the client display mode" step.
 */
// standard includes
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stop_token>
#include <thread>
#include <tuple>

// lib includes
#include <nlohmann/json.hpp>

// lib includes
#include "boost_process_compat.h"

// local includes
#include "audio.h"
#include "config.h"
#include "display_follow.h"
#include "display_modeset.h"
#include "input.h"
#include "logging.h"
#include "platform/common.h"
#include "process.h"

using namespace std::literals;

#ifndef NOVA_VD_SESSION
  #define NOVA_VD_SESSION "/usr/lib/nova-host/nova-vd-session"
#endif

namespace display_follow {
  namespace {
    constexpr auto COMMAND_TIMEOUT = 10s;

    /**
     * @brief Default runner: `cmd action` through platf::run_command with a timeout.
     */
    int run_command(const std::string &cmd, const std::string &action, const env_t &extra) {
      auto env = boost::this_process::environment();
      boost::process::v1::environment child_env = env;
      for (const auto &[key, value] : extra) {
        child_env[key] = value;
      }
      const std::string line = (cmd.find(' ') != std::string::npos ? '"' + cmd + '"' : cmd) + ' ' + action;
      boost::filesystem::path working_dir = boost::filesystem::path(cmd).parent_path();
      std::error_code ec;
      auto child = platf::run_command(false, true, line, working_dir, child_env, nullptr, ec, nullptr);
      if (ec) {
        BOOST_LOG(error) << "Display follow: couldn't run ["sv << line << "]: "sv << ec.message();
        return -1;
      }
      const auto deadline = std::chrono::steady_clock::now() + COMMAND_TIMEOUT;
      while (child.running(ec) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(50ms);
      }
      if (child.running(ec)) {
        BOOST_LOG(error) << "Display follow: ["sv << line << "] timed out after 10 s"sv;
        child.terminate(ec);
        return -1;
      }
      child.wait(ec);
      return child.exit_code();
    }

    bool command_available(const std::string &cmd) {
      const std::filesystem::path path {cmd};
      if (!path.is_absolute()) {
        return true;  // resolved through PATH by the shell
      }
      std::error_code ec;
      return std::filesystem::exists(path, ec);
    }
  }  // namespace

  /**
   * @brief One-shot timer on its own thread (the real scheduler behind the linger).
   *
   * The callback runs without the timer's lock, so it may take the controller's mutex while the
   * controller (holding that mutex) re-arms or cancels the timer.
   */
  class controller_t::linger_timer_t {
  public:
    void schedule(std::chrono::milliseconds delay, std::function<void()> fn) {
      {
        std::lock_guard lg {mutex_};
        deadline_ = std::chrono::steady_clock::now() + delay;
        fn_ = std::move(fn);
        armed_ = true;
        ++epoch_;
        if (!thread_.joinable()) {
          thread_ = std::jthread {[this](std::stop_token stop) {
            loop(stop);
          }};
        }
      }
      cv_.notify_all();
    }

    void cancel() {
      {
        std::lock_guard lg {mutex_};
        armed_ = false;
        fn_ = nullptr;
        ++epoch_;
      }
      cv_.notify_all();
    }

    ~linger_timer_t() {
      if (thread_.joinable()) {
        thread_.request_stop();
        cv_.notify_all();
        thread_.join();
      }
    }

  private:
    void loop(const std::stop_token &stop) {
      std::unique_lock lk {mutex_};
      while (!stop.stop_requested()) {
        if (!armed_) {
          cv_.wait(lk, stop, [this] {
            return armed_;
          });
          continue;
        }
        const auto epoch = epoch_;
        const auto deadline = deadline_;
        if (cv_.wait_until(lk, stop, deadline, [this, epoch] {
              return epoch_ != epoch;
            })) {
          continue;  // re-armed or cancelled
        }
        if (stop.stop_requested() || !armed_ || epoch_ != epoch) {
          continue;
        }
        auto fn = std::move(fn_);
        armed_ = false;
        lk.unlock();
        if (fn) {
          fn();
        }
        lk.lock();
      }
    }

    std::mutex mutex_;
    std::condition_variable_any cv_;
    bool armed_ = false;
    std::uint64_t epoch_ = 0;
    std::chrono::steady_clock::time_point deadline_;
    std::function<void()> fn_;
    std::jthread thread_;
  };

  controller_t::controller_t(runner_t runner, std::filesystem::path marker, std::shared_ptr<virtual_backend_t> backend, virtual_hooks_t hooks, scheduler_t scheduler, std::shared_ptr<mirror_backend_t> native):
      runner_ {std::move(runner)},
      marker_ {std::move(marker)},
      backend_ {std::move(backend)},
      hooks_ {std::move(hooks)},
      scheduler_ {std::move(scheduler)},
      native_ {std::move(native)} {
    if (!scheduler_.schedule || !scheduler_.cancel) {
      timer_ = std::make_unique<linger_timer_t>();
      scheduler_.schedule = [timer = timer_.get()](std::chrono::milliseconds delay, std::function<void()> fn) {
        timer->schedule(delay, std::move(fn));
      };
      scheduler_.cancel = [timer = timer_.get()]() {
        timer->cancel();
      };
    }
  }

  controller_t::~controller_t() {
    timer_.reset();  // join the timer thread before the state its callback uses goes away
  }

  void controller_t::cancel_linger_locked() {
    if (!lingering_) {
      return;
    }
    lingering_ = false;
    ++linger_generation_;
    scheduler_.cancel();
  }

  bool controller_t::app_running() const {
    return hooks_.app_running && hooks_.app_running();
  }

  outcome_e controller_t::on_stream_request(const std::string &setting, const std::string &cmd, const request_t &request_in, bool legacy_prep, bool other_sessions, const std::string &virtual_setting) {
    auto request = request_in;
    {
      std::lock_guard lg {mutex_};
      if (lingering_) {
        // The device came back within the grace period (e.g. a live resolution change).
        BOOST_LOG(info) << "Display follow: "sv << request.client_name << " reconnected; keeping "sv
                        << (virtual_ ? virtual_->display : "the desktop"s) << " for ["sv << request.app_name << ']';
        cancel_linger_locked();
      }
      if (held_) {
        BOOST_LOG(info) << "Display follow: "sv << request.client_name << " is back; resuming ["sv << request.app_name << "] on "sv
                        << (virtual_ ? virtual_->display : "the desktop"s);
        held_ = false;
      }
      // A running app can't move between displays: a resume streams the display it runs on.
      if (request.resume && setting != "off" && app_running()) {
        if (virtual_ && request.mode != "virtual") {
          BOOST_LOG(info) << "Display follow: ["sv << request.app_name << "] runs on "sv << virtual_->display << "; resuming there instead of mirroring the desktop"sv;
          request.mode = "virtual";
        } else if (!virtual_ && request.mode == "virtual") {
          BOOST_LOG(info) << "Display follow: ["sv << request.app_name << "] runs on the desktop; resuming it as Mirror"sv;
          request.mode = "mirror";
        }
      }
    }
    if (setting == "off") {
      return outcome_e::skipped_off;
    }

    // Virtual display: only an explicit "virtual" (Nebula's choice or the app's nova-display-mode)
    // gets its own X server. An empty mode (plain Moonlight) keeps the script behaviour.
    if (request.mode == "virtual" && virtual_setting == "headless_x" && backend_) {
      if (other_sessions) {
        // One virtual display at a time: another device joins the stream already running.
        BOOST_LOG(info) << "Display follow: another device is streaming, keeping the current display"sv;
        return outcome_e::skipped_busy;
      }
      std::lock_guard lg {mutex_};
      if (virtual_) {
        const int scale = virtual_->scale;
        if (auto resized = backend_->resize(request)) {
          virtual_ = std::move(resized);
          virtual_->scale = scale;
          if (virtual_display::valid_scale(request.scale) && request.scale != scale && backend_->set_scale(request.scale)) {
            virtual_->scale = request.scale;
          }
          if (hooks_.up) {
            hooks_.up(*virtual_);
          }
          return outcome_e::virtual_reused;
        }
        stop_virtual_locked();
      }
      if (active_ && (native_active_ || !cmd.empty())) {
        restore_locked(cmd);  // a Mirror switch of the desktop is not needed any more
      }
      BOOST_LOG(info) << "Display follow: virtual display "sv << request.width << 'x' << request.height << '@' << request.fps
                      << " for "sv << request.client_name << " ["sv << request.app_name << ']';
      virtual_ = backend_->start(request);
      if (virtual_) {
        virtual_->scale = virtual_display::valid_scale(request.scale) ? request.scale : 100;
      }
      if (!virtual_) {
        // Leave the desktop alone: the stream falls back to capturing it as it is.
        BOOST_LOG(error) << "Display follow: couldn't start the virtual display; streaming the desktop unchanged"sv;
        return outcome_e::failed;
      }
      if (hooks_.up) {
        hooks_.up(*virtual_);
      }
      return outcome_e::virtual_started;
    }

    // Portrait Mirror: the script can't rotate, so the native switcher turns the desktop output.
    if (request.rotation != 0 && native_) {
      if (legacy_prep) {
        BOOST_LOG(info) << "Display follow: ["sv << request.app_name << "] switches the display with its own prep command, skipping"sv;
        return outcome_e::skipped_legacy;
      }
      if (other_sessions) {
        BOOST_LOG(info) << "Display follow: another device is streaming, keeping the current display mode"sv;
        return outcome_e::skipped_busy;
      }
      std::lock_guard lg {mutex_};
      if (virtual_) {
        stop_virtual_locked();
      }
      if (active_ && !native_active_ && !cmd.empty()) {
        restore_locked(cmd);  // the script's landscape switch first, so the saved original is the real one
      }
      BOOST_LOG(info) << "Display follow: portrait "sv << request.width << 'x' << request.height << '@' << request.fps << " (rotation "sv << request.rotation
                      << ") for "sv << request.client_name << " ["sv << request.app_name << ']';
      if (!native_->set(request)) {
        return outcome_e::failed;
      }
      mark_native_locked();
      return outcome_e::switched;
    }

    if (cmd.empty()) {
      return outcome_e::skipped_off;
    }
    if (!command_available(cmd)) {
      BOOST_LOG(debug) << "Display follow: ["sv << cmd << "] not found, skipping"sv;
      return outcome_e::skipped_missing_cmd;
    }
    if (legacy_prep) {
      BOOST_LOG(info) << "Display follow: ["sv << request.app_name << "] switches the display with its own prep command, skipping"sv;
      return outcome_e::skipped_legacy;
    }
    if (other_sessions) {
      BOOST_LOG(info) << "Display follow: another device is streaming, keeping the current display mode"sv;
      return outcome_e::skipped_busy;
    }

    std::lock_guard lg {mutex_};
    if (virtual_) {
      // Left running by a Virtual display stream that just ended; this one mirrors the desktop.
      stop_virtual_locked();
    }
    if (active_ && native_active_) {
      restore_locked(cmd);  // turned to portrait natively before (e.g. a live rotation back to landscape)
    }
    BOOST_LOG(info) << "Display follow: "sv << request.width << 'x' << request.height << '@' << request.fps
                    << " for "sv << request.client_name << " ["sv << request.app_name << ']';
    const int ret = runner_(cmd, "set", make_env(request));
    if (ret != 0) {
      BOOST_LOG(error) << "Display follow: ["sv << cmd << " set] exited with "sv << ret;
      return outcome_e::failed;
    }
    active_ = true;
    std::error_code ec;
    std::filesystem::create_directories(marker_.parent_path(), ec);
    std::ofstream {marker_} << cmd << '\n';
    return outcome_e::switched;
  }

  void controller_t::mark_native_locked() {
    active_ = true;
    native_active_ = true;
    std::error_code ec;
    std::filesystem::create_directories(marker_.parent_path(), ec);
    std::ofstream {marker_} << native_marker << '\n';
  }

  bool controller_t::restore_locked(const std::string &cmd) {
    if (native_active_) {
      const bool ok = native_ && native_->restore();
      std::error_code ec;
      std::filesystem::remove(marker_, ec);
      active_ = false;
      native_active_ = false;
      if (ok) {
        BOOST_LOG(info) << "Display follow: display restored (rotation undone)"sv;
      }
      return ok;
    }
    const int ret = runner_(cmd, "restore", {});
    std::error_code ec;
    std::filesystem::remove(marker_, ec);
    active_ = false;
    if (ret != 0) {
      BOOST_LOG(error) << "Display follow: ["sv << cmd << " restore] exited with "sv << ret;
      return false;
    }
    BOOST_LOG(info) << "Display follow: display restored"sv;
    return true;
  }

  void controller_t::stop_virtual_locked() {
    if (!virtual_) {
      return;
    }
    if (hooks_.before_stop) {
      hooks_.before_stop();
    }
    backend_->stop();
    virtual_.reset();
    if (hooks_.down) {
      hooks_.down();
    }
  }

  bool controller_t::teardown_locked(const std::string &cmd) {
    cancel_linger_locked();
    held_ = false;
    bool done = false;
    if (virtual_) {
      stop_virtual_locked();
      done = true;
    }
    if (!active_ || (cmd.empty() && !native_active_)) {
      return done;
    }
    return restore_locked(cmd) || done;
  }

  bool controller_t::on_last_session_end(const std::string &cmd, const std::chrono::milliseconds linger) {
    std::lock_guard lg {mutex_};
    if (virtual_ && app_running()) {
      // A disconnect never ends the app: its display, desktop session and input stay until it is
      // quit or exits (display_follow::app_closed()), and a /resume reuses them.
      cancel_linger_locked();
      held_ = true;
      BOOST_LOG(info) << "Display follow: last device disconnected; keeping "sv << virtual_->display
                      << " and its app running until the app is quit or exits"sv;
      return true;
    }
    held_ = false;
    if (linger.count() <= 0 || (!virtual_ && !active_)) {
      return teardown_locked(cmd);
    }
    cancel_linger_locked();
    lingering_ = true;
    const auto generation = ++linger_generation_;
    BOOST_LOG(info) << "Display follow: last device disconnected; keeping "sv << (virtual_ ? virtual_->display + " and its app"s : "the display mode"s)
                    << " for "sv << std::chrono::duration_cast<std::chrono::seconds>(linger).count() << " s in case it reconnects"sv;
    scheduler_.schedule(linger, [this, generation, cmd]() {
      linger_expired(generation, cmd);
    });
    return true;
  }

  void controller_t::linger_expired(const std::uint64_t generation, const std::string &cmd) {
    std::lock_guard lg {mutex_};
    if (!lingering_ || generation != linger_generation_) {
      return;
    }
    lingering_ = false;
    if (virtual_ && app_running()) {
      // An app started on the display meanwhile: keep it like any other running app.
      held_ = true;
      BOOST_LOG(info) << "Display follow: no device reconnected; keeping "sv << virtual_->display << " for the app running on it"sv;
      return;
    }
    BOOST_LOG(info) << "Display follow: no device reconnected; restoring"sv;
    teardown_locked(cmd);
  }

  bool controller_t::end_now(const std::string &cmd) {
    std::lock_guard lg {mutex_};
    return teardown_locked(cmd);
  }

  bool controller_t::lingering() const {
    std::lock_guard lg {mutex_};
    return lingering_;
  }

  bool controller_t::held() const {
    std::lock_guard lg {mutex_};
    return held_;
  }

  scale_result_e controller_t::set_scale(const int percent) {
    if (!virtual_display::valid_scale(percent)) {
      return scale_result_e::invalid;
    }
    std::lock_guard lg {mutex_};
    if (!virtual_ || !backend_) {
      return scale_result_e::not_virtual;
    }
    if (virtual_->scale == percent) {
      return scale_result_e::applied;
    }
    if (!backend_->set_scale(percent)) {
      return scale_result_e::failed;
    }
    virtual_->scale = percent;  // apps launched from now on get it through app_env()
    return scale_result_e::applied;
  }

  std::optional<virtual_display::target_t> controller_t::virtual_target() const {
    std::lock_guard lg {mutex_};
    return virtual_;
  }

  bool controller_t::rotate(const int angle) {
    if (!display_modeset::valid_rotation(angle)) {
      return false;
    }
    std::lock_guard lg {mutex_};
    if (virtual_ && backend_) {
      // A headless screen has no output to rotate: it is resized to the turned size instead.
      request_t request;
      std::tie(request.width, request.height) = display_modeset::size_for_rotation(virtual_->width, virtual_->height, angle);
      request.fps = virtual_->fps;
      request.mode = "virtual";
      const int scale = virtual_->scale;
      auto resized = backend_->resize(request);
      if (!resized) {
        BOOST_LOG(error) << "Display follow: couldn't turn the virtual display to "sv << angle << " degrees"sv;
        return false;
      }
      virtual_ = std::move(resized);
      virtual_->scale = scale;
      if (hooks_.up) {
        hooks_.up(*virtual_);
      }
      BOOST_LOG(info) << "Display follow: virtual display turned to "sv << virtual_->width << 'x' << virtual_->height;
      return true;
    }
    if (!native_) {
      return false;
    }
    if (active_ && !native_active_) {
      BOOST_LOG(warning) << "Display follow: the display was switched by display_follow_cmd, which can't rotate"sv;
      return false;
    }
    if (!native_->rotate(angle)) {
      return false;
    }
    mark_native_locked();
    return true;
  }

  bool controller_t::recover(const std::string &cmd) {
    std::lock_guard lg {mutex_};
    if (backend_) {
      backend_->recover();
    }
    // The native switcher keeps its own state file, written before it switches.
    const bool native_restored = native_ && native_->recover();
    std::error_code ec;
    if (!std::filesystem::exists(marker_, ec)) {
      return native_restored;
    }
    std::string recorded;
    std::getline(std::ifstream {marker_}, recorded);
    if (recorded == native_marker) {
      std::filesystem::remove(marker_, ec);
      return native_restored;
    }
    const std::string &use = recorded.empty() ? cmd : recorded;
    if (use.empty() || !command_available(use)) {
      std::filesystem::remove(marker_, ec);
      return false;
    }
    BOOST_LOG(info) << "Display follow: restoring the display left switched by the previous run"sv;
    return restore_locked(use);
  }

  bool controller_t::active() const {
    std::lock_guard lg {mutex_};
    return active_;
  }

  bool is_legacy_prep(const std::string &prep_do_cmd, const std::string &cmd) {
    if (cmd.empty()) {
      return false;
    }
    const auto script = std::filesystem::path(cmd).filename().string();
    if (prep_do_cmd.find(script) == std::string::npos) {
      return false;
    }
    return prep_do_cmd.find(" set") != std::string::npos || prep_do_cmd.find(" virtual") != std::string::npos;
  }

  env_t make_env(const request_t &request) {
    return {
      {"SUNSHINE_CLIENT_WIDTH", std::to_string(request.width)},
      {"SUNSHINE_CLIENT_HEIGHT", std::to_string(request.height)},
      {"SUNSHINE_CLIENT_FPS", std::to_string(request.fps)},
      {"NOVA_DISPLAY_MODE", request.mode.empty() ? "virtual"s : request.mode},
    };
  }

  namespace {
    /**
     * @brief The headless X server backend (virtual_display::x_server_t) behind virtual_backend_t.
     */
    class x_backend_t: public virtual_backend_t {
    public:
      explicit x_backend_t(std::filesystem::path state_dir):
          state_dir_ {std::move(state_dir)} {
      }

      std::optional<virtual_display::target_t> start(const request_t &request) override {
        // The session settings are read per start so a Settings change applies to the next stream.
        auto wm = config::video.virtual_display_wm;
        if (wm == "none") {
          wm.clear();
        }
        const int scale = virtual_display::valid_scale(request.scale) ? request.scale : 100;
        std::string command = wm;
        virtual_display::env_list_t env;
        desktop_ = false;
        if (config::video.virtual_display_session == "desktop") {
          if (helper_available()) {
            desktop_ = true;
            // Unquoted: crash recovery matches the first word against the process's command line.
            command = NOVA_VD_SESSION;
            env = {
              {"NOVA_VD_WM", wm.empty() ? "openbox"s : wm},
              {"NOVA_VD_SCALE", std::to_string(scale)},
              {"NOVA_VD_APPS", config::stream.file_apps},
              {"NOVA_VD_WALLPAPER", (std::filesystem::path {SUNSHINE_ASSETS_DIR} / "virtual-display" / "wallpaper.png").string()},
              // Remote text context reads text field focus through AT-SPI: the session then keeps
              // accessibility on for its own apps (on its private bus only).
              {"NOVA_VD_A11Y", config::input.remote_text_context ? "1"s : "0"s},
            };
          } else {
            BOOST_LOG(warning) << "Virtual display: "sv << NOVA_VD_SESSION << " is missing; starting a bare window manager instead of the desktop"sv;
          }
        }
        server_ = std::make_unique<virtual_display::x_server_t>(state_dir_, command, virtual_display::default_ops(), std::move(env));
        auto target = server_->start({request.width, request.height, request.fps});
        if (target && !desktop_ && scale != 100) {
          set_scale(scale);  // the desktop session applies NOVA_VD_SCALE itself
        }
        return target;
      }

      std::optional<virtual_display::target_t> resize(const request_t &request) override {
        if (!server_ || !server_->resize({request.width, request.height, request.fps})) {
          return std::nullopt;
        }
        if (desktop_) {
          server_->run_on_display({NOVA_VD_SESSION, "refresh"});  // wallpaper at the new size
        }
        return server_->target();
      }

      void stop() override {
        if (server_) {
          server_->stop();
          server_.reset();
        }
      }

      bool recover() override {
        return virtual_display::x_server_t {state_dir_, {}, virtual_display::default_ops()}.recover();
      }

      bool set_scale(int percent) override {
        if (!server_ || !server_->running()) {
          return false;
        }
        if (helper_available()) {
          // Sets Xft.dpi and the DPI, and reloads the desktop session's panel, icons and menus.
          return server_->run_on_display({NOVA_VD_SESSION, "scale", std::to_string(percent)}) == 0;
        }
        const auto target = server_->target();
        const auto dpi = std::to_string(96 * percent / 100);
        return server_->run_on_display({"/usr/bin/xrandr", "--fb", std::to_string(target->width) + 'x' + std::to_string(target->height), "--dpi", dpi}) == 0;
      }

    private:
      static bool helper_available() {
        std::error_code ec;
        return std::filesystem::exists(NOVA_VD_SESSION, ec);
      }

      std::filesystem::path state_dir_;
      std::unique_ptr<virtual_display::x_server_t> server_;
      bool desktop_ = false;
    };

    /**
     * @brief Holds Nova's audio context (and so its capture sinks) while a virtual display runs,
     * so PULSE_SINK names a sink that exists when the app starts.
     */
    std::optional<audio::audio_ctx_ref_t> &held_audio() {
      static std::optional<audio::audio_ctx_ref_t> ref;
      return ref;
    }

    virtual_hooks_t process_hooks() {
      virtual_hooks_t hooks;
      hooks.up = [](const virtual_display::target_t &target) {
        virtual_display::set_capture_target(target);
        const bool rule = virtual_display::seat_rule_installed();
        if (!rule) {
          BOOST_LOG(error) << "Virtual display: the udev rule 61-nova-host-vd-seat.rules is not installed, so Nova's keyboard, "
                              "mouse, touch and pen would reach the desktop instead of "sv
                           << target.display << ". Input is disabled for this stream; reinstall the package or run "
                                                "\"sudo udevadm control --reload\" after installing the rule."sv;
        }
        input::set_virtual_display_seat(rule ? input::seat_e::virtual_display : input::seat_e::blocked);
        if (!held_audio()) {
          held_audio() = audio::get_audio_ctx_ref();
        }
      };
      hooks.before_stop = []() {
        // The app draws on the display that is about to go away; end it cleanly first.
        if (proc::proc.running()) {
          BOOST_LOG(info) << "Virtual display: ending ["sv << proc::proc.get_last_run_app_name() << "] before its display stops"sv;
          proc::proc.terminate();
        }
      };
      hooks.down = []() {
        virtual_display::set_capture_target(std::nullopt);
        input::set_virtual_display_seat(input::seat_e::desktop);
        held_audio().reset();
      };
      hooks.app_running = []() {
        return proc::proc.running() > 0;
      };
      return hooks;
    }
  }  // namespace

  namespace {
    /**
     * @brief The native Mirror switcher, bound to the desktop display this process started on.
     */
    std::shared_ptr<display_modeset::switcher_t> &native_switcher() {
#ifdef _WIN32
      static std::shared_ptr<display_modeset::switcher_t> switcher;
#else
      // DISPLAY is read once, before any virtual display exists (capture overrides it briefly).
      static std::shared_ptr<display_modeset::switcher_t> switcher = []() {
        const char *display = std::getenv("DISPLAY");
        const char *xauthority = std::getenv("XAUTHORITY");
        const char *runtime = std::getenv("XDG_RUNTIME_DIR");
        const std::filesystem::path legacy = std::filesystem::path {runtime && *runtime ? runtime : "/tmp"} / "sunshine-resolution.metamode";
        return std::make_shared<display_modeset::switcher_t>(
          display_modeset::default_runner(display && *display ? display : ":0", xauthority ? xauthority : ""),
          platf::appdata() / "display_modeset.json",
          []() {
            return std::string {"auto"};
          },
          legacy
        );
      }();
#endif
      return switcher;
    }
  }  // namespace

  controller_t &instance() {
#ifdef _WIN32
    std::shared_ptr<virtual_backend_t> backend;
#else
    auto backend = std::make_shared<x_backend_t>(platf::appdata() / "virtual-display");
#endif
    static controller_t controller {run_command, platf::appdata() / "display_follow.active", std::move(backend), process_hooks(), {}, native_switcher()};
    return controller;
  }

  bool rotate(const int angle) {
    return instance().rotate(angle);
  }

  nlohmann::json displays_json() {
    std::vector<display_modeset::output_t> outputs;
    std::string mirror;
    if (const auto &switcher = native_switcher()) {
      outputs = switcher->outputs();
      if (const auto *picked = display_modeset::pick_output(outputs, "auto")) {
        mirror = picked->name;
      }
    }
    return display_modeset::displays_json(outputs, mirror, virtual_target());
  }

  namespace {
    std::filesystem::path scale_file() {
      return platf::appdata() / "virtual-display-scale.json";
    }
  }  // namespace

  outcome_e stream_requested(const request_t &request_in, bool legacy_prep, bool other_sessions) {
    auto request = request_in;
    if (!virtual_display::valid_scale(request.scale) || request.scale == 100) {
      request.scale = request.device_key.empty() ? 100 : stored_scale(scale_file(), request.device_key);
    }
    return instance().on_stream_request(config::video.display_follow, config::video.display_follow_cmd, request, legacy_prep, other_sessions, config::video.virtual_display);
  }

  void last_session_ended() {
    instance().on_last_session_end(config::video.display_follow_cmd, std::chrono::seconds {std::max(0, config::video.virtual_display_linger)});
  }

  scale_result_e set_display_scale(int percent, const std::string &key) {
    const auto result = instance().set_scale(percent);
    if (result == scale_result_e::applied && !key.empty()) {
      store_scale(scale_file(), key, percent);
    }
    return result;
  }

  std::string device_key(const std::string &client_cert) {
    if (client_cert.empty()) {
      return {};
    }
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const unsigned char c : client_cert) {
      hash ^= c;
      hash *= 0x100000001b3ULL;
    }
    char out[17];
    std::snprintf(out, sizeof(out), "%016llx", static_cast<unsigned long long>(hash));
    return out;
  }

  int stored_scale(const std::filesystem::path &file, const std::string &key) {
    std::ifstream in {file};
    if (!in) {
      return 100;
    }
    const auto json = nlohmann::json::parse(in, nullptr, false);
    if (!json.is_object() || !json.contains(key) || !json[key].is_number_integer()) {
      return 100;
    }
    const int value = json[key].get<int>();
    return virtual_display::valid_scale(value) ? value : 100;
  }

  bool store_scale(const std::filesystem::path &file, const std::string &key, int percent) {
    if (key.empty() || !virtual_display::valid_scale(percent)) {
      return false;
    }
    nlohmann::json json = nlohmann::json::object();
    if (std::ifstream in {file}) {
      auto parsed = nlohmann::json::parse(in, nullptr, false);
      if (parsed.is_object()) {
        json = std::move(parsed);
      }
    }
    json[key] = percent;
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    const auto tmp = file.string() + ".tmp";
    {
      std::ofstream out {tmp, std::ios::trunc};
      if (!out) {
        return false;
      }
      out << json.dump(2) << '\n';
      if (!out) {
        return false;
      }
    }
    std::filesystem::permissions(tmp, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, ec);
    std::filesystem::rename(tmp, file, ec);
    return !ec;
  }

  void recover_at_startup() {
    instance().recover(config::video.display_follow_cmd);
  }

  void shutdown() {
    instance().end_now({});
  }

  void app_closed() {
    // Quit, a failed launch, or "Close app" with nobody streaming: no reconnect to wait for.
    auto &controller = instance();
    if (controller.virtual_target() || controller.lingering()) {
      controller.end_now(config::video.display_follow_cmd);
    }
  }

  bool held() {
    return instance().held();
  }

  bool lingering() {
    return instance().lingering();
  }

  std::optional<virtual_display::target_t> virtual_target() {
    return instance().virtual_target();
  }

  virtual_display::env_list_t app_env(int channels, bool host_audio, const std::string &base_ld_preload) {
    const auto target = virtual_target();
    if (!target) {
      return {};
    }
    std::string sink;
    if (!host_audio) {
      // Nova's null sinks (platform/linux/audio.cpp); the stream captures their monitor.
      sink = channels >= 8 ? "sink-sunshine-surround71" : channels >= 6 ? "sink-sunshine-surround51" : "sink-sunshine-stereo";
    }
    return virtual_display::app_env(*target, config::video.virtual_display_fps_cap, sink, base_ld_preload, virtual_display::mangohud_gl_preload());
  }
}  // namespace display_follow
