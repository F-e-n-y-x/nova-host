/**
 * @file src/display_modeset.h
 * @brief Declarations for Nova's native Mirror mode switching (xrandr and nvidia-settings).
 *
 * Used for portrait Mirror streams (the display script can't rotate): the Mirror display (the
 * desktop output, on atom the HDMI dummy with a CustomEDID) is switched to the device's size and
 * refresh, rotated for portrait, and put back afterwards. Modes come from the output's EDID (xrandr's mode list).
 * A size the EDID has no mode for is reached with an NVIDIA MetaMode using ViewPortIn scaling.
 * Only the chosen output's MetaMode entry is replaced; every other display (the owner's DP
 * monitor) keeps its entry, and no output is ever enabled, forced or turned off.
 */
#pragma once

// standard includes
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// lib includes
#include <nlohmann/json.hpp>

// local includes
#include "display_follow.h"

namespace display_modeset {
  /**
   * @brief Whether an angle is one of 0, 90, 180, 270.
   *
   * @param angle Angle in degrees.
   * @return True when valid.
   */
  constexpr bool valid_rotation(int angle) {
    return angle == 0 || angle == 90 || angle == 180 || angle == 270;
  }

  /**
   * @brief Rotation a requested size needs on the Mirror display: 90 for portrait, else 0.
   *
   * @param width Requested width.
   * @param height Requested height.
   * @return 90 when height > width, else 0.
   */
  constexpr int rotation_for_size(int width, int height) {
    return height > width ? 90 : 0;
  }

  /**
   * @brief Screen size after a rotation change: portrait for 90/270, landscape for 0/180.
   *
   * @param width Current width.
   * @param height Current height.
   * @param angle New rotation.
   * @return The turned size.
   */
  constexpr std::pair<int, int> size_for_rotation(int width, int height, int angle) {
    const bool want_portrait = angle == 90 || angle == 270;
    if ((height > width) != want_portrait) {
      return {height, width};
    }
    return {width, height};
  }
  /**
   * @brief Result of one helper run.
   */
  struct run_result_t {
    int exit_code = -1;  ///< Exit code, or -1 when it couldn't start or timed out.
    std::string output;  ///< Standard output.
  };

  /**
   * @brief Runs a helper (argv[0] looked up in PATH) against the desktop display.
   */
  using runner_t = std::function<run_result_t(const std::vector<std::string> &argv)>;

  /**
   * @brief The real runner: fork/exec with DISPLAY and XAUTHORITY set, stdout captured, 10 s timeout.
   *
   * @param display DISPLAY for the helpers (the desktop's, e.g. ":0").
   * @param xauthority XAUTHORITY for the helpers, or empty to inherit.
   * @return The runner.
   */
  runner_t default_runner(std::string display, std::string xauthority);

  /**
   * @brief One mode size of an output with its refresh rates.
   */
  struct mode_t {
    int width = 0;  ///< Width in pixels.
    int height = 0;  ///< Height in pixels.
    std::vector<double> rates;  ///< Refresh rates in the order xrandr lists them.
    double current_rate = 0;  ///< The active rate, or 0 when this size isn't active.
    bool preferred = false;  ///< The EDID's preferred mode.
  };

  /**
   * @brief One RandR output as `xrandr --query` shows it.
   */
  struct output_t {
    std::string name;  ///< Output name, e.g. "HDMI-0".
    bool connected = false;  ///< Something is plugged in (or forced by the X config).
    bool primary = false;  ///< Primary output.
    bool enabled = false;  ///< Part of the screen (has a geometry).
    int width = 0;  ///< Current size on the X screen (after rotation and ViewPortIn).
    int height = 0;  ///< Current height on the X screen.
    int x = 0;  ///< Position on the X screen.
    int y = 0;  ///< Position on the X screen.
    int rotation = 0;  ///< 0, 90 (left), 180 (inverted) or 270 (right).
    std::vector<mode_t> modes;  ///< Modes from the EDID.
    std::string monitor_name;  ///< EDID monitor name (from `xrandr --verbose`), or empty.
  };

  /**
   * @brief Parse `xrandr --query` (or `--verbose`, whose property lines are skipped).
   *
   * @param text xrandr output.
   * @return Outputs in order.
   */
  std::vector<output_t> parse_xrandr(std::string_view text);

  /**
   * @brief Monitor names from the EDID blocks of `xrandr --verbose`.
   *
   * @param text xrandr --verbose output.
   * @return Output name to EDID monitor name (outputs without one are left out).
   */
  std::map<std::string, std::string> parse_edid_names(std::string_view text);

  /**
   * @brief Monitor name (descriptor 0xFC) of an EDID.
   *
   * @param edid EDID bytes (at least the 128-byte base block).
   * @return The name without padding, or empty.
   */
  std::string edid_monitor_name(const std::vector<std::uint8_t> &edid);

  /**
   * @brief Map output names to NVIDIA display names from `nvidia-settings -q dpys`.
   *
   * @param text nvidia-settings output.
   * @return e.g. {"HDMI-0": "DPY-0"}.
   */
  std::map<std::string, std::string> parse_dpy_names(std::string_view text);

  /**
   * @brief The MetaMode string of `nvidia-settings -q CurrentMetaMode -t` (after "::").
   *
   * @param text nvidia-settings output.
   * @return The MetaMode, or empty.
   */
  std::string parse_current_metamode(std::string_view text);

  /**
   * @brief Split a MetaMode into its per-display entries (commas inside braces are kept).
   *
   * @param metamode MetaMode string.
   * @return Entries, trimmed.
   */
  std::vector<std::string> split_metamode(std::string_view metamode);

  /**
   * @brief Replace (or append) one display's entry in a MetaMode, keeping every other display.
   *
   * @param metamode Current MetaMode.
   * @param names Every name the display is known by (e.g. "DPY-0", "HDMI-0").
   * @param entry New entry for that display.
   * @return The new MetaMode.
   */
  std::string replace_metamode_entry(std::string_view metamode, const std::vector<std::string> &names, const std::string &entry);

  /**
   * @brief Position ("+X+Y") of one display's entry in a MetaMode.
   *
   * @param metamode MetaMode.
   * @param names The display's names.
   * @return {x, y}, or {0, 0} when not found.
   */
  std::pair<int, int> metamode_entry_position(std::string_view metamode, const std::vector<std::string> &names);

  /**
   * @brief Refresh rate of a mode closest to what the stream wants: the lowest rate at or above
   * `fps` (0.5 Hz tolerance), else the highest.
   *
   * @param mode Mode.
   * @param fps Wanted rate.
   * @return The rate, or std::nullopt when the mode lists none.
   */
  std::optional<double> pick_rate(const mode_t &mode, int fps);

  /**
   * @brief The output Mirror streams switch.
   *
   * "auto" picks the connected output whose EDID is Nova's ("Nova VDD"), else the only connected
   * output; with several connected outputs and no Nova EDID it picks none, so a real monitor is
   * never re-moded by guesswork. Any other value names the output, which must be connected.
   *
   * @param outputs Outputs.
   * @param setting "auto" or an output name.
   * @return The output, or nullptr.
   */
  const output_t *pick_output(const std::vector<output_t> &outputs, const std::string &setting);

  /**
   * @brief Name of Nova's EDID monitor (the HDMI dummy's CustomEDID).
   */
  inline constexpr std::string_view nova_edid_name = "Nova VDD";

  /**
   * @brief What to set on the output.
   */
  struct target_t {
    int width = 0;  ///< Screen width wanted (portrait when height > width).
    int height = 0;  ///< Screen height wanted.
    int fps = 0;  ///< Refresh wanted.
    int rotation = 0;  ///< 0, 90, 180 or 270.
  };

  /**
   * @brief A mode switch worked out for one output.
   */
  struct plan_t {
    std::string output;  ///< Output name.
    int mode_width = 0;  ///< EDID mode used (unrotated raster).
    int mode_height = 0;  ///< EDID mode height.
    double rate = 0;  ///< Refresh rate of that mode.
    int screen_width = 0;  ///< Size on the X screen (ViewPortIn, after rotation).
    int screen_height = 0;  ///< Height on the X screen.
    int rotation = 0;  ///< 0, 90, 180 or 270.
    bool scaled = false;  ///< ViewPortIn differs from the rotated mode.

    bool operator==(const plan_t &) const = default;
  };

  /**
   * @brief Work out the mode, rate, rotation and ViewPortIn for a target.
   *
   * @param output The output with its EDID modes.
   * @param target What the stream wants.
   * @return The plan, or std::nullopt when the output lists no modes.
   */
  std::optional<plan_t> plan(const output_t &output, const target_t &target);

  /**
   * @brief RandR rotation name.
   *
   * @param angle 0, 90, 180 or 270.
   * @return "normal", "left", "inverted" or "right".
   */
  std::string_view rotation_name(int angle);

  /**
   * @brief NVIDIA mode name for a mode and rate, e.g. "2560x1440_120".
   *
   * @param plan Plan.
   * @return Mode name.
   */
  std::string nvidia_mode_name(const plan_t &plan);

  /**
   * @brief The MetaMode entry for a plan.
   *
   * @param plan Plan.
   * @param display NVIDIA display name ("DPY-0") or output name.
   * @param x Position on the X screen.
   * @param y Position on the X screen.
   * @return e.g. `DPY-0: 2560x1440_120 +0+0 {ViewPortIn=1080x2340, ViewPortOut=2560x1440+0+0, Rotation=left}`.
   */
  std::string metamode_entry(const plan_t &plan, const std::string &display, int x, int y);

  /**
   * @brief xrandr arguments for a plan (the fallback when nvidia-settings isn't there or fails).
   *
   * @param plan Plan.
   * @return argv starting with "xrandr".
   */
  std::vector<std::string> xrandr_args(const plan_t &plan);

  /**
   * @brief State saved before the first switch, used to put the display back (also after a crash).
   */
  struct saved_state_t {
    std::string output;  ///< Output that was switched.
    std::string metamode;  ///< Full MetaMode before the switch (every display), or empty without nvidia-settings.
    int mode_width = 0;  ///< Active mode before the switch.
    int mode_height = 0;  ///< Active mode height.
    double rate = 0;  ///< Active rate.
    int rotation = 0;  ///< Rotation before the switch.
  };

  /**
   * @brief JSON form of the saved state.
   *
   * @param state State.
   * @return JSON object.
   */
  nlohmann::json to_json(const saved_state_t &state);

  /**
   * @brief Parse the saved state.
   *
   * @param json JSON object.
   * @return The state, or std::nullopt when malformed.
   */
  std::optional<saved_state_t> saved_state_from_json(const nlohmann::json &json);

  /**
   * @brief Native Mirror switcher (portrait Mirror streams and Foundation /rotate-display).
   */
  class switcher_t: public display_follow::mirror_backend_t {
  public:
    /**
     * @brief Create a switcher.
     *
     * @param runner Helper runner (a fake in tests).
     * @param state_file Where the pre-switch state is kept until restore.
     * @param output_setting Returns the output to switch, "auto" or a name (read on every switch).
     * @param legacy_state The old script's saved MetaMode file, used as the pre-switch state when a
     *        switch it made is still active (empty to ignore).
     */
    switcher_t(runner_t runner, std::filesystem::path state_file, std::function<std::string()> output_setting, std::filesystem::path legacy_state = {});

    bool set(const display_follow::request_t &request) override;
    bool rotate(int angle) override;
    bool restore() override;
    bool recover() override;

    /**
     * @brief The last plan applied, if the display is switched.
     *
     * @return The plan, or std::nullopt.
     */
    std::optional<plan_t> current() const;

    /**
     * @brief Connected outputs of the desktop display (for Foundation /displays).
     *
     * @return Outputs with EDID names.
     */
    std::vector<output_t> outputs();

  private:
    std::optional<output_t> find_output_locked(bool with_edid);
    bool apply_locked(const output_t &output, const target_t &target);
    bool save_state_locked(const output_t &output);
    std::optional<saved_state_t> load_state_locked() const;
    bool restore_locked();

    runner_t runner_;
    std::filesystem::path state_file_;
    std::function<std::string()> output_setting_;
    std::filesystem::path legacy_state_;
    mutable std::mutex mutex_;
    std::optional<plan_t> current_;
    std::optional<target_t> target_;
  };

  /**
   * @brief Foundation `/displays` reply.
   *
   * @param outputs Connected desktop outputs.
   * @param mirror_output The output Mirror streams use, or empty.
   * @param virtual_target The running virtual display, if any.
   * @return `{"status_code":200,"status_message":"OK","displays":[{display_name, friendly_name,
   *         device_id, width, height, refresh_rate, rotation, primary, kind}]}`.
   */
  nlohmann::json displays_json(const std::vector<output_t> &outputs, const std::string &mirror_output, const std::optional<virtual_display::target_t> &virtual_target);

  /**
   * @brief Foundation `/rotate-display` reply.
   *
   * @param status_code 200 on success, else an HTTP-like error code.
   * @param message Status message.
   * @return `{"status_code", "status_message", "success"}`.
   */
  nlohmann::json rotate_json(int status_code, const std::string &message);
}  // namespace display_modeset
