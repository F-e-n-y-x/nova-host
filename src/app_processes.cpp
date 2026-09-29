/**
 * @file src/app_processes.cpp
 * @brief Nova: the running app's processes (see app_processes.h).
 */
// standard includes
#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_map>

// local includes
#include "app_processes.h"

using namespace std::literals;

namespace app_processes {
  namespace {
    std::string lower(std::string_view s) {
      std::string out {s};
      std::ranges::transform(out, out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      return out;
    }

    /**
     * @brief Basename of a Unix or Windows path ("C:\\windows\\system32\\services.exe" -> "services.exe").
     */
    std::string basename_of(std::string_view path) {
      const auto slash = path.find_last_of("/\\");
      return lower(slash == std::string_view::npos ? path : path.substr(slash + 1));
    }

    /**
     * @brief Wine's own processes and the plumbing around a Proton game (seen in a real umu-run tree:
     * umu-run, srt-bwrap, pv-adverb, dbus-launch, dbus-daemon, at-spi-bus-launcher, the proton script,
     * umu.exe, wineserver, services.exe, winedevice.exe, plugplay.exe, svchost.exe, explorer.exe,
     * rpcss.exe, tabtip.exe, xalia.exe).
     */
    constexpr std::array helper_names_exact {
      // Wine
      "wineserver"sv,
      "services.exe"sv,
      "winedevice.exe"sv,
      "plugplay.exe"sv,
      "svchost.exe"sv,
      "explorer.exe"sv,
      "rpcss.exe"sv,
      "tabtip.exe"sv,
      "xalia.exe"sv,
      "conhost.exe"sv,
      "start.exe"sv,
      "wineboot.exe"sv,
      "winedbg.exe"sv,
      "winedbg"sv,
      "steam.exe"sv,  // Proton's launcher shim, not the Steam client
      "umu.exe"sv,
      // umu / Proton / Steam Runtime
      "umu-run"sv,
      "umu_run.py"sv,
      "proton"sv,
      "gamemoderun"sv,
      "nova-proton-run"sv,
      "reaper"sv,
      "bwrap"sv,
      "srt-bwrap"sv,
      "pv-adverb"sv,
      "pv-bwrap"sv,
      "_v2-entry-point"sv,
      "run-in-sniper"sv,
      "run-in-soldier"sv,
      "run-in-steamrt"sv,
      "steamrt-run"sv,
      // started inside the container
      "dbus-launch"sv,
      "dbus-daemon"sv,
      "at-spi-bus-launcher"sv,
      "at-spi-bus-laun"sv,  // comm is cut at 15 characters
      "at-spi2-registryd"sv,
    };

    constexpr std::array helper_prefixes {
      "pressure-vessel"sv,
      "steam-runtime-"sv,
      "srt-"sv,
      "pv-"sv,
    };

    bool helper_name(const std::string &name) {
      if (std::ranges::find(helper_names_exact, std::string_view {name}) != helper_names_exact.end()) {
        return true;
      }
      return std::ranges::any_of(helper_prefixes, [&](std::string_view p) {
        return name.starts_with(p);
      });
    }

    bool interpreter(const std::string &name) {
      return name == "sh" || name == "bash" || name == "dash" || name.starts_with("python");
    }
  }  // namespace

  std::string name_of(const process_t &process) {
    if (!process.argv.empty() && !process.argv.front().empty()) {
      // Wine rewrites argv[0] to the Windows path of the .exe; its first word is the program.
      auto first = std::string_view {process.argv.front()};
      if (first.find_first_of("/\\") == std::string_view::npos) {
        first = first.substr(0, first.find(' '));
      }
      auto name = basename_of(first);
      if (!name.empty()) {
        return name;
      }
    }
    return lower(process.comm);
  }

  bool is_helper(const process_t &process) {
    const auto name = name_of(process);
    if (helper_name(name) || helper_name(lower(process.comm))) {
      return true;
    }
    if (interpreter(name)) {
      // "python3 /usr/bin/umu-run", "python3 .../GE-Proton/proton waitforexitandrun", "bash .../gamemoderun".
      for (std::size_t i = 1; i < process.argv.size(); ++i) {
        const auto &arg = process.argv[i];
        if (arg.empty() || arg.front() == '-') {
          continue;
        }
        return helper_name(basename_of(arg));
      }
    }
    return false;
  }

  std::string members_t::helper_names() const {
    std::set<std::string> names;
    for (const auto &p : processes) {
      if (is_helper(p)) {
        names.insert(name_of(p));
      }
    }
    std::string out;
    for (const auto &n : names) {
      if (!out.empty()) {
        out += ", ";
      }
      out += n;
    }
    return out;
  }

  members_t members(const std::vector<process_t> &all, const int root_pid, const int pgid) {
    std::unordered_multimap<int, const process_t *> children;
    std::set<int> in;
    std::vector<int> todo;
    for (const auto &p : all) {
      children.emplace(p.ppid, &p);
      if ((pgid > 0 && p.pgid == pgid) || (root_pid > 0 && p.pid == root_pid)) {
        if (in.insert(p.pid).second) {
          todo.push_back(p.pid);
        }
      }
    }
    while (!todo.empty()) {
      const int pid = todo.back();
      todo.pop_back();
      const auto [begin, end] = children.equal_range(pid);
      for (auto it = begin; it != end; ++it) {
        if (in.insert(it->second->pid).second) {
          todo.push_back(it->second->pid);
        }
      }
    }
    members_t out;
    for (const auto &p : all) {
      if (p.state == 'Z' || p.state == 'X' || !in.contains(p.pid)) {
        continue;
      }
      if (!is_helper(p)) {
        ++out.games;
      }
      out.processes.push_back(p);
    }
    return out;
  }

  bool linger_t::update(const members_t &seen, const clock::time_point now) {
    if (seen.games > 0) {
      game_seen_ = true;
      helpers_only_since_.reset();
      return false;
    }
    if (!game_seen_ || seen.processes.empty()) {
      // Still starting, or nothing left at all (the normal exit path handles that).
      helpers_only_since_.reset();
      return false;
    }
    if (!helpers_only_since_) {
      helpers_only_since_ = now;
    }
    return now - *helpers_only_since_ >= grace;
  }

  void linger_t::reset() {
    game_seen_ = false;
    helpers_only_since_.reset();
  }

  bool linger_t::game_seen() const {
    return game_seen_;
  }

  std::vector<process_t> snapshot() {
    std::vector<process_t> out;
#ifdef __linux__
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator {"/proc", ec}) {
      const auto name = entry.path().filename().string();
      if (name.empty() || !std::ranges::all_of(name, [](unsigned char c) {
            return std::isdigit(c);
          })) {
        continue;
      }
      std::ifstream stat_file {entry.path() / "stat"};
      std::string stat {std::istreambuf_iterator<char> {stat_file}, std::istreambuf_iterator<char> {}};
      // "<pid> (<comm>) <state> <ppid> <pgrp> ..."; comm may contain spaces and parentheses.
      const auto open = stat.find('(');
      const auto close = stat.rfind(')');
      if (open == std::string::npos || close == std::string::npos || close < open) {
        continue;
      }
      process_t p;
      p.comm = stat.substr(open + 1, close - open - 1);
      std::istringstream rest {stat.substr(close + 1)};
      std::string state;
      rest >> state >> p.ppid >> p.pgid;
      if (!rest || state.empty()) {
        continue;
      }
      p.state = state.front();
      try {
        p.pid = std::stoi(name);
      } catch (const std::exception &) {
        continue;
      }
      std::ifstream cmdline_file {entry.path() / "cmdline", std::ios::binary};
      std::string cmdline {std::istreambuf_iterator<char> {cmdline_file}, std::istreambuf_iterator<char> {}};
      std::size_t start = 0;
      while (start < cmdline.size()) {
        auto end = cmdline.find('\0', start);
        if (end == std::string::npos) {
          end = cmdline.size();
        }
        p.argv.emplace_back(cmdline.substr(start, end - start));
        start = end + 1;
      }
      out.push_back(std::move(p));
    }
#endif
    return out;
  }
}  // namespace app_processes
