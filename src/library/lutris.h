/**
 * @file src/library/lutris.h
 * @brief Declarations for reading the Lutris game library.
 */
#pragma once

// standard includes
#include <filesystem>
#include <string>
#include <string_view>

// local includes
#include "library_types.h"

namespace library::lutris {

  /**
   * @brief Parse the output of `lutris --list-games --installed --json`.
   *
   * Log lines before the JSON array are ignored. Games whose folder is an installer are skipped.
   *
   * @param text Command output.
   * @param launcher Command that starts Lutris, e.g. "/usr/games/lutris" or "flatpak run net.lutris.Lutris".
   * @param data_dir Lutris data folder (holds coverart/ and banners/).
   * @param icons_dir Folder holding "lutris_<slug>.png" icons.
   * @return Games and skipped entries.
   */
  scan_result_t parse_list(std::string_view text, const std::string &launcher, const std::filesystem::path &data_dir, const std::filesystem::path &icons_dir);

  /**
   * @brief Run the Lutris CLI and read its installed games.
   *
   * @param home Home directory of the user running Nova.
   * @return Games and skipped entries (a skipped entry explains when Lutris isn't installed).
   */
  scan_result_t scan(const std::filesystem::path &home);

}  // namespace library::lutris
