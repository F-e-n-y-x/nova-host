/**
 * @file src/nova_perf.h
 * @brief Declarations for per-app performance profiles ("nova-perf" in apps.json).
 *
 * A profile holds host-side tuning for one game: a frame rate cap, Proton's fullscreen FSR,
 * vkBasalt contrast-adaptive sharpening and the MangoHud overlay (environment variables on the
 * game process, no gamescope involved), plus two stream settings that apply to any launcher: a
 * stream bitrate cap and a power-mode override. Paired devices read and change it through
 * `/nova/v1/apps/<id>/profile`; the web UI edits it in the app editor.
 */
#pragma once

// standard includes
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// lib includes
#include <nlohmann/json.hpp>

namespace nova_perf {
  constexpr int MAX_FPS_CAP = 1000;  ///< Highest frame rate cap accepted.
  constexpr int MAX_FSR = 5;  ///< Proton FSR levels run 1 (sharpest) to 5; 0 is off.
  constexpr int DEFAULT_CAS = 50;  ///< vkBasalt CAS sharpness in percent when none is stored.
  constexpr int MIN_BITRATE_KBPS = 500;  ///< Lowest stream bitrate cap accepted (other than 0 = none).
  constexpr int MAX_BITRATE_KBPS = 800000;  ///< Highest stream bitrate cap accepted (800 Mbps).

  /**
   * @brief Streaming power mode for one game.
   */
  enum class power_e {
    follow,  ///< Follow the host's "power_mode" setting.
    performance,  ///< Raise GPU/CPU performance while this game streams, even when the setting is off.
    balanced,  ///< Leave power settings alone while this game streams, even when the setting is on.
  };

  /**
   * @brief One app's performance profile. Every field at its default means "no profile".
   */
  struct profile_t {
    int fps_cap = 0;  ///< Frame rate cap (DXVK, VKD3D-Proton and MangoHud); 0 = no cap.
    int fsr = 0;  ///< Proton fullscreen FSR, 1 (sharpest) to 5; 0 = off.
    bool vkbasalt = false;  ///< vkBasalt contrast-adaptive sharpening on Vulkan games.
    int vkbasalt_cas = DEFAULT_CAS;  ///< CAS sharpness, 0 to 100 percent.
    bool mangohud = false;  ///< Show the MangoHud overlay.
    int bitrate_kbps = 0;  ///< Stream bitrate cap in kbps while this game runs; 0 = the client's choice.
    power_e power = power_e::follow;  ///< Streaming power mode override.

    bool operator==(const profile_t &) const = default;
  };

  /**
   * @brief Wire name of a power mode.
   *
   * @param p Power mode.
   * @return "default", "performance" or "balanced".
   */
  std::string_view to_string(power_e p);

  /**
   * @brief Parse a power mode wire name.
   *
   * @param name "default", "performance" or "balanced".
   * @return The mode, or nullopt for anything else.
   */
  std::optional<power_e> parse_power(std::string_view name);

  /**
   * @brief The power-mode override a profile asks for.
   *
   * @param p Profile.
   * @return true (raise), false (leave alone) or nullopt (follow the host setting).
   */
  std::optional<bool> power_override(const profile_t &p);

  /**
   * @brief Apply a game's stream bitrate cap to a requested bitrate.
   *
   * @param requested_kbps Bitrate the client asked for.
   * @param cap_kbps The game's cap; 0 or less means none.
   * @return The requested bitrate, lowered to the cap when it is above it.
   */
  int cap_bitrate(int requested_kbps, int cap_kbps);

  /**
   * @brief How the host starts an app, which decides whether launch variables reach the game.
   */
  enum class launcher_e {
    command,  ///< A plain command Nova runs itself: variables reach the game.
    proton,  ///< Nova's GE-Proton wrapper (nova-proton-run): variables reach the game.
    steam,  ///< A steam:// URL: Steam starts the game, so Nova's variables don't reach it.
    lutris,  ///< Lutris: variables reach the game only when Lutris isn't already open.
    none,  ///< No command (a desktop entry): there is no game to tune.
  };

  /**
   * @brief Environment variable name/value pairs.
   */
  using env_t = std::vector<std::pair<std::string, std::string>>;

  /**
   * @brief Whether a profile changes nothing.
   *
   * @param p Profile.
   * @return `true` when every setting is off.
   */
  bool is_default(const profile_t &p);

  /**
   * @brief Clamp every field into its valid range.
   *
   * @param p Profile, possibly read from a hand-edited file.
   * @return The profile with fps_cap in [0, MAX_FPS_CAP], fsr in [0, MAX_FSR], vkbasalt_cas in [0, 100]
   *         and bitrate_kbps 0 or in [MIN_BITRATE_KBPS, MAX_BITRATE_KBPS].
   */
  profile_t sanitize(profile_t p);

  /**
   * @brief Read a stored "nova-perf" object leniently: wrong types are ignored, numbers clamped.
   *
   * @param node The stored object (anything else yields the default profile).
   * @return The profile.
   */
  profile_t from_json(const nlohmann::json &node);

  /**
   * @brief Every field of a profile, as the API returns it.
   *
   * @param p Profile.
   * @return {fps_cap, fsr, vkbasalt, vkbasalt_cas, mangohud, bitrate_kbps, power}.
   */
  nlohmann::json to_json(const profile_t &p);

  /**
   * @brief The effective profile of an app from apps.json.
   *
   * "nova-perf" wins. Apps saved before it existed keep the FSR, frame cap and MangoHud options
   * that the GE-Proton settings ("nova-compat") used to hold.
   *
   * @param app App object from apps.json.
   * @return The profile.
   */
  profile_t for_app(const nlohmann::json &app);

  /**
   * @brief Write a profile into an app object.
   *
   * Sets "nova-perf" to the non-default fields (removing it when the profile is empty) and moves
   * the legacy FSR, frame cap and MangoHud keys out of "nova-compat" so there is one source.
   *
   * @param app App object from apps.json (modified).
   * @param p Profile to store.
   */
  void store(nlohmann::json &app, const profile_t &p);

  /**
   * @brief Apply a client's partial update on top of a profile, strictly.
   *
   * @param current Profile before the update.
   * @param update JSON object with any of the profile's keys.
   * @return The updated profile.
   * @throws std::invalid_argument When the update isn't an object, names an unknown key, has a
   *         value of the wrong type, or a number out of range.
   */
  profile_t apply_update(profile_t current, const nlohmann::json &update);

  /**
   * @brief Apply a `POST /nova/v1/apps/<id>/profile` body to an app object in place.
   *
   * @param app App object from apps.json (modified only when the body is valid).
   * @param body Request body: a JSON object with any of the profile's keys.
   * @return The app's new profile.
   * @throws std::invalid_argument When the body isn't valid JSON or fails apply_update().
   */
  profile_t update_app(nlohmann::json &app, std::string_view body);

  /**
   * @brief Classify how an app is started.
   *
   * @param app App object from apps.json.
   * @return The launcher kind.
   */
  launcher_e launcher_of(const nlohmann::json &app);

  /**
   * @brief Wire name of a launcher kind.
   *
   * @param l Launcher kind.
   * @return "command", "proton", "steam", "lutris" or "none".
   */
  std::string_view to_string(launcher_e l);

  /**
   * @brief The `/nova/v1/apps/<id>/profile` reply.
   *
   * @param app App object from apps.json.
   * @param can_edit Whether the asking device may change profiles.
   * @return {profile, launcher, applies, can_edit, limits}; `applies` is false when the launch
   *         variables can't reach the game (Steam URLs, desktops); the stream settings
   *         (bitrate_kbps, power) apply either way. `limits` gives the accepted ranges.
   */
  nlohmann::json api_reply(const nlohmann::json &app, bool can_edit);

  /**
   * @brief vkBasalt configuration for a profile.
   *
   * @param p Profile.
   * @return Config file text (CAS only, with the profile's sharpness).
   */
  std::string vkbasalt_config(const profile_t &p);

  /**
   * @brief Write the vkBasalt config of a game into @p dir.
   *
   * @param dir Directory to write into (created when missing).
   * @param slug Game slug, used as the file name.
   * @param p Profile.
   * @return The file written, or nothing when it couldn't be written.
   */
  std::optional<std::filesystem::path> write_vkbasalt_config(const std::filesystem::path &dir, std::string_view slug, const profile_t &p);

  /**
   * @brief Where the launcher keeps generated vkBasalt configs: `$XDG_CACHE_HOME/nova/vkbasalt`.
   *
   * @return Directory (falls back to ~/.cache).
   */
  std::filesystem::path vkbasalt_dir();

  /**
   * @brief Launch environment for a profile.
   *
   * @param p Profile.
   * @param launcher How the app starts; Proton games are capped by DXVK only, others by MangoHud too.
   * @param vkbasalt_config Path of the game's vkBasalt config, when vkBasalt is on and it was written.
   * @param outer_cap Frame cap already in force for the launch (a virtual display sets one); 0 = none.
   *        The lower of it and the profile's cap wins.
   * @return Variables to set on the game process; empty for the default profile.
   */
  env_t build_env(const profile_t &p, launcher_e launcher, const std::optional<std::filesystem::path> &vkbasalt_config, int outer_cap = 0);

  /**
   * @brief Combine a game's frame cap with one already in force.
   *
   * @param profile_cap The profile's cap; 0 = none.
   * @param outer_cap The cap already in force; 0 = none.
   * @return The lower positive cap, or 0 when neither is set.
   */
  int combined_cap(int profile_cap, int outer_cap);
}  // namespace nova_perf
