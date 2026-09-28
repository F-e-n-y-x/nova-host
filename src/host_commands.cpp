/**
 * @file src/host_commands.cpp
 * @brief Definitions for admin-defined host commands.
 */
// class header include
#include "host_commands.h"

// standard includes
#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstring>
#include <format>
#include <mutex>
#include <set>
#include <thread>

#ifndef _WIN32
  #include <fcntl.h>
  #include <poll.h>
  #include <signal.h>
  #include <spawn.h>
  #include <sys/wait.h>
  #include <unistd.h>

extern char **environ;  // NOLINT(readability-redundant-declaration)
#endif

// local includes
#include "config.h"
#include "logging.h"

using namespace std::literals;

namespace host_commands {
  namespace {
    std::string trim(std::string_view text) {
      while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
      }
      while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
      }
      return std::string {text};
    }

    std::string slug(std::string_view name) {
      std::string out;
      for (const char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
          out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        } else if (!out.empty() && out.back() != '-') {
          out.push_back('-');
        }
        if (out.size() >= 48) {
          break;
        }
      }
      while (!out.empty() && out.back() == '-') {
        out.pop_back();
      }
      return out.empty() ? "cmd"s : out;
    }

    std::int64_t unix_now() {
      return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    struct registry_t {
      std::mutex mutex;
      std::condition_variable idle;
      std::map<std::string, run_record_t, std::less<>> runs;
      int active = 0;
    };

    registry_t &registry() {
      static registry_t instance;
      return instance;
    }
  }  // namespace

  bool valid_id(const std::string_view id) {
    return !id.empty() && id.size() <= 64 && std::ranges::all_of(id, [](const char c) {
             return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
           });
  }

  std::optional<std::vector<std::string>> split_command_line(const std::string_view line) {
    std::vector<std::string> args;
    std::string current;
    bool in_arg = false;
    char quote = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
      const char c = line[i];
      if (quote == '\'') {
        if (c == '\'') {
          quote = 0;
        } else {
          current.push_back(c);
        }
        continue;
      }
      if (quote == '"') {
        if (c == '"') {
          quote = 0;
        } else if (c == '\\' && i + 1 < line.size() && (line[i + 1] == '"' || line[i + 1] == '\\')) {
          current.push_back(line[++i]);
        } else {
          current.push_back(c);
        }
        continue;
      }
      if (c == '\'' || c == '"') {
        quote = c;
        in_arg = true;
      } else if (c == '\\' && i + 1 < line.size()) {
        current.push_back(line[++i]);
        in_arg = true;
      } else if (std::isspace(static_cast<unsigned char>(c))) {
        if (in_arg) {
          args.push_back(std::move(current));
          current.clear();
          in_arg = false;
        }
      } else {
        current.push_back(c);
        in_arg = true;
      }
    }
    if (quote != 0) {
      return std::nullopt;
    }
    if (in_arg) {
      args.push_back(std::move(current));
    }
    if (args.empty()) {
      return std::nullopt;
    }
    return args;
  }

  std::vector<command_t> parse_list(const nlohmann::json &list, std::vector<std::string> *errors) {
    std::vector<command_t> out;
    const auto fail = [errors](std::string message) {
      if (errors) {
        errors->push_back(std::move(message));
      }
    };
    if (list.is_null()) {
      return out;
    }
    if (!list.is_array()) {
      fail("host commands must be a list");
      return out;
    }

    std::set<std::string, std::less<>> ids;
    for (std::size_t i = 0; i < list.size(); ++i) {
      const auto &item = list[i];
      const auto where = std::format("command {}", i + 1);
      if (!item.is_object()) {
        fail(where + " is not an object");
        continue;
      }
      if (out.size() >= max_commands) {
        fail(std::format("at most {} commands are allowed", max_commands));
        break;
      }
      command_t command;
      command.name = trim(item.value("name", ""s));
      command.cmd = trim(item.contains("cmd") && item["cmd"].is_string() ? item["cmd"].get<std::string>() : ""s);
      if (command.name.empty() || command.name.size() > 80) {
        fail(where + ": the name must be 1-80 characters");
        continue;
      }
      if (command.cmd.empty() || !split_command_line(command.cmd)) {
        fail(std::format("{} ({}): the command is empty or has an unclosed quote", where, command.name));
        continue;
      }
      if (command.cmd.size() > 4096) {
        fail(std::format("{} ({}): the command is longer than 4096 characters", where, command.name));
        continue;
      }

      command.id = item.contains("id") && item["id"].is_string() ? trim(item["id"].get<std::string>()) : ""s;
      if (command.id.empty()) {
        // Derive a stable id from the name; suffix it when two names collide.
        auto base = slug(command.name);
        command.id = base;
        for (int n = 2; ids.contains(command.id); ++n) {
          command.id = std::format("{}-{}", base, n);
        }
      }
      if (!valid_id(command.id)) {
        fail(std::format("{} ({}): the id may only use letters, digits, '-' and '_' (at most 64)", where, command.name));
        continue;
      }
      if (ids.contains(command.id)) {
        fail(std::format("{} ({}): the id '{}' is used twice", where, command.name, command.id));
        continue;
      }

      if (const auto icon = item.find("icon"); icon != item.end() && icon->is_string()) {
        const auto value = icon->get<std::string>();
        if (std::ranges::find(icon_names, value) != std::end(icon_names)) {
          command.icon = value;
        }
      }
      if (const auto confirm = item.find("confirm"); confirm != item.end() && confirm->is_boolean()) {
        command.confirm = confirm->get<bool>();
      }
      if (const auto timeout = item.find("timeout"); timeout != item.end() && timeout->is_number_integer()) {
        command.timeout = std::clamp(std::chrono::seconds {timeout->get<long long>()}, 1s, max_timeout);
      }
      ids.insert(command.id);
      out.push_back(std::move(command));
    }
    return out;
  }

  std::vector<command_t> parse_text(const std::string_view text, std::vector<std::string> *errors) {
    if (trim(text).empty()) {
      return {};
    }
    const auto list = nlohmann::json::parse(text, nullptr, false);
    if (list.is_discarded()) {
      if (errors) {
        errors->emplace_back("host commands are not valid JSON");
      }
      return {};
    }
    return parse_list(list, errors);
  }

  nlohmann::json to_json(const command_t &command) {
    return {
      {"id", command.id},
      {"name", command.name},
      {"icon", command.icon},
      {"cmd", command.cmd},
      {"confirm", command.confirm},
      {"timeout", command.timeout.count()},
    };
  }

  nlohmann::json normalize(const nlohmann::json &list, std::vector<std::string> *errors) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto &command : parse_list(list, errors)) {
      out.push_back(to_json(command));
    }
    return out;
  }

  std::optional<std::pair<command_t, bool>> resolve(const std::string_view id, const std::vector<command_t> &app, const std::vector<command_t> &global_list) {
    for (const auto &command : app) {
      if (command.id == id) {
        return std::pair {command, true};
      }
    }
    for (const auto &command : global_list) {
      if (command.id == id) {
        return std::pair {command, false};
      }
    }
    return std::nullopt;
  }

  std::vector<command_t> global() {
    return parse_text(config::sunshine.host_commands);
  }

  std::vector<command_t> for_app(const nlohmann::json &app) {
    if (!app.is_object() || !app.contains("menu-cmd")) {
      return {};
    }
    return parse_list(app["menu-cmd"]);
  }

  std::string super_cmds_json(const std::vector<command_t> &app, const std::vector<command_t> &global_list) {
    nlohmann::json list = nlohmann::json::array();
    std::set<std::string, std::less<>> seen;
    for (const auto *source : {&app, &global_list}) {
      for (const auto &command : *source) {
        // An app command shadows a global one with the same id (resolve() picks the app's).
        if (seen.insert(command.id).second) {
          list.push_back({{"id", command.id}, {"name", command.name}});
        }
      }
    }
    return list.dump();
  }

  std::vector<std::string> build_env(const std::map<std::string, std::string> &extra) {
    std::map<std::string, std::string> merged;
#ifndef _WIN32
    for (char **entry = environ; entry && *entry; ++entry) {
      std::string_view kv {*entry};
      if (const auto eq = kv.find('='); eq != std::string_view::npos && eq > 0) {
        merged.emplace(std::string {kv.substr(0, eq)}, std::string {kv.substr(eq + 1)});
      }
    }
#endif
    for (const auto &[key, value] : extra) {
      merged[key] = value;
    }
    std::vector<std::string> env;
    env.reserve(merged.size());
    for (const auto &[key, value] : merged) {
      env.push_back(key + "=" + value);
    }
    return env;
  }

  run_result_t run_sync(const command_t &command, const std::vector<std::string> &env) {
    run_result_t result;
    const auto args = split_command_line(command.cmd);
    if (!args) {
      result.error = "the command line is empty or has an unclosed quote";
      return result;
    }
#ifdef _WIN32
    (void) env;
    result.error = "host commands are only supported on Linux";
    return result;
#else
    std::vector<char *> argv;
    for (const auto &arg : *args) {
      argv.push_back(const_cast<char *>(arg.c_str()));
    }
    argv.push_back(nullptr);
    std::vector<char *> envp;
    for (const auto &kv : env) {
      envp.push_back(const_cast<char *>(kv.c_str()));
    }
    envp.push_back(nullptr);

    int pipe_fds[2];
    if (::pipe2(pipe_fds, O_CLOEXEC) != 0) {
      result.error = std::format("pipe failed: {}", std::strerror(errno));
      return result;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDERR_FILENO);

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    // Own process group, so the timeout can kill everything the command started.
    posix_spawnattr_setpgroup(&attr, 0);
    sigset_t default_signals;
    sigfillset(&default_signals);
    posix_spawnattr_setsigdefault(&attr, &default_signals);
    sigset_t no_mask;
    sigemptyset(&no_mask);
    posix_spawnattr_setsigmask(&attr, &no_mask);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);

    const auto start = std::chrono::steady_clock::now();
    pid_t pid = -1;
    const int spawn_error = ::posix_spawnp(&pid, argv[0], &actions, &attr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    ::close(pipe_fds[1]);

    if (spawn_error != 0) {
      ::close(pipe_fds[0]);
      result.error = std::format("couldn't start {}: {}", (*args)[0], std::strerror(spawn_error));
      return result;
    }
    result.started = true;

    const auto deadline = start + command.timeout;
    std::optional<std::chrono::steady_clock::time_point> kill_at;  // SIGKILL after the SIGTERM grace period
    std::optional<std::chrono::steady_clock::time_point> exited_at;
    bool pipe_open = true;
    int status = 0;
    char buffer[4096];

    while (true) {
      const auto now = std::chrono::steady_clock::now();
      if (!exited_at) {
        if (const auto done = ::waitpid(pid, &status, WNOHANG); done == pid) {
          exited_at = now;
        }
      }
      // A detached grandchild may keep the pipe open; stop reading shortly after the child exits.
      if (exited_at && (!pipe_open || now - *exited_at > 1s)) {
        break;
      }
      if (!exited_at && !result.timed_out && now >= deadline) {
        result.timed_out = true;
        ::kill(-pid, SIGTERM);
        kill_at = now + 2s;
      }
      if (kill_at && !exited_at && now >= *kill_at) {
        ::kill(-pid, SIGKILL);
        kill_at.reset();
      }

      if (pipe_open) {
        pollfd pfd {pipe_fds[0], POLLIN, 0};
        if (::poll(&pfd, 1, 100) > 0) {
          const auto n = ::read(pipe_fds[0], buffer, sizeof(buffer));
          if (n > 0) {
            const auto room = max_output_bytes - std::min(max_output_bytes, result.output.size());
            result.output.append(buffer, std::min<std::size_t>(room, static_cast<std::size_t>(n)));
            result.truncated = result.truncated || static_cast<std::size_t>(n) > room;
          } else if (n == 0 || (errno != EINTR && errno != EAGAIN)) {
            pipe_open = false;
          }
        }
      } else {
        std::this_thread::sleep_for(50ms);
      }
    }
    ::close(pipe_fds[0]);
    if (result.timed_out) {
      // Leftovers of the group (background children) go too.
      ::kill(-pid, SIGKILL);
    }

    if (WIFEXITED(status)) {
      result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      result.exit_code = -WTERMSIG(status);
    }
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    return result;
#endif
  }

  start_e start_async(const command_t &command, std::vector<std::string> env, std::string device) {
    auto &reg = registry();
    {
      std::lock_guard lock {reg.mutex};
      if (const auto it = reg.runs.find(command.id); it != reg.runs.end() && it->second.running) {
        return start_e::already_running;
      }
      if (reg.active >= max_concurrent_runs) {
        return start_e::busy;
      }
      ++reg.active;
      reg.runs[command.id] = run_record_t {
        .id = command.id,
        .name = command.name,
        .device = device,
        .started_at = unix_now(),
        .running = true,
      };
    }
    BOOST_LOG(info) << "Host command ["sv << command.id << "] "sv << command.name << " started by "sv << device;

    std::thread([command, env = std::move(env), device = std::move(device)]() {
      auto result = run_sync(command, env);
      if (!result.started) {
        BOOST_LOG(warning) << "Host command ["sv << command.id << "] couldn't start: "sv << result.error;
      } else if (result.timed_out) {
        BOOST_LOG(warning) << "Host command ["sv << command.id << "] timed out after "sv << command.timeout.count() << " s and was killed"sv;
      } else {
        BOOST_LOG(info) << "Host command ["sv << command.id << "] exited with "sv << result.exit_code << " after "sv << result.duration.count() << " ms"sv;
      }
      auto &reg = registry();
      std::lock_guard lock {reg.mutex};
      auto &record = reg.runs[command.id];
      record.running = false;
      record.result = std::move(result);
      --reg.active;
      reg.idle.notify_all();
    }).detach();
    return start_e::started;
  }

  std::optional<run_record_t> last_run(const std::string_view id) {
    auto &reg = registry();
    std::lock_guard lock {reg.mutex};
    if (const auto it = reg.runs.find(id); it != reg.runs.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  std::vector<run_record_t> all_runs() {
    std::vector<run_record_t> out;
    {
      auto &reg = registry();
      std::lock_guard lock {reg.mutex};
      for (const auto &[_, record] : reg.runs) {
        out.push_back(record);
      }
    }
    std::ranges::sort(out, [](const run_record_t &a, const run_record_t &b) {
      return a.started_at > b.started_at;
    });
    return out;
  }

  nlohmann::json run_to_json(const run_record_t &record) {
    return {
      {"id", record.id},
      {"name", record.name},
      {"device", record.device},
      {"started_at", record.started_at},
      {"running", record.running},
      {"ok", !record.running && record.result.ok()},
      {"exit_code", record.running ? nlohmann::json(nullptr) : nlohmann::json(record.result.exit_code)},
      {"timed_out", record.result.timed_out},
      {"duration_ms", record.result.duration.count()},
      {"output", record.result.output},
      {"truncated", record.result.truncated},
      {"error", record.result.error},
    };
  }

  bool wait_idle(const std::chrono::milliseconds limit) {
    auto &reg = registry();
    std::unique_lock lock {reg.mutex};
    return reg.idle.wait_for(lock, limit, [&reg]() {
      return reg.active == 0;
    });
  }
}  // namespace host_commands
