/**
 * @file src/library/library.h
 * @brief Declarations for game library scan jobs, artwork candidates and importing games as apps.
 */
#pragma once

// standard includes
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// lib includes
#include <nlohmann/json.hpp>

// local includes
#include "library_types.h"
#include "match.h"
#include "metadata.h"

namespace library {

  /**
   * @brief Paths and settings a scan or import needs.
   */
  struct settings_t {
    std::filesystem::path home;  ///< Home directory of the user Nova runs as.
    std::filesystem::path apps_file;  ///< apps.json.
    std::filesystem::path covers_dir;  ///< Folder for cached artwork (".../covers/library").
    std::string steamgriddb_api_key;  ///< Optional SteamGridDB key; enables SteamGridDB artwork.
    std::string windows_launcher;  ///< Resolved command template for Windows executables.
    bool online = true;  ///< Whether to call Steam/SteamGridDB (false in tests).
    metadata::settings_t meta;  ///< Details/artwork source settings (priority, styles, keys).
  };

  /**
   * @brief Build settings from Nova's configuration.
   *
   * @return Settings for the current host.
   */
  settings_t current_settings();

  /**
   * @brief Pick the command template used to start Windows games.
   *
   * @param configured Value of the `windows_exe_launcher` option; used when not empty.
   * @return The configured template, else "/usr/local/bin/run-windows-exe {exe}" when that wrapper
   *         exists, else "umu-run {exe}" when umu-run is installed, else "wine {exe}". On Windows hosts,
   *         an empty template (run the executable directly).
   */
  std::string resolve_windows_launcher(const std::string &configured);

  /**
   * @brief Parse a source name.
   *
   * @param name "folder", "lutris", "steam" or "heroic".
   * @return The source, or nullopt for anything else.
   */
  std::optional<source_e> parse_source(std::string_view name);

  /**
   * @brief Whether a folder may be scanned.
   *
   * @param path Folder chosen by the user.
   * @return True for existing absolute directories outside /proc, /sys, /dev and /run.
   */
  bool safe_scan_root(const std::filesystem::path &path);

  /**
   * @brief Start a background scan.
   *
   * @param source What to scan.
   * @param path Folder for @ref source_e::folder, ignored otherwise.
   * @param settings Paths and settings.
   * @return Job id, or nullopt when too many jobs are running.
   */
  std::optional<std::string> start_scan(source_e source, const std::filesystem::path &path, const settings_t &settings);

  /**
   * @brief Start a background import of games found by a scan.
   *
   * @param items Import request items (see the /api/library/import documentation).
   * @param settings Paths and settings.
   * @return Job id, or nullopt when too many jobs are running.
   */
  std::optional<std::string> start_import(const nlohmann::json &items, const settings_t &settings);

  /**
   * @brief Ask a running scan, import or artwork job to stop at its next checkpoint.
   *
   * @param id Job id.
   * @return True when the job exists and was still running.
   */
  bool cancel_job(const std::string &id);

  /**
   * @brief Resolve artwork choices ({"poster"|"hero"|"logo"|"icon": candidate id}) to candidates.
   *
   * @param choices Choices object from the client.
   * @return One candidate per chosen kind, in poster/hero/logo/icon order.
   * @throws std::invalid_argument when the object is malformed, empty, or names an unknown or
   *         wrong-kind candidate.
   */
  std::vector<art_ref_t> parse_art_choices(const nlohmann::json &choices);

  /**
   * @brief Point an app's artwork field of one kind at a stored file.
   *
   * @param app App object from apps.json (modified).
   * @param kind Artwork kind: poster sets "image-path", others "nova-hero", "nova-logo", "nova-icon".
   * @param file Stored image.
   */
  void set_app_art(nlohmann::json &app, art_kind_e kind, const std::filesystem::path &file);

  /**
   * @brief Start a background job that stores chosen artwork for an app already in apps.json.
   *
   * The job fails instead of writing when the app at @p app_index changed name meanwhile.
   *
   * @param app_index Index into the "apps" array.
   * @param choices Choices object, see @ref parse_art_choices.
   * @param settings Paths and settings.
   * @return Job id, or nullopt when too many jobs are running.
   * @throws std::invalid_argument for invalid choices.
   */
  std::optional<std::string> start_apply_artwork(std::size_t app_index, const nlohmann::json &choices, const settings_t &settings);

  /**
   * @brief Current state of a job.
   *
   * @param id Job id.
   * @return Status JSON, or nullopt for unknown ids.
   */
  std::optional<nlohmann::json> job_status(const std::string &id);

  /**
   * @brief Look up a registered artwork candidate.
   *
   * @param id Candidate id from a scan or artwork search.
   * @return The candidate, or nullopt.
   */
  std::optional<art_ref_t> candidate(const std::string &id);

  /**
   * @brief Register an artwork candidate so clients can refer to it by id.
   *
   * @param ref Candidate.
   * @return Its id.
   */
  std::string register_candidate(const art_ref_t &ref);

  /**
   * @brief Describe a game for the API, registering its artwork candidates.
   *
   * @param game Game.
   * @param temp_id Id the client uses to import it.
   * @return JSON object.
   */
  nlohmann::json game_to_json(const detected_game_t &game, const std::string &temp_id);

  /**
   * @brief Match unmatched games to Steam and add online artwork candidates.
   *
   * @param games Games to update in place.
   * @param settings Settings (online access, SteamGridDB key).
   * @param progress Called after each game with (done, total).
   */
  void enrich(std::vector<detected_game_t> &games, const settings_t &settings, const std::function<void(std::size_t, std::size_t)> &progress);

  /**
   * @brief Search artwork for "Change match" / "Choose artwork".
   *
   * @param query Title to search for (used when @p appid is 0).
   * @param appid Steam app id.
   * @param settings Settings.
   * @return {"matches":[{appid,name}], "artwork":{poster:[...],hero:[...],logo:[...],icon:[...]}}.
   */
  nlohmann::json artwork_search(const std::string &query, std::uint32_t appid, const settings_t &settings);

  /**
   * @brief A match candidate as JSON for the web UI, with a poster preview candidate id.
   *
   * @param c Candidate.
   * @return {source, appid, igdb_id, sgdb_id, name, year, edition, type, unlisted, confidence, poster}.
   */
  nlohmann::json match_json(const match::candidate_t &c);

  /**
   * @brief Where custom artwork for one kind comes from (exactly one is used).
   */
  struct custom_art_t {
    std::optional<std::string> bytes;  ///< Uploaded image bytes.
    std::optional<std::string> url;  ///< Image URL the user pasted (fetched with SSRF protection).
    bool reset = false;  ///< Go back to the automatic choice for this kind.
  };

  /**
   * @brief Start a job that sets one kind of artwork of an app from an upload, a URL, or the automatic choice.
   *
   * The result is `{"app_index", "kind", "applied": bool, "cleared": bool}`; failures carry a message for the user.
   *
   * @param app_index Index into the apps list.
   * @param kind Artwork kind.
   * @param source Image source.
   * @param settings Settings.
   * @return Job id, or nullopt when too many jobs are running.
   */
  std::optional<std::string> start_custom_artwork(std::size_t app_index, art_kind_e kind, custom_art_t source, const settings_t &settings);

  /**
   * @brief A game ready to be written to apps.json.
   */
  struct app_entry_t {
    std::string name;  ///< App name.
    std::string cmd;  ///< Launch command.
    std::string working_dir;  ///< Working directory.
    std::string source;  ///< Source name.
    std::string source_id;  ///< Id within the source.
    std::filesystem::path poster;  ///< Stored poster (PNG) or empty.
    std::filesystem::path hero;  ///< Stored hero or empty.
    std::filesystem::path logo;  ///< Stored logo or empty.
    std::filesystem::path icon;  ///< Stored icon or empty.
    std::uint32_t steam_appid = 0;  ///< Matched Steam app id (0 = none), used for store details.
  };

  /**
   * @brief Whether apps.json already has this game (same source id, or same command).
   *
   * @param apps The "apps" array.
   * @param entry Game to check.
   * @return True when already present.
   */
  bool is_duplicate(const nlohmann::json &apps, const app_entry_t &entry);

  /**
   * @brief Add games to the apps.json tree, skipping duplicates, and sort by name.
   *
   * @param tree Parsed apps.json (must have an "apps" array; one is created otherwise).
   * @param entries Games to add.
   * @return Per entry: true when added, false when it was a duplicate.
   */
  std::vector<bool> merge_into_apps(nlohmann::json &tree, const std::vector<app_entry_t> &entries);

  /**
   * @brief Folder used to cache one game's artwork.
   *
   * @param covers_dir Artwork root.
   * @param source Source name.
   * @param source_id Id within the source.
   * @return Folder path (not created).
   */
  std::filesystem::path art_dir(const std::filesystem::path &covers_dir, std::string_view source, std::string_view source_id);

  /**
   * @brief Resolve a stored artwork file of an app for serving.
   *
   * Only files inside @p covers_dir are returned for hero/logo/icon; posters also accept the
   * app's regular image-path when it is a PNG.
   *
   * @param app App object from apps.json.
   * @param kind Artwork kind.
   * @param covers_dir Artwork root.
   * @return File to serve, or nullopt.
   */
  std::optional<std::filesystem::path> app_art(const nlohmann::json &app, art_kind_e kind, const std::filesystem::path &covers_dir);

  /**
   * @brief Parse an artwork kind name.
   *
   * @param name "poster", "hero", "logo" or "icon".
   * @return The kind, or nullopt.
   */
  std::optional<art_kind_e> parse_kind(std::string_view name);

  /**
   * @brief Mutex that serialises every read-modify-write of apps.json.
   *
   * @return The mutex.
   */
  std::mutex &apps_file_mutex();

  /**
   * @brief Progress callback of a background task: items done, items total, current step.
   */
  using task_progress_t = std::function<void(std::size_t, std::size_t, const std::string &)>;

  /**
   * @brief Work of a background task. Returns the job result; call the second argument between
   *        items and stop early when it returns true (the job then ends as "cancelled").
   */
  using task_fn_t = std::function<nlohmann::json(const task_progress_t &, const std::function<bool()> &)>;

  /**
   * @brief Run work as a library job (same status, progress and cancel endpoints as scans).
   *
   * @param kind Job kind shown in the status, e.g. "metadata".
   * @param work Work to run on a background thread.
   * @return Job id, or nullopt when too many jobs are running.
   */
  std::optional<std::string> start_task(const std::string &kind, task_fn_t work);

  /**
   * @brief Read apps.json.
   *
   * @param path apps.json path.
   * @return Parsed tree, always with an "apps" array.
   */
  nlohmann::json load_apps(const std::filesystem::path &path);

  /**
   * @brief Change one app in apps.json under the apps file lock, then reload the app list.
   *
   * @param path apps.json path.
   * @param index App index.
   * @param edit Called with the app object; may modify it.
   * @return The app after the edit, or nullopt when the index is out of range or the write failed.
   */
  std::optional<nlohmann::json> update_app(const std::filesystem::path &path, std::size_t index, const std::function<void(nlohmann::json &)> &edit);

}  // namespace library
