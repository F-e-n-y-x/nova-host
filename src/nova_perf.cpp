/**
 * @file src/nova_perf.cpp
 * @brief Per-app performance profiles ("nova-perf" in apps.json).
 */
// standard includes
#include <algorithm>
#include <cstdlib>
#include <format>
#include <fstream>
#include <stdexcept>
#include <system_error>

// local includes
#include "nova_compat.h"
#include "nova_perf.h"

using namespace std::literals;

namespace nova_perf {
  namespace {
    constexpr auto KEY = "nova-perf";
    constexpr auto COMPAT_KEY = "nova-compat";

    /** Lenient integer: numbers (and numeric strings, from hand edits) or @p fallback. */
    int int_or(const nlohmann::json &node, const char *key, int fallback) {
      const auto it = node.find(key);
      if (it == node.end()) {
        return fallback;
      }
      if (it->is_number()) {
        return static_cast<int>(std::clamp<double>(it->get<double>(), -1e6, 1e6));
      }
      if (it->is_string()) {
        try {
          return std::stoi(it->get<std::string>());
        } catch (const std::exception &) {
          return fallback;
        }
      }
      return fallback;
    }

    bool bool_or(const nlohmann::json &node, const char *key, bool fallback) {
      const auto it = node.find(key);
      return it != node.end() && it->is_boolean() ? it->get<bool>() : fallback;
    }

    std::string str(const nlohmann::json &app, const char *key) {
      const auto it = app.find(key);
      return it != app.end() && it->is_string() ? it->get<std::string>() : std::string {};
    }

    /** Strict integer for API updates: a whole number within [lo, hi]. */
    int strict_int(const std::string &key, const nlohmann::json &value, int lo, int hi) {
      if (!value.is_number_integer() && !(value.is_number_float() && value.get<double>() == static_cast<double>(static_cast<long long>(value.get<double>())))) {
        throw std::invalid_argument(key + " must be a whole number");
      }
      const auto v = value.get<long long>();
      if (v < lo || v > hi) {
        throw std::invalid_argument(std::format("{} must be between {} and {}", key, lo, hi));
      }
      return static_cast<int>(v);
    }

    bool strict_bool(const std::string &key, const nlohmann::json &value) {
      if (!value.is_boolean()) {
        throw std::invalid_argument(key + " must be true or false");
      }
      return value.get<bool>();
    }
  }  // namespace

  bool is_default(const profile_t &p) {
    return p.fps_cap <= 0 && p.fsr <= 0 && !p.vkbasalt && !p.mangohud && p.bitrate_kbps <= 0 && p.power == power_e::follow;
  }

  profile_t sanitize(profile_t p) {
    p.fps_cap = std::clamp(p.fps_cap, 0, MAX_FPS_CAP);
    p.fsr = std::clamp(p.fsr, 0, MAX_FSR);
    p.vkbasalt_cas = std::clamp(p.vkbasalt_cas, 0, 100);
    p.bitrate_kbps = p.bitrate_kbps <= 0 ? 0 : std::clamp(p.bitrate_kbps, MIN_BITRATE_KBPS, MAX_BITRATE_KBPS);
    return p;
  }

  std::string_view to_string(power_e p) {
    switch (p) {
      case power_e::performance:
        return "performance"sv;
      case power_e::balanced:
        return "balanced"sv;
      case power_e::follow:
        break;
    }
    return "default"sv;
  }

  std::optional<power_e> parse_power(std::string_view name) {
    if (name == "default"sv) {
      return power_e::follow;
    }
    if (name == "performance"sv) {
      return power_e::performance;
    }
    if (name == "balanced"sv) {
      return power_e::balanced;
    }
    return std::nullopt;
  }

  std::optional<bool> power_override(const profile_t &p) {
    switch (p.power) {
      case power_e::performance:
        return true;
      case power_e::balanced:
        return false;
      case power_e::follow:
        break;
    }
    return std::nullopt;
  }

  int cap_bitrate(int requested_kbps, int cap_kbps) {
    return cap_kbps > 0 && requested_kbps > cap_kbps ? cap_kbps : requested_kbps;
  }

  profile_t from_json(const nlohmann::json &node) {
    profile_t p;
    if (!node.is_object()) {
      return p;
    }
    p.fps_cap = int_or(node, "fps_cap", 0);
    p.fsr = int_or(node, "fsr", 0);
    p.vkbasalt = bool_or(node, "vkbasalt", false);
    p.vkbasalt_cas = int_or(node, "vkbasalt_cas", DEFAULT_CAS);
    p.mangohud = bool_or(node, "mangohud", false);
    p.bitrate_kbps = int_or(node, "bitrate_kbps", 0);
    p.power = parse_power(str(node, "power")).value_or(power_e::follow);
    return sanitize(p);
  }

  nlohmann::json to_json(const profile_t &p) {
    return {
      {"fps_cap", p.fps_cap},
      {"fsr", p.fsr},
      {"vkbasalt", p.vkbasalt},
      {"vkbasalt_cas", p.vkbasalt_cas},
      {"mangohud", p.mangohud},
      {"bitrate_kbps", p.bitrate_kbps},
      {"power", std::string {to_string(p.power)}},
    };
  }

  profile_t for_app(const nlohmann::json &app) {
    if (!app.is_object()) {
      return {};
    }
    if (const auto it = app.find(KEY); it != app.end() && it->is_object()) {
      return from_json(*it);
    }
    // Before "nova-perf", the GE-Proton options carried these three.
    if (const auto it = app.find(COMPAT_KEY); it != app.end() && it->is_object()) {
      profile_t p;
      p.fsr = int_or(*it, "fsr", 0);
      p.fps_cap = int_or(*it, "fps_cap", 0);
      p.mangohud = bool_or(*it, "mangohud", false);
      return sanitize(p);
    }
    return {};
  }

  void store(nlohmann::json &app, const profile_t &raw) {
    const auto p = sanitize(raw);
    if (const auto it = app.find(COMPAT_KEY); it != app.end() && it->is_object()) {
      it->erase("fsr");
      it->erase("fps_cap");
      it->erase("mangohud");
      if (it->empty()) {
        app.erase(COMPAT_KEY);
      }
    }
    if (is_default(p)) {
      app.erase(KEY);
      return;
    }
    nlohmann::json out = nlohmann::json::object();
    if (p.fps_cap > 0) {
      out["fps_cap"] = p.fps_cap;
    }
    if (p.fsr > 0) {
      out["fsr"] = p.fsr;
    }
    if (p.vkbasalt) {
      out["vkbasalt"] = true;
    }
    // The sharpness is kept even while vkBasalt is off, so turning it back on restores it.
    if (p.vkbasalt_cas != DEFAULT_CAS) {
      out["vkbasalt_cas"] = p.vkbasalt_cas;
    }
    if (p.mangohud) {
      out["mangohud"] = true;
    }
    if (p.bitrate_kbps > 0) {
      out["bitrate_kbps"] = p.bitrate_kbps;
    }
    if (p.power != power_e::follow) {
      out["power"] = std::string {to_string(p.power)};
    }
    app[KEY] = std::move(out);
  }

  profile_t apply_update(profile_t current, const nlohmann::json &update) {
    if (!update.is_object()) {
      throw std::invalid_argument("the profile must be a JSON object");
    }
    for (const auto &[key, value] : update.items()) {
      if (key == "fps_cap") {
        current.fps_cap = strict_int(key, value, 0, MAX_FPS_CAP);
      } else if (key == "fsr") {
        current.fsr = strict_int(key, value, 0, MAX_FSR);
      } else if (key == "vkbasalt") {
        current.vkbasalt = strict_bool(key, value);
      } else if (key == "vkbasalt_cas") {
        current.vkbasalt_cas = strict_int(key, value, 0, 100);
      } else if (key == "mangohud") {
        current.mangohud = strict_bool(key, value);
      } else if (key == "bitrate_kbps") {
        current.bitrate_kbps = strict_int(key, value, 0, MAX_BITRATE_KBPS);
        if (current.bitrate_kbps > 0 && current.bitrate_kbps < MIN_BITRATE_KBPS) {
          throw std::invalid_argument(std::format("bitrate_kbps must be 0 or between {} and {}", MIN_BITRATE_KBPS, MAX_BITRATE_KBPS));
        }
      } else if (key == "power") {
        const auto parsed = value.is_string() ? parse_power(value.get<std::string>()) : std::nullopt;
        if (!parsed) {
          throw std::invalid_argument("power must be \"default\", \"performance\" or \"balanced\"");
        }
        current.power = *parsed;
      } else {
        throw std::invalid_argument("unknown profile setting: " + key);
      }
    }
    return current;
  }

  profile_t update_app(nlohmann::json &app, std::string_view body) {
    nlohmann::json update;
    try {
      update = nlohmann::json::parse(body);
    } catch (const nlohmann::json::exception &) {
      throw std::invalid_argument("the body must be a JSON object");
    }
    const auto next = apply_update(for_app(app), update);
    store(app, next);
    return next;
  }

  launcher_e launcher_of(const nlohmann::json &app) {
    const auto cmd = str(app, "cmd");
    const auto lower = [&] {
      std::string s = cmd;
      std::ranges::transform(s, s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      return s;
    }();
    if (lower.find("lutris:") != std::string::npos || str(app, "nova-source") == "lutris") {
      return launcher_e::lutris;
    }
    if (lower.find("steam://") != std::string::npos) {
      return launcher_e::steam;
    }
    if (!str(app, "nova-exe").empty() || lower.find("nova-proton-run") != std::string::npos) {
      return launcher_e::proton;
    }
    if (cmd.find_first_not_of(" \t") == std::string::npos) {
      return launcher_e::none;
    }
    return launcher_e::command;
  }

  std::string_view to_string(launcher_e l) {
    switch (l) {
      case launcher_e::command:
        return "command"sv;
      case launcher_e::proton:
        return "proton"sv;
      case launcher_e::steam:
        return "steam"sv;
      case launcher_e::lutris:
        return "lutris"sv;
      case launcher_e::none:
        return "none"sv;
    }
    return "command"sv;
  }

  nlohmann::json api_reply(const nlohmann::json &app, bool can_edit) {
    const auto launcher = launcher_of(app);
    return {
      {"profile", to_json(for_app(app))},
      {"launcher", std::string {to_string(launcher)}},
      {"applies", launcher != launcher_e::steam && launcher != launcher_e::none},
      {"can_edit", can_edit},
      {"limits", {
        {"fps_cap", {0, MAX_FPS_CAP}},
        {"fsr", {0, MAX_FSR}},
        {"vkbasalt_cas", {0, 100}},
        {"bitrate_kbps", {MIN_BITRATE_KBPS, MAX_BITRATE_KBPS}},
      }},
    };
  }

  std::string vkbasalt_config(const profile_t &p) {
    const auto cas = std::clamp(p.vkbasalt_cas, 0, 100);
    return std::format(
      "# Written by Nova for this game's performance profile; changes are overwritten at launch.\n"
      "effects = cas\n"
      "casSharpness = {}.{:02}\n",
      cas / 100,
      cas % 100
    );
  }

  std::optional<std::filesystem::path> write_vkbasalt_config(const std::filesystem::path &dir, std::string_view slug, const profile_t &p) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
      return std::nullopt;
    }
    const auto file = dir / (nova_compat::slugify(slug) + ".conf");
    const auto tmp = file.string() + ".tmp";
    {
      std::ofstream out(tmp, std::ios::trunc);
      if (!out) {
        return std::nullopt;
      }
      out << vkbasalt_config(p);
      if (!out.flush()) {
        return std::nullopt;
      }
    }
    std::filesystem::rename(tmp, file, ec);
    if (ec) {
      std::filesystem::remove(tmp, ec);
      return std::nullopt;
    }
    return file;
  }

  std::filesystem::path vkbasalt_dir() {
    if (const char *xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg == '/') {
      return std::filesystem::path(xdg) / "nova" / "vkbasalt";
    }
    const char *home = std::getenv("HOME");
    return std::filesystem::path(home && *home ? home : "/tmp") / ".cache" / "nova" / "vkbasalt";
  }

  int combined_cap(int profile_cap, int outer_cap) {
    if (profile_cap <= 0) {
      return std::max(0, outer_cap);
    }
    return outer_cap > 0 ? std::min(profile_cap, outer_cap) : profile_cap;
  }

  env_t build_env(const profile_t &raw, launcher_e launcher, const std::optional<std::filesystem::path> &vkbasalt_config, int outer_cap) {
    const auto p = sanitize(raw);
    env_t env;
    // The game's own cap, lowered to a cap already in force (a virtual display has no vblank and
    // sets one to the stream's rate); without a game cap the outer one is left as it is.
    const int cap = p.fps_cap > 0 ? combined_cap(p.fps_cap, outer_cap) : 0;
    if (cap > 0) {
      const auto fps = std::to_string(cap);
      // DXVK (D3D8-11) and VKD3D-Proton (D3D12) cap inside the translation layer.
      env.emplace_back("DXVK_FRAME_RATE", fps);
      env.emplace_back("VKD3D_FRAME_RATE", fps);
    }
    if (p.fsr > 0) {
      env.emplace_back("WINE_FULLSCREEN_FSR", "1");
      env.emplace_back("WINE_FULLSCREEN_FSR_STRENGTH", std::to_string(p.fsr - 1));
    }
    if (p.vkbasalt && vkbasalt_config) {
      env.emplace_back("ENABLE_VKBASALT", "1");
      env.emplace_back("VKBASALT_CONFIG_FILE", vkbasalt_config->string());
    }
    // Native Vulkan games have no DXVK, so MangoHud's layer caps them (hidden when the overlay is
    // off). Proton games are capped by DXVK alone: two limiters on one frame fight each other.
    // read_cfg keeps the user's MangoHud.conf when the overlay is shown. With an outer cap the
    // overlay keeps that limit instead of the outer "no_display" config hiding it.
    const bool mango_cap = cap > 0 && launcher != launcher_e::proton;
    const int overlay_limit = mango_cap ? cap : combined_cap(0, outer_cap);
    if (p.mangohud || mango_cap) {
      env.emplace_back("MANGOHUD", "1");
      if (mango_cap || (p.mangohud && overlay_limit > 0)) {
        env.emplace_back("MANGOHUD_CONFIG", std::format("{},fps_limit={}", p.mangohud ? "read_cfg" : "no_display", overlay_limit));
      }
    }
    return env;
  }
}  // namespace nova_perf
