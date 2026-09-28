/**
 * @file src/display_follow.cpp
 * @brief Definitions for Nova's session-level "follow the client display mode" step.
 */
// standard includes
#include <chrono>
#include <fstream>
#include <thread>

// lib includes
#include "boost_process_compat.h"

// local includes
#include "audio.h"
#include "config.h"
#include "display_follow.h"
#include "input.h"
#include "logging.h"
#include "platform/common.h"
#include "process.h"

using namespace std::literals;

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

  controller_t::controller_t(runner_t runner, std::filesystem::path marker, std::shared_ptr<virtual_backend_t> backend, virtual_hooks_t hooks):
      runner_ {std::move(runner)},
      marker_ {std::move(marker)},
      backend_ {std::move(backend)},
      hooks_ {std::move(hooks)} {
  }

  outcome_e controller_t::on_stream_request(const std::string &setting, const std::string &cmd, const request_t &request, bool legacy_prep, bool other_sessions, const std::string &virtual_setting) {
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
        if (auto resized = backend_->resize(request)) {
          virtual_ = std::move(resized);
          if (hooks_.up) {
            hooks_.up(*virtual_);
          }
          return outcome_e::virtual_reused;
        }
        stop_virtual_locked();
      }
      if (active_ && !cmd.empty()) {
        restore_locked(cmd);  // a Mirror switch of the desktop is not needed any more
      }
      BOOST_LOG(info) << "Display follow: virtual display "sv << request.width << 'x' << request.height << '@' << request.fps
                      << " for "sv << request.client_name << " ["sv << request.app_name << ']';
      virtual_ = backend_->start(request);
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

  bool controller_t::restore_locked(const std::string &cmd) {
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

  bool controller_t::on_last_session_end(const std::string &cmd) {
    std::lock_guard lg {mutex_};
    bool done = false;
    if (virtual_) {
      stop_virtual_locked();
      done = true;
    }
    if (!active_ || cmd.empty()) {
      return done;
    }
    return restore_locked(cmd) || done;
  }

  std::optional<virtual_display::target_t> controller_t::virtual_target() const {
    std::lock_guard lg {mutex_};
    return virtual_;
  }

  bool controller_t::recover(const std::string &cmd) {
    std::lock_guard lg {mutex_};
    if (backend_) {
      backend_->recover();
    }
    std::error_code ec;
    if (!std::filesystem::exists(marker_, ec)) {
      return false;
    }
    std::string recorded;
    std::getline(std::ifstream {marker_}, recorded);
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
        // The window manager setting is read per start so a Settings change applies to the next stream.
        const auto &wm = config::video.virtual_display_wm;
        server_ = std::make_unique<virtual_display::x_server_t>(state_dir_, wm == "none" ? std::string {} : wm, virtual_display::default_ops());
        return server_->start({request.width, request.height, request.fps});
      }

      std::optional<virtual_display::target_t> resize(const request_t &request) override {
        if (!server_ || !server_->resize({request.width, request.height, request.fps})) {
          return std::nullopt;
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

    private:
      std::filesystem::path state_dir_;
      std::unique_ptr<virtual_display::x_server_t> server_;
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
      return hooks;
    }
  }  // namespace

  controller_t &instance() {
#ifdef _WIN32
    std::shared_ptr<virtual_backend_t> backend;
#else
    auto backend = std::make_shared<x_backend_t>(platf::appdata() / "virtual-display");
#endif
    static controller_t controller {run_command, platf::appdata() / "display_follow.active", std::move(backend), process_hooks()};
    return controller;
  }

  outcome_e stream_requested(const request_t &request, bool legacy_prep, bool other_sessions) {
    return instance().on_stream_request(config::video.display_follow, config::video.display_follow_cmd, request, legacy_prep, other_sessions, config::video.virtual_display);
  }

  void last_session_ended() {
    instance().on_last_session_end(config::video.display_follow_cmd);
  }

  void recover_at_startup() {
    instance().recover(config::video.display_follow_cmd);
  }

  void shutdown() {
    if (instance().virtual_target()) {
      instance().on_last_session_end({});
    }
  }

  void app_closed() {
    if (instance().virtual_target()) {
      instance().on_last_session_end(config::video.display_follow_cmd);
    }
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
