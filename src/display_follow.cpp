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
#include "config.h"
#include "display_follow.h"
#include "logging.h"
#include "platform/common.h"

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

  controller_t::controller_t(runner_t runner, std::filesystem::path marker):
      runner_ {std::move(runner)},
      marker_ {std::move(marker)} {
  }

  outcome_e controller_t::on_stream_request(const std::string &setting, const std::string &cmd, const request_t &request, bool legacy_prep, bool other_sessions) {
    if (setting == "off" || cmd.empty()) {
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

  bool controller_t::on_last_session_end(const std::string &cmd) {
    std::lock_guard lg {mutex_};
    if (!active_ || cmd.empty()) {
      return false;
    }
    return restore_locked(cmd);
  }

  bool controller_t::recover(const std::string &cmd) {
    std::lock_guard lg {mutex_};
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

  controller_t &instance() {
    static controller_t controller {run_command, platf::appdata() / "display_follow.active"};
    return controller;
  }

  outcome_e stream_requested(const request_t &request, bool legacy_prep, bool other_sessions) {
    return instance().on_stream_request(config::video.display_follow, config::video.display_follow_cmd, request, legacy_prep, other_sessions);
  }

  void last_session_ended() {
    instance().on_last_session_end(config::video.display_follow_cmd);
  }

  void recover_at_startup() {
    instance().recover(config::video.display_follow_cmd);
  }
}  // namespace display_follow
