/**
 * @file src/vd_app_launch.cpp
 * @brief Nova: make an app launched on a virtual display open there (see vd_app_launch.h).
 */
// standard includes
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string_view>
#include <thread>

// lib includes
#include <nlohmann/json.hpp>

// local includes
#include "vd_app_launch.h"

using namespace std::literals;
namespace fs = std::filesystem;

namespace vd_app_launch {
  namespace {
    std::string lower(std::string_view s) {
      std::string out {s};
      std::ranges::transform(out, out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      return out;
    }

    /**
     * @brief One word of a command line and where it sits.
     */
    struct word_t {
      std::string text;  ///< Unquoted.
      std::size_t begin = 0;  ///< Offset of its first character.
      std::size_t end = 0;  ///< Offset after its last character.
    };

    /**
     * @brief Split like Boost.Process does for a command string: on spaces outside double quotes,
     * with the outer quotes of a word removed.
     */
    std::vector<word_t> split(const std::string &cmd) {
      std::vector<word_t> words;
      bool quoted = false;
      std::size_t begin = std::string::npos;
      for (std::size_t i = 0; i <= cmd.size(); ++i) {
        const bool at_end = i == cmd.size();
        const char c = at_end ? ' ' : cmd[i];
        if (!at_end && c == '"') {
          quoted = !quoted;
        }
        if ((c == ' ' && !quoted) || at_end) {
          if (begin != std::string::npos) {
            std::string text = cmd.substr(begin, i - begin);
            if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
              text = text.substr(1, text.size() - 2);
            }
            words.push_back({std::move(text), begin, i});
            begin = std::string::npos;
          }
          continue;
        }
        if (begin == std::string::npos) {
          begin = i;
        }
      }
      return words;
    }

    std::string basename_of(std::string_view path) {
      const auto slash = path.rfind('/');
      return lower(slash == std::string_view::npos ? path : path.substr(slash + 1));
    }

    bool is_assignment(std::string_view word) {
      const auto eq = word.find('=');
      return eq != std::string_view::npos && eq > 0 && word.front() != '-';
    }

    bool numeric(std::string_view word) {
      return !word.empty() && std::ranges::all_of(word, [](unsigned char c) {
        return std::isdigit(c) || c == '-' || c == '+';
      });
    }

    bool starts_with_any(const std::string &name, std::initializer_list<std::string_view> prefixes) {
      return std::ranges::any_of(prefixes, [&](std::string_view p) {
        return name.starts_with(p);
      });
    }

    /**
     * @brief The browser family a Chromium-based program belongs to, or empty.
     */
    std::string chromium_family(const std::string &name) {
      if (name.starts_with("google-chrome") || name == "chrome") {
        return "google-chrome";
      }
      for (const auto family : {"chromium"sv, "brave"sv, "microsoft-edge"sv, "vivaldi"sv, "opera"sv, "thorium"sv, "ungoogled-chromium"sv}) {
        if (name.starts_with(family)) {
          return std::string {family};
        }
      }
      return {};
    }

    bool firefox_name(const std::string &name) {
      return starts_with_any(name, {"firefox", "librewolf", "waterfox", "floorp", "zen-browser"});
    }

    bool electron_name(const std::string &name) {
      static constexpr std::array names {
        "code"sv,
        "code-insiders"sv,
        "codium"sv,
        "vscodium"sv,
        "cursor"sv,
        "discord"sv,
        "discord-ptb"sv,
        "discord-canary"sv,
        "vesktop"sv,
        "slack"sv,
        "obsidian"sv,
        "signal-desktop"sv,
        "element-desktop"sv,
        "teams-for-linux"sv,
        "joplin"sv,
        "notion-app"sv,
        "bitwarden"sv,
      };
      return std::ranges::find(names, std::string_view {name}) != names.end();
    }

    /**
     * @brief The Flatpak app id's kind, from its reverse-DNS name.
     */
    kind_e flatpak_kind(const std::string &id) {
      const auto l = lower(id);
      const auto any = [&](std::initializer_list<std::string_view> ids) {
        return std::ranges::any_of(ids, [&](std::string_view known) {
          return l.starts_with(known);
        });
      };
      if (any({"com.google.chrome"sv, "org.chromium.chromium"sv, "com.brave.browser"sv, "com.microsoft.edge"sv, "com.vivaldi.vivaldi"sv, "com.opera.opera"sv}) ||
          l.find("ungoogled_chromium") != std::string::npos) {
        return kind_e::chromium;
      }
      if (any({"org.mozilla.firefox"sv, "io.gitlab.librewolf-community"sv, "net.waterfox.waterfox"sv, "one.ablaze.floorp"sv})) {
        return kind_e::firefox;
      }
      if (any({"com.visualstudio.code"sv, "com.vscodium.codium"sv, "com.discordapp.discord"sv, "com.slack.slack"sv, "md.obsidian.obsidian"sv, "org.signal.signal"sv, "im.riot.riot"sv, "dev.vencord.vesktop"sv})) {
        return kind_e::electron;
      }
      return kind_e::other;
    }

    std::string quote_if_needed(const std::string &word) {
      return word.find(' ') == std::string::npos ? word : '"' + word + '"';
    }

    fs::path search_path(const std::string &program) {
      if (program.find('/') != std::string::npos) {
        return program;
      }
      const char *path = std::getenv("PATH");
      std::string_view dirs = path ? path : "/usr/local/bin:/usr/bin:/bin";
      while (!dirs.empty()) {
        const auto colon = dirs.find(':');
        const auto dir = dirs.substr(0, colon);
        std::error_code ec;
        if (!dir.empty()) {
          const auto candidate = fs::path {std::string {dir}} / program;
          if (fs::exists(candidate, ec)) {
            return candidate;
          }
        }
        if (colon == std::string_view::npos) {
          break;
        }
        dirs.remove_prefix(colon + 1);
      }
      return {};
    }
  }  // namespace

  std::optional<program_t> find_program(const std::string &cmd) {
    const auto words = split(cmd);
    std::size_t i = 0;
    while (i < words.size()) {
      const auto &w = words[i].text;
      const auto name = basename_of(w);
      if (is_assignment(w)) {
        ++i;
      } else if (name == "env") {
        ++i;
        while (i < words.size() && (is_assignment(words[i].text) || words[i].text.starts_with('-'))) {
          ++i;
        }
      } else if (name == "gamemoderun" || name == "mangohud" || name == "prime-run" || name == "nohup") {
        ++i;
      } else if (name == "nice" || name == "ionice") {
        ++i;
        while (i < words.size() && (words[i].text.starts_with('-') || numeric(words[i].text))) {
          ++i;
        }
      } else {
        break;
      }
    }
    if (i >= words.size()) {
      return std::nullopt;
    }
    program_t program;
    program.path = words[i].text;
    program.name = basename_of(program.path);
    if (program.name == "sh" || program.name == "bash" || program.name == "dash" || program.name == "zsh") {
      return std::nullopt;  // a shell script line: Nova can't tell what it runs
    }
    program.insert_at = words[i].end;
    std::size_t rest = i + 1;
    if (program.name == "flatpak" && rest < words.size() && words[rest].text == "run") {
      std::size_t j = rest + 1;
      while (j < words.size() && words[j].text.starts_with('-')) {
        ++j;
      }
      if (j < words.size()) {
        program.flatpak_id = words[j].text;
        program.insert_at = words[j].end;
        rest = j + 1;
      }
    }
    for (std::size_t j = rest; j < words.size(); ++j) {
      program.args.push_back(words[j].text);
    }
    return program;
  }

  bool looks_electron(const std::string &program) {
    std::error_code ec;
    auto path = search_path(program);
    if (path.empty()) {
      return false;
    }
    path = fs::canonical(path, ec);
    if (ec) {
      return false;
    }
    for (auto dir = path.parent_path(); !dir.empty(); dir = dir.parent_path()) {
      if (fs::exists(dir / "resources.pak", ec) &&
          (fs::exists(dir / "resources" / "app.asar", ec) || fs::is_directory(dir / "resources" / "app", ec))) {
        return true;
      }
      if (dir == path.parent_path().parent_path() || dir == dir.root_path()) {
        break;  // the program's directory and the one above (VS Code keeps a bin/code script)
      }
    }
    return false;
  }

  kind_e classify(const program_t &program, const std::function<bool(const std::string &)> &is_electron) {
    if (!program.flatpak_id.empty()) {
      return flatpak_kind(program.flatpak_id);
    }
    if (!chromium_family(program.name).empty()) {
      return kind_e::chromium;
    }
    if (firefox_name(program.name)) {
      return kind_e::firefox;
    }
    if (electron_name(program.name) || (is_electron && is_electron(program.path))) {
      return kind_e::electron;
    }
    return kind_e::other;
  }

  fs::path profile_dir(const program_t &program, const kind_e kind, const fs::path &config_dir, const fs::path &home) {
    if (!program.flatpak_id.empty()) {
      return home / ".var" / "app" / program.flatpak_id / "nova-virtual-display";
    }
    if (kind == kind_e::chromium) {
      const auto family = chromium_family(program.name);
      if (family == "google-chrome") {
        return config_dir / "virtual-display-browser";  // shared with nova-vd-session's browser
      }
      return config_dir / "virtual-display-profiles" / family;
    }
    return config_dir / "virtual-display-profiles" / program.name;
  }

  adapted_t adapt(const std::string &cmd, const fs::path &config_dir, const fs::path &home, const std::function<bool(const std::string &)> &is_electron) {
    adapted_t out;
    out.cmd = cmd;
    const auto program = find_program(cmd);
    if (!program) {
      return out;
    }
    out.kind = classify(*program, is_electron);
    if (out.kind == kind_e::other) {
      return out;
    }
    const auto has = [&](std::initializer_list<std::string_view> flags) {
      return std::ranges::any_of(program->args, [&](const std::string &arg) {
        return std::ranges::any_of(flags, [&](std::string_view f) {
          return arg == f || (arg.starts_with(f) && arg.size() > f.size() && arg[f.size()] == '=');
        });
      });
    };
    std::string insert;
    const auto dir = profile_dir(*program, out.kind, config_dir, home);
    if (out.kind == kind_e::firefox) {
      if (has({"-P", "-p", "--P", "-profile", "--profile", "-no-remote", "--no-remote", "-new-instance", "--new-instance"})) {
        out.note = "it picks its own profile or instance";
        return out;
      }
      insert = " --no-remote --profile " + quote_if_needed(dir.string());
    } else {
      if (has({"--user-data-dir", "-user-data-dir"})) {
        out.note = "it names its own --user-data-dir";
        return out;
      }
      insert = " " + quote_if_needed("--user-data-dir=" + dir.string());
      if (out.kind == kind_e::chromium) {
        insert += " --no-first-run";
      }
    }
    out.cmd = cmd.substr(0, program->insert_at) + insert + cmd.substr(program->insert_at);
    out.profile = dir;
    return out;
  }

  bool uses_session_bus(const std::string &cmd) {
    const auto program = find_program(cmd);
    if (!program) {
      return true;  // a shell line: most likely a desktop program too
    }
    const auto &n = program->name;
    return !(n == "nova-proton-run" || n.starts_with("umu-run") || n.starts_with("wine") || n == "proton" || n == "steam" ||
             n == "gamescope" || n == "heroic" || program->flatpak_id == "com.valvesoftware.Steam");
  }

  const std::vector<std::string> &session_env_keys() {
    static const std::vector<std::string> keys {
      "DBUS_SESSION_BUS_ADDRESS",
      "DCONF_PROFILE",
      "GIO_USE_VFS",
      "GIO_USE_VOLUME_MONITOR",
      "GTK_USE_PORTAL",
      "NO_AT_BRIDGE",
    };
    return keys;
  }

  env_list_t session_env(const std::string &session_dir, const std::chrono::milliseconds wait) {
    if (session_dir.empty()) {
      return {};
    }
    const auto file = fs::path {session_dir} / "app-env.json";
    const auto deadline = std::chrono::steady_clock::now() + wait;
    std::error_code ec;
    while (!fs::exists(file, ec) && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(50ms);
    }
    std::ifstream in {file};
    if (!in) {
      return {};
    }
    env_list_t env;
    try {
      const auto tree = nlohmann::json::parse(std::string {std::istreambuf_iterator<char> {in}, std::istreambuf_iterator<char> {}});
      for (const auto &key : session_env_keys()) {
        if (tree.contains(key) && tree[key].is_string() && !tree[key].get<std::string>().empty()) {
          env.emplace_back(key, tree[key].get<std::string>());
        }
      }
    } catch (const std::exception &) {
      return {};
    }
    // Without the bus the other variables would only half-isolate the app.
    const bool has_bus = std::ranges::any_of(env, [](const auto &kv) {
      return kv.first == "DBUS_SESSION_BUS_ADDRESS";
    });
    return has_bus ? env : env_list_t {};
  }
}  // namespace vd_app_launch
