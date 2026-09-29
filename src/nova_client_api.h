/**
 * @file src/nova_client_api.h
 * @brief Declarations for the Nova client API used by paired devices (Nebula) over the GameStream HTTPS port.
 */
#pragma once

// standard includes
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// lib includes
#include <nlohmann/json.hpp>

// local includes
#include "client_permissions.h"
#include "host_commands.h"
#include "stream_stats.h"

/**
 * @brief App list, artwork, game details, playtime, display mode and bitrate helpers for the Nova client API.
 */
namespace nova_api {
  constexpr std::int64_t DETAILS_TTL_S = 30LL * 24 * 3600;  ///< How long fetched store details stay fresh.
  constexpr std::int64_t NO_MATCH_TTL_S = 7LL * 24 * 3600;  ///< How long a failed store match is remembered.
  constexpr int MIN_BITRATE_KBPS = 500;  ///< Lowest bitrate a client may request.
  constexpr int MAX_BITRATE_KBPS = 800000;  ///< Highest bitrate a client may request when no host cap is set.
  constexpr std::size_t MAX_SCREENSHOTS = 12;  ///< Screenshots kept per game.

  /**
   * @brief The most recent stream of an app.
   */
  struct last_session_t {
    std::string device;  ///< Client name.
    int width = 0;  ///< Stream width.
    int height = 0;  ///< Stream height.
    double fps = 0;  ///< Average frame rate.
    std::string codec;  ///< "H.264", "HEVC" or "AV1".
  };

  /**
   * @brief Accumulated play statistics of one app.
   */
  struct app_stats_t {
    std::int64_t last_played = 0;  ///< Unix time the last session ended (0 = never).
    std::int64_t playtime_s = 0;  ///< Total streamed seconds.
    std::optional<last_session_t> last_session;  ///< Details of the last session.
  };

  /**
   * @brief Stable, URL-safe id for an app entry from apps.json.
   *
   * Uses `nova-source` + `nova-source-id` when present, then `uuid`, then name + command, so the id
   * survives reordering and re-imports.
   *
   * @param app App object from apps.json.
   * @return 16 lowercase hex characters.
   */
  std::string app_id(const nlohmann::json &app);

  /**
   * @brief Parse a display-mode argument.
   *
   * @param value Raw value from the query string or apps.json.
   * @return "virtual" or "mirror", or nullopt for anything else.
   */
  std::optional<std::string> parse_display_mode(std::string_view value);

  /**
   * @brief Clamp a requested bitrate to what Nova allows.
   *
   * @param kbps Requested bitrate.
   * @param host_max_kbps Host cap from config (0 = none).
   * @return Bitrate within [MIN_BITRATE_KBPS, cap], or nullopt when @p kbps isn't positive.
   */
  std::optional<int> clamp_bitrate(long long kbps, int host_max_kbps);

  /**
   * @brief Build the `/nova/v1/apps` reply.
   *
   * @param apps The "apps" array of apps.json.
   * @param gamestream_ids GameStream app ids by position (from proc), used to launch.
   * @param running_index Position of the running app, if any.
   * @param can_launch Whether the device may launch apps; without it only the running app is listed.
   * @param stats Play statistics keyed by app name.
   * @param covers_dir Library covers directory, used to check which artwork exists.
   * @return Array of app objects.
   */
  nlohmann::json apps_list(
    const nlohmann::json &apps,
    const std::vector<std::string> &gamestream_ids,
    std::optional<std::size_t> running_index,
    bool can_launch,
    const std::map<std::string, app_stats_t> &stats,
    const std::filesystem::path &covers_dir
  );

  /**
   * @brief Fold one ended session into per-app statistics.
   *
   * @param stats Statistics keyed by app name (modified).
   * @param entry Ended session.
   */
  void add_session(std::map<std::string, app_stats_t> &stats, const stream_stats::history_entry_t &entry);

  /**
   * @brief Read per-app statistics; a missing or corrupt file yields none.
   *
   * @param path File to read.
   * @return Statistics keyed by app name.
   */
  std::map<std::string, app_stats_t> load_stats(const std::filesystem::path &path);

  /**
   * @brief Write per-app statistics atomically.
   *
   * @param path Destination.
   * @param stats Statistics keyed by app name.
   */
  void save_stats(const std::filesystem::path &path, const std::map<std::string, app_stats_t> &stats);

  /**
   * @brief Location of the per-app statistics file next to the state file.
   *
   * @return Path of `app_stats.json`.
   */
  std::filesystem::path stats_path();

  /**
   * @brief Current per-app statistics (cached in memory, seeded from session history on first use).
   *
   * @return Statistics keyed by app name.
   */
  std::map<std::string, app_stats_t> stats();

  /**
   * @brief Record an ended session; called by stream_stats::end_session().
   *
   * @param entry Ended session.
   */
  void record_session(const stream_stats::history_entry_t &entry);

  /**
   * @brief Plain text from store HTML: tags removed, `<br>`/`<p>`/`<li>` become line breaks, entities decoded.
   *
   * @param html Input HTML.
   * @param max_chars Maximum length of the result.
   * @return Plain text.
   */
  std::string strip_html(std::string_view html, std::size_t max_chars);

  /**
   * @brief Parse a Steam `appdetails` reply into sanitized details.
   *
   * @param body Response body.
   * @param appid Steam app id that was requested.
   * @return {description, genres, developer, publisher, release_date, metacritic, screenshot_urls},
   *         or nullopt when Steam reports no data.
   */
  std::optional<nlohmann::json> parse_steam_appdetails(std::string_view body, std::uint32_t appid);

  /**
   * @brief Steam app id stored for an app (`nova-steam-appid`, or the id of a Steam-source app).
   *
   * @param app App object from apps.json.
   * @return App id, or 0 when unknown.
   */
  std::uint32_t steam_appid_for(const nlohmann::json &app);

  /**
   * @brief Details for an app from the cache, else fetched and cached for `metadata_ttl_days`.
   *
   * Sources, in order: the Steam store (when `metadata_steam` is on; the app id comes from
   * `nova-steam-appid` or a title search), IGDB (when credentials are set; `nova-igdb-id` or a
   * title search), then RAWG (when a key is set). Stale data is returned when offline.
   *
   * @param app App object from apps.json.
   * @param allow_network Whether a cache miss may go online.
   * @param force Fetch again even when the cache is fresh.
   * @return Sanitized store details, or nullopt when none are available.
   */
  std::optional<nlohmann::json> store_details(const nlohmann::json &app, bool allow_network, bool force = false);

  /**
   * @brief IGDB id stored for an app (`nova-igdb-id`), set when the user picks an IGDB match.
   *
   * @param app App object from apps.json.
   * @return IGDB id, or 0.
   */
  std::uint64_t igdb_id_for(const nlohmann::json &app);

  /**
   * @brief Current metadata match of an app, for the web UI (never goes online).
   *
   * @param app App object from apps.json.
   * @return {name, override:{steam_appid, igdb_id}, match:{source, id, name, confidence}|null, details:{source, fetched_at, has_description}|null}.
   */
  nlohmann::json metadata_status(const nlohmann::json &app);

  /**
   * @brief Drop cached matches and details of one app so the next lookup starts fresh.
   *
   * @param app App object from apps.json.
   */
  void forget_app_metadata(const nlohmann::json &app);

  /**
   * @brief Delete all cached details, matches and screenshots (play statistics are kept).
   *
   * @return Number of entries removed.
   */
  std::size_t clear_metadata_cache();

  /**
   * @brief Remember when a full metadata refresh finished.
   *
   * @param matched Apps with details.
   * @param total Apps considered.
   */
  void record_refresh(std::size_t matched, std::size_t total);

  /**
   * @brief Metadata summary for the settings page (cache only).
   *
   * @param apps The "apps" array from apps.json.
   * @return {total, matched, last_refresh_at}.
   */
  nlohmann::json metadata_summary(const nlohmann::json &apps);

  /**
   * @brief Build the `/nova/v1/apps/<id>/details` reply.
   *
   * @param id App id.
   * @param store Store details (may be null).
   * @param stats Play statistics of the app, if any.
   * @return Details object; screenshots are proxied `/nova/v1/apps/<id>/screenshot/<n>` paths.
   */
  nlohmann::json details_reply(const std::string &id, const std::optional<nlohmann::json> &store, const std::optional<app_stats_t> &stats);

  /**
   * @brief Cached, re-encoded JPEG of one store screenshot, downloading it on first use.
   *
   * @param app App object from apps.json.
   * @param n Screenshot index.
   * @return JPEG file, or nullopt.
   */
  std::optional<std::filesystem::path> screenshot_file(const nlohmann::json &app, std::size_t n);

  /**
   * @brief Fetch store details for newly imported apps in the background.
   *
   * @param apps App objects.
   */
  void prefetch_details(std::vector<nlohmann::json> apps);

  /**
   * @brief Directory holding cached store details and screenshots.
   *
   * @return Path next to the state file.
   */
  std::filesystem::path metadata_dir();
  /**
   * @brief What this host build and config support, for /nova/v1/capabilities.
   */
  struct host_features_t {
    bool mic = false;  ///< Remote microphone accepted (mic_enabled and a PipeWire build).
    bool clipboard = false;  ///< Clipboard sync available (config on and a clipboard tool present).
    bool motion = false;  ///< The virtual gamepad has motion sensors and touchpad (featureFlags 0x02).
    bool pcsleep = false;  ///< /pcsleep enabled in the config.
    bool commands = false;  ///< At least one host command is defined.
    bool abr = false;  ///< Adaptive bitrate (`/api/abr`) enabled (`abr_enabled`).
    bool network_probe = false;  ///< Network probe (`/api/network/probe`) enabled (`network_probe_enabled`).
  };

  /**
   * @brief Body of GET /nova/v1/capabilities.
   *
   * @param version Host version string.
   * @param features Supported features.
   * @param permissions The calling device's permission mask.
   * @return `{"nova":true,"version","features":[…],"permissions":[…]}`.
   */
  nlohmann::json capabilities(const std::string &version, const host_features_t &features, client_permissions::mask_t permissions);

  /**
   * @brief Why a request is refused: GameStream status code and message.
   */
  struct refusal_t {
    int status;  ///< Value for `<root status_code>`.
    std::string message;  ///< Value for `status_message`.
  };

  /**
   * @brief Check a /pcsleep request.
   *
   * @param enabled `pcsleep_enabled` setting.
   * @param permissions Caller's permissions.
   * @param other_sessions Streams owned by other devices.
   * @return The refusal, or nullopt when the host may sleep.
   */
  std::optional<refusal_t> pcsleep_refusal(bool enabled, client_permissions::mask_t permissions, int other_sessions);

  /**
   * @brief Body of GET /nova/v1/commands.
   *
   * @param allowed Whether the caller has `host_commands`.
   * @param global Global commands.
   * @param apps apps.json `apps` array (for per-app `menu-cmd`).
   * @param can_see_all Whether the caller may see every app (launch permission); otherwise only the running one.
   * @param running Index of the running app, if any.
   * @return `{"allowed", "commands":[…]}`; see phase1-wire-contracts.md.
   */
  nlohmann::json commands_list(bool allowed, const std::vector<host_commands::command_t> &global, const nlohmann::json &apps, bool can_see_all, std::optional<std::size_t> running);
}  // namespace nova_api
