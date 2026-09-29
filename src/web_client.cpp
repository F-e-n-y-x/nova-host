/**
 * @file src/web_client.cpp
 * @brief Starts, supervises and stops the browser client sidecar (see web_client.h).
 */
// standard includes
#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

// local includes
#include "config.h"
#include "file_handler.h"
#include "logging.h"
#include "platform/common.h"
#include "web_client.h"

#if defined(__linux__) || defined(__FreeBSD__)
  #include <cerrno>
  #include <fcntl.h>
  #include <signal.h>
  #include <spawn.h>
  #include <sys/stat.h>
  #include <sys/wait.h>
  #include <unistd.h>

extern char **environ;
#endif

#ifndef NOVA_WEB_CLIENT_DIR
  #define NOVA_WEB_CLIENT_DIR "/usr/lib/nova-host/web-client"
#endif

using namespace std::literals;
namespace fs = std::filesystem;

namespace web_client {
  namespace {
    std::string trim(std::string_view s) {
      const auto first = s.find_first_not_of(" \t\r");
      if (first == std::string_view::npos) {
        return {};
      }
      const auto last = s.find_last_not_of(" \t\r");
      return std::string {s.substr(first, last - first + 1)};
    }

    std::uint16_t port_at(int base, int offset) {
      return static_cast<std::uint16_t>(std::clamp(base + offset, 1, 65535));
    }

    /// An address as it goes in a URL (IPv6 in brackets).
    std::string url_host(const std::string &address) {
      return address.find(':') != std::string::npos ? "[" + address + "]" : address;
    }

    fs::path find_python() {
      if (const char *path = std::getenv("PATH")) {
        std::stringstream ss {path};
        std::string dir;
        while (std::getline(ss, dir, ':')) {
          if (dir.empty()) {
            continue;
          }
          std::error_code ec;
          const auto candidate = fs::path(dir) / "python3";
          if (fs::is_regular_file(candidate, ec)) {
            return candidate;
          }
        }
      }
      for (const auto *fallback : {"/usr/bin/python3", "/bin/python3"}) {
        std::error_code ec;
        if (fs::is_regular_file(fallback, ec)) {
          return fallback;
        }
      }
      return {};
    }

    std::string read_version(const fs::path &dir) {
      std::ifstream in(dir / "VERSION");
      std::string line;
      std::getline(in, line);
      return trim(line);
    }

    // -- supervisor state
    std::mutex apply_mutex;  // serialises apply() calls
    std::mutex mutex;  // guards everything below
    std::condition_variable wake;
    bool want = false;
    bool thread_alive = false;
    std::thread worker;
    long child_pid = -1;
    std::string state = "off";
    int restarts = 0;
    std::string last_error;

#if defined(__linux__) || defined(__FreeBSD__)
    /**
     * @brief Start the gateway in its own process group, output appended to log_path.
     * @return The pid, or -1.
     */
    pid_t spawn(const std::vector<std::string> &argv, const fs::path &log_path, std::string &error) {
      std::error_code ec;
      if (fs::file_size(log_path, ec) > (4u << 20) && !ec) {
        fs::remove(log_path, ec);
      }
      const int log_fd = ::open(log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
      if (log_fd < 0) {
        error = "can't write " + log_path.string();
        return -1;
      }
      posix_spawn_file_actions_t actions;
      posix_spawn_file_actions_init(&actions);
      posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
      posix_spawn_file_actions_adddup2(&actions, log_fd, STDOUT_FILENO);
      posix_spawn_file_actions_adddup2(&actions, log_fd, STDERR_FILENO);
  #if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
      // Nova's sockets are not all O_CLOEXEC; don't leak them into the sidecar.
      posix_spawn_file_actions_addclosefrom_np(&actions, STDERR_FILENO + 1);
  #endif
      posix_spawnattr_t attr;
      posix_spawnattr_init(&attr);
      sigset_t defaults;
      sigemptyset(&defaults);
      sigaddset(&defaults, SIGPIPE);
      sigaddset(&defaults, SIGINT);
      sigaddset(&defaults, SIGTERM);
      sigaddset(&defaults, SIGCHLD);
      posix_spawnattr_setsigdefault(&attr, &defaults);
      sigset_t none;
      sigemptyset(&none);
      posix_spawnattr_setsigmask(&attr, &none);
      posix_spawnattr_setpgroup(&attr, 0);
      posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETPGROUP);

      std::vector<char *> args;
      for (const auto &a : argv) {
        args.push_back(const_cast<char *>(a.c_str()));
      }
      args.push_back(nullptr);
      pid_t pid = -1;
      const int rc = posix_spawn(&pid, args[0], &actions, &attr, args.data(), environ);
      posix_spawn_file_actions_destroy(&actions);
      posix_spawnattr_destroy(&attr);
      ::close(log_fd);
      if (rc != 0) {
        error = "can't start "s + args[0] + ": " + std::strerror(rc);
        return -1;
      }
      return pid;
    }

    /// Whether the child has exited (reaps it and fills `status`).
    bool reaped(pid_t pid, int &status) {
      const auto r = ::waitpid(pid, &status, WNOHANG);
      return r == pid || (r < 0 && errno == ECHILD);
    }

    std::string exit_text(int status) {
      if (WIFEXITED(status)) {
        return "exited with code " + std::to_string(WEXITSTATUS(status));
      }
      if (WIFSIGNALED(status)) {
        return "killed by signal " + std::to_string(WTERMSIG(status));
      }
      return "stopped";
    }

    /// SIGTERM the whole group (gateway, moonlight-web-stream, streamers), SIGKILL after 5 s.
    void stop_group(pid_t pid) {
      ::kill(-pid, SIGTERM);
      int status = 0;
      for (int i = 0; i < 50; ++i) {
        if (reaped(pid, status)) {
          ::kill(-pid, SIGKILL);  // stragglers (streamer processes)
          return;
        }
        std::this_thread::sleep_for(100ms);
      }
      BOOST_LOG(warning) << "Browser client: sidecar didn't stop in 5 s, killing it"sv;
      ::kill(-pid, SIGKILL);
      ::waitpid(pid, &status, 0);
    }

    void run_loop() {
      auto backoff = 1s;
      for (;;) {
        {
          std::unique_lock lk {mutex};
          if (!want) {
            break;
          }
        }
        const auto dir = lib_dir();
        const auto python = find_python();
        if (!installed_in(dir) || python.empty()) {
          BOOST_LOG(warning) << "Browser client: not installed in this build ("sv << dir.string() << (python.empty() ? ", no python3" : "") << ')';
          std::lock_guard lk {mutex};
          state = "not_installed";
          break;
        }
        auto launch = make_launch(config::sunshine.port, config::sunshine.bind_address, config::sunshine.address_family == "both"sv, config::nvhttp.origin_web_ui_allowed);
        launch.lib_dir = dir;
        launch.state_dir = platf::appdata() / "web-client";
        launch.tls_cert = config::nvhttp.cert;
        launch.tls_key = config::nvhttp.pkey;
        std::error_code ec;
        fs::create_directories(launch.state_dir, ec);
        fs::permissions(launch.state_dir, fs::perms::owner_all, fs::perm_options::replace, ec);
        auto argv = command_line(launch);
        argv.front() = python.string();

        std::string why_not;
        const auto pid = spawn(argv, launch.state_dir / "web-client.log", why_not);
        if (pid < 0) {
          BOOST_LOG(error) << "Browser client: "sv << why_not;
          std::lock_guard lk {mutex};
          state = "failed";
          last_error = why_not;
          break;
        }
        BOOST_LOG(info) << "Browser client: started on port "sv << launch.gateway_port << " (pid "sv << pid << ", "sv
                        << (launch.allow == "pc"sv ? "this computer only"sv : "LAN and Tailscale"sv) << ')';
        const auto started = std::chrono::steady_clock::now();
        {
          std::lock_guard lk {mutex};
          child_pid = pid;
          state = "running";
        }

        int status = 0;
        bool exited = false;
        for (;;) {
          std::unique_lock lk {mutex};
          if (!want) {
            break;
          }
          lk.unlock();
          if (reaped(pid, status)) {
            exited = true;
            break;
          }
          lk.lock();
          wake.wait_for(lk, 250ms, [] {
            return !want;
          });
        }
        if (!exited) {
          stop_group(pid);
          BOOST_LOG(info) << "Browser client: stopped"sv;
          std::lock_guard lk {mutex};
          child_pid = -1;
          break;
        }
        ::kill(-pid, SIGKILL);  // anything the gateway left behind

        const auto ran = std::chrono::steady_clock::now() - started;
        if (ran > 60s) {
          backoff = 1s;
        }
        const auto why = exit_text(status);
        BOOST_LOG(warning) << "Browser client: sidecar "sv << why << ", restarting in "sv << backoff.count() << " s (see "sv
                           << (launch.state_dir / "web-client.log").string() << ')';
        std::unique_lock lk {mutex};
        child_pid = -1;
        ++restarts;
        last_error = why;
        state = "failed";
        if (wake.wait_for(lk, backoff, [] {
              return !want;
            })) {
          break;
        }
        state = "starting";
        backoff = std::min(backoff * 2, std::chrono::seconds {60s});
      }
      std::lock_guard lk {mutex};
      if (!want) {
        state = "off";  // otherwise it stays "not_installed" or "failed"
      }
      thread_alive = false;
    }
#endif
  }  // namespace

  launch_t make_launch(const int base_port, const std::string_view bind_address, const bool ipv6, const std::string_view allow) {
    launch_t l;
    const auto bind = trim(bind_address);
    const bool any = bind.empty() || bind == "0.0.0.0" || bind == "::";
    l.listen_host = any ? (ipv6 ? "::" : "0.0.0.0") : bind;
    l.nova_address = any ? "127.0.0.1" : bind;
    l.gateway_port = port_at(base_port, gateway_port_offset);
    l.upstream_port = port_at(base_port, upstream_port_offset);
    l.udp_min = port_at(base_port, udp_first_offset);
    l.udp_max = port_at(base_port, udp_first_offset + udp_count - 1);
    l.nova_web_port = port_at(base_port, 1);
    l.nova_http_port = port_at(base_port, 0);
    l.allow = allow == "pc"sv ? "pc" : "lan";  // wan is capped at lan: never open to the internet
    return l;
  }

  std::vector<std::string> command_line(const launch_t &l) {
    return {
      "python3",
      (l.lib_dir / "gateway" / "nova_web_gateway.py").string(),
      "run",
      "--lib-dir",
      l.lib_dir.string(),
      "--state-dir",
      l.state_dir.string(),
      "--listen-host",
      l.listen_host,
      "--listen-port",
      std::to_string(l.gateway_port),
      "--upstream-port",
      std::to_string(l.upstream_port),
      "--udp-min",
      std::to_string(l.udp_min),
      "--udp-max",
      std::to_string(l.udp_max),
      "--nova-address",
      l.nova_address,
      "--nova-address-url",
      url_host(l.nova_address),
      "--nova-web-port",
      std::to_string(l.nova_web_port),
      "--nova-http-port",
      std::to_string(l.nova_http_port),
      "--nova-web-cert",
      l.tls_cert,
      "--tls-cert",
      l.tls_cert,
      "--tls-key",
      l.tls_key,
      "--allow",
      l.allow,
    };
  }

  fs::path lib_dir() {
    if (const char *dir = std::getenv("NOVA_WEB_CLIENT_DIR"); dir && *dir) {
      return dir;
    }
    return NOVA_WEB_CLIENT_DIR;
  }

  bool installed_in(const fs::path &dir) {
    std::error_code ec;
    return fs::is_regular_file(dir / "web-server", ec) && fs::is_regular_file(dir / "streamer", ec) &&
           fs::is_regular_file(dir / "gateway" / "nova_web_gateway.py", ec) && fs::is_directory(dir / "static", ec);
  }

  std::string set_config_line(const std::string_view text, const std::string_view key, const std::string_view value) {
    std::string out;
    bool done = false;
    std::size_t pos = 0;
    while (pos < text.size()) {
      auto end = text.find('\n', pos);
      const auto line = text.substr(pos, end == std::string_view::npos ? std::string_view::npos : end - pos);
      pos = end == std::string_view::npos ? text.size() : end + 1;
      const auto eq = line.find('=');
      const auto trimmed = trim(line);
      if (eq != std::string_view::npos && !trimmed.starts_with('#') && trim(line.substr(0, eq)) == key) {
        if (!done) {
          out += std::string {key} + " = " + std::string {value} + "\n";
          done = true;
        }
        continue;  // drop duplicates
      }
      out += line;
      out += '\n';
    }
    if (!done) {
      out += std::string {key} + " = " + std::string {value} + "\n";
    }
    return out;
  }

  void apply(const bool enabled) {
#if defined(__linux__) || defined(__FreeBSD__)
    std::lock_guard serial {apply_mutex};
    std::thread finished;
    {
      std::lock_guard lk {mutex};
      want = enabled;
      if (!enabled) {
        wake.notify_all();
        finished = std::move(worker);
      } else if (!thread_alive) {
        if (worker.joinable()) {
          finished = std::move(worker);
        }
      } else {
        return;  // already running (or restarting)
      }
    }
    if (finished.joinable()) {
      finished.join();
    }
    std::lock_guard lk {mutex};
    if (enabled && want && !thread_alive) {
      restarts = 0;
      last_error.clear();
      state = "starting";
      thread_alive = true;
      worker = std::thread {run_loop};
    } else if (!enabled) {
      state = "off";
    }
#else
    if (enabled) {
      BOOST_LOG(warning) << "Browser client: only available on Linux"sv;
    }
#endif
  }

  void start() {
    if (config::nvhttp.web_client) {
      apply(true);
    }
  }

  bool set_enabled(const bool enabled) {
    const auto &file = config::sunshine.config_file;
    const auto text = file_handler::read_file(file.c_str());
    if (file_handler::write_file(file.c_str(), set_config_line(text, "web_client", enabled ? "enabled" : "disabled")) != 0) {
      BOOST_LOG(error) << "Browser client: couldn't save the setting to "sv << file;
      return false;
    }
    config::nvhttp.web_client = enabled;
    apply(enabled);
    return true;
  }

  void shutdown() {
    apply(false);
  }

  status_t status() {
    status_t s;
    const auto launch = make_launch(config::sunshine.port, config::sunshine.bind_address, config::sunshine.address_family == "both"sv, config::nvhttp.origin_web_ui_allowed);
    const auto dir = lib_dir();
    s.installed = installed_in(dir) && !find_python().empty();
    s.version = s.installed ? read_version(dir) : ""s;
    s.port = launch.gateway_port;
    s.udp_min = launch.udp_min;
    s.udp_max = launch.udp_max;
    s.allowed = launch.allow;
    std::lock_guard lk {mutex};
    s.enabled = config::nvhttp.web_client;
    s.state = !s.installed ? "not_installed"s : state;
    s.restarts = restarts;
    s.last_error = last_error;
    return s;
  }
}  // namespace web_client
