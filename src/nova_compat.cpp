/**
 * @file src/nova_compat.cpp
 * @brief Nova's Windows game compatibility layer (GE-Proton via umu).
 */
// standard includes
#include <algorithm>
#include <cctype>
#include <filesystem>

// local includes
#include "nova_compat.h"

#ifndef NOVA_PROTON_RUN
  #define NOVA_PROTON_RUN "/usr/libexec/nova-host/nova-proton-run"
#endif

namespace nova_compat {
  std::string slugify(std::string_view name) {
    std::string out;
    bool dash = false;
    for (const unsigned char c : name) {
      if (std::isalnum(c)) {
        out.push_back(static_cast<char>(std::tolower(c)));
        dash = false;
      } else if (!out.empty() && !dash) {
        out.push_back('-');
        dash = true;
      }
    }
    while (!out.empty() && out.back() == '-') {
      out.pop_back();
    }
    return out.empty() ? "game" : out;
  }

  const std::vector<std::string> &env_keys() {
    static const std::vector<std::string> keys {
      "NOVA_GAME_SLUG",
      "NOVA_STEAM_APPID",
      "NOVA_COMPAT_PREFIX",
      "NOVA_COMPAT_FSR",
      "NOVA_COMPAT_FPS",
      "NOVA_COMPAT_MANGOHUD",
      "NOVA_COMPAT_PROTON",
      "NOVA_COMPAT_ENV",
      "NOVA_PROTON_AUTO_UPDATE",
    };
    return keys;
  }

  env_t build_env(std::string_view app_name, std::uint32_t steam_appid, const options_t &options, bool auto_update) {
    env_t env;
    env.emplace_back("NOVA_GAME_SLUG", slugify(app_name));
    env.emplace_back("NOVA_PROTON_AUTO_UPDATE", auto_update ? "1" : "0");
    if (steam_appid != 0) {
      env.emplace_back("NOVA_STEAM_APPID", std::to_string(steam_appid));
    }
    if (!options.prefix.empty()) {
      env.emplace_back("NOVA_COMPAT_PREFIX", options.prefix);
    }
    if (options.fsr > 0) {
      env.emplace_back("NOVA_COMPAT_FSR", std::to_string(std::clamp(options.fsr, 1, 5)));
    }
    if (options.fps_cap > 0) {
      env.emplace_back("NOVA_COMPAT_FPS", std::to_string(std::clamp(options.fps_cap, 1, 1000)));
    }
    if (options.mangohud) {
      env.emplace_back("NOVA_COMPAT_MANGOHUD", "1");
    }
    if (!options.proton_version.empty() && options.proton_version != "latest") {
      env.emplace_back("NOVA_COMPAT_PROTON", options.proton_version);
    }
    std::string extra;
    for (const auto &kv : options.extra_env) {
      const auto eq = kv.find('=');
      // Only well-formed KEY=VALUE pairs with a shell-safe key; the wrapper exports them verbatim.
      if (eq == 0 || eq == std::string::npos || kv.find('\n') != std::string::npos) {
        continue;
      }
      const auto key = kv.substr(0, eq);
      if (!std::ranges::all_of(key, [](unsigned char c) {
            return std::isalnum(c) || c == '_';
          }) ||
          std::isdigit(static_cast<unsigned char>(key.front()))) {
        continue;
      }
      extra += (extra.empty() ? "" : "\n") + kv;
    }
    if (!extra.empty()) {
      env.emplace_back("NOVA_COMPAT_ENV", extra);
    }
    return env;
  }

  std::optional<std::string> check_launch_target(const std::string &exe) {
    if (exe.empty()) {
      return std::nullopt;
    }
    std::error_code ec;
    if (std::filesystem::is_regular_file(exe, ec)) {
      return std::nullopt;
    }
    return "Game file not found: " + exe + ". Reinstall the game or update its path in Nova's Library.";
  }

  std::string wrapper_command(const std::string &wrapper, const std::string &exe) {
    if (exe.find('"') != std::string::npos || wrapper.find('"') != std::string::npos) {
      return {};
    }
    return "\"" + wrapper + "\" \"" + exe + "\"";
  }

  std::string wrapper_path() {
    return NOVA_PROTON_RUN;
  }
}  // namespace nova_compat
