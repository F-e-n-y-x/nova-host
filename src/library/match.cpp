/**
 * @file src/library/match.cpp
 * @brief Title matching across Steam, the local Steam catalogue, SteamGridDB and IGDB.
 */
// standard includes
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <ctime>
#include <future>
#include <map>
#include <regex>
#include <set>

// lib includes
#include <nlohmann/json.hpp>

// local includes
#include "artwork.h"
#include "match.h"
#include "metadata.h"
#include "steam_catalog.h"
#include "title.h"

namespace library::match {
  namespace {
    /**
     * @brief Common game title abbreviations, as canonical tokens. Longer keys win.
     */
    constexpr std::pair<std::string_view, std::string_view> abbreviations[] {
      {"gta", "grand theft auto"},
      {"rdr", "red dead redemption"},
      {"cod", "call of duty"},
      {"mw", "modern warfare"},
      {"bo", "black ops"},
      {"ac", "assassins creed"},
      {"nfs", "need for speed"},
      {"mgs", "metal gear solid"},
      {"tlou", "last of us"},
      {"gow", "god of war"},
      {"ff", "final fantasy"},
      {"ffvii", "final fantasy 7"},
      {"ffxv", "final fantasy 15"},
      {"ffxvi", "final fantasy 16"},
      {"re", "resident evil"},
      {"dmc", "devil may cry"},
      {"tes", "elder scrolls"},
      {"botw", "breath of the wild"},
      {"totk", "tears of the kingdom"},
      {"lotr", "lord of the rings"},
      {"mk", "mortal kombat"},
      {"sf", "street fighter"},
      {"kh", "kingdom hearts"},
      {"dbz", "dragon ball z"},
      {"pubg", "playerunknowns battlegrounds"},
      {"csgo", "counter strike global offensive"},
      {"cs", "counter strike"},
      {"cs2", "counter strike 2"},
      {"bf", "battlefield"},
      {"hl", "half life"},
      {"hl2", "half life 2"},
      {"ets2", "euro truck simulator 2"},
      {"ats", "american truck simulator"},
      {"fh", "forza horizon"},
      {"fm", "forza motorsport"},
      {"rotr", "rise of the tomb raider"},
      {"sotr", "shadow of the tomb raider"},
      {"dos2", "divinity original sin 2"},
      {"bg3", "baldurs gate 3"},
      {"kcd", "kingdom come deliverance"},
    };

    /**
     * @brief Words that only name an edition of a product.
     */
    const std::set<std::string, std::less<>> &edition_tokens() {
      static const std::set<std::string, std::less<>> words {
        "enhanced", "legacy", "remastered", "remaster", "remake", "definitive", "complete", "goty", "anniversary", "deluxe",
        "ultimate", "gold", "premium", "special", "collection", "directors", "cut", "redux", "hd", "classic", "edition",
      };
      return words;
    }

    /**
     * @brief Roman numerals XI..XX (I..X are handled by title::match_key).
     */
    constexpr std::array<std::pair<std::string_view, std::string_view>, 10> more_roman {{
      {"xi", "11"}, {"xii", "12"}, {"xiii", "13"}, {"xiv", "14"}, {"xv", "15"},
      {"xvi", "16"}, {"xvii", "17"}, {"xviii", "18"}, {"xix", "19"}, {"xx", "20"},
    }};

    /**
     * @brief Arabic → Roman for 1..20, for flipping query numerals.
     */
    constexpr std::array<std::string_view, 21> roman_of {
      "", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII", "XIII", "XIV", "XV", "XVI", "XVII", "XVIII", "XIX", "XX"
    };

    std::vector<std::string> split(std::string_view s) {
      std::vector<std::string> out;
      std::string cur;
      for (const char c : s) {
        if (c == ' ') {
          if (!cur.empty()) {
            out.push_back(std::move(cur));
            cur.clear();
          }
        } else {
          cur += c;
        }
      }
      if (!cur.empty()) {
        out.push_back(std::move(cur));
      }
      return out;
    }

    std::string join(const std::vector<std::string> &tokens) {
      std::string out;
      for (const auto &t : tokens) {
        if (!out.empty()) {
          out += ' ';
        }
        out += t;
      }
      return out;
    }

    bool is_number(std::string_view t) {
      return !t.empty() && std::ranges::all_of(t, [](unsigned char c) {
        return std::isdigit(c) != 0;
      });
    }

    /**
     * @brief Canonical tokens with edition words kept (they matter for display and scoring).
     */
    std::vector<std::string> tokens_with_editions(std::string_view title) {
      // Plain lower-case words, to bring back the edition words match_key drops.
      std::set<std::string, std::less<>> raw_words;
      std::string word;
      for (const char c : title) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
          word += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (!word.empty()) {
          raw_words.insert(std::move(word));
          word.clear();
        }
      }
      if (!word.empty()) {
        raw_words.insert(std::move(word));
      }
      std::vector<std::string> out;
      for (auto t : split(title::match_key(title))) {
        for (const auto &[numeral, digit] : more_roman) {
          if (t == numeral) {
            t = digit;
            break;
          }
        }
        bool expanded = false;
        for (const auto &[abbr, full] : abbreviations) {
          if (t == abbr) {
            for (auto &part : split(full)) {
              out.push_back(std::move(part));
            }
            expanded = true;
            break;
          }
        }
        if (!expanded) {
          out.push_back(std::move(t));
        }
      }
      // Edition words match_key removed ("complete", "definitive", ...) come back for scoring.
      for (const std::string_view w : {"complete", "definitive", "deluxe", "ultimate", "gold", "premium", "goty"}) {
        if (raw_words.contains(w) && std::ranges::find(out, w) == out.end()) {
          out.emplace_back(w);
        }
      }
      return out;
    }

    std::string year_of(std::int64_t unix_seconds) {
      if (unix_seconds <= 0) {
        return {};
      }
      const std::time_t t = static_cast<std::time_t>(unix_seconds);
      std::tm tm {};
      gmtime_r(&t, &tm);
      return std::to_string(tm.tm_year + 1900);
    }

    steam_type_e steam_type(int value) {
      switch (value) {
        case 0:
          return steam_type_e::game;
        case 1:
          return steam_type_e::demo;
        case 2:
          return steam_type_e::mod;
        case 4:
          return steam_type_e::dlc;
        case 6:
        case 13:
          return steam_type_e::software;
        default:
          return steam_type_e::other;
      }
    }

    /**
     * @brief Merge what a later source knows into a candidate without losing what's there.
     */
    void merge_into(candidate_t &into, const candidate_t &from) {
      into.steam_appid = into.steam_appid ? into.steam_appid : from.steam_appid;
      into.igdb_id = into.igdb_id ? into.igdb_id : from.igdb_id;
      into.sgdb_id = into.sgdb_id ? into.sgdb_id : from.sgdb_id;
      if (into.name.empty()) {
        into.name = from.name;
      }
      if (into.year.empty()) {
        into.year = from.year;
      }
      if (into.type == steam_type_e::unknown) {
        into.type = from.type;
      }
      if (into.poster_url.empty()) {
        into.poster_url = from.poster_url;
      }
      into.unlisted = into.unlisted || from.unlisted;
    }

    std::string key_of(const candidate_t &c) {
      if (c.steam_appid) {
        return "s" + std::to_string(c.steam_appid);
      }
      if (c.igdb_id) {
        return "i" + std::to_string(c.igdb_id);
      }
      if (c.sgdb_id) {
        return "g" + std::to_string(c.sgdb_id);
      }
      return "n" + canonical(c.name);
    }
  }  // namespace

  std::vector<std::string> canonical_tokens(std::string_view title) {
    auto tokens = tokens_with_editions(title);
    std::erase_if(tokens, [](const std::string &t) {
      return t == "edition" || t == "the";
    });
    return tokens;
  }

  std::string canonical(std::string_view title) {
    return join(canonical_tokens(title));
  }

  std::vector<std::string> query_variants(std::string_view query) {
    std::vector<std::string> out;
    const auto lower = [](std::string_view t) {
      std::string l;
      for (const char c : t) {
        l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      return l;
    };
    const auto add = [&out, &lower](std::string term) {
      if (term.empty() || term.size() > 120) {
        return;
      }
      if (std::ranges::none_of(out, [&](const std::string &t) {
            return lower(t) == lower(term);
          })) {
        out.push_back(std::move(term));
      }
    };
    const auto cleaned = title::clean(query);
    add(cleaned);

    // Expand abbreviations word by word, keeping the user's other words as typed.
    std::vector<std::string> words = split(cleaned);
    std::vector<std::string> expanded_words;
    bool changed = false;
    for (const auto &w : words) {
      std::string lw;
      for (const char c : w) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
          lw += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
      }
      const auto it = std::ranges::find_if(abbreviations, [&lw](const auto &p) {
        return p.first == lw;
      });
      if (it != std::ranges::end(abbreviations) && lw.size() >= 2) {
        std::string full(it->second);
        // Title case for store search engines that rank exact names higher.
        bool start = true;
        for (auto &c : full) {
          if (start) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
          }
          start = c == ' ';
        }
        expanded_words.push_back(full);
        changed = true;
      } else {
        expanded_words.push_back(w);
      }
    }
    const auto expanded = join(expanded_words);
    if (changed) {
      add(expanded);
    }

    // Flip numerals: "V" ↔ "5".
    std::vector<std::string> flipped;
    bool flipped_any = false;
    for (const auto &w : split(expanded)) {
      std::string upper;
      for (const char c : w) {
        upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      }
      const auto roman = std::ranges::find(roman_of, upper);
      int n = 0;
      if (is_number(w) && std::from_chars(w.data(), w.data() + w.size(), n).ec == std::errc {} && n >= 2 && n <= 20) {
        flipped.emplace_back(roman_of[static_cast<std::size_t>(n)]);
        flipped_any = true;
      } else if (roman != roman_of.end() && roman != roman_of.begin() && upper != "I" && upper == w) {
        flipped.push_back(std::to_string(std::distance(roman_of.begin(), roman)));
        flipped_any = true;
      } else {
        flipped.push_back(w);
      }
    }
    if (flipped_any) {
      add(join(flipped));
    }

    // Without edition words ("GTA V Enhanced" also finds "Legacy").
    std::vector<std::string> plain;
    for (const auto &w : split(expanded)) {
      std::string lw;
      for (const char c : w) {
        lw += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      if (!edition_tokens().contains(lw)) {
        plain.push_back(w);
      }
    }
    if (plain.size() != split(expanded).size() && !plain.empty()) {
      add(join(plain));
    }
    if (out.size() > 4) {
      out.resize(4);
    }
    return out;
  }

  double score(std::string_view query, std::string_view name) {
    const auto q = canonical_tokens(query);
    const auto n = canonical_tokens(name);
    if (q.empty() || n.empty()) {
      return 0.0;
    }
    if (q == n) {
      return 1.0;
    }
    const std::set<std::string, std::less<>> nq(n.begin(), n.end());
    const std::set<std::string, std::less<>> qq(q.begin(), q.end());
    double found = 0;
    for (const auto &t : qq) {
      if (nq.contains(t)) {
        found += 1;
      } else if (t.size() >= 4 && std::ranges::any_of(nq, [&t](const std::string &u) {
                   return u.starts_with(t) || (t.starts_with(u) && u.size() >= 4);
                 })) {
        found += 0.6;  // "spiderman" vs "spider"
      }
    }
    const double coverage = found / static_cast<double>(qq.size());
    double extra = 0;
    for (const auto &t : nq) {
      if (!qq.contains(t)) {
        extra += edition_tokens().contains(t) ? 0.35 : 1.0;
      }
    }
    const double precision = found / (found + extra + 1e-9);

    // A different sequel number ("GTA IV" for "GTA V") is a different game.
    double penalty = 1.0;
    std::set<std::string, std::less<>> qnums;
    std::set<std::string, std::less<>> nnums;
    for (const auto &t : qq) {
      if (is_number(t) && t.size() <= 2) {
        qnums.insert(t);
      }
    }
    for (const auto &t : nq) {
      if (is_number(t) && t.size() <= 2) {
        nnums.insert(t);
      }
    }
    if (!qnums.empty() && std::ranges::none_of(qnums, [&nnums](const std::string &t) {
          return nnums.contains(t);
        })) {
      penalty = 0.45;
    } else if (qnums.empty() && !nnums.empty()) {
      penalty = 0.9;
    }
    const double ordered = title::similarity(join(q), join(n));
    return std::clamp((0.6 * coverage + 0.25 * precision + 0.15 * ordered) * penalty, 0.0, 1.0);
  }

  std::string edition_of(std::string_view name) {
    static const std::regex re(
      R"((game of the year edition|goty edition|goty|director'?s cut|definitive edition|complete edition|deluxe edition|ultimate edition|gold edition|special edition|anniversary edition|remastered|remaster|remake|enhanced edition|enhanced|legacy|redux|classic)\s*$)",
      std::regex::icase
    );
    std::string s(name);
    while (!s.empty() && (s.back() == ' ' || s.back() == ')' || s.back() == ']')) {
      s.pop_back();
    }
    std::smatch m;
    if (std::regex_search(s, m, re)) {
      auto out = m[1].str();
      if (!out.empty()) {
        out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
      }
      return out;
    }
    return {};
  }

  std::string to_string(steam_type_e type) {
    switch (type) {
      case steam_type_e::game:
        return "game";
      case steam_type_e::demo:
        return "demo";
      case steam_type_e::mod:
        return "mod";
      case steam_type_e::dlc:
        return "dlc";
      case steam_type_e::software:
        return "software";
      case steam_type_e::other:
        return "other";
      case steam_type_e::unknown:
        break;
    }
    return {};
  }

  std::vector<candidate_t> parse_steam_store_items(std::string_view json) {
    std::vector<candidate_t> out;
    const auto doc = nlohmann::json::parse(json, nullptr, false);
    if (!doc.is_object() || !doc.contains("response") || !doc["response"].is_object()) {
      return out;
    }
    const auto &response = doc["response"];
    if (!response.contains("store_items") || !response["store_items"].is_array()) {
      return out;
    }
    for (const auto &item : response["store_items"]) {
      if (!item.is_object() || item.value("success", 0) != 1 || !item.contains("appid") || !item["appid"].is_number_unsigned()) {
        continue;
      }
      candidate_t c;
      c.source = "steam";
      c.steam_appid = item["appid"].get<std::uint32_t>();
      c.name = item.value("name", std::string {}).substr(0, 200);
      c.type = item.contains("type") && item["type"].is_number_integer() ? steam_type(item["type"].get<int>()) : steam_type_e::unknown;
      c.unlisted = item.value("unlisted", false);
      if (item.contains("release") && item["release"].is_object()) {
        const auto &r = item["release"];
        const auto date = r.contains("original_release_date") && r["original_release_date"].is_number_integer() ? r["original_release_date"].get<std::int64_t>() : r.value("steam_release_date", std::int64_t {0});
        c.year = year_of(date);
      }
      if (item.contains("assets") && item["assets"].is_object()) {
        const auto format = item["assets"].value("asset_url_format", std::string {});
        auto file = item["assets"].value("library_capsule", std::string {});
        const auto pos = format.find("${FILENAME}");
        const auto safe = std::ranges::all_of(file, [](unsigned char ch) {
          return std::isalnum(ch) || ch == '_' || ch == '.' || ch == '/' || ch == '-';
        });
        if (pos != std::string::npos && !file.empty() && safe && format.starts_with("steam/apps/")) {
          c.poster_url = "https://shared.steamstatic.com/store_item_assets/" + format.substr(0, pos) + file + format.substr(pos + 11);
        }
      }
      c.edition = edition_of(c.name);
      if (!c.name.empty()) {
        out.push_back(std::move(c));
      }
    }
    return out;
  }

  std::uint32_t parse_sgdb_steam_appid(std::string_view json) {
    const auto doc = nlohmann::json::parse(json, nullptr, false);
    if (!doc.is_object() || !doc.value("success", false) || !doc.contains("data") || !doc["data"].is_object()) {
      return 0;
    }
    const auto &data = doc["data"];
    if (!data.contains("external_platform_data") || !data["external_platform_data"].is_object()) {
      return 0;
    }
    const auto &platforms = data["external_platform_data"];
    if (!platforms.contains("steam") || !platforms["steam"].is_array()) {
      return 0;
    }
    for (const auto &p : platforms["steam"]) {
      if (!p.is_object()) {
        continue;
      }
      const auto id = p.contains("id") && p["id"].is_string() ? p["id"].get<std::string>() : std::string {};
      std::uint32_t appid = 0;
      if (!id.empty() && std::from_chars(id.data(), id.data() + id.size(), appid).ec == std::errc {} && appid) {
        return appid;
      }
    }
    return 0;
  }

  std::optional<std::uint32_t> steam_appid_in(std::string_view query) {
    static const std::regex url(R"(^\s*https?://(store\.steampowered\.com|steamcommunity\.com)/app/(\d{1,10})\b)", std::regex::icase);
    static const std::regex number(R"(^\s*(\d{1,10})\s*$)");
    const std::string q(query);
    std::smatch m;
    std::string digits;
    if (std::regex_search(q, m, url)) {
      digits = m[2].str();
    } else if (std::regex_match(q, m, number)) {
      digits = m[1].str();
    } else {
      return std::nullopt;
    }
    std::uint64_t id = 0;
    if (std::from_chars(digits.data(), digits.data() + digits.size(), id).ec != std::errc {} || id == 0 || id > 0xFFFFFFFFULL) {
      return std::nullopt;
    }
    return static_cast<std::uint32_t>(id);
  }

  std::vector<candidate_t> search(const std::string &query, const sources_t &sources, std::size_t limit) {
    const auto variants = query_variants(query);
    std::vector<std::future<std::vector<candidate_t>>> jobs;
    const auto run = [&jobs](auto fn) {
      jobs.push_back(std::async(std::launch::async, fn));
    };
    if (sources.steam_search) {
      for (const auto &term : variants) {
        run([&sources, term] {
          return sources.steam_search(term);
        });
      }
    }
    if (sources.catalog_search) {
      run([&sources, &query] {
        return sources.catalog_search(query, 25);
      });
    }
    if (sources.sgdb_search && !variants.empty()) {
      run([&sources, term = variants.size() > 1 ? variants[1] : variants[0]] {
        return sources.sgdb_search(term);
      });
    }
    if (sources.igdb_search && !variants.empty()) {
      run([&sources, term = variants.size() > 1 ? variants[1] : variants[0]] {
        return sources.igdb_search(term);
      });
    }

    std::vector<candidate_t> merged;
    std::map<std::string, std::size_t> by_key;
    const auto add = [&merged, &by_key](candidate_t c) {
      const auto key = key_of(c);
      if (const auto it = by_key.find(key); it != by_key.end()) {
        merge_into(merged[it->second], c);
        return;
      }
      by_key[key] = merged.size();
      merged.push_back(std::move(c));
    };
    if (const auto appid = steam_appid_in(query)) {
      candidate_t direct;
      direct.source = "steam";
      direct.steam_appid = *appid;
      add(std::move(direct));
    }
    for (auto &job : jobs) {
      try {
        for (auto &c : job.get()) {
          add(std::move(c));
        }
      } catch (const std::exception &) {
        // One source failing leaves the others.
      }
    }

    // Current Steam names, types, years and posters for every Steam candidate.
    if (sources.steam_items) {
      std::vector<std::uint32_t> appids;
      for (const auto &c : merged) {
        if (c.steam_appid && appids.size() < 60) {
          appids.push_back(c.steam_appid);
        }
      }
      if (!appids.empty()) {
        try {
          for (const auto &item : sources.steam_items(appids)) {
            if (const auto it = by_key.find("s" + std::to_string(item.steam_appid)); it != by_key.end()) {
              auto &c = merged[it->second];
              c.name = item.name.empty() ? c.name : item.name;
              c.type = item.type == steam_type_e::unknown ? c.type : item.type;
              c.year = item.year.empty() ? c.year : item.year;
              c.poster_url = item.poster_url.empty() ? c.poster_url : item.poster_url;
              c.unlisted = c.unlisted || item.unlisted;
            }
          }
        } catch (const std::exception &) {
          // Keep what the searches returned.
        }
      }
    }

    const bool direct_id = steam_appid_in(query).has_value();
    for (auto &c : merged) {
      c.edition = edition_of(c.name);
      c.confidence = direct_id && c.steam_appid == steam_appid_in(query).value_or(0) ? 1.0 : score(query, c.name);
    }
    std::erase_if(merged, [](const candidate_t &c) {
      return c.name.empty() || c.confidence < 0.2;
    });
    const auto rank = [](const candidate_t &c) {
      // Games first; DLC, demos, soundtracks and tools after.
      const bool game = c.type == steam_type_e::game || c.type == steam_type_e::unknown;
      return (game ? 1.0 : 0.0) + c.confidence;
    };
    std::ranges::stable_sort(merged, [&rank](const candidate_t &a, const candidate_t &b) {
      return rank(a) > rank(b);
    });
    if (merged.size() > limit) {
      merged.resize(limit);
    }
    return merged;
  }

  sources_t live_sources(const metadata::settings_t &settings) {
    sources_t s;
    const auto store_query = [](const std::string &method, const nlohmann::json &input) {
      const auto url = "https://api.steampowered.com/" + method + "/v1/?input_json=" + artwork::url_escape(input.dump());
      const auto body = artwork::http_get(url, 2 * 1024 * 1024);
      return body ? parse_steam_store_items(*body) : std::vector<candidate_t> {};
    };
    const nlohmann::json context = {{"language", "english"}, {"country_code", "US"}};
    const nlohmann::json request = {{"include_release", true}, {"include_assets", true}};
    s.steam_search = [store_query, context, request](const std::string &term) {
      return store_query("IStoreQueryService/SearchSuggestions", {{"search_term", term}, {"max_results", 25}, {"context", context}, {"data_request", request}});
    };
    s.steam_items = [store_query, context, request](const std::vector<std::uint32_t> &appids) {
      nlohmann::json ids = nlohmann::json::array();
      for (const auto id : appids) {
        ids.push_back({{"appid", id}});
      }
      return store_query("IStoreBrowseService/GetItems", {{"ids", ids}, {"context", context}, {"data_request", request}});
    };
    s.catalog_search = [key = settings.steam_web_api_key](const std::string &query, std::size_t limit) {
      return steam_catalog::search(key, query, limit);
    };
    if (!settings.steamgriddb_api_key.empty()) {
      s.sgdb_search = [key = settings.steamgriddb_api_key](const std::string &term) {
        std::vector<candidate_t> out;
        auto games = artwork::sgdb_search(key, term);
        if (games.size() > 10) {
          games.resize(10);
        }
        std::vector<std::future<std::uint32_t>> ids;
        for (const auto &g : games) {
          ids.push_back(std::async(std::launch::async, [&key, id = g.id] {
            const auto body = artwork::http_get("https://www.steamgriddb.com/api/v2/games/id/" + std::to_string(id) + "?platformdata=steam", 256 * 1024, key);
            return body ? parse_sgdb_steam_appid(*body) : 0U;
          }));
        }
        for (std::size_t i = 0; i < games.size(); ++i) {
          candidate_t c;
          c.source = "steamgriddb";
          c.sgdb_id = games[i].id;
          c.name = games[i].name;
          c.year = year_of(games[i].release_date);
          c.steam_appid = ids[i].get();
          out.push_back(std::move(c));
        }
        return out;
      };
    }
    if (settings.igdb_enabled()) {
      s.igdb_search = [settings](const std::string &term) {
        std::vector<candidate_t> out;
        for (const auto &g : metadata::igdb_search(settings, term)) {
          candidate_t c;
          c.source = "igdb";
          c.igdb_id = g.id;
          c.steam_appid = g.steam_appid;
          c.name = g.name;
          c.year = year_of(g.first_release);
          if (!g.cover_image_id.empty()) {
            c.poster_url = "https://images.igdb.com/igdb/image/upload/t_cover_big/" + g.cover_image_id + ".jpg";
          }
          out.push_back(std::move(c));
        }
        return out;
      };
    }
    return s;
  }
}  // namespace library::match
