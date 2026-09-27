/**
 * @file src/library/library_types.h
 * @brief Shared types for game library detection and import.
 */
#pragma once

// standard includes
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace library {

  /**
   * @brief Kind of artwork a game can have.
   */
  enum class art_kind_e {
    poster,  ///< Portrait 2:3 box art, used as the app's cover in Moonlight.
    hero,  ///< Wide banner shown behind the title.
    logo,  ///< Transparent title logo.
    icon,  ///< Small square icon for lists and cards.
  };

  /**
   * @brief Where a detected game came from.
   */
  enum class source_e {
    folder,  ///< Found by scanning a folder the user picked.
    lutris,  ///< Lutris library.
    steam,  ///< Steam library folders.
    heroic,  ///< Heroic Games Launcher (Epic, GOG, Amazon, sideloaded).
  };

  /**
   * @brief Convert an artwork kind to its API name.
   *
   * @param kind Artwork kind.
   * @return "poster", "hero", "logo" or "icon".
   */
  const char *to_string(art_kind_e kind);

  /**
   * @brief Convert a source to its API name.
   *
   * @param source Source.
   * @return "folder", "lutris", "steam" or "heroic".
   */
  const char *to_string(source_e source);

  /**
   * @brief One artwork option for a game: a remote URL on an allowed host or a local file.
   */
  struct art_ref_t {
    art_kind_e kind;  ///< What the image is for.
    std::string url;  ///< HTTPS URL; empty when @ref path is used.
    std::filesystem::path path;  ///< Local image found by a scanner; empty when @ref url is used.
    std::string label;  ///< Where it came from, e.g. "Steam", "SteamGridDB", "Lutris".
  };

  /**
   * @brief A game found by a scanner, before the user imports it.
   */
  struct detected_game_t {
    std::string title;  ///< Display title.
    source_e source = source_e::folder;  ///< Scanner that found it.
    std::string source_id;  ///< Stable id within the source (appid, Lutris id, exe path).
    std::string launch_cmd;  ///< Command Nova runs to start the game.
    std::string working_dir;  ///< Working directory for @ref launch_cmd.
    std::filesystem::path executable;  ///< Main executable, when known.
    std::uint32_t steam_appid = 0;  ///< Steam app id when known or matched, else 0.
    std::string matched_name;  ///< Store name the title was matched to.
    double match_confidence = 0.0;  ///< 0..1 confidence of @ref steam_appid.
    std::vector<art_ref_t> artwork;  ///< Artwork options, best first per kind.
  };

  /**
   * @brief Something a scanner looked at and deliberately left out.
   */
  struct skipped_t {
    std::string path;  ///< Folder or file.
    std::string reason;  ///< Human-readable reason, e.g. "Looks like an installer".
  };

  /**
   * @brief Result of one scanner run.
   */
  struct scan_result_t {
    std::vector<detected_game_t> games;  ///< Games found.
    std::vector<skipped_t> skipped;  ///< Entries left out, with reasons.
  };

}  // namespace library
