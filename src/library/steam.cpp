/**
 * @file src/library/steam.cpp
 * @brief Definitions for reading installed Steam games and Steam artwork URLs.
 */
// standard includes
#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <system_error>

// local includes
#include "steam.h"

namespace fs = std::filesystem;

namespace library::steam {
  namespace {
    /**
     * @brief Lowercase an ASCII string.
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
     * @brief Tokenizer for text KeyValues.
     */
    class vdf_lexer_t {
    public:
      /**
       * @brief Start reading text.
       *
       * @param text Input.
       */
      explicit vdf_lexer_t(std::string_view text):
          text_(text) {}

      /**
       * @brief Token kinds.
       */
      enum class kind_e {
        string,  ///< Quoted or bare word.
        open,  ///< "{"
        close,  ///< "}"
        end,  ///< End of input.
        error,  ///< Unterminated string.
      };

      /**
       * @brief Read the next token.
       *
       * @param out Receives the string for @ref kind_e::string.
       * @return Token kind.
       */
      kind_e next(std::string &out) {
        skip_space();
        if (pos_ >= text_.size()) {
          return kind_e::end;
        }
        const char c = text_[pos_];
        if (c == '{') {
          ++pos_;
          return kind_e::open;
        }
        if (c == '}') {
          ++pos_;
          return kind_e::close;
        }
        out.clear();
        if (c == '"') {
          ++pos_;
          while (pos_ < text_.size() && text_[pos_] != '"') {
            if (text_[pos_] == '\\' && pos_ + 1 < text_.size()) {
              const char e = text_[pos_ + 1];
              out += e == 'n' ? '\n' : e == 't' ? '\t' : e;
              pos_ += 2;
              continue;
            }
            out += text_[pos_++];
          }
          if (pos_ >= text_.size()) {
            return kind_e::error;
          }
          ++pos_;
          skip_conditional();
          return kind_e::string;
        }
        while (pos_ < text_.size() && !std::isspace(static_cast<unsigned char>(text_[pos_])) && text_[pos_] != '{' && text_[pos_] != '}' && text_[pos_] != '"') {
          out += text_[pos_++];
        }
        skip_conditional();
        return kind_e::string;
      }

    private:
      /**
       * @brief Skip whitespace and // comments.
       */
      void skip_space() {
        while (pos_ < text_.size()) {
          if (std::isspace(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
          } else if (text_.substr(pos_, 2) == "//") {
            while (pos_ < text_.size() && text_[pos_] != '\n') {
              ++pos_;
            }
          } else {
            break;
          }
        }
      }

      /**
       * @brief Skip a trailing platform conditional such as [$WIN32].
       */
      void skip_conditional() {
        auto save = pos_;
        while (save < text_.size() && (text_[save] == ' ' || text_[save] == '\t')) {
          ++save;
        }
        if (save < text_.size() && text_[save] == '[') {
          const auto close = text_.find(']', save);
          if (close != std::string_view::npos) {
            pos_ = close + 1;
          }
        }
      }

      std::string_view text_;  ///< Input.
      std::size_t pos_ = 0;  ///< Read position.
    };

    /**
     * @brief Parse the members of a section until "}" or end of input.
     *
     * @param lex Lexer.
     * @param node Section to fill.
     * @param top_level True for the implicit root, which ends at end of input.
     * @param depth Nesting depth, to reject absurd input.
     * @return True on success.
     */
    bool parse_members(vdf_lexer_t &lex, vdf_node_t &node, bool top_level, int depth) {
      if (depth > 64) {
        return false;
      }
      std::string key;
      std::string value;
      for (;;) {
        auto k = lex.next(key);
        if (k == vdf_lexer_t::kind_e::end) {
          return top_level;
        }
        if (k == vdf_lexer_t::kind_e::close) {
          return !top_level;
        }
        if (k != vdf_lexer_t::kind_e::string) {
          return false;
        }
        auto v = lex.next(value);
        auto &child = node.children[lower(key)];
        if (v == vdf_lexer_t::kind_e::string) {
          child.value = value;
        } else if (v == vdf_lexer_t::kind_e::open) {
          if (!parse_members(lex, child, false, depth + 1)) {
            return false;
          }
        } else {
          return false;
        }
      }
    }

    /**
     * @brief Read a whole file.
     *
     * @param path File path.
     * @return Contents, or empty on error.
     */
    std::string read_file(const fs::path &path) {
      std::ifstream in(path, std::ios::binary);
      std::ostringstream ss;
      ss << in.rdbuf();
      return ss.str();
    }

    /**
     * @brief Whether a Steam root belongs to the Flatpak build.
     *
     * @param root Steam root.
     * @return True for ~/.var/app/com.valvesoftware.Steam roots.
     */
    bool is_flatpak_root(const fs::path &root) {
      return root.string().find("com.valvesoftware.Steam") != std::string::npos;
    }

    /**
     * @brief Parse a decimal app id.
     *
     * @param s Text.
     * @return App id, or 0.
     */
    std::uint32_t to_appid(std::string_view s) {
      std::uint32_t id = 0;
      auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), id);
      return ec == std::errc() && ptr == s.data() + s.size() ? id : 0;
    }
  }  // namespace

  const vdf_node_t *vdf_node_t::find(std::string_view key) const {
    const auto it = children.find(lower(key));
    return it == children.end() ? nullptr : &it->second;
  }

  std::string vdf_node_t::get(std::string_view key) const {
    const auto *child = find(key);
    return child ? child->value : std::string {};
  }

  std::optional<vdf_node_t> parse_vdf(std::string_view text) {
    vdf_lexer_t lex(text);
    vdf_node_t root;
    if (!parse_members(lex, root, true, 0)) {
      return std::nullopt;
    }
    return root;
  }

  std::optional<app_t> parse_app_manifest(std::string_view text, const fs::path &library_path) {
    const auto root = parse_vdf(text);
    if (!root) {
      return std::nullopt;
    }
    const auto *state = root->find("AppState");
    if (!state) {
      return std::nullopt;
    }
    app_t app;
    app.appid = to_appid(state->get("appid"));
    app.name = state->get("name");
    const auto installdir = state->get("installdir");
    if (app.appid == 0 || app.name.empty() || installdir.empty() || installdir.find("..") != std::string::npos) {
      return std::nullopt;
    }
    app.install_dir = library_path / "steamapps" / "common" / installdir;
    unsigned flags = 0;
    const auto flag_text = state->get("StateFlags");
    std::from_chars(flag_text.data(), flag_text.data() + flag_text.size(), flags);
    app.fully_installed = (flags & 4U) != 0;
    return app;
  }

  std::vector<fs::path> parse_library_folders(std::string_view text) {
    std::vector<fs::path> out;
    const auto root = parse_vdf(text);
    if (!root) {
      return out;
    }
    const auto *folders = root->find("libraryfolders");
    if (!folders) {
      return out;
    }
    for (const auto &[key, child] : folders->children) {
      if (to_appid(key) == 0 && key != "0") {
        continue;
      }
      // New format: "0" { "path" "..." }. Old format: "1" "/path".
      auto path = child.children.empty() ? child.value : child.get("path");
      if (!path.empty()) {
        out.emplace_back(path);
      }
    }
    return out;
  }

  bool is_tool(const app_t &app) {
    static const std::regex tools(R"(^(Proton\b|Steam Linux Runtime|Steamworks Common Redistributables|SteamVR|Steam Audio)|\bRedistributable)", std::regex::icase);
    static const std::set<std::uint32_t> ids {228980, 1070560, 1391110, 1628350, 1493710, 2180100, 1826330, 2348590, 2805730};
    return ids.contains(app.appid) || std::regex_search(app.name, tools);
  }

  std::vector<fs::path> find_roots(const fs::path &home) {
    std::vector<fs::path> out;
    std::set<fs::path> seen;
    for (const auto &rel : {".local/share/Steam", ".steam/steam", ".steam/root", ".var/app/com.valvesoftware.Steam/.local/share/Steam",
                            ".var/app/com.valvesoftware.Steam/data/Steam", "snap/steam/common/.local/share/Steam"}) {
      std::error_code ec;
      const auto candidate = home / rel;
      if (!fs::is_directory(candidate / "steamapps", ec)) {
        continue;
      }
      const auto canonical = fs::weakly_canonical(candidate, ec);
      if (seen.insert(ec ? candidate : canonical).second) {
        out.push_back(candidate);
      }
    }
    return out;
  }

  std::vector<art_ref_t> cdn_artwork(std::uint32_t appid) {
    const auto base = "https://cdn.cloudflare.steamstatic.com/steam/apps/" + std::to_string(appid) + "/";
    return {
      {art_kind_e::poster, base + "library_600x900_2x.jpg", {}, "Steam"},
      {art_kind_e::poster, base + "library_600x900.jpg", {}, "Steam"},
      {art_kind_e::hero, base + "library_hero.jpg", {}, "Steam"},
      {art_kind_e::hero, base + "header.jpg", {}, "Steam"},
      {art_kind_e::logo, base + "logo.png", {}, "Steam"},
    };
  }

  std::vector<art_ref_t> cached_artwork(const fs::path &root, std::uint32_t appid) {
    std::vector<art_ref_t> out;
    std::error_code ec;
    const auto cache = root / "appcache" / "librarycache";
    const auto id = std::to_string(appid);
    const auto add = [&](art_kind_e kind, const fs::path &p) {
      if (fs::is_regular_file(p, ec)) {
        out.push_back({kind, {}, p, "Steam (local)"});
      }
    };
    // Newer clients: librarycache/<appid>/..., older: librarycache/<appid>_....
    add(art_kind_e::poster, cache / id / "library_600x900_2x.jpg");
    add(art_kind_e::poster, cache / id / "library_600x900.jpg");
    add(art_kind_e::poster, cache / (id + "_library_600x900.jpg"));
    add(art_kind_e::hero, cache / id / "library_hero.jpg");
    add(art_kind_e::hero, cache / (id + "_library_hero.jpg"));
    add(art_kind_e::logo, cache / id / "logo.png");
    add(art_kind_e::logo, cache / (id + "_logo.png"));
    add(art_kind_e::icon, cache / (id + "_icon.jpg"));
    static const std::regex hashed(R"(^[0-9a-f]{40}\.jpg$)");
    for (fs::directory_iterator it(cache / id, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
      if (std::regex_match(it->path().filename().string(), hashed)) {
        out.push_back({art_kind_e::icon, {}, it->path(), "Steam (local)"});
      }
    }
    return out;
  }

  scan_result_t scan(const fs::path &home) {
    scan_result_t result;
    const auto roots = find_roots(home);
    if (roots.empty()) {
      result.skipped.push_back({(home / ".local/share/Steam").string(), "Steam isn't installed, or has no library yet."});
      return result;
    }
    std::set<std::uint32_t> seen;
    for (const auto &root : roots) {
      std::vector<fs::path> libraries = parse_library_folders(read_file(root / "steamapps" / "libraryfolders.vdf"));
      if (std::ranges::find(libraries, root) == libraries.end()) {
        libraries.push_back(root);
      }
      const std::string steam_cmd = is_flatpak_root(root) ? "flatpak run com.valvesoftware.Steam" : "steam";
      for (const auto &library : libraries) {
        std::error_code ec;
        for (fs::directory_iterator it(library / "steamapps", ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
          const auto name = it->path().filename().string();
          if (!name.starts_with("appmanifest_") || !name.ends_with(".acf")) {
            continue;
          }
          const auto app = parse_app_manifest(read_file(it->path()), library);
          if (!app || seen.contains(app->appid)) {
            continue;
          }
          if (is_tool(*app)) {
            continue;
          }
          if (!app->fully_installed) {
            result.skipped.push_back({app->install_dir.string(), app->name + " isn't fully installed in Steam."});
            continue;
          }
          seen.insert(app->appid);
          detected_game_t game;
          game.title = app->name;
          game.source = source_e::steam;
          game.source_id = std::to_string(app->appid);
          game.steam_appid = app->appid;
          game.matched_name = app->name;
          game.match_confidence = 1.0;
          game.launch_cmd = steam_cmd + " steam://rungameid/" + std::to_string(app->appid);
          game.artwork = cached_artwork(root, app->appid);
          auto cdn = cdn_artwork(app->appid);
          game.artwork.insert(game.artwork.end(), cdn.begin(), cdn.end());
          result.games.push_back(std::move(game));
        }
      }
    }
    return result;
  }

}  // namespace library::steam
