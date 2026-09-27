/**
 * @file src/library/match.h
 * @brief Finding which game a title refers to: query expansion ("GTA V" → "Grand Theft Auto V"),
 * scoring, and one merged candidate list from Steam, the local Steam catalogue, SteamGridDB and IGDB.
 */
#pragma once

// standard includes
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// local includes
#include "library_types.h"

namespace library::metadata {
  struct settings_t;
}

namespace library::match {

  /**
   * @brief Canonical tokens of a title: lower case, no punctuation or edition words, Roman
   * numerals as digits and known abbreviations expanded ("GTA V" → grand theft auto 5).
   *
   * @param title Title or query.
   * @return Tokens in order.
   */
  std::vector<std::string> canonical_tokens(std::string_view title);

  /**
   * @brief Canonical tokens joined with single spaces.
   *
   * @param title Title or query.
   * @return Canonical key.
   */
  std::string canonical(std::string_view title);

  /**
   * @brief Search terms to send to store search APIs for a query.
   *
   * The cleaned query first, then abbreviations expanded, then numerals flipped between
   * Roman and Arabic ("Grand Theft Auto 5" ↔ "Grand Theft Auto V"). Repack, version and
   * release-group noise is removed.
   *
   * @param query What the user typed or the detected title.
   * @return 1 to 4 distinct terms, best first.
   */
  std::vector<std::string> query_variants(std::string_view query);

  /**
   * @brief How well a candidate name matches a query (0..1).
   *
   * All query words present counts most; extra words cost a little (edition words such as
   * "Enhanced" or "Legacy" less); a different sequel number costs a lot.
   *
   * @param query Query.
   * @param name Candidate name.
   * @return Score.
   */
  double score(std::string_view query, std::string_view name);

  /**
   * @brief Edition or version qualifier in a product name ("Enhanced", "Definitive Edition").
   *
   * @param name Product name.
   * @return Qualifier as written, or empty.
   */
  std::string edition_of(std::string_view name);

  /**
   * @brief Steam store app types (EStoreAppType), for ranking games above DLC and demos.
   */
  enum class steam_type_e {
    unknown = -1,  ///< Not known yet.
    game = 0,  ///< Game.
    demo = 1,  ///< Demo.
    mod = 2,  ///< Mod.
    dlc = 4,  ///< Downloadable content.
    software = 6,  ///< Software / tool.
    other = 99,  ///< Video, music, hardware, ...
  };

  /**
   * @brief Type name for the API ("game", "dlc", ...).
   *
   * @param type Type.
   * @return Name, empty for unknown.
   */
  std::string to_string(steam_type_e type);

  /**
   * @brief One possible match for a title.
   */
  struct candidate_t {
    std::string source;  ///< Where it came from first: "steam", "catalog", "steamgriddb" or "igdb".
    std::uint32_t steam_appid = 0;  ///< Steam app id, 0 when not on Steam.
    std::uint64_t igdb_id = 0;  ///< IGDB id, 0 when unknown.
    std::uint64_t sgdb_id = 0;  ///< SteamGridDB game id, 0 when unknown.
    std::string name;  ///< Product name.
    std::string year;  ///< Release year, empty when unknown.
    std::string edition;  ///< Edition qualifier, empty when none.
    steam_type_e type = steam_type_e::unknown;  ///< Steam app type.
    bool unlisted = false;  ///< Not sold on the Steam store any more.
    std::string poster_url;  ///< Small poster image URL (allowlisted host), empty when none.
    double confidence = 0;  ///< Match score for the query.
  };

  /**
   * @brief Parse an IStoreQueryService/SearchSuggestions or IStoreBrowseService/GetItems reply.
   *
   * @param json Response body.
   * @return Steam candidates (source "steam"), with names, types, years and poster URLs.
   */
  std::vector<candidate_t> parse_steam_store_items(std::string_view json);

  /**
   * @brief Parse a SteamGridDB `/games/id/<id>?platformdata=steam` reply.
   *
   * @param json Response body.
   * @return Steam app id, or 0.
   */
  std::uint32_t parse_sgdb_steam_appid(std::string_view json);

  /**
   * @brief Steam app id from a query that is an app id or a store/community URL.
   *
   * @param query "271590", "https://store.steampowered.com/app/271590/..." or similar.
   * @return App id, or nullopt.
   */
  std::optional<std::uint32_t> steam_appid_in(std::string_view query);

  /**
   * @brief Where candidates come from; each may be empty (source off or not configured).
   */
  struct sources_t {
    std::function<std::vector<candidate_t>(const std::string &term)> steam_search;  ///< Steam store search for one term.
    std::function<std::vector<candidate_t>(const std::string &query, std::size_t limit)> catalog_search;  ///< Local Steam catalogue.
    std::function<std::vector<candidate_t>(const std::string &term)> sgdb_search;  ///< SteamGridDB (with Steam ids).
    std::function<std::vector<candidate_t>(const std::string &term)> igdb_search;  ///< IGDB (with Steam ids).
    std::function<std::vector<candidate_t>(const std::vector<std::uint32_t> &appids)> steam_items;  ///< Resolve Steam app ids.
  };

  /**
   * @brief Search every source in parallel and merge the results.
   *
   * Candidates are deduplicated by Steam app id (then IGDB / SteamGridDB id), filled in from
   * the Steam store (current name, type, year, poster), scored against the query and sorted:
   * games first, then by confidence.
   *
   * @param query Query.
   * @param sources Sources.
   * @param limit Most candidates to return.
   * @return Candidates, best first.
   */
  std::vector<candidate_t> search(const std::string &query, const sources_t &sources, std::size_t limit = 60);

  /**
   * @brief Sources backed by the live services and the local catalogue, as configured.
   *
   * @param settings Metadata settings (keys).
   * @return Sources.
   */
  sources_t live_sources(const metadata::settings_t &settings);
}  // namespace library::match
