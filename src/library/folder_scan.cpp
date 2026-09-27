/**
 * @file src/library/folder_scan.cpp
 * @brief Definitions for detecting games inside a folder the user picked.
 */
// standard includes
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <optional>
#include <system_error>

// local includes
#include "folder_scan.h"
#include "title.h"

namespace fs = std::filesystem;

namespace library::folder {
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
     * @brief Whether a string contains any of the given fragments.
     *
     * @param haystack Lowercased text.
     * @param needles Lowercased fragments.
     * @return True when one fragment is present.
     */
    template<std::size_t N>
    bool contains_any(const std::string &haystack, const std::array<std::string_view, N> &needles) {
      return std::ranges::any_of(needles, [&haystack](std::string_view needle) {
        return haystack.find(needle) != std::string::npos;
      });
    }

    /**
     * @brief Read the first bytes of a file.
     *
     * @param path File to read.
     * @param count Number of bytes.
     * @return The bytes read (possibly fewer).
     */
    std::string head_bytes(const fs::path &path, std::size_t count) {
      std::ifstream in(path, std::ios::binary);
      std::string buf(count, '\0');
      in.read(buf.data(), static_cast<std::streamsize>(count));
      buf.resize(static_cast<std::size_t>(std::max<std::streamsize>(0, in.gcount())));
      return buf;
    }

    /**
     * @brief Whether a file is a Windows PE executable (starts with "MZ").
     *
     * @param path File to check.
     * @return True for PE files.
     */
    bool is_pe(const fs::path &path) {
      return head_bytes(path, 2) == "MZ";
    }

    /**
     * @brief Whether a file is a Linux ELF executable with the user-execute bit.
     *
     * @param path File to check.
     * @return True for executable ELF files.
     */
    bool is_native_binary(const fs::path &path) {
      std::error_code ec;
      const auto perms = fs::status(path, ec).permissions();
      if (ec || (perms & fs::perms::owner_exec) == fs::perms::none) {
        return false;
      }
      return head_bytes(path, 4) == "\x7F" "ELF";
    }

    /**
     * @brief A launchable file found in a game folder.
     */
    struct candidate_t {
      fs::path path;  ///< File path.
      double score;  ///< Likelihood it is the game.
      bool windows;  ///< True for .exe, false for native binaries and scripts.
    };

    /**
     * @brief Names that mean "this folder is a generic sub-folder, not a game".
     */
    constexpr std::array<std::string_view, 8> generic_names {"bin", "binaries", "win64", "x64", "game", "games", "app", "data"};

    /**
     * @brief Collect launchable files below a game folder.
     *
     * @param game_dir Folder to search.
     * @param options Depth and entry limits.
     * @return Candidates in no particular order.
     */
    std::vector<candidate_t> collect_candidates(const fs::path &game_dir, const options_t &options) {
      std::vector<candidate_t> out;
      std::error_code ec;
      fs::recursive_directory_iterator it(game_dir, fs::directory_options::skip_permission_denied, ec);
      std::size_t visited = 0;
      for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (++visited > options.max_entries_per_game) {
          break;
        }
        const auto &entry = *it;
        const auto name = entry.path().filename().string();
        std::error_code type_ec;
        if (entry.is_directory(type_ec)) {
          if (it.depth() >= options.max_depth - 1 || is_excluded_dir(name) || entry.is_symlink(type_ec)) {
            it.disable_recursion_pending();
          }
          continue;
        }
        if (!entry.is_regular_file(type_ec)) {
          continue;
        }

        const auto lname = lower(name);
        const auto size = entry.file_size(type_ec);
        if (lname.ends_with(".exe")) {
          if (is_excluded_exe(name) || size < 200 * 1024 || !is_pe(entry.path())) {
            continue;
          }
          out.push_back({entry.path(), score_windows_exe(game_dir, entry.path(), size), true});
        } else if (it.depth() == 0 && (lname == "start.sh" || lname == "run.sh" || lname.ends_with(".appimage"))) {
          out.push_back({entry.path(), 90.0, false});
        } else if (it.depth() <= 1 && (lname.ends_with(".x86_64") || lname.find('.') == std::string::npos) && size > 100 * 1024 && is_native_binary(entry.path())) {
          const auto folder_title = title::clean(game_dir.filename().string());
          out.push_back({entry.path(), 40.0 + 50.0 * title::similarity(entry.path().stem().string(), folder_title), false});
        }
      }
      return out;
    }

    /**
     * @brief Whether a folder has any launchable file within the scan depth.
     *
     * @param dir Folder to check.
     * @param options Scan limits.
     * @return True when at least one candidate exists.
     */
    bool has_candidates(const fs::path &dir, const options_t &options) {
      return !collect_candidates(dir, options).empty();
    }

    /**
     * @brief Sub-folder names that hold a game's own binaries rather than other games.
     */
    constexpr std::array<std::string_view, 10> binary_dirs {"bin", "bin64", "binaries", "x64", "win64", "game", "engine", "retail", "_retail_", "system"};

    /**
     * @brief Whether a folder is one game (as opposed to a folder of games).
     *
     * True when it has a launchable file at its top level, keeps its executables under
     * engine-style folders (bin/, Binaries/, ...), or is an Unreal game (Engine/ plus <Project>/Binaries/Win64).
     *
     * @param dir Folder to check.
     * @param options Scan limits.
     * @return True for a game folder.
     */
    bool is_game_dir(const fs::path &dir, const options_t &options) {
      options_t shallow = options;
      shallow.max_depth = 1;
      if (has_candidates(dir, shallow)) {
        return true;
      }
      std::error_code ec;
      for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::error_code type_ec;
        if (!it->is_directory(type_ec)) {
          continue;
        }
        const auto name = lower(it->path().filename().string());
        if (std::ranges::find(binary_dirs, name) != binary_dirs.end() && has_candidates(it->path(), options)) {
          return true;
        }
        // Unreal layout: <game>/Engine next to <game>/<Project>/Binaries/Win64.
        if (fs::is_directory(it->path() / "Binaries" / "Win64", type_ec) && fs::is_directory(dir / "Engine", type_ec)) {
          return true;
        }
      }
      return false;
    }

    /**
     * @brief Turn one game folder into a detected game or a skipped entry.
     *
     * @param dir Game folder.
     * @param options Scan settings.
     * @param result Output.
     */
    void evaluate_game_dir(const fs::path &dir, const options_t &options, scan_result_t &result) {
      if (looks_like_installer(dir)) {
        result.skipped.push_back({dir.string(), "Looks like an installer (setup program and data files). Install the game, then scan the install folder."});
        return;
      }

      auto candidates = collect_candidates(dir, options);
      std::erase_if(candidates, [](const candidate_t &c) {
        return c.score < 0;
      });
      if (candidates.empty()) {
        result.skipped.push_back({dir.string(), "No game program found."});
        return;
      }
      const auto best = std::ranges::max_element(candidates, {}, &candidate_t::score);

      detected_game_t game;
      auto name = dir.filename().string();
      if (std::ranges::find(generic_names, lower(name)) != generic_names.end()) {
        name = best->path.stem().string();
      }
      game.title = title::clean(name);
      game.source = source_e::folder;
      game.executable = best->path;
      game.source_id = best->path.string();
      game.working_dir = best->path.parent_path().string();
      game.launch_cmd = best->windows ? windows_command(options.windows_launcher, best->path) : quote(best->path);
      if (game.launch_cmd.empty()) {
        result.skipped.push_back({best->path.string(), "The path contains a double quote, which Nova can't pass safely."});
        return;
      }
      result.games.push_back(std::move(game));
    }

    /**
     * @brief Walk container folders until game folders are found.
     *
     * @param dir Folder to inspect.
     * @param depth Container levels already descended.
     * @param options Scan settings.
     * @param result Output.
     */
    void walk(const fs::path &dir, int depth, const options_t &options, scan_result_t &result) {
      std::error_code ec;
      std::vector<fs::path> children;
      for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::error_code type_ec;
        if (it->is_directory(type_ec) && !it->is_symlink(type_ec) && !is_excluded_dir(it->path().filename().string()) &&
            !it->path().filename().string().starts_with('.')) {
          children.push_back(it->path());
        }
      }
      std::ranges::sort(children);

      for (const auto &child : children) {
        if (result.games.size() >= options.max_games) {
          return;
        }
        if (looks_like_installer(child) || is_game_dir(child, options)) {
          evaluate_game_dir(child, options, result);
        } else if (depth + 1 < options.max_container_depth && has_candidates(child, options)) {
          walk(child, depth + 1, options, result);
        } else if (has_candidates(child, options)) {
          evaluate_game_dir(child, options, result);
        } else {
          result.skipped.push_back({child.string(), "No game program found."});
        }
      }
    }
  }  // namespace

  bool is_excluded_exe(std::string_view filename) {
    const auto name = lower(filename);
    static constexpr std::array<std::string_view, 36> fragments {
      "unins", "uninstall", "setup", "installer", "install.exe", "redist", "vcredist", "vc_redist", "dxsetup", "dxwebsetup",
      "directx", "dotnet", "ndp4", "crashreport", "crashhandler", "crashpad", "bugreport", "errorreport", "easyanticheat",
      "eac_launcher", "battleye", "beservice", "be_service", "prereq", "ue4prereq", "ue5prereq", "cefprocess", "quicksfv",
      "notification_helper", "vivox", "physx", "oalinst", "touchup", "cleanup", "7z", "unrar"
    };
    static constexpr std::array<std::string_view, 6> exact {
      "upc.exe", "uplaywebcore.exe", "steamerrorreporter.exe", "steamerrorreporter64.exe", "dxdiag.exe", "benchmark.exe"
    };
    return contains_any(name, fragments) || std::ranges::find(exact, name) != exact.end();
  }

  bool is_excluded_dir(std::string_view dirname) {
    static constexpr std::array<std::string_view, 24> names {
      "_commonredist", "commonredist", "redist", "redists", "redistributables", "directx", "dotnet", "vcredist", "__installer",
      "_installer", "installer", "installers", "support", "prereqs", "prerequisites", "easyanticheat", "battleye",
      "crashreporter", "crashreport", "__macosx", "$recycle.bin", "system volume information", "thirdparty", "_redist"
    };
    return std::ranges::find(names, lower(dirname)) != names.end();
  }

  bool looks_like_installer(const fs::path &dir) {
    bool has_setup = false;
    bool has_data = false;
    bool has_other_program = false;
    std::error_code ec;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
      std::error_code type_ec;
      if (!it->is_regular_file(type_ec)) {
        continue;
      }
      const auto name = lower(it->path().filename().string());
      const auto ext = lower(it->path().extension().string());
      if (name.starts_with("setup") && ext == ".exe") {
        has_setup = true;
      } else if (ext == ".exe" && !is_excluded_exe(name)) {
        has_other_program = true;
      } else if (ext == ".bin" || ext == ".doi" || ext == ".cab" || ext == ".rar" || ext == ".7z" || ext == ".iso" || ext == ".esd" ||
                 ext == ".tmp" || name.starts_with("data") || name.starts_with("fg-") || name.starts_with("setup-")) {
        has_data = true;
      }
    }
    return has_setup && has_data && !has_other_program;
  }

  double score_windows_exe(const fs::path &game_dir, const fs::path &exe, std::uintmax_t size_bytes) {
    const auto name = lower(exe.filename().string());
    if (is_excluded_exe(name)) {
      return -1.0;
    }
    const auto stem = exe.stem().string();
    const auto folder_title = title::clean(game_dir.filename().string());

    double score = 60.0 * title::similarity(stem, folder_title);
    const double mb = static_cast<double>(size_bytes) / (1024.0 * 1024.0);
    score += std::min(40.0, 6.0 * std::log2(mb + 1.0));

    std::error_code ec;
    const auto rel = lower(fs::relative(exe.parent_path(), game_dir, ec).generic_string());
    const auto depth = rel.empty() || rel == "." ? 0 : static_cast<int>(std::ranges::count(rel, '/') + 1);
    score -= 8.0 * depth;
    if (rel.find("binaries/win64") != std::string::npos) {
      score += 18.0;
    } else if (rel.find("bin/x64") != std::string::npos || rel.find("bin64") != std::string::npos || rel == "bin") {
      score += 8.0;
    }

    if (name.ends_with("-win64-shipping.exe")) {
      score += 30.0;
    }
    // Unreal root bootstrapper ("b1.exe" next to "b1/Binaries/Win64/b1-Win64-Shipping.exe") is the intended launcher.
    if (depth == 0 && fs::exists(game_dir / stem / "Binaries" / "Win64" / (stem + "-Win64-Shipping.exe"), ec)) {
      score += 60.0;
    }
    if (name.find("fitgirl-launcher") != std::string::npos) {
      score += 25.0;
    } else if (name.find("launcher") != std::string::npos) {
      score -= 10.0;
    }
    if (name.find("dedicated") != std::string::npos || name.find("server") != std::string::npos || name.find("editor") != std::string::npos ||
        name.find("config") != std::string::npos || name.find("settings") != std::string::npos) {
      score -= 30.0;
    }
    return score;
  }

  std::string quote(const fs::path &path) {
    const auto s = path.string();
    if (s.find('"') != std::string::npos) {
      return {};
    }
    std::string out = "\"";
    for (const char c : s) {
      if (c == '$') {
        out += "$$";
      } else {
        out += c;
      }
    }
    out += '"';
    return out;
  }

  std::string windows_command(std::string_view launcher, const fs::path &exe) {
    const auto quoted = quote(exe);
    if (quoted.empty()) {
      return {};
    }
    if (launcher.empty()) {
      return quoted;
    }
    std::string cmd(launcher);
    if (const auto pos = cmd.find("{exe}"); pos != std::string::npos) {
      cmd.replace(pos, 5, quoted);
      return cmd;
    }
    return cmd + " " + quoted;
  }

  scan_result_t scan(const fs::path &root, const options_t &options) {
    scan_result_t result;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
      result.skipped.push_back({root.string(), "Folder not found."});
      return result;
    }
    // A folder that is itself one game (the user picked the install folder).
    if (looks_like_installer(root)) {
      evaluate_game_dir(root, options, result);
      return result;
    }
    if (is_game_dir(root, options)) {
      evaluate_game_dir(root, options, result);
      return result;
    }
    walk(root, 0, options, result);
    return result;
  }

}  // namespace library::folder
