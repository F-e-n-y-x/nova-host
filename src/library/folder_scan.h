/**
 * @file src/library/folder_scan.h
 * @brief Declarations for detecting games inside a folder the user picked.
 */
#pragma once

// standard includes
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

// local includes
#include "library_types.h"

namespace library::folder {

  /**
   * @brief Options for @ref scan.
   */
  struct options_t {
    int max_depth = 4;  ///< How deep to look for executables below each game folder.
    int max_container_depth = 2;  ///< How many levels of plain folders to descend to find game folders.
    std::size_t max_games = 500;  ///< Stop after this many games.
    std::size_t max_entries_per_game = 20000;  ///< Directory entries visited per game folder before giving up.
    std::string windows_launcher;  ///< Command template for Windows games, "{exe}" is replaced; empty runs the exe directly.
  };

  /**
   * @brief Find games below a folder.
   *
   * Each child folder is treated as one game; folders without executables are descended into
   * (for layouts like "Games/Epic/<game>"). Installers (Setup.exe plus data files) and folders
   * without a launchable executable are reported as skipped.
   *
   * @param root Folder to scan.
   * @param options Scan limits and launch settings.
   * @return Detected games and skipped entries.
   */
  scan_result_t scan(const std::filesystem::path &root, const options_t &options);

  /**
   * @brief Whether an executable name is a helper that is never the game.
   *
   * @param filename File name, e.g. "unins000.exe" or "UnityCrashHandler64.exe".
   * @return True for installers, uninstallers, redistributables, crash reporters and anti-cheat helpers.
   */
  bool is_excluded_exe(std::string_view filename);

  /**
   * @brief Whether a folder only holds redistributables or tools and should not be searched.
   *
   * @param dirname Folder name, e.g. "_CommonRedist".
   * @return True for redistributable, installer, anti-cheat and support folders.
   */
  bool is_excluded_dir(std::string_view dirname);

  /**
   * @brief Whether a folder is a game installer rather than an installed game.
   *
   * @param dir Folder to inspect (top level only).
   * @return True when it has a setup executable next to archive or data files and no other program.
   */
  bool looks_like_installer(const std::filesystem::path &dir);

  /**
   * @brief Score how likely an executable is the game's main program.
   *
   * @param game_dir Game folder.
   * @param exe Executable inside it.
   * @param size_bytes File size.
   * @return Higher is more likely; negative means "not the game".
   */
  double score_windows_exe(const std::filesystem::path &game_dir, const std::filesystem::path &exe, std::uintmax_t size_bytes);

  /**
   * @brief Quote a path for a Nova app command.
   *
   * Wraps it in double quotes and escapes "$" as "$$" so Nova's $(VAR) expansion leaves it alone.
   *
   * @param path Path to quote.
   * @return Quoted argument, or an empty string when the path contains a double quote.
   */
  std::string quote(const std::filesystem::path &path);

  /**
   * @brief Build the launch command for a Windows executable.
   *
   * @param launcher Template such as "/usr/local/bin/run-windows-exe {exe}"; "{exe}" is replaced by the quoted
   *                 path, otherwise the path is appended. Empty runs the executable directly.
   * @param exe Executable to launch.
   * @return Command line, or empty when the path cannot be quoted safely.
   */
  std::string windows_command(std::string_view launcher, const std::filesystem::path &exe);

}  // namespace library::folder
