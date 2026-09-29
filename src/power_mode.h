/**
 * @file src/power_mode.h
 * @brief Declarations for Nova's streaming power mode: GPU PowerMizer, CPU profile and sleep/screensaver inhibit.
 */
#pragma once

// standard includes
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

/**
 * @brief Raises the host's performance settings while a stream is active and puts them back afterwards.
 *
 * On the first session start the controller can:
 * - switch the NVIDIA PowerMizer to "prefer maximum performance" (`nvidia-settings -a GpuPowerMizerMode=1`),
 * - select the "performance" profile through power-profiles-daemon or tuned (the CPU governor is only read:
 *   changing it needs root),
 * - hold a logind sleep/idle inhibitor and an org.freedesktop.ScreenSaver inhibit.
 *
 * Previous GPU and CPU values are written to a marker file so a crash mid-stream can be undone at the next start.
 * The inhibitors are tied to Nova's D-Bus connection and file descriptor, so the system drops them by itself
 * when the process dies.
 */
namespace power_mode {
  /**
   * @brief Result of one external command.
   */
  struct run_result_t {
    int exit_code = -1;  ///< Exit status; 127 when the program is not installed, -1 when it could not run.
    std::string output;  ///< Standard output.
  };

  /**
   * @brief Runs a program (argv[0] is looked up in PATH) and returns its exit status and stdout.
   */
  using runner_t = std::function<run_result_t(const std::vector<std::string> &argv)>;

  /**
   * @brief Returns the scaling governor of every CPU (read from sysfs in production).
   */
  using governor_reader_t = std::function<std::vector<std::string>()>;

  /**
   * @brief Holds the sleep/idle and screensaver inhibitors.
   */
  class inhibitor_t {
  public:
    virtual ~inhibitor_t() = default;

    /**
     * @brief Take the inhibitors.
     *
     * @param reason Human-readable reason shown by `systemd-inhibit --list`.
     * @return Names of what is now inhibited, e.g. {"sleep", "screensaver"}.
     */
    virtual std::vector<std::string> acquire(const std::string &reason) = 0;

    /**
     * @brief Drop every inhibitor taken by acquire().
     */
    virtual void release() = 0;
  };

  /**
   * @brief Which parts of the power mode to apply.
   */
  struct settings_t {
    bool enabled = false;  ///< Master switch (config `power_mode`).
    bool gpu = true;  ///< PowerMizer "prefer maximum performance" (config `power_mode_gpu`).
    bool cpu = true;  ///< Performance power profile (config `power_mode_cpu`).
    bool inhibit = true;  ///< Sleep and screensaver inhibit (config `power_mode_inhibit`).
    std::string x_display = ":0";  ///< X display passed to nvidia-settings.
  };

  /**
   * @brief Values to put back after the stream, persisted in the marker file.
   */
  struct saved_state_t {
    std::string gpu_mode;  ///< Previous GpuPowerMizerMode, empty when the GPU was not changed.
    std::string x_display;  ///< X display the GPU value was changed on.
    std::string cpu_tool;  ///< "powerprofilesctl" or "tuned-adm", empty when the CPU profile was not changed.
    std::string cpu_value;  ///< Previous profile name.

    /**
     * @brief Whether anything needs restoring.
     * @return True when the GPU or CPU was changed.
     */
    bool empty() const;

    /**
     * @brief Serialize as `key=value` lines for the marker file.
     * @return Marker file contents.
     */
    std::string serialize() const;

    /**
     * @brief Parse marker file contents written by serialize().
     *
     * Values are checked (GPU mode must be a small integer, profile names are limited to
     * `[A-Za-z0-9_.-]`) so a tampered marker can't turn into command arguments.
     *
     * @param text Marker file contents.
     * @return The saved state, or nullopt when the text is malformed.
     */
    static std::optional<saved_state_t> parse(const std::string &text);
  };

  /**
   * @brief What the controller did, for logs and the web UI.
   */
  struct status_t {
    bool active = false;  ///< True between apply() and restore().
    std::string gpu;  ///< "performance", "already", "unavailable", "failed" or "off".
    std::string cpu;  ///< "performance", "already", "governor:<name>", "unavailable", "failed" or "off".
    std::vector<std::string> inhibited;  ///< What the inhibitor took.
  };

  /**
   * @brief Stateful power mode controller; one per process (see instance()).
   */
  class controller_t {
  public:
    /**
     * @brief Create a controller.
     *
     * @param runner Command runner (a fake in tests).
     * @param inhibitor Inhibitor implementation (a fake in tests); may be null.
     * @param governors CPU governor reader.
     * @param marker File that exists while GPU/CPU settings are changed.
     */
    controller_t(runner_t runner, std::unique_ptr<inhibitor_t> inhibitor, governor_reader_t governors, std::filesystem::path marker);

    /**
     * @brief Apply the power mode for a starting stream. Does nothing when disabled or already active.
     *
     * @param settings What to apply.
     * @param reason Inhibit reason.
     * @return What was done.
     */
    status_t apply(const settings_t &settings, const std::string &reason);

    /**
     * @brief Put everything back after the last stream ended. Does nothing when not active.
     *
     * @return True when every restore command succeeded.
     */
    bool restore();

    /**
     * @brief Undo settings left changed by a previous run (crash or kill), using the marker file.
     *
     * @return True when a marker was found and restored.
     */
    bool recover();

    /**
     * @brief Current status.
     * @return A copy of the status.
     */
    status_t status() const;

  private:
    std::string apply_gpu(const settings_t &settings, saved_state_t &saved);
    std::string apply_cpu(saved_state_t &saved);
    bool restore_saved(const saved_state_t &saved);
    void write_marker(const saved_state_t &saved) const;

    runner_t runner_;
    std::unique_ptr<inhibitor_t> inhibitor_;
    governor_reader_t governors_;
    std::filesystem::path marker_;
    mutable std::mutex mutex_;
    status_t status_;
    saved_state_t saved_;
  };

  /**
   * @brief Parse the value printed by `nvidia-settings -t -q GpuPowerMizerMode`.
   *
   * @param output Command output; warnings on other lines are ignored.
   * @return The mode (e.g. "0", "1", "2"), or nullopt.
   */
  std::optional<std::string> parse_powermizer(const std::string &output);

  /**
   * @brief Parse the active profile from `powerprofilesctl get` or `tuned-adm active` output.
   *
   * @param output Command output.
   * @return The profile name, or nullopt.
   */
  std::optional<std::string> parse_profile(const std::string &output);

  /**
   * @brief Process-wide controller with the real runner, D-Bus inhibitor and a marker in the state directory.
   *
   * @return The controller.
   */
  controller_t &instance();

  /**
   * @brief Config-driven hook for the first session start. Runs on a background thread.
   *
   * @param client_name Device name for the inhibit reason.
   */
  void first_session_started(const std::string &client_name);

  /**
   * @brief Config-driven hook for the last session end. Runs on a background thread.
   */
  void last_session_ended();

  /**
   * @brief Undo a previous run's changes; called once at startup.
   */
  void recover_at_startup();

  /**
   * @brief Whether the power mode should be on for a stream.
   *
   * @param setting_enabled The host setting (config `power_mode`).
   * @param app_override The running game's override: true (raise), false (leave alone), nullopt (follow the setting).
   * @return The decision.
   */
  constexpr bool wanted(bool setting_enabled, std::optional<bool> app_override) {
    return app_override.value_or(setting_enabled);
  }

  /**
   * @brief Set the running game's power-mode override (its performance profile's "power").
   *
   * Called when a game starts (and with nullopt when it ends). While a stream runs, the power mode
   * switches right away to match; otherwise the next stream start uses it.
   *
   * @param app_override true (raise), false (leave alone) or nullopt (follow the host setting).
   */
  void set_app_override(std::optional<bool> app_override);
}  // namespace power_mode
