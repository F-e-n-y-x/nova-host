/**
 * @file src/library/steam_catalog.h
 * @brief Local list of Steam games for matching titles that the store search no longer returns
 * (delisted or renamed apps such as "Grand Theft Auto V Legacy").
 *
 * With a Steam Web API key the official IStoreService/GetAppList (games only) is used; without
 * one, the most-owned games from SteamSpy. The list is kept in the metadata folder as
 * `steam-catalog.tsv` and refreshed weekly in the background.
 */
#pragma once

// standard includes
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// lib includes
#include <nlohmann/json.hpp>

// local includes
#include "match.h"

namespace library::steam_catalog {

  /**
   * @brief One Steam app in the catalogue.
   */
  struct entry_t {
    std::uint32_t appid = 0;  ///< Steam app id.
    std::string name;  ///< Name.
  };

  /**
   * @brief Parse one IStoreService/GetAppList page.
   *
   * @param json Response body.
   * @param have_more Set when more pages follow.
   * @param last_appid Set to the value for the next page's `last_appid`.
   * @return Entries.
   */
  std::vector<entry_t> parse_store_app_list(std::string_view json, bool &have_more, std::uint32_t &last_appid);

  /**
   * @brief Parse one SteamSpy `request=all` page.
   *
   * @param json Response body (object keyed by app id).
   * @return Entries.
   */
  std::vector<entry_t> parse_steamspy_page(std::string_view json);

  /**
   * @brief Searchable in-memory catalogue.
   */
  class index_t {
  public:
    /**
     * @brief Replace the contents.
     *
     * @param entries Entries (duplicates by app id keep the first).
     */
    void assign(std::vector<entry_t> entries);

    /**
     * @brief Fuzzy search by title.
     *
     * @param query Title or query (abbreviations and numerals are understood).
     * @param limit Most results.
     * @return Candidates (source "catalog"), best first, only reasonable matches.
     */
    std::vector<match::candidate_t> search(std::string_view query, std::size_t limit) const;

    /**
     * @brief Number of entries.
     *
     * @return Count.
     */
    std::size_t size() const {
      return entries_.size();
    }

  private:
    std::vector<entry_t> entries_;  ///< Entries.
    std::map<std::string, std::vector<std::uint32_t>, std::less<>> postings_;  ///< Canonical token → entry indexes.
  };

  /**
   * @brief Write entries as "appid<TAB>name" lines (atomically).
   *
   * @param path File.
   * @param entries Entries.
   * @return True on success.
   */
  bool save(const std::filesystem::path &path, const std::vector<entry_t> &entries);

  /**
   * @brief Read a file written by @ref save.
   *
   * @param path File.
   * @return Entries (empty when missing or unreadable).
   */
  std::vector<entry_t> load(const std::filesystem::path &path);

  /**
   * @brief Search the catalogue in the metadata folder, loading it on first use.
   *
   * Starts a background refresh when the file is missing or older than a week.
   *
   * @param steam_web_api_key Optional Steam Web API key (official full list).
   * @param query Query.
   * @param limit Most results.
   * @return Candidates.
   */
  std::vector<match::candidate_t> search(const std::string &steam_web_api_key, std::string_view query, std::size_t limit);

  /**
   * @brief Catalogue state for the settings page.
   *
   * @return {"games": n, "updated_at": unix|null, "refreshing": bool}.
   */
  nlohmann::json status();
}  // namespace library::steam_catalog
