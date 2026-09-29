/**
 * @file src/client_permissions.h
 * @brief Per-client permission flags, presets and their JSON form.
 */
#pragma once

// standard includes
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

// lib includes
#include <nlohmann/json.hpp>

/**
 * @brief What a paired client may do while it streams.
 *
 * Flags are enforced server-side: disallowed input packets are dropped, clipboard frames
 * are neither accepted from nor sent to the client, and app launches are refused.
 */
namespace client_permissions {
  /**
   * @brief Bit mask of permission flags.
   */
  using mask_t = std::uint32_t;

  constexpr mask_t input_keyboard = 1u << 0;  ///< Keyboard and text input.
  constexpr mask_t input_mouse = 1u << 1;  ///< Mouse movement, buttons and scrolling.
  constexpr mask_t input_controller = 1u << 2;  ///< Gamepads, including their touchpads and motion sensors.
  constexpr mask_t input_touch_pen = 1u << 3;  ///< Touchscreen and pen input.
  constexpr mask_t clipboard = 1u << 4;  ///< Clipboard sync and file transfer in both directions.
  constexpr mask_t launch_apps = 1u << 5;  ///< See every app and start one; without it only the running app can be resumed.
  constexpr mask_t power = 1u << 6;  ///< Put the host to sleep (/pcsleep).
  constexpr mask_t host_commands = 1u << 7;  ///< Run admin-defined host commands (/supercmd).
  constexpr mask_t app_profiles = 1u << 8;  ///< Change a game's host performance profile (frame cap, FSR, bitrate cap, power).

  constexpr mask_t input_all = input_keyboard | input_mouse | input_controller | input_touch_pen;  ///< Every input kind.
  constexpr mask_t full = input_all | clipboard | launch_apps | app_profiles | power | host_commands;  ///< Preset: everything, including sleep and host commands.
  constexpr mask_t standard = input_all | clipboard | launch_apps | app_profiles;  ///< Preset: stream, launch, tune games and clipboard; no host control (the default for new devices).
  constexpr mask_t play = input_all | launch_apps | app_profiles;  ///< Preset: launch and tune apps and use all input, no clipboard.
  constexpr mask_t view_only = 0;  ///< Preset: watch the running app; no input, no launching, no clipboard.
  constexpr mask_t paired_default = standard;  ///< What a newly paired device gets.

  /**
   * @brief Flags that control the host rather than the stream.
   *
   * State files written before a flag existed have no key for it; these default to off there,
   * the others to on (see default_when_missing()).
   */
  constexpr mask_t host_control = power | host_commands;

  /**
   * @brief JSON key for each flag, in display order.
   */
  constexpr std::array<std::pair<std::string_view, mask_t>, 9> flag_names {{
    {"input_keyboard", input_keyboard},
    {"input_mouse", input_mouse},
    {"input_controller", input_controller},
    {"input_touch_pen", input_touch_pen},
    {"clipboard", clipboard},
    {"launch_apps", launch_apps},
    {"app_profiles", app_profiles},
    {"power", power},
    {"host_commands", host_commands},
  }};

  /**
   * @brief Value to assume for a flag a stored permission object doesn't mention.
   *
   * @param flag Flag.
   * @return False for host-control flags (so an upgrade never grants them), true otherwise.
   */
  constexpr bool default_when_missing(const mask_t flag) {
    return (flag & host_control) == 0;
  }

  /**
   * @brief Value to assume for a missing flag, given what the stored object says about launching.
   *
   * "app_profiles" came after "launch_apps" and follows it, so a device that may not launch games
   * doesn't gain the right to tune them, and a stored preset keeps matching after an upgrade.
   *
   * @param flag Flag.
   * @param launch_apps Stored value of "launch_apps" (true when that is missing too).
   * @return The value to assume.
   */
  constexpr bool default_when_missing(const mask_t flag, const bool launch_apps) {
    return flag == app_profiles ? launch_apps : default_when_missing(flag);
  }

  /**
   * @brief Test whether a mask grants a flag.
   *
   * @param mask Permission mask to check.
   * @param flag Flag to look for.
   * @return `true` when every bit of `flag` is set in `mask`.
   */
  constexpr bool has(const mask_t mask, const mask_t flag) {
    return (mask & flag) == flag;
  }

  /**
   * @brief Keep only the defined flag bits.
   *
   * @param mask Mask that may contain unknown bits (e.g. read from disk).
   * @return The mask with unknown bits cleared.
   */
  constexpr mask_t sanitize(const mask_t mask) {
    return mask & full;
  }

  /**
   * @brief Name of the preset a mask matches.
   *
   * @param mask Permission mask.
   * @return "full", "standard", "play", "view_only", or "custom".
   */
  constexpr std::string_view preset_name(const mask_t mask) {
    switch (sanitize(mask)) {
      case full:
        return "full";
      case standard:
        return "standard";
      case play:
        return "play";
      case view_only:
        return "view_only";
      default:
        return "custom";
    }
  }

  /**
   * @brief Mask for a preset name.
   *
   * @param name "full", "standard", "play" or "view_only".
   * @return The preset mask, or `std::nullopt` for an unknown name.
   */
  constexpr std::optional<mask_t> preset_mask(const std::string_view name) {
    if (name == "full") {
      return full;
    }
    if (name == "standard") {
      return standard;
    }
    if (name == "play") {
      return play;
    }
    if (name == "view_only") {
      return view_only;
    }
    return std::nullopt;
  }

  /**
   * @brief Serialize a mask as an object of booleans plus its preset name.
   *
   * @param mask Permission mask.
   * @return JSON such as `{"input_keyboard": true, ..., "preset": "full"}`.
   */
  inline nlohmann::json to_json(const mask_t mask) {
    nlohmann::json node = nlohmann::json::object();
    for (const auto &[name, flag] : flag_names) {
      node[std::string {name}] = has(mask, flag);
    }
    node["preset"] = std::string {preset_name(mask)};
    return node;
  }

  /**
   * @brief Apply a JSON permission update on top of an existing mask.
   *
   * Accepts `{"preset": "play"}`, individual flags such as `{"clipboard": false}`, or both
   * (the preset applies first, then the individual flags).
   *
   * @param current Mask before the update.
   * @param update JSON object with an optional "preset" string and optional boolean flags.
   * @return The updated mask.
   * @throws std::invalid_argument When the update is not an object, names an unknown key or
   *         preset, or gives a flag a non-boolean value.
   */
  inline mask_t apply_json(mask_t current, const nlohmann::json &update) {
    if (!update.is_object()) {
      throw std::invalid_argument("permissions must be an object");
    }

    if (const auto preset = update.find("preset"); preset != update.end()) {
      if (!preset->is_string()) {
        throw std::invalid_argument("preset must be a string");
      }
      const auto mask = preset_mask(preset->get<std::string>());
      if (!mask) {
        throw std::invalid_argument("unknown permission preset");
      }
      current = *mask;
    }

    for (const auto &[key, value] : update.items()) {
      if (key == "preset") {
        continue;
      }
      const auto *it = std::ranges::find_if(flag_names, [&key](const auto &entry) {
        return entry.first == key;
      });
      if (it == flag_names.end()) {
        throw std::invalid_argument("unknown permission: " + key);
      }
      if (!value.is_boolean()) {
        throw std::invalid_argument("permission " + key + " must be a boolean");
      }
      current = value.get<bool>() ? (current | it->second) : (current & ~it->second);
    }
    return sanitize(current);
  }
}  // namespace client_permissions
