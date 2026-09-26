/**
 * @file src/config_migration.cpp
 * @brief Definitions for one-time adoption of settings left by the pre-rename host.
 */
// local includes
#include "config_migration.h"

namespace fs = std::filesystem;

namespace config_migration {
  namespace {
    /**
     * @brief Return the legacy config file inside @p dir, if any.
     *
     * @param dir Directory to look in.
     * @return Path of the first existing legacy config file, or an empty path.
     */
    fs::path find_legacy_file(const fs::path &dir) {
      std::error_code ec;
      for (const auto &name : legacy_config_names) {
        if (auto candidate = dir / name; fs::is_regular_file(candidate, ec)) {
          return candidate;
        }
      }
      return {};
    }
  }  // namespace

  result_t migrate_directory(const fs::path &new_dir, const std::vector<fs::path> &candidates, const std::string &new_config_name) {
    result_t result;
    std::error_code ec;
    if (fs::exists(new_dir, ec)) {
      return result;
    }

    for (const auto &dir : candidates) {
      if (dir == new_dir) {
        continue;
      }
      const auto legacy_file = find_legacy_file(dir);
      if (legacy_file.empty()) {
        continue;
      }

      result.source = dir;
      auto staging = new_dir;
      staging += ".migrating";
      fs::remove_all(staging, ec);
      ec.clear();

      fs::create_directories(staging, ec);
      for (fs::directory_iterator it {dir, ec}, end; !ec && it != end; it.increment(ec)) {
        const auto &entry = it->path();
        if (entry.extension() == ".log") {
          continue;
        }
        const auto target = entry == legacy_file ? staging / new_config_name : staging / entry.filename();
        fs::copy(entry, target, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
        if (ec) {
          break;
        }
      }
      if (!ec) {
        fs::create_directories(new_dir.parent_path(), ec);
      }
      if (!ec) {
        fs::rename(staging, new_dir, ec);
      }
      if (ec) {
        result.error = ec.message();
        std::error_code cleanup;
        fs::remove_all(staging, cleanup);
        return result;
      }

      result.migrated = true;
      return result;
    }
    return result;
  }

  result_t adopt_legacy_file(const fs::path &config_file) {
    result_t result;
    std::error_code ec;
    if (fs::exists(config_file, ec)) {
      return result;
    }

    const auto legacy_file = find_legacy_file(config_file.parent_path());
    if (legacy_file.empty() || legacy_file == config_file) {
      return result;
    }

    result.source = legacy_file;
    fs::copy_file(legacy_file, config_file, ec);
    if (ec) {
      result.error = ec.message();
      return result;
    }
    result.migrated = true;
    return result;
  }
}  // namespace config_migration
