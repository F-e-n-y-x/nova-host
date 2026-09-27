/**
 * @file src/library/metadata.cpp
 * @brief Game details and artwork source settings, plus the IGDB and RAWG lookups used for non-Steam games.
 */
// standard includes
#include <charconv>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <mutex>
#include <string>

// lib includes
#include <curl/curl.h>

// local includes
#include "metadata.h"
#include "src/config.h"
#include "title.h"

using namespace std::literals;

namespace library::metadata {
  using artwork::url_escape;
  namespace {
    constexpr std::size_t max_api_bytes = 2 * 1024 * 1024;  ///< Largest metadata API reply accepted.
    constexpr double match_threshold = 0.72;  ///< Minimum title similarity for an automatic match.
    constexpr std::size_t max_screenshots = 8;  ///< Screenshots kept per game.

    /**
     * @brief Lower-case ASCII copy.
     *
     * @param s Input.
     * @return Lower-cased string.
     */
    std::string lower(std::string_view s) {
      std::string out(s);
      std::ranges::transform(out, out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      return out;
    }

    /**
     * @brief Trim spaces and tabs.
     *
     * @param s Input.
     * @return Trimmed view.
     */
    std::string_view trim(std::string_view s) {
      while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
      }
      while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
        s.remove_suffix(1);
      }
      return s;
    }


    /**
     * @brief String field of a JSON object, or empty.
     *
     * @param j Object.
     * @param key Field.
     * @return Value.
     */
    std::string str(const nlohmann::json &j, const char *key) {
      return j.is_object() && j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string {};
    }

    /**
     * @brief Cap a string, cutting on a UTF-8 boundary.
     *
     * @param s Input.
     * @param max Maximum bytes.
     * @return Capped string.
     */
    std::string cap(std::string s, std::size_t max) {
      if (s.size() <= max) {
        return s;
      }
      std::size_t cut = max;
      while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) {
        --cut;
      }
      s.resize(cut);
      return s;
    }

    /**
     * @brief Plain text: control characters (except newlines) removed and runs of blank lines collapsed.
     *
     * @param s Input.
     * @param max Maximum bytes.
     * @return Clean text.
     */
    std::string clean_text(std::string_view s, std::size_t max) {
      std::string out;
      out.reserve(s.size());
      int newlines = 0;
      for (const char c : s) {
        if (c == '\r') {
          continue;
        }
        if (c == '\n') {
          if (++newlines <= 2) {
            out += c;
          }
          continue;
        }
        newlines = 0;
        if (static_cast<unsigned char>(c) < 0x20 && c != '\t') {
          continue;
        }
        out += c;
      }
      while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) {
        out.pop_back();
      }
      return cap(std::move(out), max);
    }

    /**
     * @brief Current Unix time.
     *
     * @return Seconds.
     */
    std::int64_t unix_now() {
      return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    /**
     * @brief Cached Twitch app token for IGDB.
     */
    struct token_cache_t {
      std::mutex mutex;  ///< Guards the fields below.
      std::string client_id;  ///< Client id the token belongs to.
      std::string token;  ///< Access token.
      std::int64_t expires_at = 0;  ///< Unix time the token stops working.
    };

    /**
     * @brief The process-wide token cache.
     *
     * @return Cache.
     */
    token_cache_t &token_cache() {
      static token_cache_t c;
      return c;
    }

    /**
     * @brief A Twitch token for IGDB, fetched once and reused until shortly before it expires.
     *
     * @param settings Settings with credentials.
     * @return Token, or nullopt.
     */
    std::optional<std::string> igdb_token(const settings_t &settings) {
      if (!settings.igdb_enabled()) {
        return std::nullopt;
      }
      auto &c = token_cache();
      std::scoped_lock lock(c.mutex);
      if (c.client_id == settings.igdb_client_id && !c.token.empty() && unix_now() < c.expires_at - 300) {
        return c.token;
      }
      const auto body = artwork::http_request(
        "https://id.twitch.tv/oauth2/token",
        64 * 1024,
        {"Content-Type: application/x-www-form-urlencoded"},
        "client_id=" + url_escape(settings.igdb_client_id) + "&client_secret=" + url_escape(settings.igdb_client_secret) + "&grant_type=client_credentials"
      );
      std::int64_t expires_in = 0;
      const auto token = body ? parse_twitch_token(*body, expires_in) : std::nullopt;
      if (!token) {
        return std::nullopt;
      }
      c.client_id = settings.igdb_client_id;
      c.token = *token;
      c.expires_at = unix_now() + expires_in;
      return token;
    }

    /**
     * @brief POST a query to IGDB's games endpoint.
     *
     * @param settings Settings with credentials.
     * @param query APIcalypse query.
     * @return Parsed games.
     */
    std::vector<igdb_game_t> igdb_query(const settings_t &settings, const std::string &query) {
      const auto token = igdb_token(settings);
      if (!token) {
        return {};
      }
      const auto body = artwork::http_request(
        "https://api.igdb.com/v4/games",
        max_api_bytes,
        {"Client-ID: " + settings.igdb_client_id, "Authorization: Bearer " + *token, "Accept: application/json"},
        query
      );
      return body ? parse_igdb_games(*body) : std::vector<igdb_game_t> {};
    }

    constexpr std::string_view igdb_fields =
      "fields name,summary,first_release_date,genres.name,cover.image_id,artworks.image_id,screenshots.image_id,"
      "involved_companies.developer,involved_companies.publisher,involved_companies.company.name,"
      "external_games.uid,external_games.category,external_games.external_game_source;";  ///< Fields Nova asks IGDB for.

    /**
     * @brief IGDB image URL.
     *
     * @param id Image id.
     * @param size Size preset, e.g. "t_cover_big".
     * @return HTTPS URL.
     */
    std::string igdb_image(const std::string &id, std::string_view size) {
      return "https://images.igdb.com/igdb/image/upload/" + std::string(size) + "/" + id + ".jpg";
    }

    /**
     * @brief Whether an IGDB image id is safe to put in a URL.
     *
     * @param id Image id.
     * @return True for short alphanumeric ids.
     */
    bool safe_image_id(std::string_view id) {
      return !id.empty() && id.size() <= 64 && std::ranges::all_of(id, [](unsigned char c) {
        return std::isalnum(c) != 0;
      });
    }

    /**
     * @brief Whether a URL is HTTPS on an allowed host.
     *
     * @param url URL.
     * @return True when usable.
     */
    bool allowed_url(std::string_view url) {
      if (!url.starts_with("https://"sv)) {
        return false;
      }
      const auto rest = url.substr(8);
      return artwork::allowed_host(rest.substr(0, rest.find('/')));
    }
  }  // namespace

  bool settings_t::art_source_enabled(std::string_view source) const {
    return std::ranges::find(art_priority, source) != art_priority.end();
  }

  settings_t from_config() {
    settings_t s;
    const auto &l = config::library;
    s.steam = l.metadata_steam;
    s.language = valid_steam_language(l.metadata_language) ? lower(l.metadata_language) : "english";
    s.auto_fetch = l.metadata_auto_fetch;
    s.ttl_days = std::clamp(l.metadata_ttl_days, 1, 365);
    s.art_priority = parse_priority(l.art_source_priority);
    s.prefer_official = l.art_prefer_official;
    s.sgdb.poster_style = l.art_steamgriddb_poster_style;
    s.sgdb.hero_style = l.art_steamgriddb_hero_style;
    s.sgdb.animated = l.art_allow_animated;
    s.sgdb.nsfw = l.art_nsfw;
    s.sgdb.humor = l.art_humor;
    s.steamgriddb_api_key = l.steamgriddb_api_key;
    s.igdb_client_id = l.igdb_client_id;
    s.igdb_client_secret = l.igdb_client_secret;
    s.rawg_api_key = l.rawg_api_key;
    s.steam_web_api_key = l.steam_web_api_key;
    return s;
  }

  std::vector<std::string> parse_priority(std::string_view csv) {
    std::vector<std::string> out;
    while (!csv.empty()) {
      const auto comma = csv.find(',');
      const auto name = lower(trim(csv.substr(0, comma)));
      csv = comma == std::string_view::npos ? std::string_view {} : csv.substr(comma + 1);
      if (std::ranges::find(known_art_sources, name) != known_art_sources.end() && std::ranges::find(out, name) == out.end()) {
        out.push_back(name);
      }
    }
    return out;
  }

  std::string art_source_of(const art_ref_t &ref) {
    const auto label = lower(ref.label);
    if (label == "steamgriddb") {
      return "steamgriddb";
    }
    if (label.starts_with("steam")) {
      return "steam";
    }
    if (label == "lutris") {
      return "lutris";
    }
    if (label == "igdb") {
      return "igdb";
    }
    return {};
  }

  void rank_artwork(std::vector<art_ref_t> &refs, const settings_t &settings) {
    const auto rank = [&settings](const art_ref_t &ref) -> std::size_t {
      const auto source = art_source_of(ref);
      if (source.empty()) {
        return settings.art_priority.size() + 1;  // local/uploaded art: keep, after the listed sources
      }
      if (settings.prefer_official && source == "steam") {
        return 0;
      }
      const auto it = std::ranges::find(settings.art_priority, source);
      return static_cast<std::size_t>(it - settings.art_priority.begin()) + 1;
    };
    std::erase_if(refs, [&settings](const art_ref_t &ref) {
      const auto source = art_source_of(ref);
      return !source.empty() && !settings.art_source_enabled(source);
    });
    std::ranges::stable_sort(refs, {}, rank);
  }

  bool valid_steam_language(std::string_view language) {
    return !language.empty() && language.size() <= 20 && std::ranges::all_of(language, [](unsigned char c) {
      return std::isalpha(c) != 0;
    });
  }

  std::string format_date(std::int64_t unix_seconds) {
    if (unix_seconds <= 0) {
      return {};
    }
    const std::time_t t = static_cast<std::time_t>(unix_seconds);
    std::tm tm {};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%d %b, %Y", &tm);
    std::string out = buf;
    if (out.starts_with('0')) {
      out.erase(0, 1);
    }
    return out;
  }

  std::optional<std::string> parse_twitch_token(std::string_view json, std::int64_t &expires_in) {
    const auto j = nlohmann::json::parse(json, nullptr, false);
    const auto token = str(j, "access_token");
    if (token.empty() || token.size() > 256) {
      return std::nullopt;
    }
    expires_in = j.contains("expires_in") && j["expires_in"].is_number_integer() ? j["expires_in"].get<std::int64_t>() : 3600;
    return token;
  }

  std::vector<igdb_game_t> parse_igdb_games(std::string_view json) {
    std::vector<igdb_game_t> out;
    const auto j = nlohmann::json::parse(json, nullptr, false);
    if (!j.is_array()) {
      return out;
    }
    for (const auto &g : j) {
      if (!g.is_object() || !g.contains("id") || !g["id"].is_number_unsigned()) {
        continue;
      }
      igdb_game_t game;
      game.id = g["id"].get<std::uint64_t>();
      game.name = cap(str(g, "name"), 200);
      game.summary = clean_text(str(g, "summary"), 2000);
      if (g.contains("first_release_date") && g["first_release_date"].is_number_integer()) {
        game.first_release = g["first_release_date"].get<std::int64_t>();
      }
      if (g.contains("genres") && g["genres"].is_array()) {
        for (const auto &genre : g["genres"]) {
          if (const auto name = cap(str(genre, "name"), 40); !name.empty() && game.genres.size() < 10) {
            game.genres.push_back(name);
          }
        }
      }
      if (g.contains("involved_companies") && g["involved_companies"].is_array()) {
        for (const auto &ic : g["involved_companies"]) {
          if (!ic.is_object() || !ic.contains("company") || !ic["company"].is_object()) {
            continue;
          }
          const auto name = cap(str(ic["company"], "name"), 100);
          if (game.developer.empty() && ic.value("developer", false)) {
            game.developer = name;
          }
          if (game.publisher.empty() && ic.value("publisher", false)) {
            game.publisher = name;
          }
        }
      }
      if (g.contains("external_games") && g["external_games"].is_array()) {
        for (const auto &ext : g["external_games"]) {
          // Source/category 1 is Steam; uid is the app id as text.
          const bool steam = ext.is_object() && (ext.value("external_game_source", 0) == 1 || ext.value("category", 0) == 1);
          const auto uid = steam ? str(ext, "uid") : std::string {};
          std::uint32_t appid = 0;
          if (!uid.empty() && std::from_chars(uid.data(), uid.data() + uid.size(), appid).ec == std::errc {} && appid) {
            game.steam_appid = appid;
            break;
          }
        }
      }
      if (g.contains("cover") && g["cover"].is_object()) {
        if (const auto id = str(g["cover"], "image_id"); safe_image_id(id)) {
          game.cover_image_id = id;
        }
      }
      const auto images = [&g](const char *field, std::vector<std::string> &dest) {
        if (g.contains(field) && g[field].is_array()) {
          for (const auto &img : g[field]) {
            if (const auto id = str(img, "image_id"); safe_image_id(id) && dest.size() < max_screenshots) {
              dest.push_back(id);
            }
          }
        }
      };
      images("artworks", game.artwork_image_ids);
      images("screenshots", game.screenshot_image_ids);
      out.push_back(std::move(game));
    }
    return out;
  }

  std::string igdb_escape(std::string_view title) {
    std::string out;
    for (const char c : title) {
      if (c == '"' || c == '\\' || static_cast<unsigned char>(c) < 0x20) {
        continue;
      }
      out += c;
    }
    return cap(std::move(out), 200);
  }

  nlohmann::json igdb_details(const igdb_game_t &game) {
    auto shots = nlohmann::json::array();
    for (const auto &id : game.screenshot_image_ids) {
      shots.push_back(igdb_image(id, "t_1080p"));
    }
    return {
      {"igdb_id", game.id},
      {"description", game.summary},
      {"genres", game.genres},
      {"developer", game.developer},
      {"publisher", game.publisher},
      {"release_date", format_date(game.first_release)},
      {"metacritic", nullptr},
      {"screenshot_urls", std::move(shots)},
    };
  }

  std::vector<art_ref_t> igdb_artwork(const igdb_game_t &game) {
    std::vector<art_ref_t> out;
    if (!game.cover_image_id.empty()) {
      out.push_back({art_kind_e::poster, igdb_image(game.cover_image_id, "t_cover_big_2x"), {}, "IGDB"});
    }
    for (const auto &id : game.artwork_image_ids) {
      out.push_back({art_kind_e::hero, igdb_image(id, "t_1080p"), {}, "IGDB"});
    }
    return out;
  }

  std::vector<igdb_game_t> igdb_search(const settings_t &settings, const std::string &title) {
    const auto q = igdb_escape(title);
    if (q.empty()) {
      return {};
    }
    return igdb_query(settings, "search \"" + q + "\"; " + std::string(igdb_fields) + " limit 8;");
  }

  std::optional<igdb_game_t> igdb_game(const settings_t &settings, std::uint64_t id) {
    if (id == 0) {
      return std::nullopt;
    }
    auto games = igdb_query(settings, std::string(igdb_fields) + " where id = " + std::to_string(id) + ";");
    if (games.empty()) {
      return std::nullopt;
    }
    return std::move(games.front());
  }

  std::vector<rawg_hit_t> parse_rawg_search(std::string_view json) {
    std::vector<rawg_hit_t> out;
    const auto j = nlohmann::json::parse(json, nullptr, false);
    if (!j.is_object() || !j.contains("results") || !j["results"].is_array()) {
      return out;
    }
    for (const auto &r : j["results"]) {
      if (r.is_object() && r.contains("id") && r["id"].is_number_unsigned()) {
        out.push_back({r["id"].get<std::uint64_t>(), cap(str(r, "slug"), 120), cap(str(r, "name"), 200)});
      }
    }
    return out;
  }

  std::optional<nlohmann::json> parse_rawg_game(std::string_view game_json, std::string_view screenshots_json) {
    const auto g = nlohmann::json::parse(game_json, nullptr, false);
    if (!g.is_object() || !g.contains("id") || !g["id"].is_number_unsigned()) {
      return std::nullopt;
    }
    const auto names = [&g](const char *field, std::size_t limit) {
      std::vector<std::string> out;
      if (g.contains(field) && g[field].is_array()) {
        for (const auto &e : g[field]) {
          if (const auto n = cap(str(e, "name"), 100); !n.empty() && out.size() < limit) {
            out.push_back(n);
          }
        }
      }
      return out;
    };
    const auto developers = names("developers", 1);
    const auto publishers = names("publishers", 1);
    nlohmann::json metacritic = nullptr;
    if (g.contains("metacritic") && g["metacritic"].is_number_integer()) {
      metacritic = std::clamp(g["metacritic"].get<int>(), 0, 100);
    }
    std::string release;
    if (const auto released = str(g, "released"); released.size() == 10 && released[4] == '-' && released[7] == '-') {
      try {
        std::tm tm {};
        tm.tm_year = std::stoi(released.substr(0, 4)) - 1900;
        tm.tm_mon = std::stoi(released.substr(5, 2)) - 1;
        tm.tm_mday = std::stoi(released.substr(8, 2));
#ifdef _WIN32
        release = format_date(_mkgmtime(&tm));
#else
        release = format_date(timegm(&tm));
#endif
      } catch (const std::exception &) {
        release.clear();
      }
    }
    auto shots = nlohmann::json::array();
    const auto s = nlohmann::json::parse(screenshots_json, nullptr, false);
    if (s.is_object() && s.contains("results") && s["results"].is_array()) {
      for (const auto &r : s["results"]) {
        if (const auto url = str(r, "image"); allowed_url(url) && shots.size() < max_screenshots) {
          shots.push_back(url);
        }
      }
    }
    return nlohmann::json {
      {"rawg_id", g["id"].get<std::uint64_t>()},
      {"description", clean_text(str(g, "description_raw"), 2000)},
      {"genres", names("genres", 10)},
      {"developer", developers.empty() ? "" : developers.front()},
      {"publisher", publishers.empty() ? "" : publishers.front()},
      {"release_date", release},
      {"metacritic", metacritic},
      {"screenshot_urls", std::move(shots)},
    };
  }

  std::optional<nlohmann::json> rawg_details(const settings_t &settings, const std::string &title) {
    if (settings.rawg_api_key.empty() || title.empty()) {
      return std::nullopt;
    }
    const auto key = url_escape(settings.rawg_api_key);
    const auto query = title::clean(title);
    const auto search = artwork::http_get("https://api.rawg.io/api/games?key=" + key + "&page_size=5&search=" + url_escape(query), max_api_bytes);
    if (!search) {
      return std::nullopt;
    }
    std::uint64_t best_id = 0;
    double best = 0.0;
    for (const auto &hit : parse_rawg_search(*search)) {
      if (const double s = title::similarity(query, hit.name); s > best && s >= match_threshold) {
        best = s;
        best_id = hit.id;
      }
    }
    if (best_id == 0) {
      return std::nullopt;
    }
    const auto id = std::to_string(best_id);
    const auto game = artwork::http_get("https://api.rawg.io/api/games/" + id + "?key=" + key, max_api_bytes);
    if (!game) {
      return std::nullopt;
    }
    const auto shots = artwork::http_get("https://api.rawg.io/api/games/" + id + "/screenshots?key=" + key, max_api_bytes);
    return parse_rawg_game(*game, shots.value_or(std::string {}));
  }
}  // namespace library::metadata
