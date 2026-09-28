/**
 * @file src/host_commands.h
 * @brief Admin-defined host commands that paired devices may run by id (Foundation `/supercmd`).
 */
#pragma once

// standard includes
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// lib includes
#include <nlohmann/json.hpp>

namespace host_commands {
  constexpr std::size_t max_output_bytes = 16 * 1024;  ///< Output kept per run (the rest is dropped).
  constexpr std::chrono::seconds default_timeout {30};  ///< Timeout when a command sets none.
  constexpr std::chrono::seconds max_timeout {600};  ///< Longest timeout a command may set.
  constexpr std::size_t max_commands = 64;  ///< Commands per list (global or one app).
  constexpr int max_concurrent_runs = 4;  ///< Runs allowed at the same time, over all commands.

  /**
   * @brief Icon names the web UI and Nebula know; anything else is stored as empty (default icon).
   */
  inline constexpr std::string_view icon_names[] {"terminal", "refresh", "power", "lock", "volume", "mic-off", "monitor", "gamepad", "stop", "play", "folder", "settings"};

  /**
   * @brief One command definition.
   */
  struct command_t {
    std::string id;  ///< `^[A-Za-z0-9_-]{1,64}$`; what the client sends.
    std::string name;  ///< Label shown in menus.
    std::string icon;  ///< One of icon_names, or empty.
    std::string cmd;  ///< Command line, split into argv without a shell.
    bool confirm = false;  ///< Ask the user before running.
    std::chrono::seconds timeout = default_timeout;  ///< Kill the command after this long.
  };

  /**
   * @brief Whether @p id is a valid command id.
   * @param id Candidate id.
   * @return True for 1-64 characters of `[A-Za-z0-9_-]`.
   */
  bool valid_id(std::string_view id);

  /**
   * @brief Parse and validate a command list (the `host_commands` setting, or an app's `menu-cmd`).
   *
   * Entries need a non-empty `name` and `cmd` (Foundation's `menu-cmd` uses the same keys). A missing
   * id is derived from the name; duplicate or invalid ids, unbalanced quotes and over-long lists are
   * errors.
   *
   * @param list JSON array; `null` or a missing value means no commands.
   * @param errors When non-null, receives one message per rejected entry.
   * @return The valid commands, in order.
   */
  std::vector<command_t> parse_list(const nlohmann::json &list, std::vector<std::string> *errors = nullptr);

  /**
   * @brief Parse the JSON text of the `host_commands` setting.
   * @param text JSON array text; empty means none.
   * @param errors Receives parse and validation errors when non-null.
   * @return The valid commands.
   */
  std::vector<command_t> parse_text(std::string_view text, std::vector<std::string> *errors = nullptr);

  /**
   * @brief Normalize a list for saving: validated entries only, ids filled in, known keys only.
   * @param list JSON array from the web UI.
   * @param errors Receives validation errors when non-null.
   * @return JSON array to store.
   */
  nlohmann::json normalize(const nlohmann::json &list, std::vector<std::string> *errors = nullptr);

  /**
   * @brief Serialize a command for storage and for the web UI.
   * @param command Command.
   * @return `{"id","name","icon","cmd","confirm","timeout"}`.
   */
  nlohmann::json to_json(const command_t &command);

  /**
   * @brief Split a command line into argv, honouring '…', "…" and backslash escapes.
   *
   * No variables, globs, pipes or redirections are interpreted; those need an explicit `sh -c '…'`.
   *
   * @param line Command line.
   * @return The arguments, or nullopt for an empty line or unbalanced quotes.
   */
  std::optional<std::vector<std::string>> split_command_line(std::string_view line);

  /**
   * @brief Outcome of one run.
   */
  struct run_result_t {
    bool started = false;  ///< Whether the process could be spawned.
    int exit_code = -1;  ///< Exit status; the negative signal number when killed by a signal.
    bool timed_out = false;  ///< Whether the timeout killed it.
    std::string output;  ///< Combined stdout and stderr, at most max_output_bytes.
    bool truncated = false;  ///< Whether output was cut.
    std::chrono::milliseconds duration {0};  ///< Wall time.
    std::string error;  ///< Spawn error, when not started.

    /**
     * @brief Whether the run succeeded.
     * @return True when it started, finished in time and exited 0.
     */
    [[nodiscard]] bool ok() const {
      return started && !timed_out && exit_code == 0;
    }
  };

  /**
   * @brief Run a command and wait for it: no shell, own process group, stdin from /dev/null,
   *        stdout+stderr captured, the whole group killed at the timeout.
   *
   * @param command Command to run.
   * @param env Environment as `KEY=VALUE` strings (the child sees only these).
   * @return The result.
   */
  run_result_t run_sync(const command_t &command, const std::vector<std::string> &env);

  /**
   * @brief Record of the last run of a command, kept in memory.
   */
  struct run_record_t {
    std::string id;  ///< Command id.
    std::string name;  ///< Command name at the time.
    std::string device;  ///< Device (or "web UI") that started it.
    std::int64_t started_at = 0;  ///< Unix time.
    bool running = false;  ///< Still running.
    run_result_t result;  ///< Result, once finished.
  };

  /**
   * @brief Result of asking to start a command.
   */
  enum class start_e {
    started,  ///< Running in the background.
    already_running,  ///< This id is running; not started again.
    busy,  ///< max_concurrent_runs reached.
  };

  /**
   * @brief Start a command in the background; the outcome lands in last_run().
   * @param command Command.
   * @param env Environment for the child.
   * @param device Who asked, for the log.
   * @return Whether it started.
   */
  start_e start_async(const command_t &command, std::vector<std::string> env, std::string device);

  /**
   * @brief The last (or current) run of a command id.
   * @param id Command id.
   * @return The record, or nullopt when it never ran since Nova started.
   */
  std::optional<run_record_t> last_run(std::string_view id);

  /**
   * @brief Every run record since Nova started, newest first.
   * @return Records (one per command id).
   */
  std::vector<run_record_t> all_runs();

  /**
   * @brief JSON form of a run record for the web UI (includes the captured output).
   * @param record Record.
   * @return `{"id","name","device","started_at","running","ok","exit_code","timed_out","duration_ms","output","truncated","error"}`.
   */
  nlohmann::json run_to_json(const run_record_t &record);

  /**
   * @brief Block until no command runs (tests only).
   * @param limit Longest wait.
   * @return True when idle.
   */
  bool wait_idle(std::chrono::milliseconds limit);

  /**
   * @brief Find a command by id: the running app's list first, then the global list.
   * @param id Id the client sent.
   * @param app App commands (empty when nothing runs).
   * @param global Global commands.
   * @return The command and whether it came from the app list.
   */
  std::optional<std::pair<command_t, bool>> resolve(std::string_view id, const std::vector<command_t> &app, const std::vector<command_t> &global);

  /**
   * @brief The global commands from the `host_commands` setting.
   * @return Parsed list (invalid entries dropped).
   */
  std::vector<command_t> global();

  /**
   * @brief The commands stored on one apps.json entry (`menu-cmd`).
   * @param app App object.
   * @return Parsed list.
   */
  std::vector<command_t> for_app(const nlohmann::json &app);

  /**
   * @brief Foundation `SuperCmds` text for /applist: `[{"id","name"}]`, app commands then global ones.
   * @param app App commands.
   * @param global Global commands.
   * @return Compact JSON array text.
   */
  std::string super_cmds_json(const std::vector<command_t> &app, const std::vector<command_t> &global);

  /**
   * @brief The current process environment plus Nova's variables, as `KEY=VALUE` strings.
   * @param extra Variables to set or override (e.g. the stream's SUNSHINE_* values).
   * @return Environment for run_sync()/start_async().
   */
  std::vector<std::string> build_env(const std::map<std::string, std::string> &extra);
}  // namespace host_commands
