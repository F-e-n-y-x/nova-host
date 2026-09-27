/**
 * @file src/library/heroic.h
 * @brief Declarations for reading the Heroic Games Launcher library (Epic, GOG, Amazon, sideloaded).
 */
#pragma once

// standard includes
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// local includes
#include "library_types.h"

namespace library::heroic {

  /**
   * @brief Collect installed games from Heroic's library JSON files.
   *
   * Accepts any of Heroic's electron-store files; entries are the objects that carry
   * "app_name", "runner" and "title". Only installed games are returned.
   *
   * @param documents Parsed JSON text of each library file.
   * @param launcher Command that starts Heroic, e.g. "heroic" or "flatpak run com.heroicgameslauncher.hgl".
   * @return Games and skipped entries.
   */
  scan_result_t parse_libraries(const std::vector<std::string> &documents, const std::string &launcher);

  /**
   * @brief Build Heroic's launch URL for a game.
   *
   * @param app_name Heroic app name.
   * @param runner Runner, e.g. "legendary", "gog", "nile" or "sideload".
   * @return URL such as "heroic://launch?appName=Quail&runner=legendary".
   */
  std::string launch_url(std::string_view app_name, std::string_view runner);

  /**
   * @brief Read Heroic's library on this machine.
   *
   * @param home Home directory of the user running Nova.
   * @return Games and skipped entries (a skipped entry explains when Heroic isn't installed).
   */
  scan_result_t scan(const std::filesystem::path &home);

}  // namespace library::heroic
