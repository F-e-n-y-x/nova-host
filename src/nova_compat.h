/**
 * @file src/nova_compat.h
 * @brief Declarations for Nova's Windows game compatibility layer (GE-Proton via umu).
 */
#pragma once

// standard includes
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nova_compat {
  /**
   * @brief Per-app compatibility options stored in apps.json under "nova-compat".
   */
  struct options_t {
    std::string prefix;  ///< Wine prefix to use; empty picks ~/Games/nova/<slug>.
    int fsr = 0;  ///< Proton fullscreen FSR sharpness 1-5; 0 disables FSR.
    int fps_cap = 0;  ///< DXVK frame rate cap; 0 disables the cap.
    bool mangohud = false;  ///< Whether to enable the MangoHud overlay.
    std::string proton_version;  ///< "latest" (or empty) for GE-Proton latest, else a GE-Proton release name.
    std::vector<std::string> extra_env;  ///< Extra "KEY=VALUE" pairs passed to the game.
  };

  /**
   * @brief Environment variable name/value pairs.
   */
  using env_t = std::vector<std::pair<std::string, std::string>>;

  /**
   * @brief Turn an app name into a prefix-safe slug ("Grand Theft Auto V" -> "grand-theft-auto-v").
   *
   * @param name App name.
   * @return Lower-case slug of letters, digits and single dashes; "game" when nothing is left.
   */
  std::string slugify(std::string_view name);

  /**
   * @brief Build the NOVA_* environment the nova-proton-run wrapper reads.
   *
   * @param app_name App name, used for the default prefix slug.
   * @param steam_appid Matched Steam app id, or 0 when unknown.
   * @param options Per-app compatibility options.
   * @param auto_update Whether GE-Proton should track the latest release.
   * @return Environment pairs; keys absent from the list must be cleared by the caller.
   */
  env_t build_env(std::string_view app_name, std::uint32_t steam_appid, const options_t &options, bool auto_update);

  /**
   * @brief All NOVA_* variable names that build_env() may set, so a caller can clear stale ones.
   *
   * @return Variable names.
   */
  const std::vector<std::string> &env_keys();

  /**
   * @brief Check that the game executable a launch needs actually exists.
   *
   * @param exe Executable path, or empty when the app has none to check.
   * @return A user-facing error message when the file is missing, otherwise nothing.
   */
  std::optional<std::string> check_launch_target(const std::string &exe);

  /**
   * @brief Launch command for a Windows executable using the given wrapper.
   *
   * @param wrapper Absolute path of nova-proton-run.
   * @param exe Executable path.
   * @return The quoted command, or empty when the path cannot be quoted safely.
   */
  std::string wrapper_command(const std::string &wrapper, const std::string &exe);

  /**
   * @brief Absolute path of the installed nova-proton-run wrapper.
   *
   * @return Wrapper path (compile-time install location).
   */
  std::string wrapper_path();
}  // namespace nova_compat
