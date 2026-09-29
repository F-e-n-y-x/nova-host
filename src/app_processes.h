/**
 * @file src/app_processes.h
 * @brief Nova: which processes belong to the running app, and whether its game is still among them.
 *
 * Nova starts an app in its own process group, but a Proton game doesn't stay there: umu-run
 * starts a pressure-vessel container (a new session), and Wine puts every Windows process in a
 * session of its own. What is left in the group is umu-run, which lives as long as anything in the
 * container does. When the game exits and a Wine or runtime helper lingers (wineserver, services.exe,
 * explorer.exe, a hung winedevice.exe...), umu-run keeps waiting and the app would count as
 * running forever.
 *
 * So the app's processes are its process group plus every descendant of the process Nova started,
 * and a process that is only Wine/Proton/runtime plumbing is a "helper". Once the app had a game
 * process and only helpers remain for a few seconds, the game is over (linger_t).
 */
#pragma once

// standard includes
#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace app_processes {
  /**
   * @brief One process, as /proc shows it.
   */
  struct process_t {
    int pid = 0;  ///< Process id.
    int ppid = 0;  ///< Parent process id.
    int pgid = 0;  ///< Process group id.
    char state = 'S';  ///< State letter from /proc/<pid>/stat (Z = zombie).
    std::string comm;  ///< Kernel name (at most 15 characters; Wine sets it to the .exe name).
    std::vector<std::string> argv;  ///< Command line.
  };

  /**
   * @brief Every process on the host (Linux: /proc; empty elsewhere).
   *
   * @return The processes, in no particular order.
   */
  std::vector<process_t> snapshot();

  /**
   * @brief The name a process goes by: the basename of argv[0] (Unix or Windows path), else comm.
   *
   * @param process The process.
   * @return Lower-case name, e.g. "services.exe", "umu-run", "python3".
   */
  std::string name_of(const process_t &process);

  /**
   * @brief Whether a process is Wine, Proton or container plumbing rather than a game or app.
   *
   * wineserver and the Wine system processes (services.exe, winedevice.exe, explorer.exe, ...),
   * umu-run and the Proton script, pressure-vessel, bwrap, Steam's reaper, and the bus daemons a
   * container starts. An interpreter counts as a helper only when it runs one of those scripts.
   *
   * @param process The process.
   * @return True for a helper.
   */
  bool is_helper(const process_t &process);

  /**
   * @brief The app's processes right now.
   */
  struct members_t {
    std::vector<process_t> processes;  ///< Live (non-zombie) processes of the app.
    int games = 0;  ///< How many of them are not helpers.

    /**
     * @brief A short list of the helpers' names, for the log.
     * @return E.g. "umu-run, wineserver, services.exe".
     */
    std::string helper_names() const;
  };

  /**
   * @brief Pick the app's processes out of a snapshot.
   *
   * Members are the processes in the app's process group and every descendant of the process
   * Nova started (or of any group member), skipping zombies.
   *
   * @param all Snapshot of the host.
   * @param root_pid The process Nova started (0 when unknown or gone).
   * @param pgid The app's process group (0 when unknown).
   * @return The members.
   */
  members_t members(const std::vector<process_t> &all, int root_pid, int pgid);

  /**
   * @brief Decides when a game is over although helpers still run (no threads or I/O; unit-tested).
   */
  class linger_t {
  public:
    using clock = std::chrono::steady_clock;  ///< Clock for the grace period.

    /**
     * @brief How long only helpers must remain before the game counts as ended.
     */
    static constexpr std::chrono::seconds grace {3};

    /**
     * @brief Look at the app's processes.
     *
     * Only a game that was seen can end this way, so the minutes umu-run may spend preparing a
     * prefix before the game starts never count.
     *
     * @param seen The app's processes now.
     * @param now Current time.
     * @return True once the game has ended: only helpers have remained for `grace`.
     */
    bool update(const members_t &seen, clock::time_point now);

    /**
     * @brief Forget everything (a new app starts, or the app ended).
     */
    void reset();

    /**
     * @brief Whether a game process was seen since the app started.
     * @return True after the first game process.
     */
    bool game_seen() const;

  private:
    bool game_seen_ = false;
    std::optional<clock::time_point> helpers_only_since_;
  };
}  // namespace app_processes
