/**
 * @file src/display_follow.h
 * @brief Declarations for Nova's session-level "follow the client display mode" step.
 */
#pragma once

// standard includes
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// local includes
#include "virtual_display.h"

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
    virtual_started,  ///< A new headless X display was started for this client (Virtual display).
    virtual_reused,  ///< The running virtual display was kept (resized to this client if needed).
  };

  /**
   * @brief Starts and stops the separate display used by Virtual display streams.
   */
  class virtual_backend_t {
  public:
    virtual ~virtual_backend_t() = default;

    /**
     * @brief Start a display at the client's size.
     *
     * @param request The client's requested mode.
     * @return The display, or std::nullopt on failure.
     */
    virtual std::optional<virtual_display::target_t> start(const request_t &request) = 0;

    /**
     * @brief Resize the running display for a new client mode.
     *
     * @param request The client's requested mode.
     * @return The display after the resize, or std::nullopt on failure.
     */
    virtual std::optional<virtual_display::target_t> resize(const request_t &request) = 0;

    /**
     * @brief Stop the running display.
     */
    virtual void stop() = 0;

    /**
     * @brief Clean up a display left by a previous run.
     *
     * @return True if something was cleaned up.
     */
    virtual bool recover() = 0;
  };

  /**
   * @brief Callbacks around the virtual display's lifetime (used by the process-wide controller).
   */
  struct virtual_hooks_t {
    std::function<void(const virtual_display::target_t &)> up;  ///< The display is ready (capture, input and audio switch to it).
    std::function<void()> before_stop;  ///< The display is about to stop (the app running on it is ended first).
    std::function<void()> down;  ///< The display is gone (capture, input and audio go back to the desktop).
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
     * @param backend Separate-display backend for Virtual display streams, or null when unavailable.
     * @param hooks Callbacks around the virtual display's lifetime.
     */
    controller_t(runner_t runner, std::filesystem::path marker, std::shared_ptr<virtual_backend_t> backend = nullptr, virtual_hooks_t hooks = {});

    /**
     * @brief Apply the client's mode at the start of a stream.
     *
     * @param setting Value of display_follow ("virtual" or "off").
     * @param cmd Value of display_follow_cmd.
     * @param request The client's requested mode.
     * @param legacy_prep True when the app's own prep command already switches the display for this launch.
     * @param other_sessions True when another client is streaming right now.
     * @param virtual_setting Value of virtual_display: "headless_x" gives an explicit "virtual" request its
     *        own display; "off" (the default here) sends it through `cmd` like Mirror.
     * @return What was done.
     */
    outcome_e on_stream_request(const std::string &setting, const std::string &cmd, const request_t &request, bool legacy_prep, bool other_sessions, const std::string &virtual_setting = "off");

    /**
     * @brief Restore the display after the last stream ended.
     *
     * Stops the virtual display too (after before_stop, which ends the app running on it).
     *
     * @param cmd Value of display_follow_cmd.
     * @return True if a restore ran successfully or a virtual display was stopped.
     */
    bool on_last_session_end(const std::string &cmd);

    /**
     * @brief Restore a display left switched by a previous run (crash or kill).
     *
     * Also kills a virtual display a crashed run left behind (through its marker file).
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

    /**
     * @brief The running virtual display.
     *
     * @return The display, or std::nullopt when streams use the desktop.
     */
    std::optional<virtual_display::target_t> virtual_target() const;

  private:
    bool restore_locked(const std::string &cmd);
    void stop_virtual_locked();

    runner_t runner_;
    std::filesystem::path marker_;
    std::shared_ptr<virtual_backend_t> backend_;
    virtual_hooks_t hooks_;
    mutable std::mutex mutex_;
    bool active_ = false;
    std::optional<virtual_display::target_t> virtual_;
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

  /**
   * @brief Stop the virtual display when Nova exits.
   */
  void shutdown();

  /**
   * @brief The app was closed (or a launch failed) while no device streams: stop the virtual display.
   *
   * A stream that ends stops it through last_session_ended(); this covers a launch that never got
   * a stream and "Close app" with nobody connected. No-op without a virtual display.
   */
  void app_closed();

  /**
   * @brief The running virtual display of the process-wide controller.
   *
   * @return The display, or std::nullopt when streams use the desktop.
   */
  std::optional<virtual_display::target_t> virtual_target();

  /**
   * @brief Environment for an app launched while a virtual display runs.
   *
   * @param channels Audio channels of the stream (2, 6 or 8), used to pick Nova's capture sink.
   * @param host_audio True when the client asked to keep playing audio on the host (no PULSE_SINK then).
   * @param base_ld_preload LD_PRELOAD the app would otherwise inherit.
   * @return Variables to set, or empty when no virtual display runs.
   */
  virtual_display::env_list_t app_env(int channels, bool host_audio, const std::string &base_ld_preload);
}  // namespace display_follow
