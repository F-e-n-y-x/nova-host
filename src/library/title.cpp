/**
 * @file src/library/title.cpp
 * @brief Definitions for cleaning up and comparing game titles.
 */
// standard includes
#include <algorithm>
#include <array>
#include <cctype>
#include <regex>
#include <set>
#include <sstream>
#include <vector>

// local includes
#include "title.h"

namespace library::title {
  namespace {
    /**
     * @brief Release groups and repackers that appear in folder names.
     */
    constexpr std::array release_groups {
      "fitgirl", "dodi", "elamigos", "codex", "plaza", "skidrow", "reloaded", "empress", "rune", "tenoke",
      "flt", "gog", "darksiders", "kaos", "chronos", "p2p", "razor1911", "cpy", "hoodlum", "prophet",
      "tinyiso", "xatab", "masquerade", "decepticon", "gamesfull"
    };

    /**
     * @brief Trim spaces and separator characters from both ends of a string.
     *
     * @param s String to trim.
     * @return Trimmed copy.
     */
    std::string trim(std::string s) {
      const auto is_junk = [](unsigned char c) {
        return std::isspace(c) || c == '-' || c == '_' || c == '.' || c == ',' || c == ':';
      };
      while (!s.empty() && is_junk(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
      }
      std::size_t start = 0;
      while (start < s.size() && is_junk(static_cast<unsigned char>(s[start]))) {
        ++start;
      }
      return s.substr(start);
    }

    /**
     * @brief Lowercase an ASCII string; other bytes are kept as they are.
     *
     * @param s Input.
     * @return Lowercased copy.
     */
    std::string lower(std::string_view s) {
      std::string out(s);
      std::ranges::transform(out, out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      return out;
    }

    /**
     * @brief Split a string on whitespace.
     *
     * @param s Input.
     * @return Tokens in order.
     */
    std::vector<std::string> tokens(const std::string &s) {
      std::vector<std::string> out;
      std::istringstream in(s);
      for (std::string t; in >> t;) {
        out.push_back(t);
      }
      return out;
    }

    /**
     * @brief Whether a bracketed tag only carries release metadata.
     *
     * @param inner Text between the brackets.
     * @return True for years, versions, repack and platform tags.
     */
    bool is_metadata_tag(const std::string &inner) {
      static const std::regex meta(
        R"(^\s*((19|20)\d\d|v?\d+(\.\d+)+.*|.*\b(repack|rip|portable|multi\d*|x64|x86|win(64|32)?|build|update|gog|steam|dlcs?|eng?|pc)\b.*)\s*$)",
        std::regex::icase
      );
      if (std::regex_match(inner, meta)) {
        return true;
      }
      const auto key = lower(inner);
      return std::ranges::any_of(release_groups, [&key](const char *group) {
        return key.find(group) != std::string::npos;
      });
    }

    /**
     * @brief Levenshtein distance between two byte strings.
     *
     * @param a First string.
     * @param b Second string.
     * @return Number of single-byte edits.
     */
    std::size_t edit_distance(const std::string &a, const std::string &b) {
      std::vector<std::size_t> prev(b.size() + 1);
      std::vector<std::size_t> cur(b.size() + 1);
      for (std::size_t j = 0; j <= b.size(); ++j) {
        prev[j] = j;
      }
      for (std::size_t i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
          const std::size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
          cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
        }
        std::swap(prev, cur);
      }
      return prev[b.size()];
    }
  }  // namespace

  std::string clean(std::string_view raw) {
    std::string s(raw);

    // [DODI Repack], {CODEX}: always release metadata.
    static const std::regex square(R"(\[[^\]]*\]|\{[^}]*\})");
    s = std::regex_replace(s, square, " ");

    // (2018), (v1.2), (GOG): drop only metadata; keep "(Enhanced)" style qualifiers.
    static const std::regex paren(R"(\(([^)]*)\))");
    std::string rebuilt;
    auto begin = std::sregex_iterator(s.begin(), s.end(), paren);
    std::size_t last = 0;
    for (auto it = begin; it != std::sregex_iterator(); ++it) {
      const auto &m = *it;
      rebuilt.append(s, last, static_cast<std::size_t>(m.position()) - last);
      if (!is_metadata_tag(m[1].str())) {
        rebuilt.append(m.str());
      }
      last = static_cast<std::size_t>(m.position() + m.length());
    }
    rebuilt.append(s, last);
    s = rebuilt;

    // Scene style "Far.Cry.5-CODEX" or "Black_Myth_Wukong".
    if (s.find(' ') == std::string::npos) {
      std::ranges::replace(s, '.', ' ');
    }
    std::ranges::replace(s, '_', ' ');

    // Trailing "-GROUP" / " - FitGirl".
    static const std::regex trailing_group(R"(\s*-\s*([A-Za-z0-9]+)\s*$)");
    if (std::smatch m; std::regex_search(s, m, trailing_group)) {
      const auto key = lower(m[1].str());
      if (std::ranges::any_of(release_groups, [&key](const char *group) {
            return key == group;
          })) {
        s = s.substr(0, static_cast<std::size_t>(m.position()));
      }
    }

    static const std::regex noise(
      R"(\b(v\d+(\.\d+)*[a-z]?|build\s*\d+|update\s*\d+|repack|multi\d+|x64|x86|win64|portable|dlcs?|by\s+(fitgirl|dodi))\b)",
      std::regex::icase
    );
    s = std::regex_replace(s, noise, " ");

    static const std::regex spaces(R"(\s{2,})");
    s = std::regex_replace(s, spaces, " ");
    s = trim(s);
    if (s.empty()) {
      return trim(std::string(raw));
    }
    return s;
  }

  std::string match_key(std::string_view title) {
    std::string s(title);

    // Drop ™ ® © (UTF-8) and apostrophes so "Marvel's" and "Marvels" match.
    for (const std::string_view symbol : {"\xE2\x84\xA2", "\xC2\xAE", "\xC2\xA9", "\xE2\x80\x99", "'"}) {
      for (auto pos = s.find(symbol); pos != std::string::npos; pos = s.find(symbol)) {
        s.erase(pos, symbol.size());
      }
    }
    for (auto pos = s.find('&'); pos != std::string::npos; pos = s.find('&')) {
      s.replace(pos, 1, " and ");
    }

    s = lower(s);
    std::ranges::transform(s, s.begin(), [](unsigned char c) {
      return std::isalnum(c) || c >= 0x80 ? static_cast<char>(c) : ' ';
    });

    static const std::regex goty(R"(\bgame of the year\b)");
    s = std::regex_replace(s, goty, " ");

    static const std::set<std::string, std::less<>> edition_words {
      "goty", "edition", "deluxe", "ultimate", "complete", "definitive", "gold", "premium", "standard", "digital", "the"
    };
    static const std::array<std::pair<std::string_view, std::string_view>, 9> roman {{
      {"ii", "2"},
      {"iii", "3"},
      {"iv", "4"},
      {"v", "5"},
      {"vi", "6"},
      {"vii", "7"},
      {"viii", "8"},
      {"ix", "9"},
      {"x", "10"},
    }};

    std::string out;
    for (auto &t : tokens(s)) {
      if (edition_words.contains(t)) {
        continue;
      }
      for (const auto &[numeral, digit] : roman) {
        if (t == numeral) {
          t = digit;
          break;
        }
      }
      if (!out.empty()) {
        out += ' ';
      }
      out += t;
    }
    return out;
  }

  double similarity(std::string_view a, std::string_view b) {
    const auto ka = match_key(a);
    const auto kb = match_key(b);
    if (ka.empty() || kb.empty()) {
      return 0.0;
    }
    if (ka == kb) {
      return 1.0;
    }

    const auto ta = tokens(ka);
    const auto tb = tokens(kb);
    const std::set<std::string, std::less<>> sa(ta.begin(), ta.end());
    const std::set<std::string, std::less<>> sb(tb.begin(), tb.end());
    std::size_t common = 0;
    for (const auto &t : sa) {
      common += sb.contains(t) ? 1 : 0;
    }
    const auto uni = sa.size() + sb.size() - common;
    const double jaccard = uni == 0 ? 0.0 : static_cast<double>(common) / static_cast<double>(uni);

    const auto longest = std::max(ka.size(), kb.size());
    const double edit = 1.0 - static_cast<double>(edit_distance(ka, kb)) / static_cast<double>(longest);

    return std::clamp(0.6 * jaccard + 0.4 * edit, 0.0, 1.0);
  }

}  // namespace library::title
