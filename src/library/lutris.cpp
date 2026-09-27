/**
 * @file src/library/lutris.cpp
 * @brief Definitions for reading the Lutris game library.
 */
// standard includes
#include <algorithm>
#include <chrono>
#include <fstream>
#include <random>
#include <sstream>
#include <system_error>
#include <thread>

// lib includes
#include <nlohmann/json.hpp>

// local includes
#include "folder_scan.h"
#include "lutris.h"
#include "src/boost_process_compat.h"

namespace fs = std::filesystem;
namespace bp = boost::process::v1;

namespace library::lutris {
  namespace {
    /**
     * @brief First existing file among "<dir>/<stem><ext>" for the given extensions.
     *
     * @param dir Folder.
     * @param stem File name without extension.
     * @return The path, or empty.
     */
    fs::path first_existing(const fs::path &dir, const std::string &stem) {
      std::error_code ec;
      for (const auto *ext : {".jpg", ".png", ".jpeg", ".webp"}) {
        if (auto p = dir / (stem + ext); fs::is_regular_file(p, ec)) {
          return p;
        }
      }
      return {};
    }

    /**
     * @brief Whether a Lutris slug is safe to use in file names.
     *
     * @param slug Slug from Lutris.
     * @return True for [a-z0-9-] slugs.
     */
    bool safe_slug(const std::string &slug) {
      return !slug.empty() && slug.size() < 200 && std::ranges::all_of(slug, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
      });
    }
  }  // namespace

  scan_result_t parse_list(std::string_view text, const std::string &launcher, const fs::path &data_dir, const fs::path &icons_dir) {
    scan_result_t result;
    const auto start = text.find('[');
    if (start == std::string_view::npos) {
      return result;
    }
    nlohmann::json list = nlohmann::json::parse(text.substr(start), nullptr, false);
    if (!list.is_array()) {
      result.skipped.push_back({"lutris", "Lutris returned output Nova couldn't read."});
      return result;
    }
    for (const auto &item : list) {
      if (!item.is_object() || !item.contains("id") || !item["id"].is_number_integer()) {
        continue;
      }
      const auto id = item["id"].get<long long>();
      const auto name = item.value("name", std::string {});
      const auto slug = item.value("slug", std::string {});
      const auto directory = item.contains("directory") && item["directory"].is_string() ? item["directory"].get<std::string>() : std::string {};
      if (id <= 0 || name.empty()) {
        continue;
      }
      std::error_code ec;
      if (!directory.empty() && fs::is_directory(directory, ec) && folder::looks_like_installer(directory)) {
        result.skipped.push_back({directory, "The Lutris entry \"" + name + "\" points at an installer folder, not an installed game."});
        continue;
      }

      detected_game_t game;
      game.title = name;
      game.source = source_e::lutris;
      game.source_id = std::to_string(id);
      game.launch_cmd = launcher + " lutris:rungameid/" + std::to_string(id);
      game.working_dir = directory;

      if (item.contains("coverPath") && item["coverPath"].is_string()) {
        if (fs::path cover = item["coverPath"].get<std::string>(); fs::is_regular_file(cover, ec)) {
          game.artwork.push_back({art_kind_e::poster, {}, cover, "Lutris"});
        }
      }
      if (safe_slug(slug)) {
        if (auto cover = first_existing(data_dir / "coverart", slug); !cover.empty()) {
          game.artwork.push_back({art_kind_e::poster, {}, cover, "Lutris"});
        }
        if (auto banner = first_existing(data_dir / "banners", slug); !banner.empty()) {
          game.artwork.push_back({art_kind_e::hero, {}, banner, "Lutris"});
        }
        if (auto icon = icons_dir / ("lutris_" + slug + ".png"); fs::is_regular_file(icon, ec)) {
          game.artwork.push_back({art_kind_e::icon, {}, icon, "Lutris"});
        }
      }
      result.games.push_back(std::move(game));
    }
    return result;
  }

  scan_result_t scan(const fs::path &home) {
    std::error_code ec;
    std::vector<std::string> command;
    std::string launcher;
    fs::path data_dir = home / ".local/share/lutris";
    fs::path icons_dir = home / ".local/share/icons/hicolor/128x128/apps";

    for (const auto *candidate : {"/usr/games/lutris", "/usr/bin/lutris", "/usr/local/bin/lutris"}) {
      if (fs::exists(candidate, ec)) {
        launcher = candidate;
        command = {candidate};
        break;
      }
    }
    if (launcher.empty() && fs::is_directory(home / ".var/app/net.lutris.Lutris", ec)) {
      launcher = "flatpak run net.lutris.Lutris";
      command = {"/usr/bin/flatpak", "run", "net.lutris.Lutris"};
      data_dir = home / ".var/app/net.lutris.Lutris/data/lutris";
      icons_dir = home / ".var/app/net.lutris.Lutris/data/icons/hicolor/128x128/apps";
    }
    if (launcher.empty()) {
      scan_result_t result;
      result.skipped.push_back({"lutris", "Lutris isn't installed."});
      return result;
    }

    std::random_device rd;
    const auto out_file = fs::temp_directory_path(ec) / ("nova-lutris-" + std::to_string(rd()) + ".json");
    std::string output;
    try {
      std::vector<std::string> args(command.begin() + 1, command.end());
      args.insert(args.end(), {"--list-games", "--installed", "--json"});
      bp::child child(command.front(), bp::args(args), bp::env["HOME"] = home.string(), bp::std_out > out_file.string(), bp::std_err > bp::null, bp::std_in < bp::null);
      // Poll instead of wait_for(), which Boost marks unreliable; Lutris normally answers in 1-3 s.
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
      while (child.running() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      if (child.running()) {
        child.terminate();
      }
      child.wait();
      std::ifstream in(out_file);
      std::ostringstream ss;
      ss << in.rdbuf();
      output = ss.str();
    } catch (const std::exception &) {
      output.clear();
    }
    fs::remove(out_file, ec);

    auto result = parse_list(output, launcher, data_dir, icons_dir);
    if (output.empty()) {
      result.skipped.push_back({"lutris", "Lutris didn't answer."});
    }
    return result;
  }

}  // namespace library::lutris
