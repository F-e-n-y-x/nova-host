/**
 * @file src/process.h
 * @brief Declarations for the startup and shutdown of the apps started by a streaming Session.
 */
#pragma once

#ifndef __kernel_entry
  /**
   * @def __kernel_entry
   * @brief Macro for kernel entry.
   */
  #define __kernel_entry
#endif

// standard includes
#include <atomic>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>

// local includes
#include "app_processes.h"
#include "config.h"
#include "nova_compat.h"
#include "platform/common.h"
#include "rtsp.h"
#include "src/boost_process_compat.h"
#include "utility.h"

/**
 * @def DEFAULT_APP_IMAGE_PATH
 * @brief Macro for DEFAULT APP IMAGE PATH.
 */
#define DEFAULT_APP_IMAGE_PATH SUNSHINE_ASSETS_DIR "/box.png"

namespace proc {
  /**
   * @brief Boost.Process pipe stream used for child-process I/O.
   */
  using file_t = util::safe_ptr_v2<FILE, int, fclose>;

  /**
   * @brief Parsed command arguments used when launching a child process.
   */
  typedef config::prep_cmd_t cmd_t;

  /**
   * pre_cmds -- guaranteed to be executed unless any of the commands fail.
   * detached -- commands detached from Sunshine
   * cmd -- Runs indefinitely until:
   *    No session is running and a different set of commands it to be executed
   *    Command exits
   * working_dir -- the process working directory. This is required for some games to run properly.
   * cmd_output --
   *    empty    -- The output of the commands are appended to the output of sunshine
   *    "null"   -- The output of the commands are discarded
   *    filename -- The output of the commands are appended to filename
   */
  struct ctx_t {
    std::vector<cmd_t> prep_cmds;  ///< Prep cmds.

    /**
     * Some applications, such as Steam, either exit quickly, or keep running indefinitely.
     *
     * Apps that launch normal child processes and terminate will be handled by the process
     * grouping logic (wait_all). However, apps that launch child processes indirectly or
     * into another process group (such as UWP apps) can only be handled by the auto-detach
     * heuristic which catches processes that exit 0 very quickly, but we won't have proper
     * process tracking for those.
     *
     * For cases where users just want to kick off a background process and never manage the
     * lifetime of that process, they can use detached commands for that.
     */
    std::vector<std::string> detached;

    std::string name;  ///< Human-readable name for this item.
    std::string cmd;  ///< Command line used to launch the application.
    std::string working_dir;  ///< Working dir.
    std::string output;  ///< Captured output from the launched process.
    std::string image_path;  ///< Image path.
    std::string id;  ///< Stable identifier for the configured application.
    bool elevated;  ///< Whether the process should be launched elevated.
    bool auto_detach;  ///< Whether the process should detach automatically.
    bool wait_all;  ///< Whether Sunshine waits for all child processes.
    std::chrono::seconds exit_timeout;  ///< Exit timeout.
    std::string nova_exe;  ///< Game executable checked before launch ("nova-exe"), or empty.
    std::uint32_t steam_appid = 0;  ///< Matched Steam app id ("nova-steam-appid"), or 0.
    nova_compat::options_t compat;  ///< Windows compatibility options ("nova-compat").
    bool vd_share_profile = false;  ///< Nova: on a virtual display, keep the desktop's browser profile and D-Bus ("nova-vd-share-profile").
  };

  /**
   * @brief Tracks launched child processes and terminates them during shutdown.
   */
  /**
   * @brief An atomic that proc_t can still be moved with (proc::parse() returns one by value).
   *
   * @tparam T Value type.
   */
  template<class T>
  class movable_atomic_t {
  public:
    movable_atomic_t(T value = {}):
        value_ {value} {
    }

    movable_atomic_t(movable_atomic_t &&other) noexcept:
        value_ {other.value_.load()} {
    }

    movable_atomic_t &operator=(movable_atomic_t &&other) noexcept {
      value_ = other.value_.load();
      return *this;
    }

    movable_atomic_t &operator=(T value) {
      value_ = value;
      return *this;
    }

    operator T() const {
      return value_.load();
    }

  private:
    std::atomic<T> value_;
  };

  class proc_t {
  public:
    KITTY_DEFAULT_CONSTR_MOVE_THROW(proc_t)

    /**
     * @brief Construct a process manager.
     *
     * @param env Environment used when launching processes.
     * @param apps Application launch contexts.
     */
    proc_t(
      boost::process::v1::environment &&env,
      std::vector<ctx_t> &&apps
    ):
        _app_id(0),
        _env(std::move(env)),
        _apps(std::move(apps)) {
    }

    /**
     * @brief Launch the configured application process.
     *
     * @param app_id App ID.
     * @param launch_session Launch session.
     * @return Process exit code or launch error status.
     */
    int execute(int app_id, std::shared_ptr<rtsp_stream::launch_session_t> launch_session);

    /**
     * @brief User-facing reason the last execute() call failed, if it knows one.
     *
     * @return Message such as a missing game file, or empty for a generic failure.
     */
    const std::string &last_error() const;

    /**
     * @return `_app_id` if a process is running, otherwise returns `0`
     */
    int running();

    ~proc_t();

    /**
     * @brief Return the configured applications.
     *
     * @return Immutable application list owned by the process manager.
     */
    const std::vector<ctx_t> &get_apps() const;
    /**
     * @brief Return the configured applications.
     *
     * @return Mutable application list owned by the process manager.
     */
    std::vector<ctx_t> &get_apps();
    /**
     * @brief Get app image.
     *
     * @param app_id App ID.
     * @return Validated image path for the requested application.
     */
    std::string get_app_image(int app_id);
    /**
     * @brief Get last run app name.
     *
     * @return Name of the most recently launched application.
     */
    std::string get_last_run_app_name();
    /**
     * @brief Terminate the launched application process.
     */
    void terminate();

    /**
     * @brief When the running app was started (Nova).
     *
     * @return Unix time in seconds, or 0 when nothing was launched.
     */
    std::int64_t started_at() const;

    /**
     * @brief Take a freshly parsed app list, keeping track of the app that runs now (Nova).
     *
     * Replacing the whole proc_t (as a plain move did) forgot the running app's process: a
     * library scan or an edit in the web UI while a game ran made Nova think it had exited.
     *
     * @param parsed Result of parse().
     */
    void replace_apps(proc_t &&parsed);

    /**
     * @brief Whether execute() is starting an app right now (Nova: the idle watcher leaves it alone).
     *
     * @return True during execute().
     */
    bool executing() const;

    /**
     * @brief Nova: the app is being ended (terminate() runs); it no longer counts as running.
     * @return True while terminate() runs.
     */
    bool ending() const;

    /**
     * @brief Nova: whether Nova can see the running app's processes.
     *
     * False when the command exited at once and the app is treated as detached (auto-detach), so
     * Nova can't tell when it closes; it counts as running until it is quit.
     *
     * @return True for a tracked app (and for the Desktop entries).
     */
    bool tracked() const;

  private:
    int _app_id;
    std::string _last_error;

    boost::process::v1::environment _env;
    std::map<std::string, std::optional<std::string>> _vd_base_env;  ///< Desktop values of the variables a virtual-display launch overrides.
    std::vector<ctx_t> _apps;
    ctx_t _app;
    std::chrono::steady_clock::time_point _app_launch_time;
    movable_atomic_t<std::int64_t> _app_started_at {0};  ///< Nova: Unix time the running app was started, 0 when none.
    movable_atomic_t<bool> _executing {false};  ///< Nova: execute() is running.

    movable_atomic_t<bool> _ending {false};  ///< Nova: terminate() is running.
    movable_atomic_t<bool> _detached {false};  ///< Nova: the command exited at once and the app is treated as detached.
    app_processes::linger_t _linger;  ///< Nova: notices a game that exited while Wine/Proton helpers linger.
    std::chrono::steady_clock::time_point _linger_checked {};  ///< Nova: last look at the app's processes.

    /**
     * @brief Nova: whether the app's game has ended although helpers still keep its process alive.
     *
     * Looks at /proc at most once a second.
     *
     * @return True when only Wine/Proton helpers have remained for a few seconds.
     */
    bool game_lingered();

    /**
     * @brief Nova: make the app open on the virtual display it is launched on (see vd_app_launch.h):
     * its own browser/Electron profile and the desktop session's private D-Bus, unless the app
     * sets "nova-vd-share-profile".
     */
    void adapt_for_virtual_display();

    // If no command associated with _app_id, yet it's still running
    bool placebo {};

    boost::process::v1::child _process;
    boost::process::v1::group _process_group;

    file_t _pipe;
    std::vector<cmd_t>::const_iterator _app_prep_it;
    std::vector<cmd_t>::const_iterator _app_prep_begin;
  };

  /**
   * @brief Calculate a stable id based on name and image data
   * @return Tuple of id calculated without index (for use if no collision) and one with.
   *
   * @param app_name App name.
   * @param app_image_path App image path.
   * @param index Zero-based index of the item being addressed.
   */
  std::tuple<std::string, std::string> calculate_app_id(const std::string &app_name, std::string app_image_path, int index);

  /**
   * @brief Prepare a configured command for execution.
   *
   * @param command Configured command line.
   * @return Command line with any package-specific launcher prefix applied.
   */
  std::string prepare_command(const std::string &command);

  bool check_valid_png(const std::filesystem::path &path);
  /**
   * @brief Validate app image path.
   *
   * @param app_image_path Candidate image path from the application configuration.
   * @return Existing PNG path, or the default application image when validation fails.
   */
  std::string validate_app_image_path(std::string app_image_path);
  /**
   * @brief Refresh cached platform state from the operating system.
   *
   * @param file_name File name.
   */
  void refresh(const std::string &file_name);
  /**
   * @brief Parse serialized text into the corresponding runtime representation.
   *
   * @param file_name File name.
   * @return Parsed value or parse status.
   */
  std::optional<proc::proc_t> parse(const std::string &file_name);

  /**
   * @brief Initialize proc functions
   * @return Unique pointer to `deinit_t` to manage cleanup
   */
  std::unique_ptr<platf::deinit_t> init();

  /**
   * @brief Terminates all child processes in a process group.
   * @param proc The child process itself.
   * @param group The group of all children in the process tree.
   * @param exit_timeout The timeout to wait for the process group to gracefully exit.
   */
  void terminate_process_group(boost::process::v1::child &proc, boost::process::v1::group &group, std::chrono::seconds exit_timeout);

  extern proc_t proc;
}  // namespace proc
