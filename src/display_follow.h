/**
 * @file src/display_follow.h
 * @brief Declarations for Nova's session-level "follow the client display mode" step.
 */
#pragma once

// standard includes
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

/**
 * @brief Switches the host display to the client's mode on every stream start and restores it afterwards.
 *
 * Per-app prep commands only run when an app is launched, so a resumed app, a second device or a new
 * resolution never re-applied the mode. This step runs on every /launch and /resume instead.
 */
namespace display_follow {
  using env_t = std::vector<std::pair<std::string, std::string>>;  ///< Extra environment passed to the command.

  /**
   * @brief Runs `cmd action` with the extra environment.
   *
   * @return The command's exit code, or -1 if it could not be started or timed out.
   */
  using runner_t = std::function<int(const std::string &cmd, const std::string &action, const env_t &env)>;

  /**
   * @brief The client mode a stream asked for.
   */
  struct request_t {
    int width = 0;  ///< Requested width in pixels.
    int height = 0;  ///< Requested height in pixels.
    int fps = 0;  ///< Requested frame rate.
    std::string mode;  ///< "virtual", "mirror", or empty for the default (virtual); both follow the client size, passed on as NOVA_DISPLAY_MODE.
    std::string app_name;  ///< App being launched or resumed, for logging.
    std::string client_name;  ///< Device name, for logging.
  };

  /**
   * @brief What happened for one stream request.
   */
  enum class outcome_e {
    switched,  ///< The command ran successfully and the display now follows the client.
    failed,  ///< The command failed or timed out.
    skipped_off,  ///< The feature is disabled or no command is configured.
    skipped_missing_cmd,  ///< The configured command does not exist on disk.
    skipped_legacy,  ///< The app's own prep command already switches the display on launch.
    skipped_busy,  ///< Another client is already streaming; its mode is kept.
  };

  /**
   * @brief Stateful controller; one instance per process (see instance()).
   */
  class controller_t {
  public:
    /**
     * @brief Create a controller.
     *
     * @param runner Command runner (injected for tests).
     * @param marker File that exists while the display is switched, used to restore after a crash.
     */
    controller_t(runner_t runner, std::filesystem::path marker);

    /**
     * @brief Apply the client's mode at the start of a stream.
     *
     * @param setting Value of display_follow ("virtual" or "off").
     * @param cmd Value of display_follow_cmd.
     * @param request The client's requested mode.
     * @param legacy_prep True when the app's own prep command already switches the display for this launch.
     * @param other_sessions True when another client is streaming right now.
     * @return What was done.
     */
    outcome_e on_stream_request(const std::string &setting, const std::string &cmd, const request_t &request, bool legacy_prep, bool other_sessions);

    /**
     * @brief Restore the display after the last stream ended.
     *
     * @param cmd Value of display_follow_cmd.
     * @return True if a restore ran successfully.
     */
    bool on_last_session_end(const std::string &cmd);

    /**
     * @brief Restore a display left switched by a previous run (crash or kill).
     *
     * @param cmd Value of display_follow_cmd.
     * @return True if a restore ran.
     */
    bool recover(const std::string &cmd);

    /**
     * @brief Whether the display is currently switched by this controller.
     *
     * @return True while switched.
     */
    bool active() const;

  private:
    bool restore_locked(const std::string &cmd);

    runner_t runner_;
    std::filesystem::path marker_;
    mutable std::mutex mutex_;
    bool active_ = false;
  };

  /**
   * @brief Whether a prep command is the display script this step replaces.
   *
   * @param prep_do_cmd A prep command's "do" string.
   * @param cmd Value of display_follow_cmd.
   * @return True if the prep command runs the same script with "set" or "virtual".
   */
  bool is_legacy_prep(const std::string &prep_do_cmd, const std::string &cmd);

  /**
   * @brief Build the environment passed to the command.
   *
   * @param request The client's requested mode.
   * @return Environment pairs.
   */
  env_t make_env(const request_t &request);

  /**
   * @brief Process-wide controller using the real command runner and the state directory marker.
   *
   * @return The controller.
   */
  controller_t &instance();

  /**
   * @brief Config-driven wrapper for /launch and /resume.
   *
   * @param request The client's requested mode.
   * @param legacy_prep See controller_t::on_stream_request().
   * @param other_sessions See controller_t::on_stream_request().
   * @return What was done.
   */
  outcome_e stream_requested(const request_t &request, bool legacy_prep, bool other_sessions);

  /**
   * @brief Config-driven wrapper called when the last stream session ends.
   */
  void last_session_ended();

  /**
   * @brief Config-driven wrapper called once at startup.
   */
  void recover_at_startup();
}  // namespace display_follow
