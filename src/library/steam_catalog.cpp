/**
 * @file src/library/steam_catalog.cpp
 * @brief Local Steam game list for title matching.
 */
// standard includes
#include <algorithm>
#include <atomic>
#include <chrono>
#include <charconv>
#include <fstream>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>

// local includes
#include "artwork.h"
#include "src/logging.h"
#include "src/nova_client_api.h"
#include "steam_catalog.h"

namespace fs = std::filesystem;
using namespace std::literals;

namespace library::steam_catalog {
  namespace {
    constexpr auto max_age = std::chrono::hours(24 * 7);  ///< Refresh the list weekly.
    constexpr auto retry_after = std::chrono::hours(6);  ///< Wait this long after a failed refresh.
    constexpr int steamspy_pages = 15;  ///< SteamSpy pages (1000 most-owned games each) without a Steam key.
    constexpr auto steamspy_interval = std::chrono::seconds(61);  ///< SteamSpy allows one "all" page a minute.
    constexpr std::size_t max_entries = 250000;  ///< Sanity cap.

    /**
     * @brief Shared catalogue state.
     */
    struct state_t {
      std::mutex mutex;  ///< Guards the members below.
      index_t index;  ///< Loaded catalogue.
      fs::file_time_type loaded_mtime {};  ///< File time of the loaded catalogue.
      bool loaded = false;  ///< Whether the file was read once.
      std::chrono::steady_clock::time_point last_attempt {};  ///< Last refresh start.
      bool attempted = false;  ///< Whether a refresh was started in this process.
      std::atomic<bool> refreshing = false;  ///< A refresh thread is running.
    };

    state_t &state() {
      static state_t s;
      return s;
    }

    fs::path catalog_path() {
      return nova_api::metadata_dir() / "steam-catalog.tsv";
    }

    std::string clean_name(std::string name) {
      std::ranges::replace(name, '\t', ' ');
      std::ranges::replace(name, '\n', ' ');
      std::ranges::replace(name, '\r', ' ');
      if (name.size() > 200) {
        name.resize(200);
      }
      return name;
    }

    /**
     * @brief Union of two lists; entries from @p fresh win by app id.
     */
    std::vector<entry_t> merge(std::vector<entry_t> fresh, const std::vector<entry_t> &old) {
      std::set<std::uint32_t> seen;
      for (const auto &e : fresh) {
        seen.insert(e.appid);
      }
      for (const auto &e : old) {
        if (fresh.size() >= max_entries) {
          break;
        }
        if (seen.insert(e.appid).second) {
          fresh.push_back(e);
        }
      }
      return fresh;
    }

    /**
     * @brief Download the list (official with a key, SteamSpy otherwise) and save it as it arrives.
     */
    void refresh(const std::string &key) {
      const auto path = catalog_path();
      const auto old = load(path);
      std::vector<entry_t> fresh;
      bool ok = false;
      if (!key.empty()) {
        std::uint32_t last = 0;
        for (int page = 0; page < 40; ++page) {
          const auto url = "https://api.steampowered.com/IStoreService/GetAppList/v1/?key=" + artwork::url_escape(key) +
                           "&include_games=true&include_dlc=false&include_software=false&include_videos=false&include_hardware=false"
                           "&max_results=50000&last_appid=" +
                           std::to_string(last);
          const auto body = artwork::http_get(url, 16 * 1024 * 1024);
          if (!body) {
            break;
          }
          bool more = false;
          auto page_entries = parse_store_app_list(*body, more, last);
          fresh.insert(fresh.end(), page_entries.begin(), page_entries.end());
          ok = true;
          if (!more || fresh.size() >= max_entries) {
            break;
          }
        }
        if (ok) {
          save(path, fresh);  // the official list replaces the old one
        }
      }
      if (!ok) {
        for (int page = 0; page < steamspy_pages; ++page) {
          if (page > 0) {
            std::this_thread::sleep_for(steamspy_interval);
          }
          const auto body = artwork::http_get("https://steamspy.com/api.php?request=all&page=" + std::to_string(page), 4 * 1024 * 1024);
          if (!body) {
            break;
          }
          auto page_entries = parse_steamspy_page(*body);
          if (page_entries.empty()) {
            break;
          }
          fresh.insert(fresh.end(), page_entries.begin(), page_entries.end());
          ok = true;
          save(path, merge(fresh, old));  // usable while the rest downloads
        }
      }
      BOOST_LOG(info) << "Steam catalogue: "sv << (ok ? "updated, "s + std::to_string(fresh.size()) + " games" : "refresh failed"s);
    }

    /**
     * @brief Load the file into the index when it changed; start a refresh when it is stale.
     */
    void ensure_loaded(const std::string &key) {
      auto &s = state();
      const auto path = catalog_path();
      std::error_code ec;
      const auto mtime = fs::last_write_time(path, ec);
      const bool exists = !ec;
      {
        std::lock_guard lock {s.mutex};
        if (exists && (!s.loaded || mtime != s.loaded_mtime)) {
          s.index.assign(load(path));
          s.loaded_mtime = mtime;
          s.loaded = true;
        }
        const bool stale = !exists || fs::file_time_type::clock::now() - mtime > max_age;
        const auto now = std::chrono::steady_clock::now();
        if (!stale || s.refreshing || (s.attempted && now - s.last_attempt < retry_after)) {
          return;
        }
        s.attempted = true;
        s.last_attempt = now;
        s.refreshing = true;
      }
      std::thread([key] {
        try {
          refresh(key);
        } catch (const std::exception &e) {
          BOOST_LOG(warning) << "Steam catalogue refresh failed: "sv << e.what();
        }
        state().refreshing = false;
      }).detach();
    }
  }  // namespace

  std::vector<entry_t> parse_store_app_list(std::string_view json, bool &have_more, std::uint32_t &last_appid) {
    std::vector<entry_t> out;
    have_more = false;
    const auto doc = nlohmann::json::parse(json, nullptr, false);
    if (!doc.is_object() || !doc.contains("response") || !doc["response"].is_object()) {
      return out;
    }
    const auto &r = doc["response"];
    have_more = r.value("have_more_results", false);
    if (r.contains("last_appid") && r["last_appid"].is_number_unsigned()) {
      last_appid = r["last_appid"].get<std::uint32_t>();
    }
    if (!r.contains("apps") || !r["apps"].is_array()) {
      return out;
    }
    for (const auto &app : r["apps"]) {
      if (app.is_object() && app.contains("appid") && app["appid"].is_number_unsigned() && app.contains("name") && app["name"].is_string()) {
        auto name = clean_name(app["name"].get<std::string>());
        if (!name.empty()) {
          out.push_back({app["appid"].get<std::uint32_t>(), std::move(name)});
        }
      }
    }
    return out;
  }

  std::vector<entry_t> parse_steamspy_page(std::string_view json) {
    std::vector<entry_t> out;
    const auto doc = nlohmann::json::parse(json, nullptr, false);
    if (!doc.is_object()) {
      return out;
    }
    for (const auto &[id, app] : doc.items()) {
      if (!app.is_object() || !app.contains("name") || !app["name"].is_string()) {
        continue;
      }
      std::uint32_t appid = 0;
      if (app.contains("appid") && app["appid"].is_number_unsigned()) {
        appid = app["appid"].get<std::uint32_t>();
      } else if (std::from_chars(id.data(), id.data() + id.size(), appid).ec != std::errc {}) {
        continue;
      }
      auto name = clean_name(app["name"].get<std::string>());
      if (appid && !name.empty()) {
        out.push_back({appid, std::move(name)});
      }
    }
    return out;
  }

  void index_t::assign(std::vector<entry_t> entries) {
    entries_.clear();
    postings_.clear();
    std::set<std::uint32_t> seen;
    for (auto &e : entries) {
      if (e.appid == 0 || e.name.empty() || !seen.insert(e.appid).second) {
        continue;
      }
      const auto idx = static_cast<std::uint32_t>(entries_.size());
      for (const auto &token : match::canonical_tokens(e.name)) {
        auto &list = postings_[token];
        if (list.empty() || list.back() != idx) {
          list.push_back(idx);
        }
      }
      entries_.push_back(std::move(e));
    }
  }

  std::vector<match::candidate_t> index_t::search(std::string_view query, std::size_t limit) const {
    std::vector<match::candidate_t> out;
    const auto tokens = match::canonical_tokens(query);
    if (tokens.empty() || entries_.empty()) {
      return out;
    }
    // Entries sharing the rarest query tokens are the only ones worth scoring.
    std::unordered_map<std::uint32_t, int> hits;
    for (const auto &t : tokens) {
      const auto it = postings_.find(t);
      if (it == postings_.end() || it->second.size() > 20000) {
        continue;
      }
      for (const auto idx : it->second) {
        ++hits[idx];
      }
    }
    const int need = std::max(1, static_cast<int>((tokens.size() + 1) / 2));
    std::vector<std::pair<double, std::uint32_t>> scored;
    for (const auto &[idx, count] : hits) {
      if (count < need) {
        continue;
      }
      const auto s = match::score(query, entries_[idx].name);
      if (s >= 0.45) {
        scored.emplace_back(s, idx);
      }
    }
    std::ranges::sort(scored, [](const auto &a, const auto &b) {
      return a.first > b.first || (a.first == b.first && a.second < b.second);
    });
    for (const auto &[s, idx] : scored) {
      if (out.size() >= limit) {
        break;
      }
      match::candidate_t c;
      c.source = "catalog";
      c.steam_appid = entries_[idx].appid;
      c.name = entries_[idx].name;
      c.confidence = s;
      out.push_back(std::move(c));
    }
    return out;
  }

  bool save(const fs::path &path, const std::vector<entry_t> &entries) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    const auto tmp = path.string() + ".tmp";
    {
      std::ofstream out(tmp, std::ios::trunc | std::ios::binary);
      if (!out) {
        return false;
      }
      for (const auto &e : entries) {
        out << e.appid << '\t' << clean_name(e.name) << '\n';
      }
      if (!out) {
        return false;
      }
    }
    fs::rename(tmp, path, ec);
    return !ec;
  }

  std::vector<entry_t> load(const fs::path &path) {
    std::vector<entry_t> out;
    std::ifstream in(path, std::ios::binary);
    std::string line;
    while (out.size() < max_entries && std::getline(in, line)) {
      const auto tab = line.find('\t');
      if (tab == std::string::npos || tab == 0) {
        continue;
      }
      std::uint32_t appid = 0;
      if (std::from_chars(line.data(), line.data() + tab, appid).ec != std::errc {} || appid == 0) {
        continue;
      }
      out.push_back({appid, line.substr(tab + 1, 200)});
    }
    return out;
  }

  std::vector<match::candidate_t> search(const std::string &steam_web_api_key, std::string_view query, std::size_t limit) {
    ensure_loaded(steam_web_api_key);
    std::lock_guard lock {state().mutex};
    return state().index.search(query, limit);
  }

  nlohmann::json status() {
    auto &s = state();
    std::error_code ec;
    const auto mtime = fs::last_write_time(catalog_path(), ec);
    nlohmann::json out = {{"games", 0}, {"updated_at", nullptr}, {"refreshing", s.refreshing.load()}};
    if (!ec) {
      const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(mtime);
      out["updated_at"] = std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
      std::lock_guard lock {s.mutex};
      out["games"] = s.loaded ? s.index.size() : load(catalog_path()).size();
    }
    return out;
  }
}  // namespace library::steam_catalog
