/**
 * @file src/library/steam.h
 * @brief Declarations for reading installed Steam games and Steam artwork URLs.
 */
#pragma once

// standard includes
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// local includes
#include "library_types.h"

namespace library::steam {

  /**
   * @brief A node of Valve's text KeyValues (VDF/ACF) format.
   */
  struct vdf_node_t {
    std::string value;  ///< Value for leaf keys; empty for sections.
    std::map<std::string, vdf_node_t, std::less<>> children;  ///< Sub-keys for sections (keys lowercased).

    /**
     * @brief Look up a child by (case-insensitive) key.
     *
     * @param key Key to find.
     * @return The child, or nullptr.
     */
    const vdf_node_t *find(std::string_view key) const;

    /**
     * @brief Get a child's value.
     *
     * @param key Key to find.
     * @return The value, or an empty string.
     */
    std::string get(std::string_view key) const;
  };

  /**
   * @brief Parse text KeyValues.
   *
   * @param text File contents.
   * @return Root node (its children are the top-level keys), or nullopt on malformed input.
   */
  std::optional<vdf_node_t> parse_vdf(std::string_view text);

  /**
   * @brief An installed Steam app read from an appmanifest.
   */
  struct app_t {
    std::uint32_t appid = 0;  ///< Steam app id.
    std::string name;  ///< Store name.
    std::filesystem::path install_dir;  ///< Game folder.
    bool fully_installed = false;  ///< StateFlags has the "fully installed" bit.
  };

  /**
   * @brief Parse one appmanifest_<id>.acf.
   *
   * @param text File contents.
   * @param library_path Library root holding "steamapps".
   * @return The app, or nullopt when the manifest is malformed.
   */
  std::optional<app_t> parse_app_manifest(std::string_view text, const std::filesystem::path &library_path);

  /**
   * @brief Library folder paths listed in libraryfolders.vdf.
   *
   * @param text File contents.
   * @return Library roots.
   */
  std::vector<std::filesystem::path> parse_library_folders(std::string_view text);

  /**
   * @brief Whether an app is a Steam tool (Proton, runtimes, redistributables) rather than a game.
   *
   * @param app App to check.
   * @return True for tools.
   */
  bool is_tool(const app_t &app);

  /**
   * @brief Steam installation roots that exist on this machine (native and Flatpak).
   *
   * @param home Home directory.
   * @return Roots that contain a "steamapps" folder.
   */
  std::vector<std::filesystem::path> find_roots(const std::filesystem::path &home);

  /**
   * @brief Scan all Steam libraries for installed games.
   *
   * @param home Home directory.
   * @return Games (with launch command and artwork) and skipped entries.
   */
  scan_result_t scan(const std::filesystem::path &home);

  /**
   * @brief Artwork on Steam's public CDN for an app.
   *
   * @param appid Steam app id.
   * @return Poster (2x then 1x), hero, logo and header (used as a wide card) references.
   */
  std::vector<art_ref_t> cdn_artwork(std::uint32_t appid);

  /**
   * @brief Steam's local library cache images for an app, when present.
   *
   * @param root Steam root.
   * @param appid Steam app id.
   * @return References to cached images that exist.
   */
  std::vector<art_ref_t> cached_artwork(const std::filesystem::path &root, std::uint32_t appid);

}  // namespace library::steam
