/**
 * @file src/secure_files.h
 * @brief Owner-only permissions for Nova's config, state, credential and certificate files.
 */
#pragma once

// standard includes
#include <filesystem>
#include <string_view>
#include <vector>

namespace secure_files {
  /**
   * @brief Write a file readable and writable by its owner only (0600), replacing it atomically.
   *
   * The data goes to a sibling temp file created with mode 0600 and is renamed over @p path, so a
   * crash never leaves a truncated file and there is no window in which the file is world-readable.
   *
   * @param path Destination file.
   * @param contents Bytes to write.
   * @return True on success.
   */
  bool write_private(const std::filesystem::path &path, std::string_view contents);

  /**
   * @brief Restrict an existing file to 0600, or a directory to 0700.
   *
   * Symlinks are not followed and files owned by someone else are left alone.
   *
   * @param path File or directory; a missing path is not an error.
   * @return True when the path is missing or now has owner-only permissions.
   */
  bool restrict(const std::filesystem::path &path);

  /**
   * @brief Whether a path grants any permission to group or others.
   * @param path File or directory.
   * @return True when group/other bits are set; false for missing paths.
   */
  bool is_exposed(const std::filesystem::path &path);

  /**
   * @brief The paths Nova keeps private: the config directory, config file, credentials,
   *        state (paired devices), apps (host commands), certificates and logs.
   * @return Paths in the order they are restricted (directories first).
   */
  std::vector<std::filesystem::path> private_paths();

  /**
   * @brief Restrict every path from private_paths(); logs each change once.
   * @return Number of paths that could not be restricted.
   */
  int harden_all();
}  // namespace secure_files
