/**
 * @file src/library/metadata.h
 * @brief Game details and artwork source settings, plus the IGDB and RAWG lookups used for non-Steam games.
 */
#pragma once

// standard includes
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// lib includes
#include <nlohmann/json.hpp>

// local includes
#include "artwork.h"
#include "library_types.h"

namespace library::metadata {

  /**
   * @brief Artwork source names understood by `art_source_priority`.
   */
  inline constexpr std::array<std::string_view, 4> known_art_sources {"steam", "steamgriddb", "lutris", "igdb"};

  /**
   * @brief Resolved metadata and artwork settings (from the `metadata_*`, `art_*` and key config options).
   */
  struct settings_t {
    bool steam = true;  ///< Use the Steam store for details.
    std::string language = "english";  ///< Steam store language (`l=` value).
    bool auto_fetch = true;  ///< Fetch automatically on import and on first request.
    int ttl_days = 30;  ///< Days before cached details are fetched again.
    std::vector<std::string> art_priority {"steam", "steamgriddb", "lutris", "igdb"};  ///< Artwork sources, best first; others are not used.
    bool prefer_official = true;  ///< Rank Steam artwork first regardless of @ref art_priority.
    artwork::sgdb_options_t sgdb;  ///< SteamGridDB style/content filters.
    std::string steamgriddb_api_key;  ///< SteamGridDB API key.
    std::string igdb_client_id;  ///< Twitch client id for IGDB.
    std::string igdb_client_secret;  ///< Twitch client secret for IGDB.
    std::string rawg_api_key;  ///< RAWG API key.
    std::string steam_web_api_key;  ///< Steam Web API key (full Steam catalogue for matching).

    /**
     * @brief Whether IGDB lookups are possible.
     *
     * @return True when both Twitch credentials are set.
     */
    bool igdb_enabled() const {
      return !igdb_client_id.empty() && !igdb_client_secret.empty();
    }

    /**
     * @brief Whether an artwork source is enabled by the priority list.
     *
     * @param source Source name (see @ref known_art_sources).
     * @return True when listed.
     */
    bool art_source_enabled(std::string_view source) const;
  };

  /**
   * @brief Settings from the loaded Nova config.
   *
   * @return Resolved settings.
   */
  settings_t from_config();

  /**
   * @brief Parse a comma-separated artwork source list.
   *
   * Names are trimmed and lower-cased; unknown names and repeats are dropped.
   *
   * @param csv e.g. "steam, SteamGridDB,igdb".
   * @return Known sources in order.
   */
  std::vector<std::string> parse_priority(std::string_view csv);

  /**
   * @brief Source name of an artwork reference, from its label ("Steam" → "steam").
   *
   * @param ref Artwork reference.
   * @return Source name, or empty for uploads and unknown labels.
   */
  std::string art_source_of(const art_ref_t &ref);

  /**
   * @brief Order artwork by source priority and drop sources the settings disable.
   *
   * Uploaded or unlabelled artwork is kept. The order within one source is preserved,
   * so the first reference of each kind is the automatic choice.
   *
   * @param refs Artwork references (modified in place).
   * @param settings Settings.
   */
  void rank_artwork(std::vector<art_ref_t> &refs, const settings_t &settings);

  /**
   * @brief Whether a Steam store language code is valid (letters only, at most 20).
   *
   * @param language Value to check.
   * @return True when usable in the `l=` parameter.
   */
  bool valid_steam_language(std::string_view language);

  /**
   * @brief Human-readable date ("19 Aug, 2024") from Unix seconds, in UTC.
   *
   * @param unix_seconds Seconds since the epoch.
   * @return Formatted date, or empty for non-positive input.
   */
  std::string format_date(std::int64_t unix_seconds);

  /**
   * @brief A game found on IGDB.
   */
  struct igdb_game_t {
    std::uint64_t id = 0;  ///< IGDB id.
    std::string name;  ///< Title.
    std::string summary;  ///< Description.
    std::vector<std::string> genres;  ///< Genre names.
    std::int64_t first_release = 0;  ///< First release date (Unix seconds), 0 when unknown.
    std::string developer;  ///< First developer company.
    std::string publisher;  ///< First publisher company.
    std::string cover_image_id;  ///< Cover image id.
    std::uint32_t steam_appid = 0;  ///< Steam app id from IGDB's external games, 0 when unknown.
    std::vector<std::string> artwork_image_ids;  ///< Artwork (banner) image ids.
    std::vector<std::string> screenshot_image_ids;  ///< Screenshot image ids.
  };

  /**
   * @brief Parse a Twitch client-credentials token reply.
   *
   * @param json Response body.
   * @param expires_in Set to the token lifetime in seconds.
   * @return Access token, or nullopt.
   */
  std::optional<std::string> parse_twitch_token(std::string_view json, std::int64_t &expires_in);

  /**
   * @brief Parse an IGDB `/v4/games` reply.
   *
   * @param json Response body (array of games with expanded fields).
   * @return Games; malformed entries are skipped.
   */
  std::vector<igdb_game_t> parse_igdb_games(std::string_view json);

  /**
   * @brief Escape a title for an IGDB `search "..."` clause.
   *
   * @param title Title.
   * @return Title without quotes, backslashes or control characters.
   */
  std::string igdb_escape(std::string_view title);

  /**
   * @brief Details object (same shape as parsed Steam details) from an IGDB game.
   *
   * @param game IGDB game.
   * @return {igdb_id, description, genres, developer, publisher, release_date, metacritic, screenshot_urls}.
   */
  nlohmann::json igdb_details(const igdb_game_t &game);

  /**
   * @brief Artwork references for an IGDB game (cover as poster, first artwork as hero).
   *
   * @param game IGDB game.
   * @return References labelled "IGDB".
   */
  std::vector<art_ref_t> igdb_artwork(const igdb_game_t &game);

  /**
   * @brief Search IGDB by title (uses a cached Twitch token).
   *
   * @param settings Settings with IGDB credentials.
   * @param title Title.
   * @return Games, empty on failure or without credentials.
   */
  std::vector<igdb_game_t> igdb_search(const settings_t &settings, const std::string &title);

  /**
   * @brief Fetch one IGDB game by id.
   *
   * @param settings Settings with IGDB credentials.
   * @param id IGDB id.
   * @return Game, or nullopt.
   */
  std::optional<igdb_game_t> igdb_game(const settings_t &settings, std::uint64_t id);

  /**
   * @brief A RAWG search hit.
   */
  struct rawg_hit_t {
    std::uint64_t id = 0;  ///< RAWG id.
    std::string slug;  ///< RAWG slug.
    std::string name;  ///< Title.
  };

  /**
   * @brief Parse a RAWG `/api/games?search=` reply.
   *
   * @param json Response body.
   * @return Hits.
   */
  std::vector<rawg_hit_t> parse_rawg_search(std::string_view json);

  /**
   * @brief Details object from RAWG game and screenshot replies.
   *
   * @param game_json `/api/games/<id>` body.
   * @param screenshots_json `/api/games/<id>/screenshots` body (may be empty).
   * @return {rawg_id, description, genres, developer, publisher, release_date, metacritic, screenshot_urls}, or nullopt.
   */
  std::optional<nlohmann::json> parse_rawg_game(std::string_view game_json, std::string_view screenshots_json);

  /**
   * @brief Details for a title from RAWG (best title match).
   *
   * @param settings Settings with a RAWG key.
   * @param title Title.
   * @return Details, or nullopt.
   */
  std::optional<nlohmann::json> rawg_details(const settings_t &settings, const std::string &title);
}  // namespace library::metadata
