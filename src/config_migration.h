/**
 * @file src/config_migration.h
 * @brief One-time adoption of settings left by the pre-rename host (Zenith, Sunshine).
 */
#pragma once

// standard includes
#include <filesystem>
#include <string>
#include <vector>

namespace config_migration {
  /**
   * @brief Config file names an earlier build used, most specific first.
   */
  inline const std::vector<std::string> legacy_config_names {"zenith.conf", "sunshine.conf"};

  /**
   * @brief Outcome of a migration attempt.
   */
  struct result_t {
    bool migrated = false;  ///< True when settings were copied into the new location.
    std::filesystem::path source;  ///< Directory or file the settings were copied from.
    std::string error;  ///< Why a migration that was needed did not happen; empty on success or no-op.
  };

  /**
   * @brief Copy a legacy config directory to a new one the first time the new one is needed.
   *
   * Does nothing when @p new_dir already exists or no candidate holds a legacy config file.
   * The first candidate containing one of @ref legacy_config_names is copied (never moved, so
   * the old install keeps working) into a staging directory that is renamed into place only when
   * the copy is complete; the legacy config file is renamed to @p new_config_name and old
   * `*.log` files are left behind.
   *
   * @param new_dir Directory the renamed host reads its settings from.
   * @param candidates Legacy directories, in order of preference.
   * @param new_config_name File name the renamed host reads (for example `nova-host.conf`).
   * @return What happened.
   */
  result_t migrate_directory(const std::filesystem::path &new_dir, const std::vector<std::filesystem::path> &candidates, const std::string &new_config_name);

  /**
   * @brief Copy a legacy config file next to @p config_file when the directory stayed the same.
   *
   * Used where the settings directory did not change with the rename (for example Windows, which
   * keeps it beside the executable), so only the file name did. Does nothing when @p config_file
   * exists or no legacy file is found in its directory.
   *
   * @param config_file Config file the renamed host is about to read.
   * @return What happened.
   */
  result_t adopt_legacy_file(const std::filesystem::path &config_file);
}  // namespace config_migration
