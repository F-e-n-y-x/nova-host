/**
 * @file src/virtual_display.cpp
 * @brief Definitions for Nova's separate headless X server ("Virtual display" mode).
 */
// standard includes
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

#ifndef _WIN32
  // platform includes
  #include <fcntl.h>
  #include <poll.h>
  #include <signal.h>
  #include <sys/stat.h>
  #include <sys/wait.h>
  #include <unistd.h>
  #ifdef __linux__
    #include <sys/syscall.h>
  #endif
#endif

// local includes
#include "logging.h"
#include "virtual_display.h"

#ifndef _WIN32
extern char **environ;
#endif

using namespace std::literals;
namespace fs = std::filesystem;

namespace virtual_display {
  namespace {
    constexpr int SIG_TERM = 15;  ///< SIGTERM (spelled out so the portable code builds everywhere).
    constexpr int SIG_KILL = 9;  ///< SIGKILL.

    std::recursive_mutex &env_mutex() {
      static std::recursive_mutex mutex;
      return mutex;
    }

    std::mutex &target_mutex() {
      static std::mutex mutex;
      return mutex;
    }

    std::optional<target_t> &target_slot() {
      static std::optional<target_t> target;
      return target;
    }

    std::optional<std::string> get_env(const char *name) {
      if (const char *value = std::getenv(name)) {
        return std::string {value};
      }
      return std::nullopt;
    }

    void put_env(const char *name, const std::optional<std::string> &value) {
#ifdef _WIN32
      _putenv_s(name, value ? value->c_str() : "");
#else
      if (value) {
        setenv(name, value->c_str(), 1);
      } else {
        unsetenv(name);
      }
#endif
    }

    std::string join(const std::vector<std::string> &argv) {
      std::string out;
      for (const auto &arg : argv) {
        if (!out.empty()) {
          out += ' ';
        }
        out += arg;
      }
      return out;
    }

#ifndef _WIN32
    /**
     * @brief Build a child's environment: ours with the overrides applied (and empty values removed).
     */
    std::vector<std::string> child_environment(const env_list_t &extra) {
      std::vector<std::string> out;
      for (char **e = environ; e && *e; ++e) {
        std::string_view entry {*e};
        const auto eq = entry.find('=');
        const auto key = entry.substr(0, eq);
        const bool overridden = std::ranges::any_of(extra, [&](const auto &kv) {
          return kv.first == key;
        });
        if (!overridden) {
          out.emplace_back(entry);
        }
      }
      for (const auto &[key, value] : extra) {
        if (!value.empty()) {
          out.push_back(key + '=' + value);
        }
      }
      return out;
    }

    /**
     * @brief fork + exec with only async-signal-safe calls in the child.
     *
     * The child gets its own session (so its whole group can be signalled), stdin from /dev/null,
     * stdout and stderr to the log, the signal mask cleared, and every descriptor above 2 closed
     * except `ready_fd`, which becomes fd 3. Without the close, Xorg would inherit Nova's listening
     * sockets and keep the ports bound after Nova exits.
     */
    long fork_exec(const std::vector<std::string> &argv, const env_list_t &extra, const fs::path &log, int ready_fd) {
      if (argv.empty()) {
        return -1;
      }
      auto env_strings = child_environment(extra);
      std::vector<char *> c_argv;
      for (const auto &a : argv) {
        c_argv.push_back(const_cast<char *>(a.c_str()));
      }
      c_argv.push_back(nullptr);
      std::vector<char *> c_env;
      for (auto &e : env_strings) {
        c_env.push_back(e.data());
      }
      c_env.push_back(nullptr);

      const int null_fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
      const int log_fd = log.empty() ? -1 : open(log.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);

      const pid_t pid = fork();
      if (pid == 0) {
        setsid();
        sigset_t empty;
        sigemptyset(&empty);
        sigprocmask(SIG_SETMASK, &empty, nullptr);
        struct sigaction dfl {};
        dfl.sa_handler = SIG_DFL;
        for (int sig : {SIGTERM, SIGINT, SIGHUP, SIGPIPE, SIGCHLD, SIGUSR1, SIGUSR2}) {
          sigaction(sig, &dfl, nullptr);
        }
        if (null_fd >= 0) {
          dup2(null_fd, 0);
        }
        const int out_fd = log_fd >= 0 ? log_fd : null_fd;
        if (out_fd >= 0) {
          dup2(out_fd, 1);
          dup2(out_fd, 2);
        }
        int first_to_close = 3;
        if (ready_fd >= 0) {
          if (ready_fd == 3) {
            fcntl(3, F_SETFD, 0);
          } else {
            dup2(ready_fd, 3);
          }
          first_to_close = 4;
        }
  #if defined(__linux__) && defined(SYS_close_range)
        if (syscall(SYS_close_range, first_to_close, ~0U, 0) != 0)
  #endif
        {
          for (int fd = first_to_close; fd < 4096; ++fd) {
            close(fd);
          }
        }
        execve(c_argv[0], c_argv.data(), c_env.data());
        _exit(127);
      }

      if (null_fd >= 0) {
        close(null_fd);
      }
      if (log_fd >= 0) {
        close(log_fd);
      }
      return pid < 0 ? -1 : static_cast<long>(pid);
    }

    bool real_alive(long pid) {
      if (pid <= 0) {
        return false;
      }
      int status = 0;
      const pid_t r = waitpid(static_cast<pid_t>(pid), &status, WNOHANG);
      if (r == pid) {
        return false;  // reaped just now
      }
      if (r == 0) {
        return true;  // our child, still running
      }
      // Not our child (a previous run's server): check it exists and isn't a zombie.
      if (kill(static_cast<pid_t>(pid), 0) != 0 && errno != EPERM) {
        return false;
      }
      std::ifstream stat {"/proc/" + std::to_string(pid) + "/stat"};
      std::string line;
      if (std::getline(stat, line)) {
        const auto paren = line.rfind(')');
        if (paren != std::string::npos && paren + 2 < line.size() && line[paren + 2] == 'Z') {
          return false;
        }
      }
      return true;
    }

    long real_spawn_server(const std::vector<std::string> &argv, const env_list_t &extra, const fs::path &log, int timeout_ms) {
      int fds[2];
      if (pipe2(fds, O_CLOEXEC) != 0) {
        return -1;
      }
      auto with_fd = argv;
      with_fd.emplace_back("-displayfd");
      with_fd.emplace_back("3");
      const long pid = fork_exec(with_fd, extra, log, fds[1]);
      close(fds[1]);
      if (pid < 0) {
        close(fds[0]);
        return -1;
      }

      // Xorg writes the display number to -displayfd once it accepts connections.
      std::string got;
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
      while (got.find('\n') == std::string::npos) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) {
          break;
        }
        pollfd p {fds[0], POLLIN, 0};
        if (poll(&p, 1, static_cast<int>(left)) <= 0) {
          if (errno == EINTR) {
            continue;
          }
          break;
        }
        char buf[32];
        const auto n = read(fds[0], buf, sizeof(buf));
        if (n <= 0) {
          break;  // the server exited before it was ready
        }
        got.append(buf, static_cast<std::size_t>(n));
      }
      close(fds[0]);
      if (got.empty()) {
        kill(-static_cast<pid_t>(pid), SIGKILL);
        waitpid(static_cast<pid_t>(pid), nullptr, 0);
        return -1;
      }
      return pid;
    }

    int real_run(const std::vector<std::string> &argv, const env_list_t &extra) {
      const long pid = fork_exec(argv, extra, {}, -1);
      if (pid < 0) {
        return -1;
      }
      const auto deadline = std::chrono::steady_clock::now() + 5s;
      while (true) {
        int status = 0;
        const pid_t r = waitpid(static_cast<pid_t>(pid), &status, WNOHANG);
        if (r == pid) {
          return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        }
        if (r < 0) {
          return -1;
        }
        if (std::chrono::steady_clock::now() > deadline) {
          kill(-static_cast<pid_t>(pid), SIGKILL);
          waitpid(static_cast<pid_t>(pid), nullptr, 0);
          return -1;
        }
        std::this_thread::sleep_for(10ms);
      }
    }

    std::string real_cmdline(long pid) {
      std::ifstream in {"/proc/" + std::to_string(pid) + "/cmdline", std::ios::binary};
      std::string raw {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
      std::ranges::replace(raw, '\0', ' ');
      while (!raw.empty() && raw.back() == ' ') {
        raw.pop_back();
      }
      return raw;
    }

    std::string real_cookie() {
      unsigned char bytes[16] {};
      std::ifstream in {"/dev/urandom", std::ios::binary};
      in.read(reinterpret_cast<char *>(bytes), sizeof(bytes));
      if (!in) {
        return {};
      }
      static constexpr char hex[] = "0123456789abcdef";
      std::string out;
      for (auto b : bytes) {
        out += hex[b >> 4];
        out += hex[b & 0xF];
      }
      return out;
    }

    std::string real_gpu_bus_id() {
      std::error_code ec;
      std::vector<std::string> slots;
      for (const auto &entry : fs::directory_iterator("/proc/driver/nvidia/gpus", ec)) {
        slots.push_back(entry.path().filename().string());
      }
      std::ranges::sort(slots);
      for (const auto &slot : slots) {
        if (auto id = bus_id_from_pci_slot(slot); !id.empty()) {
          return id;
        }
      }
      return {};
    }
#endif
  }  // namespace

  void set_capture_target(std::optional<target_t> target) {
    std::lock_guard lg {target_mutex()};
    target_slot() = std::move(target);
  }

  std::optional<target_t> capture_target() {
    std::lock_guard lg {target_mutex()};
    return target_slot();
  }

  scoped_x_env_t::scoped_x_env_t(const std::optional<target_t> &target):
      lock_ {env_mutex()} {
    if (!target || target->display.empty()) {
      return;
    }
    old_display_ = get_env("DISPLAY");
    old_xauthority_ = get_env("XAUTHORITY");
    put_env("DISPLAY", target->display);
    put_env("XAUTHORITY", target->xauthority.empty() ? old_xauthority_ : std::optional<std::string> {target->xauthority});
    applied_ = true;
  }

  scoped_x_env_t::~scoped_x_env_t() {
    if (applied_) {
      put_env("DISPLAY", old_display_);
      put_env("XAUTHORITY", old_xauthority_);
    }
  }

  std::vector<std::string> nvfbc_names_with_headless_fallback(std::vector<std::string> names, unsigned outputs, unsigned screen_width, unsigned screen_height) {
    if (outputs == 0 && names.empty() && screen_width > 0 && screen_height > 0) {
      BOOST_LOG(info) << "NvFBC: headless screen, offering it as display [0]"sv;
      return {"0"};
    }
    return names;
  }

  ops_t default_ops() {
    ops_t ops;
#ifndef _WIN32
    ops.spawn_server = real_spawn_server;
    ops.spawn = [](const std::vector<std::string> &argv, const env_list_t &env, const fs::path &log) {
      return fork_exec(argv, env, log, -1);
    };
    ops.run = real_run;
    ops.alive = real_alive;
    ops.signal = [](long pid, int sig) {
      if (pid <= 0) {
        return;
      }
      if (kill(-static_cast<pid_t>(pid), sig) != 0) {
        kill(static_cast<pid_t>(pid), sig);
      }
    };
    ops.cmdline = real_cmdline;
    ops.display_taken = [](int n) {
      std::error_code ec;
      return fs::exists("/tmp/.X11-unix/X" + std::to_string(n), ec) || fs::exists("/tmp/.X" + std::to_string(n) + "-lock", ec);
    };
    ops.cookie = real_cookie;
    ops.gpu_bus_id = real_gpu_bus_id;
    ops.xorg_path = []() -> std::string {
      std::error_code ec;
      // Not /usr/bin/Xorg: that is the setuid wrapper, which refuses non-console users.
      for (const auto *path : {"/usr/lib/xorg/Xorg", "/usr/libexec/Xorg", "/usr/lib/Xorg"}) {
        if (fs::exists(path, ec)) {
          return path;
        }
      }
      return {};
    };
#else
    ops.spawn_server = [](const auto &, const auto &, const auto &, int) {
      return -1L;
    };
    ops.spawn = [](const auto &, const auto &, const auto &) {
      return -1L;
    };
    ops.run = [](const auto &, const auto &) {
      return -1;
    };
    ops.alive = [](long) {
      return false;
    };
    ops.signal = [](long, int) {
    };
    ops.cmdline = [](long) {
      return std::string {};
    };
    ops.display_taken = [](int) {
      return true;
    };
    ops.cookie = []() {
      return std::string {};
    };
    ops.gpu_bus_id = []() {
      return std::string {};
    };
    ops.xorg_path = []() {
      return std::string {};
    };
#endif
    ops.sleep_ms = [](int ms) {
      std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    };
    return ops;
  }

  std::string bus_id_from_pci_slot(std::string_view slot) {
    // domain:bus:device.function, all hex, e.g. 0000:08:00.0
    unsigned domain = 0, bus = 0, device = 0, function = 0;
    const auto c1 = slot.find(':');
    const auto c2 = slot.find(':', c1 == std::string_view::npos ? 0 : c1 + 1);
    const auto dot = slot.find('.', c2 == std::string_view::npos ? 0 : c2 + 1);
    if (c1 == std::string_view::npos || c2 == std::string_view::npos || dot == std::string_view::npos) {
      return {};
    }
    auto parse = [](std::string_view part, unsigned &out) {
      if (part.empty()) {
        return false;
      }
      const auto [ptr, ec] = std::from_chars(part.data(), part.data() + part.size(), out, 16);
      return ec == std::errc {} && ptr == part.data() + part.size();
    };
    if (!parse(slot.substr(0, c1), domain) || !parse(slot.substr(c1 + 1, c2 - c1 - 1), bus) ||
        !parse(slot.substr(c2 + 1, dot - c2 - 1), device) || !parse(slot.substr(dot + 1), function)) {
      return {};
    }
    std::string out = "PCI:" + std::to_string(bus);
    if (domain != 0) {
      out += '@' + std::to_string(domain);
    }
    out += ':' + std::to_string(device) + ':' + std::to_string(function);
    return out;
  }

  std::string xauth_entry(int display_number, std::string_view cookie_hex) {
    // Xauthority record: family, address, display number, auth name, auth data; each field but the
    // family is a big-endian 16-bit length followed by the bytes. FamilyWild matches any host.
    auto field = [](std::string &out, std::string_view bytes) {
      out += static_cast<char>((bytes.size() >> 8) & 0xFF);
      out += static_cast<char>(bytes.size() & 0xFF);
      out.append(bytes);
    };
    std::string data;
    for (std::size_t i = 0; i + 1 < cookie_hex.size(); i += 2) {
      unsigned byte = 0;
      std::from_chars(cookie_hex.data() + i, cookie_hex.data() + i + 2, byte, 16);
      data += static_cast<char>(byte);
    }
    std::string out;
    out += static_cast<char>(0xFF);
    out += static_cast<char>(0xFF);
    field(out, "");
    field(out, std::to_string(display_number));
    field(out, "MIT-MAGIC-COOKIE-1");
    field(out, data);
    return out;
  }

  std::string render_xorg_conf(const std::string &bus_id, int width, int height) {
    std::ostringstream out;
    out << "# Written by Nova for one Virtual display session; removed when the session ends.\n"
           "Section \"ServerFlags\"\n"
           "    Option \"AutoAddDevices\" \"true\"\n"  // hot-adds input devices tagged seat-nova
           "    Option \"AutoEnableDevices\" \"true\"\n"
           "    Option \"AutoAddGPU\" \"false\"\n"
           "    Option \"AutoBindGPU\" \"false\"\n"
           "    Option \"DontVTSwitch\" \"true\"\n"
           "    Option \"BlankTime\" \"0\"\n"
           "    Option \"StandbyTime\" \"0\"\n"
           "    Option \"SuspendTime\" \"0\"\n"
           "    Option \"OffTime\" \"0\"\n"
           "EndSection\n"
           "Section \"ServerLayout\"\n"
           "    Identifier \"NovaVirtual\"\n"
           "    Screen 0 \"NovaScreen\"\n"
           "EndSection\n"
           "Section \"InputClass\"\n"
           "    Identifier \"Nova virtual display input\"\n"
           "    MatchDevicePath \"/dev/input/event*\"\n"
           "    Driver \"libinput\"\n"
           "EndSection\n"
           "Section \"InputClass\"\n"
           "    Identifier \"Nova virtual display touchscreen\"\n"
           "    MatchProduct \"libvirtualhid Touchscreen\"\n"
           "    MatchDevicePath \"/dev/input/event*\"\n"
           "    Driver \"libinput\"\n"
           "    Option \"Tapping\" \"on\"\n"
           "EndSection\n"
           "Section \"Device\"\n"
           "    Identifier \"NovaGPU\"\n"
           "    Driver \"nvidia\"\n"
           "    BusID \""
        << bus_id << "\"\n"
                     "EndSection\n"
                     "Section \"Screen\"\n"
                     "    Identifier \"NovaScreen\"\n"
                     "    Device \"NovaGPU\"\n"
                     "    DefaultDepth 24\n"
                     "    Option \"UseDisplayDevice\" \"none\"\n"  // NoScanout: never asks for the desktop's heads
                     "    Option \"AllowEmptyInitialConfiguration\" \"true\"\n"
                     "    SubSection \"Display\"\n"
                     "        Depth 24\n"
                     "        Virtual "
        << width << ' ' << height << "\n"
                                     "    EndSubSection\n"
                                     "EndSection\n";
    return out.str();
  }

  int pick_display_number(const ops_t &ops, int first) {
    for (int n = std::max(first, 1); n < 100; ++n) {
      if (!ops.display_taken(n)) {
        return n;
      }
    }
    return -1;
  }

  x_server_t::x_server_t(fs::path state_dir, std::string wm, ops_t ops, env_list_t wm_env):
      state_dir_ {std::move(state_dir)},
      wm_ {std::move(wm)},
      ops_ {std::move(ops)},
      wm_env_ {std::move(wm_env)} {
  }

  env_list_t x_server_t::display_env() const {
    env_list_t env {{"DISPLAY", target_->display}, {"XAUTHORITY", target_->xauthority}, {"WAYLAND_DISPLAY", ""}};
    env.emplace_back("NOVA_VD_DIR", (state_dir_ / ("X" + std::to_string(number_))).string());
    env.insert(env.end(), wm_env_.begin(), wm_env_.end());
    return env;
  }

  int x_server_t::run_on_display(const std::vector<std::string> &argv) {
    if (!running() || argv.empty()) {
      return -1;
    }
    return ops_.run(argv, display_env());
  }

  bool valid_scale(const int percent) {
    return std::ranges::find(scales, percent) != scales.end();
  }

  env_list_t scale_env(const int percent) {
    if (!valid_scale(percent) || percent == 100) {
      return {};
    }
    const int gdk_scale = percent >= 200 ? 2 : 1;
    const auto fmt = [](double v) {
      std::ostringstream out;
      out << v;
      return out.str();
    };
    const double factor = percent / 100.0;
    return {
      {"GDK_SCALE", std::to_string(gdk_scale)},
      {"GDK_DPI_SCALE", fmt(factor / gdk_scale)},
      {"QT_SCALE_FACTOR", fmt(factor)},
      {"QT_AUTO_SCREEN_SCALE_FACTOR", "0"},
      {"ELM_SCALE", fmt(factor)},
    };
  }

  fs::path x_server_t::marker() const {
    return state_dir_ / "active";
  }

  bool x_server_t::running() const {
    return target_.has_value();
  }

  std::optional<target_t> x_server_t::target() const {
    return target_;
  }

  void x_server_t::write_marker() const {
    std::ofstream out {marker()};
    out << "display=" << number_ << '\n'
        << "server=" << server_pid_ << '\n'
        << "wm=" << wm_pid_ << '\n'
        << "wm_cmd=" << wm_ << '\n';
  }

  bool x_server_t::set_screen_size(const mode_t &mode) {
    const env_list_t env {{"DISPLAY", target_->display}, {"XAUTHORITY", target_->xauthority}};
    const auto size = std::to_string(mode.width) + 'x' + std::to_string(mode.height);
    if (ops_.run({"/usr/bin/xrandr", "--fb", size}, env) != 0) {
      BOOST_LOG(error) << "Virtual display: couldn't set "sv << target_->display << " to "sv << size;
      return false;
    }
    target_->width = mode.width;
    target_->height = mode.height;
    target_->fps = mode.fps;
    return true;
  }

  std::optional<target_t> x_server_t::start(const mode_t &mode) {
    if (running()) {
      stop();
    }
    if (mode.width <= 0 || mode.height <= 0) {
      BOOST_LOG(error) << "Virtual display: invalid size "sv << mode.width << 'x' << mode.height;
      return std::nullopt;
    }
    const auto xorg = ops_.xorg_path();
    if (xorg.empty()) {
      BOOST_LOG(error) << "Virtual display: no Xorg server binary found (install xserver-xorg-core)"sv;
      return std::nullopt;
    }
    const auto bus_id = ops_.gpu_bus_id();
    if (bus_id.empty()) {
      BOOST_LOG(error) << "Virtual display: no NVIDIA GPU found in /proc/driver/nvidia/gpus; the headless X server needs the NVIDIA driver"sv;
      return std::nullopt;
    }
    const int n = pick_display_number(ops_);
    if (n < 0) {
      BOOST_LOG(error) << "Virtual display: no free X display number between :20 and :99"sv;
      return std::nullopt;
    }

    std::error_code ec;
    const auto dir = state_dir_ / ("X" + std::to_string(n));
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "conf.d", ec);
    fs::permissions(state_dir_, fs::perms::owner_all, ec);
    fs::permissions(dir, fs::perms::owner_all, ec);
    const auto conf = dir / "xorg.conf";
    const auto xauth = dir / "xauthority";
    const auto log = dir / "Xorg.log";
    std::ofstream {conf} << render_xorg_conf(bus_id, mode.width, mode.height);

    const auto display = ":" + std::to_string(n);
    const auto cookie = ops_.cookie();
    if (cookie.size() != 32) {
      BOOST_LOG(error) << "Virtual display: couldn't generate an X cookie"sv;
      fs::remove_all(dir, ec);
      return std::nullopt;
    }
    // Written directly (not with `xauth add`) so the cookie never appears on a command line,
    // where every local user could read it from /proc.
    {
      std::ofstream {xauth}.close();
      fs::permissions(xauth, fs::perms::owner_read | fs::perms::owner_write, ec);
      std::ofstream out {xauth, std::ios::binary | std::ios::trunc};
      out << xauth_entry(n, cookie);
      if (!out) {
        BOOST_LOG(error) << "Virtual display: couldn't write "sv << xauth.string();
        fs::remove_all(dir, ec);
        return std::nullopt;
      }
    }

    number_ = n;
    const std::vector<std::string> argv {
      xorg,
      display,
      "-seat",
      "seat-nova",
      "-config",
      conf.string(),
      "-configdir",
      (dir / "conf.d").string(),
      "-logfile",
      log.string(),
      "-auth",
      xauth.string(),
      "-nolisten",
      "tcp",
      "-noreset",
      "-novtswitch",
      "-s",
      "0",
    };
    BOOST_LOG(info) << "Virtual display: starting ["sv << join(argv) << ']';
    const auto started = std::chrono::steady_clock::now();
    server_pid_ = ops_.spawn_server(argv, {{"DISPLAY", ""}}, log, 5000);
    if (server_pid_ < 0) {
      BOOST_LOG(error) << "Virtual display: the X server on "sv << display << " didn't start; see "sv << log.string();
      // Keep the log for diagnosis.
      fs::rename(log, state_dir_ / "Xorg.failed.log", ec);
      fs::remove_all(dir, ec);
      number_ = -1;
      return std::nullopt;
    }
    target_ = target_t {display, xauth.string(), mode.width, mode.height, mode.fps};
    if (const auto first_word = wm_.substr(0, wm_.find(' ')); fs::path(first_word).filename() == "nova-vd-session") {
      // The session publishes its private D-Bus address there (see vd_app_launch::session_env()).
      target_->session_dir = (dir / "session").string();
    }
    write_marker();
    BOOST_LOG(info) << "Virtual display: "sv << display << " is up at "sv << mode.width << 'x' << mode.height << " in "sv
                    << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count() << " ms"sv;

    // The Virtual line in the config sets the size already; this also covers drivers that round it.
    set_screen_size(mode);

    if (!wm_.empty()) {
      wm_pid_ = ops_.spawn({"/bin/sh", "-c", "exec " + wm_}, display_env(), dir / "wm.log");
      if (wm_pid_ < 0) {
        BOOST_LOG(warning) << "Virtual display: couldn't start the window manager ["sv << wm_ << ']';
      }
      write_marker();
    }
    return target_;
  }

  bool x_server_t::resize(const mode_t &mode) {
    if (!running() || mode.width <= 0 || mode.height <= 0) {
      return false;
    }
    if (target_->width == mode.width && target_->height == mode.height) {
      target_->fps = mode.fps;
      return true;
    }
    BOOST_LOG(info) << "Virtual display: resizing "sv << target_->display << " to "sv << mode.width << 'x' << mode.height;
    return set_screen_size(mode);
  }

  void x_server_t::terminate(long pid, const std::string &expect) {
    if (pid <= 0 || !ops_.alive(pid)) {
      return;
    }
    if (!expect.empty() && ops_.cmdline(pid).find(expect) == std::string::npos) {
      BOOST_LOG(warning) << "Virtual display: pid "sv << pid << " is no longer ["sv << expect << "], leaving it alone"sv;
      return;
    }
    ops_.signal(pid, SIG_TERM);
    for (int i = 0; i < 40 && ops_.alive(pid); ++i) {
      ops_.sleep_ms(50);
    }
    if (ops_.alive(pid)) {
      BOOST_LOG(warning) << "Virtual display: pid "sv << pid << " ignored SIGTERM, killing it"sv;
      ops_.signal(pid, SIG_KILL);
      for (int i = 0; i < 20 && ops_.alive(pid); ++i) {
        ops_.sleep_ms(50);
      }
    }
  }

  void x_server_t::stop() {
    if (!running() && server_pid_ < 0) {
      return;
    }
    const auto display = target_ ? target_->display : std::string {};
    BOOST_LOG(info) << "Virtual display: stopping "sv << display;
    terminate(wm_pid_, {});
    terminate(server_pid_, {});
    std::error_code ec;
    if (number_ >= 0) {
      const auto dir = state_dir_ / ("X" + std::to_string(number_));
      fs::rename(dir / "Xorg.log", state_dir_ / "Xorg.last.log", ec);
      fs::remove_all(dir, ec);
    }
    fs::remove(marker(), ec);
    target_.reset();
    number_ = -1;
    server_pid_ = -1;
    wm_pid_ = -1;
  }

  bool x_server_t::recover() {
    std::error_code ec;
    if (!fs::exists(marker(), ec)) {
      return false;
    }
    int number = -1;
    long server = -1;
    long wm = -1;
    std::string wm_cmd;
    {
      std::ifstream in {marker()};
      std::string line;
      while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
          continue;
        }
        const auto key = line.substr(0, eq);
        const auto value = line.substr(eq + 1);
        try {
          if (key == "display") {
            number = std::stoi(value);
          } else if (key == "server") {
            server = std::stol(value);
          } else if (key == "wm") {
            wm = std::stol(value);
          } else if (key == "wm_cmd") {
            wm_cmd = value;
          }
        } catch (...) {
        }
      }
    }
    BOOST_LOG(info) << "Virtual display: cleaning up :"sv << number << " left by the previous run"sv;
    if (number >= 0) {
      // Only a process that is still that server: "<xorg> :N -seat seat-nova ...".
      terminate(server, ":" + std::to_string(number) + " -seat seat-nova");
      const auto first_word = wm_cmd.substr(0, wm_cmd.find(' '));
      if (!first_word.empty()) {
        terminate(wm, fs::path(first_word).filename().string());
      }
      const auto dir = state_dir_ / ("X" + std::to_string(number));
      fs::rename(dir / "Xorg.log", state_dir_ / "Xorg.last.log", ec);
      fs::remove_all(dir, ec);
    }
    fs::remove(marker(), ec);
    return true;
  }

  env_list_t app_env(const target_t &target, int fps_cap, const std::string &pulse_sink, const std::string &base_ld_preload, const std::string &mangohud_gl) {
    env_list_t env {
      {"DISPLAY", target.display},
      {"XAUTHORITY", target.xauthority},
      {"NOVA_VIRTUAL_DISPLAY", target.display},
    };
    if (!pulse_sink.empty()) {
      env.emplace_back("PULSE_SINK", pulse_sink);
    }
    for (auto &kv : scale_env(target.scale)) {
      env.push_back(std::move(kv));
    }
    const int cap = fps_cap == 0 ? target.fps : fps_cap;
    if (cap > 0) {
      // A NoScanout screen has no vblank, so an uncapped game renders thousands of frames a second.
      const auto fps = std::to_string(cap);
      env.emplace_back("__GL_SYNC_TO_VBLANK", "0");
      env.emplace_back("DXVK_FRAME_RATE", fps);
      env.emplace_back("VKD3D_FRAME_RATE", fps);
      if (!mangohud_gl.empty()) {
        // MangoHud's limiter covers native Vulkan (implicit layer) and OpenGL (preload); no_display hides the overlay.
        env.emplace_back("MANGOHUD", "1");
        env.emplace_back("MANGOHUD_CONFIG", "fps_limit=" + fps + ",no_display");
        std::string preload = base_ld_preload;
        if (preload.find(mangohud_gl) == std::string::npos) {
          preload += (preload.empty() ? "" : ":") + mangohud_gl;
        }
        env.emplace_back("LD_PRELOAD", preload);
      }
    }
    return env;
  }

  const std::vector<std::string> &app_env_keys() {
    static const std::vector<std::string> keys {
      "DISPLAY",
      "XAUTHORITY",
      "NOVA_VIRTUAL_DISPLAY",
      "PULSE_SINK",
      "__GL_SYNC_TO_VBLANK",
      "DXVK_FRAME_RATE",
      "VKD3D_FRAME_RATE",
      "MANGOHUD",
      "MANGOHUD_CONFIG",
      "LD_PRELOAD",
      "GDK_SCALE",
      "GDK_DPI_SCALE",
      "QT_SCALE_FACTOR",
      "QT_AUTO_SCREEN_SCALE_FACTOR",
      "ELM_SCALE",
      // the desktop session's private D-Bus and dconf (vd_app_launch::session_env())
      "DBUS_SESSION_BUS_ADDRESS",
      "DCONF_PROFILE",
      "GIO_USE_VFS",
      "GIO_USE_VOLUME_MONITOR",
      "GTK_USE_PORTAL",
      "NO_AT_BRIDGE",
    };
    return keys;
  }

  std::string mangohud_gl_preload() {
    std::error_code ec;
    for (const auto *lib : {"lib/x86_64-linux-gnu", "lib64", "lib", "lib/aarch64-linux-gnu"}) {
      if (fs::exists(fs::path("/usr") / lib / "mangohud" / "libMangoHud_opengl.so", ec)) {
        return "/usr/$LIB/mangohud/libMangoHud_opengl.so";
      }
    }
    return {};
  }

  bool seat_rule_installed(const std::vector<fs::path> &roots) {
    static const std::vector<fs::path> defaults {"/etc/udev/rules.d", "/run/udev/rules.d", "/usr/lib/udev/rules.d", "/lib/udev/rules.d", "/usr/local/lib/udev/rules.d"};
    std::error_code ec;
    for (const auto &root : roots.empty() ? defaults : roots) {
      if (fs::exists(root / "61-nova-host-vd-seat.rules", ec)) {
        return true;
      }
    }
    return false;
  }
}  // namespace virtual_display
