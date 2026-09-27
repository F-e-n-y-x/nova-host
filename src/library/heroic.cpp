/**
 * @file src/library/heroic.cpp
 * @brief Definitions for reading the Heroic Games Launcher library.
 */
// standard includes
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <system_error>

// lib includes
#include <nlohmann/json.hpp>

// local includes
#include "heroic.h"

namespace fs = std::filesystem;

namespace library::heroic {
  namespace {
    /**
     * @brief Percent-encode a URL query value.
     *
     * @param s Value.
     * @return Encoded value.
     */
    std::string encode(std::string_view s) {
      static constexpr char hex[] = "0123456789ABCDEF";
      std::string out;
      for (const unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
          out += static_cast<char>(c);
        } else {
          out += '%';
          out += hex[c >> 4];
          out += hex[c & 0xF];
        }
      }
      return out;
    }

    /**
     * @brief Turn an art field into an artwork reference.
     *
     * @param kind Artwork kind.
     * @param value URL or local path from Heroic.
     * @param out Receives the reference when usable.
     */
    void add_art(art_kind_e kind, const std::string &value, std::vector<art_ref_t> &out) {
      if (value.starts_with("https://")) {
        out.push_back({kind, value, {}, "Heroic"});
        return;
      }
      auto local = value.starts_with("file://") ? value.substr(7) : value;
      std::error_code ec;
      if (!local.empty() && fs::path(local).is_absolute() && fs::is_regular_file(local, ec)) {
        out.push_back({kind, {}, local, "Heroic"});
      }
    }

    /**
     * @brief Read a string member.
     *
     * @param obj JSON object.
     * @param key Member name.
     * @return The string, or empty.
     */
    std::string str(const nlohmann::json &obj, const char *key) {
      const auto it = obj.find(key);
      return it != obj.end() && it->is_string() ? it->get<std::string>() : std::string {};
    }
  }  // namespace

  std::string launch_url(std::string_view app_name, std::string_view runner) {
    return "heroic://launch?appName=" + encode(app_name) + "&runner=" + encode(runner);
  }

  scan_result_t parse_libraries(const std::vector<std::string> &documents, const std::string &launcher) {
    scan_result_t result;
    std::set<std::string> seen;

    std::function<void(const nlohmann::json &, int)> visit = [&](const nlohmann::json &node, int depth) {
      if (depth > 8) {
        return;
      }
      if (node.is_array()) {
        for (const auto &child : node) {
          visit(child, depth + 1);
        }
        return;
      }
      if (!node.is_object()) {
        return;
      }
      const auto app_name = str(node, "app_name");
      const auto runner = str(node, "runner");
      const auto title = str(node, "title");
      if (app_name.empty() || runner.empty() || title.empty()) {
        for (const auto &[key, child] : node.items()) {
          visit(child, depth + 1);
        }
        return;
      }
      if (!node.value("is_installed", false) || !seen.insert(runner + "/" + app_name).second) {
        return;
      }
      if (node.value("is_dlc", false)) {
        return;
      }

      detected_game_t game;
      game.title = title;
      game.source = source_e::heroic;
      game.source_id = runner + "/" + app_name;
      game.launch_cmd = launcher + " \"" + launch_url(app_name, runner) + "\"";
      if (const auto it = node.find("install"); it != node.end() && it->is_object()) {
        game.working_dir = str(*it, "install_path");
      }
      add_art(art_kind_e::poster, str(node, "art_square"), game.artwork);
      add_art(art_kind_e::hero, str(node, "art_background"), game.artwork);
      add_art(art_kind_e::hero, str(node, "art_cover"), game.artwork);
      add_art(art_kind_e::logo, str(node, "art_logo"), game.artwork);
      add_art(art_kind_e::icon, str(node, "art_icon"), game.artwork);
      result.games.push_back(std::move(game));
    };

    for (const auto &doc : documents) {
      const auto json = nlohmann::json::parse(doc, nullptr, false);
      if (!json.is_discarded()) {
        visit(json, 0);
      }
    }
    return result;
  }

  scan_result_t scan(const fs::path &home) {
    std::error_code ec;
    struct install_t {
      fs::path config;  ///< Heroic config folder.
      std::string launcher;  ///< Command that starts Heroic.
    };
    std::vector<install_t> installs;
    if (fs::is_directory(home / ".config/heroic", ec)) {
      std::string launcher = "heroic";
      for (const auto *candidate : {"/usr/bin/heroic", "/opt/Heroic/heroic", "/usr/local/bin/heroic"}) {
        if (fs::exists(candidate, ec)) {
          launcher = candidate;
          break;
        }
      }
      installs.push_back({home / ".config/heroic", launcher});
    }
    if (fs::is_directory(home / ".var/app/com.heroicgameslauncher.hgl/config/heroic", ec)) {
      installs.push_back({home / ".var/app/com.heroicgameslauncher.hgl/config/heroic", "flatpak run com.heroicgameslauncher.hgl"});
    }

    scan_result_t result;
    if (installs.empty()) {
      result.skipped.push_back({(home / ".config/heroic").string(), "Heroic isn't installed."});
      return result;
    }
    for (const auto &install : installs) {
      std::vector<std::string> documents;
      for (const auto *rel : {"store_cache/legendary_library.json", "store_cache/gog_library.json", "store_cache/nile_library.json",
                              "sideload_apps/library.json"}) {
        std::ifstream in(install.config / rel);
        if (!in) {
          continue;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        documents.push_back(ss.str());
      }
      auto part = parse_libraries(documents, install.launcher);
      result.games.insert(result.games.end(), part.games.begin(), part.games.end());
      result.skipped.insert(result.skipped.end(), part.skipped.begin(), part.skipped.end());
    }
    return result;
  }

}  // namespace library::heroic
