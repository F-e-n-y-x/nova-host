/**
 * @file src/nova_client_api.cpp
 * @brief Definitions for the Nova client API used by paired devices (Nebula).
 */
// standard includes
#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <fstream>
#include <mutex>
#include <thread>

// local includes
#include "config.h"
#include "file_handler.h"
#include "library/artwork.h"
#include "library/library.h"
#include "library/title.h"
#include "logging.h"
#include "nova_client_api.h"

using namespace std::literals;
namespace fs = std::filesystem;

namespace nova_api {
  namespace {
    /**
     * @brief 64-bit FNV-1a hash.
     *
     * @param data Bytes to hash.
     * @return Hash value.
     */
    std::uint64_t fnv1a(std::string_view data) {
      std::uint64_t h = 0xcbf29ce484222325ULL;
      for (const unsigned char c : data) {
        h ^= c;
        h *= 0x100000001b3ULL;
      }
      return h;
    }

    /**
     * @brief Current Unix time in seconds.
     *
     * @return Seconds since the epoch.
     */
    std::int64_t unix_now() {
      return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    /**
     * @brief String field of a JSON object, or empty.
     *
     * @param j Object.
     * @param key Field name.
     * @return Value.
     */
    std::string str(const nlohmann::json &j, const char *key) {
      if (j.is_object() && j.contains(key) && j[key].is_string()) {
        return j[key].get<std::string>();
      }
      return {};
    }

    /**
     * @brief Cut a UTF-8 string to at most @p max bytes without splitting a code point.
     *
     * @param s Input.
     * @param max Byte limit.
     * @return Truncated string.
     */
    std::string cap(std::string s, std::size_t max) {
      if (s.size() <= max) {
        return s;
      }
      std::size_t n = max;
      while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) {
        --n;
      }
      s.resize(n);
      return s;
    }

    /**
     * @brief Write text to a file atomically (temp file + rename).
     *
     * @param path Destination.
     * @param text Content.
     */
    void write_atomic(const fs::path &path, const std::string &text) {
      std::error_code ec;
      fs::create_directories(path.parent_path(), ec);
      auto tmp = path;
      tmp += ".tmp";
      {
        std::ofstream out {tmp, std::ios::trunc | std::ios::binary};
        if (!out) {
          return;
        }
        out << text;
      }
      fs::rename(tmp, path, ec);
      if (ec) {
        fs::remove(tmp, ec);
      }
    }

    /**
     * @brief Read a file into a string.
     *
     * @param path File.
     * @return Content, or nullopt.
     */
    std::optional<std::string> read_file(const fs::path &path) {
      std::ifstream in {path, std::ios::binary};
      if (!in) {
        return std::nullopt;
      }
      return std::string {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

    /**
     * @brief Guards the in-memory statistics and the metadata cache files.
     *
     * @return The mutex.
     */
    std::mutex &data_mutex() {
      static std::mutex m;
      return m;
    }

    /**
     * @brief Serializes network fetches so one game isn't fetched twice at once.
     *
     * @return The mutex.
     */
    std::mutex &fetch_mutex() {
      static std::mutex m;
      return m;
    }

    /**
     * @brief In-memory statistics, loaded lazily.
     */
    struct stats_cache_t {
      bool loaded = false;  ///< Whether the file was read.
      std::map<std::string, app_stats_t> stats;  ///< Statistics keyed by app name.
    };

    /**
     * @brief The process-wide statistics cache.
     *
     * @return Cache object.
     */
    stats_cache_t &stats_cache() {
      static stats_cache_t c;
      return c;
    }

    /**
     * @brief Load the statistics once, seeding them from session history when no file exists yet.
     *
     * @param c Cache (caller holds data_mutex()).
     */
    void ensure_loaded(stats_cache_t &c) {
      if (c.loaded) {
        return;
      }
      c.loaded = true;
      const auto path = stats_path();
      std::error_code ec;
      if (fs::exists(path, ec)) {
        c.stats = load_stats(path);
        return;
      }
      auto history = stream_stats::history();
      std::ranges::reverse(history);  // oldest first so the newest becomes last_session
      for (const auto &e : history) {
        add_session(c.stats, e);
      }
    }

    /**
     * @brief Path of cached store details for a Steam app.
     *
     * @param appid Steam app id.
     * @return JSON file path.
     */
    fs::path details_cache_path(std::uint32_t appid) {
      return metadata_dir() / ("steam-" + std::to_string(appid) + ".json");
    }

    /**
     * @brief Path of the title → Steam app id match cache.
     *
     * @return JSON file path.
     */
    fs::path matches_path() {
      return metadata_dir() / "matches.json";
    }

    /**
     * @brief Resolve an app's Steam id by store title search, remembering hits and misses.
     *
     * @param name App name.
     * @param allow_network Whether a store search may run.
     * @return App id, or 0.
     */
    std::uint32_t resolve_appid_by_title(const std::string &name, bool allow_network) {
      const auto key = library::title::match_key(name);
      if (key.empty()) {
        return 0;
      }
      nlohmann::json matches = nlohmann::json::object();
      {
        std::lock_guard lock {data_mutex()};
        if (const auto text = read_file(matches_path())) {
          matches = nlohmann::json::parse(*text, nullptr, false);
          if (!matches.is_object()) {
            matches = nlohmann::json::object();
          }
        }
      }
      if (matches.contains(key) && matches[key].is_object()) {
        const auto appid = matches[key].value("appid", 0U);
        const auto checked = matches[key].value("checked_at", std::int64_t {0});
        if (appid != 0 || unix_now() - checked < NO_MATCH_TTL_S) {
          return appid;
        }
      }
      if (!allow_network) {
        return 0;
      }
      const auto query = library::title::clean(name);
      std::uint32_t best_id = 0;
      double best = 0.0;
      for (const auto &hit : library::artwork::store_search(query)) {
        if (const double s = library::title::similarity(query, hit.name); s > best) {
          best = s;
          best_id = s >= 0.72 ? hit.appid : best_id;
        }
      }
      std::lock_guard lock {data_mutex()};
      if (const auto text = read_file(matches_path())) {
        auto fresh = nlohmann::json::parse(*text, nullptr, false);
        if (fresh.is_object()) {
          matches = std::move(fresh);
        }
      }
      matches[key] = {{"appid", best_id}, {"checked_at", unix_now()}};
      write_atomic(matches_path(), matches.dump(2));
      return best_id;
    }

    /**
     * @brief Decode the next HTML entity at @p pos (just after '&').
     *
     * @param html Input.
     * @param pos Position after the '&' (advanced past the entity on success).
     * @param out Receives the decoded text.
     * @return True when an entity was decoded.
     */
    bool decode_entity(std::string_view html, std::size_t &pos, std::string &out) {
      static constexpr std::array<std::pair<std::string_view, std::string_view>, 9> named {{
        {"amp", "&"},
        {"lt", "<"},
        {"gt", ">"},
        {"quot", "\""},
        {"apos", "'"},
        {"nbsp", " "},
        {"reg", "®"},
        {"trade", "™"},
        {"copy", "©"},
      }};
      const auto end = html.find(';', pos);
      if (end == std::string_view::npos || end - pos > 8) {
        return false;
      }
      const auto name = html.substr(pos, end - pos);
      if (!name.empty() && name[0] == '#') {
        unsigned long cp = 0;
        try {
          cp = name.size() > 1 && (name[1] == 'x' || name[1] == 'X') ? std::stoul(std::string(name.substr(2)), nullptr, 16) : std::stoul(std::string(name.substr(1)));
        } catch (...) {
          return false;
        }
        if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF) || cp < 0x20) {
          cp = ' ';
        }
        if (cp < 0x80) {
          out += static_cast<char>(cp);
        } else if (cp < 0x800) {
          out += static_cast<char>(0xC0 | (cp >> 6));
          out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
          out += static_cast<char>(0xE0 | (cp >> 12));
          out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
          out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
          out += static_cast<char>(0xF0 | (cp >> 18));
          out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
          out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
          out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        pos = end + 1;
        return true;
      }
      for (const auto &[n, v] : named) {
        if (name == n) {
          out += v;
          pos = end + 1;
          return true;
        }
      }
      return false;
    }
  }  // namespace

  std::string app_id(const nlohmann::json &app) {
    std::string key;
    if (const auto source_id = str(app, "nova-source-id"); !source_id.empty()) {
      key = "src:" + str(app, "nova-source") + ":" + source_id;
    } else if (const auto uuid = str(app, "uuid"); !uuid.empty()) {
      key = "uuid:" + uuid;
    } else {
      key = "app:" + str(app, "name") + "\n" + str(app, "cmd");
    }
    return std::format("{:016x}", fnv1a(key));
  }

  std::optional<std::string> parse_display_mode(std::string_view value) {
    if (value == "virtual"sv || value == "mirror"sv) {
      return std::string {value};
    }
    return std::nullopt;
  }

  std::optional<int> clamp_bitrate(long long kbps, int host_max_kbps) {
    if (kbps <= 0) {
      return std::nullopt;
    }
    const long long ceiling = host_max_kbps > 0 ? std::min<long long>(host_max_kbps, MAX_BITRATE_KBPS) : MAX_BITRATE_KBPS;
    return static_cast<int>(std::clamp<long long>(kbps, MIN_BITRATE_KBPS, std::max<long long>(ceiling, MIN_BITRATE_KBPS)));
  }

  nlohmann::json apps_list(
    const nlohmann::json &apps,
    const std::vector<std::string> &gamestream_ids,
    std::optional<std::size_t> running_index,
    bool can_launch,
    const std::map<std::string, app_stats_t> &stats,
    const fs::path &covers_dir
  ) {
    auto out = nlohmann::json::array();
    if (!apps.is_array()) {
      return out;
    }
    for (std::size_t i = 0; i < apps.size(); ++i) {
      const auto &app = apps[i];
      const bool running = running_index && *running_index == i;
      if (!can_launch && !running) {
        continue;
      }
      const auto name = str(app, "name");
      nlohmann::json has;
      for (const auto kind : {library::art_kind_e::poster, library::art_kind_e::hero, library::art_kind_e::logo, library::art_kind_e::icon}) {
        has[library::to_string(kind)] = library::app_art(app, kind, covers_dir).has_value();
      }
      nlohmann::json entry = {
        {"id", app_id(app)},
        {"index", i},
        {"appid", i < gamestream_ids.size() ? gamestream_ids[i] : std::string {}},
        {"name", name},
        {"running", running},
        {"source", str(app, "nova-source").empty() ? "app" : str(app, "nova-source")},
        {"has", std::move(has)},
        {"last_played", nullptr},
        {"playtime_s", 0},
        {"mode_default", nullptr},
      };
      if (const auto mode = parse_display_mode(str(app, "nova-display-mode"))) {
        entry["mode_default"] = *mode;
      }
      if (const auto it = stats.find(name); it != stats.end()) {
        if (it->second.last_played > 0) {
          entry["last_played"] = it->second.last_played;
        }
        entry["playtime_s"] = it->second.playtime_s;
      }
      out.push_back(std::move(entry));
    }
    return out;
  }

  void add_session(std::map<std::string, app_stats_t> &stats, const stream_stats::history_entry_t &entry) {
    if (entry.app_name.empty()) {
      return;
    }
    auto &s = stats[entry.app_name];
    s.playtime_s += std::max<std::int64_t>(entry.duration_s, 0);
    const auto ended = entry.started_at + std::max<std::int64_t>(entry.duration_s, 0);
    if (ended >= s.last_played) {
      s.last_played = ended;
      s.last_session = last_session_t {entry.client_name, entry.width, entry.height, entry.avg_fps, stream_stats::codec_name(entry.video_format)};
    }
  }

  std::map<std::string, app_stats_t> load_stats(const fs::path &path) {
    std::map<std::string, app_stats_t> out;
    const auto text = read_file(path);
    if (!text) {
      return out;
    }
    const auto j = nlohmann::json::parse(*text, nullptr, false);
    if (!j.is_object()) {
      return out;
    }
    for (const auto &[name, v] : j.items()) {
      if (!v.is_object()) {
        continue;
      }
      app_stats_t s;
      s.last_played = v.value("last_played", std::int64_t {0});
      s.playtime_s = v.value("playtime_s", std::int64_t {0});
      if (v.contains("last_session") && v["last_session"].is_object()) {
        const auto &l = v["last_session"];
        s.last_session = last_session_t {l.value("device", std::string {}), l.value("width", 0), l.value("height", 0), l.value("fps", 0.0), l.value("codec", std::string {})};
      }
      out[name] = s;
    }
    return out;
  }

  void save_stats(const fs::path &path, const std::map<std::string, app_stats_t> &stats) {
    nlohmann::json j = nlohmann::json::object();
    for (const auto &[name, s] : stats) {
      nlohmann::json v = {{"last_played", s.last_played}, {"playtime_s", s.playtime_s}};
      if (s.last_session) {
        v["last_session"] = {{"device", s.last_session->device}, {"width", s.last_session->width}, {"height", s.last_session->height}, {"fps", s.last_session->fps}, {"codec", s.last_session->codec}};
      }
      j[name] = std::move(v);
    }
    write_atomic(path, j.dump(2));
  }

  fs::path stats_path() {
    return fs::path {config::nvhttp.file_state}.parent_path() / "app_stats.json";
  }

  fs::path metadata_dir() {
    return fs::path {config::nvhttp.file_state}.parent_path() / "nova-metadata";
  }

  std::map<std::string, app_stats_t> stats() {
    std::lock_guard lock {data_mutex()};
    auto &c = stats_cache();
    ensure_loaded(c);
    return c.stats;
  }

  void record_session(const stream_stats::history_entry_t &entry) {
    std::map<std::string, app_stats_t> copy;
    {
      std::lock_guard lock {data_mutex()};
      auto &c = stats_cache();
      ensure_loaded(c);
      add_session(c.stats, entry);
      copy = c.stats;
    }
    if (!config::sunshine.flags[config::flag::FRESH_STATE]) {
      save_stats(stats_path(), copy);
    }
  }

  std::string strip_html(std::string_view html, std::size_t max_chars) {
    std::string out;
    out.reserve(std::min(html.size(), max_chars + 16));
    std::size_t i = 0;
    while (i < html.size() && out.size() < max_chars + 16) {
      const char c = html[i];
      if (c == '<') {
        const auto end = html.find('>', i);
        if (end == std::string_view::npos) {
          break;
        }
        std::string tag {html.substr(i + 1, end - i - 1)};
        std::ranges::transform(tag, tag.begin(), [](unsigned char ch) {
          return static_cast<char>(std::tolower(ch));
        });
        const bool closing = tag.starts_with('/');
        const auto name = tag.substr(0, tag.find_first_of(" /\t", 1));
        // Opening block tags and <br> start a new line; closing tags add nothing, so "</p><p>" is one break.
        if (!closing && (name == "br" || name == "p" || name == "li" || name == "h1" || name == "h2" || name == "h3" || name == "div")) {
          out += '\n';
        }
        i = end + 1;
        continue;
      }
      if (c == '&') {
        std::size_t pos = i + 1;
        if (decode_entity(html, pos, out)) {
          i = pos;
          continue;
        }
      }
      if (static_cast<unsigned char>(c) < 0x20 && c != '\n') {
        out += ' ';
      } else {
        out += c;
      }
      ++i;
    }
    // Collapse runs of spaces and blank lines.
    std::string tidy;
    tidy.reserve(out.size());
    int newlines = 0;
    bool space = false;
    for (const char c : out) {
      if (c == '\n') {
        while (!tidy.empty() && tidy.back() == ' ') {
          tidy.pop_back();
        }
        if (!tidy.empty() && newlines < 2) {
          tidy += '\n';
        }
        ++newlines;
        space = false;
      } else if (c == ' ') {
        space = !tidy.empty() && tidy.back() != '\n';
      } else {
        if (space) {
          tidy += ' ';
        }
        tidy += c;
        newlines = 0;
        space = false;
      }
    }
    while (!tidy.empty() && (tidy.back() == '\n' || tidy.back() == ' ')) {
      tidy.pop_back();
    }
    return cap(std::move(tidy), max_chars);
  }

  std::optional<nlohmann::json> parse_steam_appdetails(std::string_view body, std::uint32_t appid) {
    const auto j = nlohmann::json::parse(body, nullptr, false);
    const auto key = std::to_string(appid);
    if (!j.is_object() || !j.contains(key) || !j[key].is_object() || !j[key].value("success", false) || !j[key].contains("data") || !j[key]["data"].is_object()) {
      return std::nullopt;
    }
    const auto &d = j[key]["data"];
    auto description = strip_html(str(d, "short_description"), 1000);
    if (description.empty()) {
      description = strip_html(str(d, "about_the_game"), 2000);
    }
    auto genres = nlohmann::json::array();
    if (d.contains("genres") && d["genres"].is_array()) {
      for (const auto &g : d["genres"]) {
        if (const auto name = strip_html(str(g, "description"), 40); !name.empty() && genres.size() < 10) {
          genres.push_back(name);
        }
      }
    }
    const auto first = [&d](const char *field) {
      if (d.contains(field) && d[field].is_array() && !d[field].empty() && d[field][0].is_string()) {
        return strip_html(d[field][0].get<std::string>(), 100);
      }
      return std::string {};
    };
    nlohmann::json metacritic = nullptr;
    if (d.contains("metacritic") && d["metacritic"].is_object() && d["metacritic"].contains("score") && d["metacritic"]["score"].is_number_integer()) {
      metacritic = std::clamp(d["metacritic"]["score"].get<int>(), 0, 100);
    }
    std::string release;
    if (d.contains("release_date") && d["release_date"].is_object()) {
      release = strip_html(str(d["release_date"], "date"), 40);
    }
    auto shots = nlohmann::json::array();
    if (d.contains("screenshots") && d["screenshots"].is_array()) {
      for (const auto &s : d["screenshots"]) {
        const auto url = str(s, "path_full");
        const auto host_start = url.starts_with("https://"sv) ? 8 : std::string::npos;
        if (host_start == std::string::npos) {
          continue;
        }
        const auto host = url.substr(host_start, url.find('/', host_start) - host_start);
        if (library::artwork::allowed_host(host) && shots.size() < MAX_SCREENSHOTS) {
          shots.push_back(url);
        }
      }
    }
    return nlohmann::json {
      {"appid", appid},
      {"description", description},
      {"genres", std::move(genres)},
      {"developer", first("developers")},
      {"publisher", first("publishers")},
      {"release_date", release},
      {"metacritic", metacritic},
      {"screenshot_urls", std::move(shots)},
    };
  }

  std::uint32_t steam_appid_for(const nlohmann::json &app) {
    try {
      if (app.contains("nova-steam-appid")) {
        const auto &v = app["nova-steam-appid"];
        if (v.is_number_unsigned() || v.is_number_integer()) {
          return v.get<std::uint32_t>();
        }
        if (v.is_string()) {
          return static_cast<std::uint32_t>(std::stoul(v.get<std::string>()));
        }
      }
      if (str(app, "nova-source") == "steam") {
        return static_cast<std::uint32_t>(std::stoul(str(app, "nova-source-id")));
      }
    } catch (...) {
    }
    return 0;
  }

  std::optional<nlohmann::json> store_details(const nlohmann::json &app, bool allow_network) {
    auto appid = steam_appid_for(app);
    const auto source = str(app, "nova-source");
    // Title search only for real programs: skip desktops and command-less entries.
    if (appid == 0 && !str(app, "cmd").empty() && !str(app, "name").starts_with("Desktop")) {
      appid = resolve_appid_by_title(str(app, "name"), allow_network);
    }
    if (appid == 0) {
      return std::nullopt;
    }
    const auto path = details_cache_path(appid);
    {
      std::lock_guard lock {data_mutex()};
      if (const auto text = read_file(path)) {
        const auto cached = nlohmann::json::parse(*text, nullptr, false);
        if (cached.is_object() && unix_now() - cached.value("fetched_at", std::int64_t {0}) < DETAILS_TTL_S) {
          if (cached.contains("data") && cached["data"].is_object()) {
            return cached["data"];
          }
          return std::nullopt;  // remembered "no data"
        }
      }
    }
    if (!allow_network) {
      return std::nullopt;
    }
    std::lock_guard fetch_lock {fetch_mutex()};
    const auto body = library::artwork::http_get("https://store.steampowered.com/api/appdetails?appids=" + std::to_string(appid) + "&l=english", 4 * 1024 * 1024);
    if (!body) {
      BOOST_LOG(info) << "Nova: couldn't fetch store details for Steam app "sv << appid;
      return std::nullopt;  // network failure: try again next time, don't cache
    }
    auto data = parse_steam_appdetails(*body, appid);
    nlohmann::json cached = {{"fetched_at", unix_now()}, {"data", data ? *data : nlohmann::json(nullptr)}};
    std::lock_guard lock {data_mutex()};
    write_atomic(path, cached.dump(2));
    return data;
  }

  nlohmann::json details_reply(const std::string &id, const std::optional<nlohmann::json> &store, const std::optional<app_stats_t> &stats) {
    nlohmann::json out = {
      {"id", id},
      {"description", ""},
      {"genres", nlohmann::json::array()},
      {"developer", ""},
      {"publisher", ""},
      {"release_date", ""},
      {"screenshots", nlohmann::json::array()},
      {"metacritic", nullptr},
      {"steam_appid", nullptr},
      {"last_played", nullptr},
      {"playtime_s", 0},
      {"last_session", nullptr},
    };
    if (store) {
      for (const char *field : {"description", "genres", "developer", "publisher", "release_date", "metacritic"}) {
        if (store->contains(field)) {
          out[field] = (*store)[field];
        }
      }
      out["steam_appid"] = store->value("appid", 0U);
      if (store->contains("screenshot_urls") && (*store)["screenshot_urls"].is_array()) {
        for (std::size_t n = 0; n < (*store)["screenshot_urls"].size(); ++n) {
          out["screenshots"].push_back("/nova/v1/apps/" + id + "/screenshot/" + std::to_string(n));
        }
      }
    }
    if (stats) {
      if (stats->last_played > 0) {
        out["last_played"] = stats->last_played;
      }
      out["playtime_s"] = stats->playtime_s;
      if (stats->last_session) {
        const auto &l = *stats->last_session;
        out["last_session"] = {{"device", l.device}, {"resolution", std::format("{}x{}", l.width, l.height)}, {"fps", l.fps}, {"codec", l.codec}};
      }
    }
    return out;
  }

  std::optional<fs::path> screenshot_file(const nlohmann::json &app, std::size_t n) {
    const auto store = store_details(app, false);
    if (!store || !store->contains("screenshot_urls") || n >= (*store)["screenshot_urls"].size()) {
      return std::nullopt;
    }
    const auto appid = store->value("appid", 0U);
    const auto path = metadata_dir() / ("steam-" + std::to_string(appid)) / ("shot-" + std::to_string(n) + ".jpg");
    std::error_code ec;
    if (fs::exists(path, ec)) {
      return path;
    }
    std::lock_guard fetch_lock {fetch_mutex()};
    if (fs::exists(path, ec)) {
      return path;
    }
    const auto body = library::artwork::http_get((*store)["screenshot_urls"][n].get<std::string>(), 12 * 1024 * 1024);
    if (!body) {
      return std::nullopt;
    }
    const auto img = library::artwork::decode(*body);
    if (!img) {
      return std::nullopt;
    }
    write_atomic(path, library::artwork::encode_jpeg(library::artwork::fit(*img, 1920, 1080), 85));
    return fs::exists(path, ec) ? std::optional {path} : std::nullopt;
  }

  void prefetch_details(std::vector<nlohmann::json> apps) {
    if (apps.empty()) {
      return;
    }
    std::thread([apps = std::move(apps)]() {
      for (const auto &app : apps) {
        store_details(app, true);
        std::this_thread::sleep_for(500ms);
      }
    }).detach();
  }
}  // namespace nova_api
