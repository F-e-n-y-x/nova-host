/**
 * @file src/power_mode.cpp
 * @brief Definitions for Nova's streaming power mode.
 */
// standard includes
#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <fstream>
#include <sstream>
#include <thread>

// local includes
#include "config.h"
#include "logging.h"
#include "platform/common.h"
#include "power_mode.h"
#include "run_program.h"

#if defined(__linux__)
  // lib includes
  #include <gio/gio.h>
  #include <gio/gunixfdlist.h>
  #include <unistd.h>
#endif

using namespace std::literals;

namespace power_mode {
  namespace {
    constexpr auto PPD = "powerprofilesctl";
    constexpr auto TUNED = "tuned-adm";
    constexpr auto TUNED_PROFILE = "latency-performance";

    std::string trim(std::string s) {
      const auto first = s.find_first_not_of(" \t\r\n");
      if (first == std::string::npos) {
        return {};
      }
      const auto last = s.find_last_not_of(" \t\r\n");
      return s.substr(first, last - first + 1);
    }

    bool safe_token(const std::string &s) {
      return !s.empty() && s.size() <= 64 && std::ranges::all_of(s, [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.';
      });
    }

    bool safe_display(const std::string &s) {
      // ":0", ":1.0", "localhost:10.0"
      return !s.empty() && s.size() <= 64 && s.find(':') != std::string::npos && std::ranges::all_of(s, [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == ':' || c == '.' || c == '-';
      });
    }

    std::string powermizer_attr(const std::string &value = {}) {
      return value.empty() ? "[gpu:0]/GpuPowerMizerMode"s : "[gpu:0]/GpuPowerMizerMode=" + value;
    }
  }  // namespace

  bool saved_state_t::empty() const {
    return gpu_mode.empty() && cpu_tool.empty();
  }

  std::string saved_state_t::serialize() const {
    std::ostringstream out;
    out << "gpu_mode=" << gpu_mode << '\n'
        << "x_display=" << x_display << '\n'
        << "cpu_tool=" << cpu_tool << '\n'
        << "cpu_value=" << cpu_value << '\n';
    return out.str();
  }

  std::optional<saved_state_t> saved_state_t::parse(const std::string &text) {
    saved_state_t s;
    std::istringstream in {text};
    std::string line;
    while (std::getline(in, line)) {
      const auto eq = line.find('=');
      if (eq == std::string::npos) {
        continue;
      }
      const auto key = line.substr(0, eq);
      const auto value = trim(line.substr(eq + 1));
      if (key == "gpu_mode") {
        s.gpu_mode = value;
      } else if (key == "x_display") {
        s.x_display = value;
      } else if (key == "cpu_tool") {
        s.cpu_tool = value;
      } else if (key == "cpu_value") {
        s.cpu_value = value;
      }
    }
    if (!s.gpu_mode.empty() && (s.gpu_mode.size() > 2 || !std::ranges::all_of(s.gpu_mode, ::isdigit) || !safe_display(s.x_display))) {
      return std::nullopt;
    }
    if (!s.cpu_tool.empty() && ((s.cpu_tool != PPD && s.cpu_tool != TUNED) || !safe_token(s.cpu_value))) {
      return std::nullopt;
    }
    return s;
  }

  std::optional<std::string> parse_powermizer(const std::string &output) {
    std::istringstream in {output};
    std::string line;
    std::optional<std::string> value;
    while (std::getline(in, line)) {
      line = trim(line);
      if (!line.empty() && line.size() <= 2 && std::ranges::all_of(line, ::isdigit)) {
        value = line;
      }
    }
    return value;
  }

  std::optional<std::string> parse_profile(const std::string &output) {
    // powerprofilesctl get -> "balanced"; tuned-adm active -> "Current active profile: balanced"
    std::istringstream in {output};
    std::string line;
    while (std::getline(in, line)) {
      line = trim(line);
      if (line.empty()) {
        continue;
      }
      if (const auto colon = line.rfind(':'); colon != std::string::npos) {
        if (line.find("profile") == std::string::npos) {
          continue;
        }
        line = trim(line.substr(colon + 1));
      }
      if (safe_token(line)) {
        return line;
      }
    }
    return std::nullopt;
  }

  controller_t::controller_t(runner_t runner, std::unique_ptr<inhibitor_t> inhibitor, governor_reader_t governors, std::filesystem::path marker):
      runner_ {std::move(runner)},
      inhibitor_ {std::move(inhibitor)},
      governors_ {std::move(governors)},
      marker_ {std::move(marker)} {
  }

  std::string controller_t::apply_gpu(const settings_t &settings, saved_state_t &saved) {
    if (!safe_display(settings.x_display)) {
      return "failed";
    }
    const auto query = runner_({"nvidia-settings", "-c", settings.x_display, "-t", "-q", powermizer_attr()});
    if (query.exit_code == 127) {
      return "unavailable";
    }
    const auto current = parse_powermizer(query.output);
    if (query.exit_code != 0 || !current) {
      BOOST_LOG(warning) << "Power mode: couldn't read the PowerMizer mode on "sv << settings.x_display;
      return "unavailable";
    }
    if (*current == "1") {
      return "already";
    }
    const auto set = runner_({"nvidia-settings", "-c", settings.x_display, "-a", powermizer_attr("1")});
    if (set.exit_code != 0) {
      BOOST_LOG(warning) << "Power mode: nvidia-settings couldn't set PowerMizer to maximum performance (exit "sv << set.exit_code << ')';
      return "failed";
    }
    saved.gpu_mode = *current;
    saved.x_display = settings.x_display;
    BOOST_LOG(info) << "Power mode: PowerMizer set to prefer maximum performance (was "sv << *current << ')';
    return "performance";
  }

  std::string controller_t::apply_cpu(saved_state_t &saved) {
    if (const auto ppd = runner_({PPD, "get"}); ppd.exit_code == 0) {
      const auto current = parse_profile(ppd.output);
      if (!current) {
        return "unavailable";
      }
      if (*current == "performance") {
        return "already";
      }
      if (runner_({PPD, "set", "performance"}).exit_code != 0) {
        BOOST_LOG(warning) << "Power mode: power-profiles-daemon refused the performance profile"sv;
        return "failed";
      }
      saved.cpu_tool = PPD;
      saved.cpu_value = *current;
      BOOST_LOG(info) << "Power mode: power profile set to performance (was "sv << *current << ')';
      return "performance";
    }
    if (const auto tuned = runner_({TUNED, "active"}); tuned.exit_code == 0) {
      const auto current = parse_profile(tuned.output);
      if (!current) {
        return "unavailable";
      }
      if (*current == TUNED_PROFILE || *current == "throughput-performance") {
        return "already";
      }
      if (runner_({TUNED, "profile", TUNED_PROFILE}).exit_code != 0) {
        BOOST_LOG(warning) << "Power mode: tuned refused the "sv << TUNED_PROFILE << " profile"sv;
        return "failed";
      }
      saved.cpu_tool = TUNED;
      saved.cpu_value = *current;
      BOOST_LOG(info) << "Power mode: tuned profile set to "sv << TUNED_PROFILE << " (was "sv << *current << ')';
      return "performance";
    }

    // No profile daemon: the governor is root-only, so report it and leave it alone.
    const auto governors = governors_ ? governors_() : std::vector<std::string> {};
    if (governors.empty()) {
      return "unavailable";
    }
    if (std::ranges::all_of(governors, [](const auto &g) {
          return g == "performance";
        })) {
      return "already";
    }
    BOOST_LOG(info) << "Power mode: CPU governor is "sv << governors.front()
                    << "; changing it needs root (see the power_mode docs for a helper script)"sv;
    return "governor:" + governors.front();
  }

  void controller_t::write_marker(const saved_state_t &saved) const {
    std::error_code ec;
    if (saved.empty()) {
      std::filesystem::remove(marker_, ec);
      return;
    }
    std::filesystem::create_directories(marker_.parent_path(), ec);
    std::ofstream {marker_, std::ios::trunc} << saved.serialize();
  }

  status_t controller_t::apply(const settings_t &settings, const std::string &reason) {
    std::lock_guard lg {mutex_};
    if (!settings.enabled || status_.active) {
      return status_;
    }
    status_ = {};
    status_.active = true;
    saved_ = {};

    status_.gpu = settings.gpu ? apply_gpu(settings, saved_) : "off";
    // Write after each change so a crash between them still restores what was done.
    write_marker(saved_);
    status_.cpu = settings.cpu ? apply_cpu(saved_) : "off";
    write_marker(saved_);
    if (settings.inhibit && inhibitor_) {
      status_.inhibited = inhibitor_->acquire(reason);
    }
    return status_;
  }

  bool controller_t::restore_saved(const saved_state_t &saved) {
    bool ok = true;
    if (!saved.gpu_mode.empty()) {
      const auto r = runner_({"nvidia-settings", "-c", saved.x_display, "-a", powermizer_attr(saved.gpu_mode)});
      if (r.exit_code != 0) {
        BOOST_LOG(warning) << "Power mode: couldn't restore PowerMizer mode "sv << saved.gpu_mode << " (exit "sv << r.exit_code << ')';
        ok = false;
      } else {
        BOOST_LOG(info) << "Power mode: PowerMizer restored to "sv << saved.gpu_mode;
      }
    }
    if (!saved.cpu_tool.empty()) {
      const std::vector<std::string> argv = saved.cpu_tool == PPD ? std::vector<std::string> {PPD, "set", saved.cpu_value} : std::vector<std::string> {TUNED, "profile", saved.cpu_value};
      const auto r = runner_(argv);
      if (r.exit_code != 0) {
        BOOST_LOG(warning) << "Power mode: couldn't restore the "sv << saved.cpu_value << " profile (exit "sv << r.exit_code << ')';
        ok = false;
      } else {
        BOOST_LOG(info) << "Power mode: power profile restored to "sv << saved.cpu_value;
      }
    }
    return ok;
  }

  bool controller_t::restore() {
    std::lock_guard lg {mutex_};
    if (!status_.active) {
      return true;
    }
    if (inhibitor_ && !status_.inhibited.empty()) {
      inhibitor_->release();
    }
    const bool ok = restore_saved(saved_);
    std::error_code ec;
    std::filesystem::remove(marker_, ec);
    saved_ = {};
    status_ = {};
    return ok;
  }

  bool controller_t::recover() {
    std::lock_guard lg {mutex_};
    std::error_code ec;
    if (!std::filesystem::exists(marker_, ec)) {
      return false;
    }
    std::ostringstream text;
    text << std::ifstream {marker_}.rdbuf();
    const auto saved = saved_state_t::parse(text.str());
    std::filesystem::remove(marker_, ec);
    if (!saved) {
      BOOST_LOG(warning) << "Power mode: ignoring a malformed marker at "sv << marker_.string();
      return false;
    }
    BOOST_LOG(info) << "Power mode: restoring settings left by the previous run"sv;
    restore_saved(*saved);
    return true;
  }

  status_t controller_t::status() const {
    std::lock_guard lg {mutex_};
    return status_;
  }

#if defined(__linux__)
  namespace {
    /**
     * @brief logind "sleep:idle" inhibitor (file descriptor) plus org.freedesktop.ScreenSaver inhibit (cookie).
     *
     * Both are released by the system when Nova exits: logind when the fd closes, the screensaver
     * service when Nova's session-bus connection goes away.
     */
    class dbus_inhibitor_t: public inhibitor_t {
    public:
      ~dbus_inhibitor_t() override {
        dbus_inhibitor_t::release();
      }

      std::vector<std::string> acquire(const std::string &reason) override {
        std::vector<std::string> taken;
        if (take_logind(reason)) {
          taken.emplace_back("sleep");
        }
        if (take_screensaver(reason)) {
          taken.emplace_back("screensaver");
        }
        if (!taken.empty()) {
          BOOST_LOG(info) << "Power mode: inhibiting "sv << (taken.size() == 2 ? "sleep and the screensaver"sv : std::string_view {taken.front()});
        }
        return taken;
      }

      void release() override {
        if (logind_fd_ >= 0) {
          close(logind_fd_);
          logind_fd_ = -1;
        }
        if (session_ && cookie_) {
          for (const auto *path : {"/org/freedesktop/ScreenSaver", "/ScreenSaver"}) {
            GError *err = nullptr;
            GVariant *r = g_dbus_connection_call_sync(session_, "org.freedesktop.ScreenSaver", path, "org.freedesktop.ScreenSaver", "UnInhibit", g_variant_new("(u)", cookie_), nullptr, G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, &err);
            if (r) {
              g_variant_unref(r);
              break;
            }
            g_clear_error(&err);
          }
          cookie_ = 0;
        }
        if (session_) {
          g_object_unref(session_);
          session_ = nullptr;
        }
      }

    private:
      bool take_logind(const std::string &reason) {
        GError *err = nullptr;
        GDBusConnection *system = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &err);
        if (!system) {
          BOOST_LOG(debug) << "Power mode: no system bus: "sv << (err ? err->message : "");
          g_clear_error(&err);
          return false;
        }
        GUnixFDList *fds = nullptr;
        GVariant *r = g_dbus_connection_call_with_unix_fd_list_sync(
          system, "org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager", "Inhibit",
          g_variant_new("(ssss)", "sleep:idle", "Nova", reason.c_str(), "block"), G_VARIANT_TYPE("(h)"),
          G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, &fds, nullptr, &err
        );
        g_object_unref(system);
        if (!r) {
          BOOST_LOG(warning) << "Power mode: logind refused the sleep inhibitor: "sv << (err ? err->message : "");
          g_clear_error(&err);
          return false;
        }
        gint32 index = -1;
        g_variant_get(r, "(h)", &index);
        g_variant_unref(r);
        if (fds) {
          logind_fd_ = g_unix_fd_list_get(fds, index, nullptr);
          g_object_unref(fds);
        }
        return logind_fd_ >= 0;
      }

      bool take_screensaver(const std::string &reason) {
        GError *err = nullptr;
        session_ = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &err);
        if (!session_) {
          BOOST_LOG(debug) << "Power mode: no session bus: "sv << (err ? err->message : "");
          g_clear_error(&err);
          return false;
        }
        // GNOME/Cinnamon/KDE/Xfce all serve /org/freedesktop/ScreenSaver; some older proxies use /ScreenSaver.
        for (const auto *path : {"/org/freedesktop/ScreenSaver", "/ScreenSaver"}) {
          GVariant *r = g_dbus_connection_call_sync(session_, "org.freedesktop.ScreenSaver", path, "org.freedesktop.ScreenSaver", "Inhibit", g_variant_new("(ss)", "Nova", reason.c_str()), G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, &err);
          if (r) {
            g_variant_get(r, "(u)", &cookie_);
            g_variant_unref(r);
            return true;
          }
          g_clear_error(&err);
        }
        BOOST_LOG(info) << "Power mode: no org.freedesktop.ScreenSaver service; the screensaver is not inhibited"sv;
        return false;
      }

      int logind_fd_ = -1;
      GDBusConnection *session_ = nullptr;
      guint32 cookie_ = 0;
    };

    std::vector<std::string> read_governors() {
      std::vector<std::string> out;
      std::error_code ec;
      for (const auto &entry : std::filesystem::directory_iterator("/sys/devices/system/cpu/cpufreq", ec)) {
        if (entry.path().filename().string().starts_with("policy")) {
          std::string g;
          std::getline(std::ifstream {entry.path() / "scaling_governor"}, g);
          if (!g.empty()) {
            out.push_back(trim(g));
          }
        }
      }
      return out;
    }

    std::unique_ptr<inhibitor_t> make_inhibitor() {
      return std::make_unique<dbus_inhibitor_t>();
    }
  }  // namespace
#else
  namespace {
    std::vector<std::string> read_governors() {
      return {};
    }

    std::unique_ptr<inhibitor_t> make_inhibitor() {
      return nullptr;
    }
  }  // namespace
#endif

  controller_t &instance() {
    static controller_t controller {
      [](const std::vector<std::string> &argv) {
        const auto r = run_program::run(argv, 10s);
        return run_result_t {r.exit_code, r.output};
      },
      make_inhibitor(),
      read_governors,
      platf::appdata() / "power_mode.active"
    };
    return controller;
  }

  namespace {
    /**
     * @brief Serializes apply/restore on one background thread so a stream start never waits on
     *        nvidia-settings, and a quick connect/disconnect always ends in the right state.
     */
    class worker_t {
    public:
      void want(bool active, std::string client) {
        {
          std::lock_guard lg {mutex_};
          desired_ = active;
          client_ = std::move(client);
          if (!thread_.joinable()) {
            thread_ = std::thread {&worker_t::run, this};
            thread_.detach();
          }
        }
        cv_.notify_one();
      }

    private:
      void run() {
        platf::set_thread_name("nova::power");
        bool applied = false;
        std::unique_lock ul {mutex_};
        for (;;) {
          cv_.wait(ul, [&] {
            return desired_ != applied;
          });
          const bool target = desired_;
          const auto client = client_;
          ul.unlock();
          if (target) {
            settings_t s;
            // want(true) is only asked for when the setting or the running game's override wants it.
            s.enabled = true;
            s.gpu = config::nova.power_mode_gpu;
            s.cpu = config::nova.power_mode_cpu;
            s.inhibit = config::nova.power_mode_inhibit;
            instance().apply(s, client.empty() ? "Streaming"s : "Streaming to " + client);
          } else {
            instance().restore();
          }
          applied = target;
          ul.lock();
        }
      }

      std::mutex mutex_;
      std::condition_variable cv_;
      std::thread thread_;
      bool desired_ = false;
      std::string client_;
    };

    worker_t &worker() {
      static auto *w = new worker_t;  // leaked on purpose: the detached thread outlives static destruction
      return *w;
    }
  }  // namespace

  namespace {
    /**
     * @brief Stream and per-game state the hooks decide from.
     */
    struct hook_state_t {
      std::mutex mutex;
      bool streaming = false;  ///< Between the first session start and the last session end.
      std::optional<bool> app_override;  ///< The running game's override.
      std::string client;  ///< Device of the first session, for the inhibit reason.
      bool asked = false;  ///< Whether want(true) was the last request.
    };

    hook_state_t &hook_state() {
      static auto *state = new hook_state_t;  // leaked like the worker, which may outlive static destruction
      return *state;
    }

    /**
     * @brief Ask the worker for the state the stream and the running game call for.
     *
     * @param state Hook state (locked by the caller).
     */
    void reconcile(hook_state_t &state) {
      const bool target = state.streaming && wanted(config::nova.power_mode, state.app_override);
      // Nothing to do when the last request already matches, unless the mode is still on from
      // an earlier stream (for example the setting was turned off mid-stream).
      if (target == state.asked && (target || !instance().status().active)) {
        return;
      }
      state.asked = target;
      worker().want(target, state.client);
    }
  }  // namespace

  void first_session_started(const std::string &client_name) {
    auto &state = hook_state();
    std::lock_guard lg {state.mutex};
    state.streaming = true;
    state.client = client_name;
    reconcile(state);
  }

  void last_session_ended() {
    auto &state = hook_state();
    std::lock_guard lg {state.mutex};
    state.streaming = false;
    reconcile(state);
  }

  void set_app_override(std::optional<bool> app_override) {
    auto &state = hook_state();
    std::lock_guard lg {state.mutex};
    if (state.app_override == app_override) {
      return;
    }
    state.app_override = app_override;
    if (app_override) {
      BOOST_LOG(info) << "Power mode: this game asks for "sv << (*app_override ? "performance"sv : "no change"sv) << " while it streams"sv;
    }
    reconcile(state);
  }

  void recover_at_startup() {
    instance().recover();
  }
}  // namespace power_mode
