/**
 * @file src/display_modeset.cpp
 * @brief Definitions for Nova's native Mirror mode switching (xrandr and nvidia-settings).
 */
// standard includes
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <format>
#include <fstream>
#include <sstream>
#include <thread>
#include <tuple>

#ifndef _WIN32
  #include <cerrno>
  #include <csignal>
  #include <fcntl.h>
  #include <poll.h>
  #include <spawn.h>
  #include <sys/wait.h>
  #include <unistd.h>
extern char **environ;
#endif

// local includes
#include "display_modeset.h"
#include "logging.h"

using namespace std::literals;

namespace display_modeset {
  namespace {
    constexpr auto helper_timeout = 10s;

    std::string_view trim(std::string_view s) {
      while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) {
        s.remove_prefix(1);
      }
      while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
        s.remove_suffix(1);
      }
      return s;
    }

    std::vector<std::string_view> lines_of(std::string_view text) {
      std::vector<std::string_view> lines;
      while (!text.empty()) {
        const auto nl = text.find('\n');
        lines.push_back(text.substr(0, nl));
        if (nl == std::string_view::npos) {
          break;
        }
        text.remove_prefix(nl + 1);
      }
      return lines;
    }

    std::vector<std::string_view> words_of(std::string_view line) {
      std::vector<std::string_view> words;
      std::size_t i = 0;
      while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
          ++i;
        }
        const auto start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t') {
          ++i;
        }
        if (i > start) {
          words.push_back(line.substr(start, i - start));
        }
      }
      return words;
    }

    /**
     * @brief Parse "WxH" (digits only).
     */
    std::optional<std::pair<int, int>> parse_size(std::string_view s) {
      const auto x = s.find('x');
      if (x == std::string_view::npos || x == 0 || x + 1 >= s.size()) {
        return std::nullopt;
      }
      int w = 0;
      int h = 0;
      for (const char c : s.substr(0, x)) {
        if (c < '0' || c > '9') {
          return std::nullopt;
        }
        w = w * 10 + (c - '0');
      }
      for (const char c : s.substr(x + 1)) {
        if (c < '0' || c > '9') {
          return std::nullopt;
        }
        h = h * 10 + (c - '0');
      }
      return std::make_pair(w, h);
    }

    /**
     * @brief Parse "WxH+X+Y".
     */
    bool parse_geometry(std::string_view s, output_t &out) {
      const auto plus = s.find('+');
      if (plus == std::string_view::npos) {
        return false;
      }
      const auto size = parse_size(s.substr(0, plus));
      if (!size) {
        return false;
      }
      const auto rest = s.substr(plus + 1);
      const auto plus2 = rest.find('+');
      if (plus2 == std::string_view::npos) {
        return false;
      }
      try {
        out.x = std::stoi(std::string {rest.substr(0, plus2)});
        out.y = std::stoi(std::string {rest.substr(plus2 + 1)});
      } catch (...) {
        return false;
      }
      out.width = size->first;
      out.height = size->second;
      return true;
    }

    int rotation_from_name(std::string_view name) {
      if (name == "left"sv) {
        return 90;
      }
      if (name == "inverted"sv) {
        return 180;
      }
      if (name == "right"sv) {
        return 270;
      }
      return 0;
    }

    std::string format_rate(double rate) {
      return std::format("{:.2f}", rate);
    }

    const mode_t *find_mode(const output_t &output, int w, int h) {
      for (const auto &mode : output.modes) {
        if (mode.width == w && mode.height == h) {
          return &mode;
        }
      }
      return nullptr;
    }

    const mode_t *covering_mode(const output_t &output, int w, int h) {
      const mode_t *best = nullptr;
      for (const auto &mode : output.modes) {
        if (mode.width >= w && mode.height >= h && (!best || static_cast<long long>(mode.width) * mode.height < static_cast<long long>(best->width) * best->height)) {
          best = &mode;
        }
      }
      return best;
    }

    const mode_t *largest_mode(const output_t &output) {
      const mode_t *best = nullptr;
      for (const auto &mode : output.modes) {
        if (!best || static_cast<long long>(mode.width) * mode.height > static_cast<long long>(best->width) * best->height) {
          best = &mode;
        }
      }
      return best;
    }

    const mode_t *current_mode(const output_t &output) {
      for (const auto &mode : output.modes) {
        if (mode.current_rate > 0) {
          return &mode;
        }
      }
      return nullptr;
    }

    std::vector<std::uint8_t> parse_hex(std::string_view hex) {
      std::vector<std::uint8_t> bytes;
      auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') {
          return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
          return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
          return c - 'A' + 10;
        }
        return -1;
      };
      for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        const int hi = nibble(hex[i]);
        const int lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) {
          break;
        }
        bytes.push_back(static_cast<std::uint8_t>(hi * 16 + lo));
      }
      return bytes;
    }

    /**
     * @brief The name before ':' of a MetaMode entry.
     */
    std::string_view entry_display(std::string_view entry) {
      const auto colon = entry.find(':');
      return colon == std::string_view::npos ? std::string_view {} : trim(entry.substr(0, colon));
    }

    bool names_match(std::string_view display, const std::vector<std::string> &names) {
      return std::ranges::any_of(names, [&](const std::string &n) {
        return n == display;
      });
    }
  }  // namespace

#ifndef _WIN32
  runner_t default_runner(std::string display, std::string xauthority) {
    return [display = std::move(display), xauthority = std::move(xauthority)](const std::vector<std::string> &argv) -> run_result_t {
      run_result_t result;
      if (argv.empty()) {
        return result;
      }
      std::vector<std::string> env_strings;
      for (char **e = environ; e && *e; ++e) {
        const std::string_view entry {*e};
        if (entry.starts_with("DISPLAY="sv) || (!xauthority.empty() && entry.starts_with("XAUTHORITY="sv))) {
          continue;
        }
        env_strings.emplace_back(entry);
      }
      env_strings.push_back("DISPLAY=" + display);
      if (!xauthority.empty()) {
        env_strings.push_back("XAUTHORITY=" + xauthority);
      }
      std::vector<char *> envp;
      for (auto &s : env_strings) {
        envp.push_back(s.data());
      }
      envp.push_back(nullptr);
      std::vector<std::string> args = argv;
      std::vector<char *> cargv;
      for (auto &a : args) {
        cargv.push_back(a.data());
      }
      cargv.push_back(nullptr);

      int out_pipe[2];
      if (pipe2(out_pipe, O_CLOEXEC) != 0) {
        return result;
      }
      posix_spawn_file_actions_t actions;
      posix_spawn_file_actions_init(&actions);
      posix_spawn_file_actions_adddup2(&actions, out_pipe[1], STDOUT_FILENO);
      posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
      posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
      pid_t pid = -1;
      const int rc = posix_spawnp(&pid, cargv[0], &actions, nullptr, cargv.data(), envp.data());
      posix_spawn_file_actions_destroy(&actions);
      close(out_pipe[1]);
      if (rc != 0) {
        close(out_pipe[0]);
        BOOST_LOG(debug) << "Display modeset: couldn't start "sv << argv[0] << ": "sv << std::strerror(rc);
        return result;
      }

      const auto deadline = std::chrono::steady_clock::now() + helper_timeout;
      char buf[4096];
      bool timed_out = false;
      while (true) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) {
          timed_out = true;
          break;
        }
        pollfd pfd {out_pipe[0], POLLIN, 0};
        const int pr = poll(&pfd, 1, static_cast<int>(left));
        if (pr < 0 && errno == EINTR) {
          continue;
        }
        if (pr <= 0) {
          timed_out = pr == 0;
          break;
        }
        const auto n = read(out_pipe[0], buf, sizeof(buf));
        if (n < 0 && errno == EINTR) {
          continue;
        }
        if (n <= 0) {
          break;
        }
        result.output.append(buf, static_cast<std::size_t>(n));
      }
      close(out_pipe[0]);
      if (timed_out) {
        kill(pid, SIGKILL);
      }
      int status = 0;
      while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
      }
      if (timed_out) {
        BOOST_LOG(error) << "Display modeset: "sv << argv[0] << " timed out"sv;
        result.exit_code = -1;
      } else {
        result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
      }
      return result;
    };
  }
#else
  runner_t default_runner(std::string, std::string) {
    return [](const std::vector<std::string> &) {
      return run_result_t {};
    };
  }
#endif

  std::vector<output_t> parse_xrandr(std::string_view text) {
    std::vector<output_t> outputs;
    for (const auto raw : lines_of(text)) {
      if (raw.empty()) {
        continue;
      }
      if (raw.front() != ' ' && raw.front() != '\t') {
        const auto words = words_of(raw);
        if (words.size() < 2 || words[0] == "Screen"sv) {
          continue;
        }
        if (words[1] != "connected"sv && words[1] != "disconnected"sv && words[1] != "unknown"sv) {
          continue;
        }
        output_t out;
        out.name = std::string {words[0]};
        out.connected = words[1] == "connected"sv;
        for (std::size_t i = 2; i < words.size(); ++i) {
          const auto w = words[i];
          if (w.starts_with('(')) {
            // "(0x1bd)" in --verbose is the mode id; "(normal left ..." starts the capability list.
            if (w.starts_with("(0x"sv)) {
              continue;
            }
            break;
          }
          if (w == "primary"sv) {
            out.primary = true;
          } else if (parse_geometry(w, out)) {
            out.enabled = true;
          } else if (w == "left"sv || w == "inverted"sv || w == "right"sv || w == "normal"sv) {
            out.rotation = rotation_from_name(w);
          }
        }
        outputs.push_back(std::move(out));
        continue;
      }
      if (outputs.empty()) {
        continue;
      }
      // Mode line: "   1920x1080     60.00*+ 119.88    59.94  " (query format only; --verbose
      // mode lines carry "(0x..)" and a pixel clock and are skipped).
      const auto words = words_of(raw);
      if (words.empty()) {
        continue;
      }
      const auto size = parse_size(words[0]);
      if (!size || (words.size() > 1 && words[1].starts_with('('))) {
        continue;
      }
      mode_t mode;
      mode.width = size->first;
      mode.height = size->second;
      for (std::size_t i = 1; i < words.size(); ++i) {
        std::string token {words[i]};
        const bool current = token.find('*') != std::string::npos;
        const bool preferred = token.find('+') != std::string::npos;
        std::erase(token, '*');
        std::erase(token, '+');
        if (token.empty()) {
          // "60.00 +" puts the flag in its own word.
          if (preferred) {
            mode.preferred = true;
          }
          if (current && !mode.rates.empty()) {
            mode.current_rate = mode.rates.back();
          }
          continue;
        }
        char *end = nullptr;
        const double rate = std::strtod(token.c_str(), &end);
        if (end == token.c_str() || rate <= 0) {
          continue;
        }
        mode.rates.push_back(rate);
        if (current) {
          mode.current_rate = rate;
        }
        if (preferred) {
          mode.preferred = true;
        }
      }
      auto &modes = outputs.back().modes;
      if (const auto it = std::ranges::find_if(modes, [&](const mode_t &m) {
            return m.width == mode.width && m.height == mode.height;
          });
          it != modes.end()) {
        // xrandr lists the same size twice when two modelines share it: merge the rates.
        it->rates.insert(it->rates.end(), mode.rates.begin(), mode.rates.end());
        it->preferred = it->preferred || mode.preferred;
        if (mode.current_rate > 0) {
          it->current_rate = mode.current_rate;
        }
      } else {
        modes.push_back(std::move(mode));
      }
    }
    return outputs;
  }

  std::map<std::string, std::string> parse_edid_names(std::string_view text) {
    std::map<std::string, std::string> names;
    std::string current;
    std::string hex;
    bool in_edid = false;
    auto flush = [&]() {
      if (in_edid && !current.empty()) {
        if (auto name = edid_monitor_name(parse_hex(hex)); !name.empty()) {
          names[current] = std::move(name);
        }
      }
      in_edid = false;
      hex.clear();
    };
    for (const auto raw : lines_of(text)) {
      if (!raw.empty() && raw.front() != ' ' && raw.front() != '\t') {
        flush();
        const auto words = words_of(raw);
        current = words.size() >= 2 && (words[1] == "connected"sv || words[1] == "disconnected"sv) ? std::string {words[0]} : std::string {};
        continue;
      }
      const auto line = trim(raw);
      if (line == "EDID:"sv) {
        flush();
        in_edid = true;
        continue;
      }
      if (in_edid) {
        const bool is_hex = !line.empty() && std::ranges::all_of(line, [](char c) {
          return std::isxdigit(static_cast<unsigned char>(c));
        });
        if (is_hex) {
          hex += line;
        } else {
          flush();
        }
      }
    }
    flush();
    return names;
  }

  std::string edid_monitor_name(const std::vector<std::uint8_t> &edid) {
    if (edid.size() < 128) {
      return {};
    }
    for (std::size_t off = 54; off + 18 <= 126; off += 18) {
      if (edid[off] == 0 && edid[off + 1] == 0 && edid[off + 2] == 0 && edid[off + 3] == 0xFC) {
        std::string name;
        for (std::size_t i = off + 5; i < off + 18; ++i) {
          if (edid[i] == 0x0A || edid[i] == 0) {
            break;
          }
          name.push_back(static_cast<char>(edid[i]));
        }
        while (!name.empty() && name.back() == ' ') {
          name.pop_back();
        }
        return name;
      }
    }
    return {};
  }

  std::map<std::string, std::string> parse_dpy_names(std::string_view text) {
    // "    [0] atom:0[dpy:0] (HDMI-0) (connected, enabled)"
    std::map<std::string, std::string> map;
    for (const auto raw : lines_of(text)) {
      const auto line = trim(raw);
      if (!line.starts_with('[')) {
        continue;
      }
      const auto dpy = line.find("[dpy:"sv);
      if (dpy == std::string_view::npos) {
        continue;
      }
      const auto close = line.find(']', dpy);
      const auto open_name = line.find('(', close);
      const auto close_name = line.find(')', open_name);
      if (close == std::string_view::npos || open_name == std::string_view::npos || close_name == std::string_view::npos) {
        continue;
      }
      const auto number = line.substr(dpy + 5, close - dpy - 5);
      const auto name = line.substr(open_name + 1, close_name - open_name - 1);
      if (!number.empty() && !name.empty()) {
        map[std::string {name}] = "DPY-" + std::string {number};
      }
    }
    return map;
  }

  std::string parse_current_metamode(std::string_view text) {
    for (const auto raw : lines_of(text)) {
      const auto sep = raw.find(" :: "sv);
      if (sep != std::string_view::npos) {
        return std::string {trim(raw.substr(sep + 4))};
      }
    }
    return {};
  }

  std::vector<std::string> split_metamode(std::string_view metamode) {
    std::vector<std::string> entries;
    int depth = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i < metamode.size(); ++i) {
      const char c = metamode[i];
      if (c == '{' || c == '(') {
        ++depth;
      } else if ((c == '}' || c == ')') && depth > 0) {
        --depth;
      } else if (c == ',' && depth == 0) {
        if (const auto e = trim(metamode.substr(start, i - start)); !e.empty()) {
          entries.emplace_back(e);
        }
        start = i + 1;
      }
    }
    if (const auto e = trim(metamode.substr(start)); !e.empty()) {
      entries.emplace_back(e);
    }
    return entries;
  }

  std::string replace_metamode_entry(std::string_view metamode, const std::vector<std::string> &names, const std::string &entry) {
    auto entries = split_metamode(metamode);
    bool replaced = false;
    for (auto &e : entries) {
      if (names_match(entry_display(e), names)) {
        e = entry;
        replaced = true;
      }
    }
    if (!replaced) {
      entries.push_back(entry);
    }
    std::string out;
    for (const auto &e : entries) {
      if (!out.empty()) {
        out += ", ";
      }
      out += e;
    }
    return out;
  }

  std::pair<int, int> metamode_entry_position(std::string_view metamode, const std::vector<std::string> &names) {
    for (const auto &e : split_metamode(metamode)) {
      if (!names_match(entry_display(e), names)) {
        continue;
      }
      // The position is the first "+X+Y" word outside braces.
      const auto brace = e.find('{');
      const auto head = std::string_view {e}.substr(0, brace);
      for (const auto w : words_of(head)) {
        if (w.size() > 1 && (w[0] == '+' || w[0] == '-')) {
          const auto second = w.find_first_of("+-", 1);
          if (second == std::string_view::npos) {
            continue;
          }
          try {
            return {std::stoi(std::string {w.substr(0, second)}), std::stoi(std::string {w.substr(second)})};
          } catch (...) {
            return {0, 0};
          }
        }
      }
    }
    return {0, 0};
  }

  std::optional<double> pick_rate(const mode_t &mode, int fps) {
    if (mode.rates.empty()) {
      return std::nullopt;
    }
    auto sorted = mode.rates;
    std::ranges::sort(sorted);
    for (const double r : sorted) {
      if (r + 0.5 >= fps) {
        return r;
      }
    }
    return sorted.back();
  }

  const output_t *pick_output(const std::vector<output_t> &outputs, const std::string &setting) {
    if (!setting.empty() && setting != "auto") {
      for (const auto &o : outputs) {
        if (o.name == setting) {
          return o.connected ? &o : nullptr;
        }
      }
      return nullptr;
    }
    const output_t *only = nullptr;
    int connected = 0;
    for (const auto &o : outputs) {
      if (!o.connected) {
        continue;
      }
      if (o.monitor_name == nova_edid_name) {
        return &o;
      }
      ++connected;
      only = &o;
    }
    return connected == 1 ? only : nullptr;
  }

  std::optional<plan_t> plan(const output_t &output, const target_t &target) {
    if (output.modes.empty() || target.width <= 0 || target.height <= 0) {
      return std::nullopt;
    }
    const bool turned = target.rotation == 90 || target.rotation == 270;
    const int raster_w = turned ? target.height : target.width;
    const int raster_h = turned ? target.width : target.height;

    const mode_t *mode = nullptr;
    bool scaled = false;
    int screen_w = target.width;
    int screen_h = target.height;
    // The EDID's exact mode (rotated for portrait), else ViewPortIn scaling on the smallest mode that covers it.
    mode = find_mode(output, raster_w, raster_h);
    if (!mode) {
      mode = covering_mode(output, raster_w, raster_h);
      if (!mode) {
        mode = largest_mode(output);
      }
      scaled = true;
    }

    plan_t p;
    p.output = output.name;
    p.mode_width = mode->width;
    p.mode_height = mode->height;
    p.rate = pick_rate(*mode, target.fps).value_or(0);
    p.screen_width = screen_w;
    p.screen_height = screen_h;
    p.rotation = target.rotation;
    p.scaled = scaled;
    return p;
  }

  std::string_view rotation_name(int angle) {
    switch (angle) {
      case 90:
        return "left"sv;
      case 180:
        return "inverted"sv;
      case 270:
        return "right"sv;
      default:
        return "normal"sv;
    }
  }

  std::string nvidia_mode_name(const plan_t &plan) {
    if (plan.rate <= 0) {
      return std::format("{}x{}", plan.mode_width, plan.mode_height);
    }
    return std::format("{}x{}_{}", plan.mode_width, plan.mode_height, static_cast<int>(std::lround(plan.rate)));
  }

  std::string metamode_entry(const plan_t &plan, const std::string &display, int x, int y) {
    std::string entry = std::format("{}: {} {:+d}{:+d} {{ViewPortIn={}x{}, ViewPortOut={}x{}+0+0", display, nvidia_mode_name(plan), x, y, plan.screen_width, plan.screen_height, plan.mode_width, plan.mode_height);
    if (plan.rotation != 0) {
      entry += ", Rotation=";
      entry += rotation_name(plan.rotation);
    }
    entry += '}';
    return entry;
  }

  std::vector<std::string> xrandr_args(const plan_t &plan) {
    std::vector<std::string> args {"xrandr", "--output", plan.output, "--mode", std::format("{}x{}", plan.mode_width, plan.mode_height)};
    if (plan.rate > 0) {
      args.insert(args.end(), {"--rate", format_rate(plan.rate)});
    }
    args.insert(args.end(), {"--rotate", std::string {rotation_name(plan.rotation)}});
    if (plan.scaled) {
      args.insert(args.end(), {"--scale-from", std::format("{}x{}", plan.screen_width, plan.screen_height)});
    } else {
      args.insert(args.end(), {"--scale", "1x1"});
    }
    return args;
  }

  nlohmann::json to_json(const saved_state_t &state) {
    return {
      {"output", state.output},
      {"metamode", state.metamode},
      {"mode_width", state.mode_width},
      {"mode_height", state.mode_height},
      {"rate", state.rate},
      {"rotation", state.rotation},
    };
  }

  std::optional<saved_state_t> saved_state_from_json(const nlohmann::json &json) {
    try {
      saved_state_t s;
      s.output = json.at("output").get<std::string>();
      s.metamode = json.value("metamode", std::string {});
      s.mode_width = json.value("mode_width", 0);
      s.mode_height = json.value("mode_height", 0);
      s.rate = json.value("rate", 0.0);
      s.rotation = json.value("rotation", 0);
      if (s.output.empty()) {
        return std::nullopt;
      }
      return s;
    } catch (...) {
      return std::nullopt;
    }
  }

  switcher_t::switcher_t(runner_t runner, std::filesystem::path state_file, std::function<std::string()> output_setting, std::filesystem::path legacy_state):
      runner_ {std::move(runner)},
      state_file_ {std::move(state_file)},
      output_setting_ {std::move(output_setting)},
      legacy_state_ {std::move(legacy_state)} {
  }

  std::optional<output_t> switcher_t::find_output_locked(bool with_edid) {
    const auto query = runner_({"xrandr", "--query"});
    if (query.exit_code != 0) {
      BOOST_LOG(error) << "Display modeset: xrandr --query failed ("sv << query.exit_code << ')';
      return std::nullopt;
    }
    auto outputs = parse_xrandr(query.output);
    const auto setting = output_setting_ ? output_setting_() : "auto"s;
    if (with_edid || setting.empty() || setting == "auto") {
      const auto verbose = runner_({"xrandr", "--verbose"});
      if (verbose.exit_code == 0) {
        const auto names = parse_edid_names(verbose.output);
        for (auto &o : outputs) {
          if (const auto it = names.find(o.name); it != names.end()) {
            o.monitor_name = it->second;
          }
        }
      }
    }
    const auto *picked = pick_output(outputs, setting);
    if (!picked) {
      BOOST_LOG(warning) << "Display modeset: no output to switch (display_follow_output = "sv << setting
                         << "); with several monitors connected set display_follow_output to the one Mirror streams use"sv;
      return std::nullopt;
    }
    return *picked;
  }

  bool switcher_t::save_state_locked(const output_t &output) {
    std::error_code ec;
    if (std::filesystem::exists(state_file_, ec)) {
      return true;  // keep the state from before the first switch
    }
    saved_state_t state;
    state.output = output.name;
    state.rotation = output.rotation;
    if (const auto *mode = current_mode(output)) {
      state.mode_width = mode->width;
      state.mode_height = mode->height;
      state.rate = mode->current_rate;
    }
    std::string legacy;
    if (!legacy_state_.empty() && std::filesystem::exists(legacy_state_, ec)) {
      std::getline(std::ifstream {legacy_state_}, legacy);
    }
    if (!trim(legacy).empty()) {
      // The old script switched the display and never restored it: its saved MetaMode is the real original.
      BOOST_LOG(info) << "Display modeset: using the MetaMode saved by the display script as the original"sv;
      state.metamode = std::string {trim(legacy)};
    } else if (const auto q = runner_({"nvidia-settings", "-q", "CurrentMetaMode", "-t"}); q.exit_code == 0) {
      state.metamode = parse_current_metamode(q.output);
    }
    std::filesystem::create_directories(state_file_.parent_path(), ec);
    std::ofstream out {state_file_, std::ios::trunc};
    out << to_json(state).dump() << '\n';
    return static_cast<bool>(out);
  }

  std::optional<saved_state_t> switcher_t::load_state_locked() const {
    std::error_code ec;
    if (!std::filesystem::exists(state_file_, ec)) {
      return std::nullopt;
    }
    try {
      std::ifstream in {state_file_};
      return saved_state_from_json(nlohmann::json::parse(in));
    } catch (...) {
      return std::nullopt;
    }
  }

  bool switcher_t::apply_locked(const output_t &output, const target_t &target) {
    const auto p = plan(output, target);
    if (!p) {
      BOOST_LOG(error) << "Display modeset: "sv << output.name << " lists no modes"sv;
      return false;
    }
    if (!save_state_locked(output)) {
      BOOST_LOG(error) << "Display modeset: couldn't save the current mode to "sv << state_file_.string() << "; not switching"sv;
      return false;
    }
    BOOST_LOG(info) << "Display modeset: "sv << output.name << ' ' << p->mode_width << 'x' << p->mode_height << '@' << format_rate(p->rate)
                    << " screen "sv << p->screen_width << 'x' << p->screen_height << " rotation "sv << p->rotation << (p->scaled ? " (ViewPortIn)"sv : ""sv);

    auto verify = [&]() {
      const auto after = runner_({"xrandr", "--query"});
      if (after.exit_code != 0) {
        return false;
      }
      for (const auto &o : parse_xrandr(after.output)) {
        if (o.name == output.name) {
          return o.width == p->screen_width && o.height == p->screen_height;
        }
      }
      return false;
    };

    // An unscaled, unrotated mode is a plain RandR mode switch (what the script did). Scaling and
    // rotation go through a MetaMode first, which only replaces this output's entry.
    const bool plain = !p->scaled && p->rotation == 0;
    auto try_xrandr = [&]() {
      return runner_(xrandr_args(*p)).exit_code == 0 && verify();
    };
    auto try_metamode = [&]() {
      const auto q = runner_({"nvidia-settings", "-q", "CurrentMetaMode", "-t"});
      if (q.exit_code != 0) {
        return false;
      }
      const auto current = parse_current_metamode(q.output);
      std::vector<std::string> names {output.name};
      if (const auto d = runner_({"nvidia-settings", "-q", "dpys"}); d.exit_code == 0) {
        const auto dpys = parse_dpy_names(d.output);
        if (const auto it = dpys.find(output.name); it != dpys.end()) {
          names.push_back(it->second);
        }
      }
      const auto [x, y] = metamode_entry_position(current, names);
      const auto entry = metamode_entry(*p, names.back(), x, y);
      const auto metamode = replace_metamode_entry(current, names, entry);
      return runner_({"nvidia-settings", "-a", "CurrentMetaMode=" + metamode}).exit_code == 0 && verify();
    };

    const bool ok = plain ? (try_xrandr() || try_metamode()) : (try_metamode() || try_xrandr());
    if (!ok) {
      BOOST_LOG(error) << "Display modeset: couldn't switch "sv << output.name;
      return false;
    }
    current_ = p;
    target_ = target;
    return true;
  }

  bool switcher_t::set(const display_follow::request_t &request) {
    std::lock_guard lg {mutex_};
    const auto output = find_output_locked(false);
    if (!output) {
      return false;
    }
    return apply_locked(*output, {request.width, request.height, request.fps, request.rotation});
  }

  bool switcher_t::rotate(int angle) {
    if (!valid_rotation(angle)) {
      return false;
    }
    std::lock_guard lg {mutex_};
    const auto output = find_output_locked(false);
    if (!output) {
      return false;
    }
    target_t target;
    if (target_) {
      target = *target_;
    } else {
      // Not switched by a stream: rotate the mode the output has now.
      target.width = output->width;
      target.height = output->height;
      const auto *mode = current_mode(*output);
      target.fps = mode ? static_cast<int>(std::lround(mode->current_rate)) : 60;
    }
    std::tie(target.width, target.height) = size_for_rotation(target.width, target.height, angle);
    target.rotation = angle;
    return apply_locked(*output, target);
  }

  bool switcher_t::restore_locked() {
    const auto state = load_state_locked();
    std::error_code ec;
    current_.reset();
    target_.reset();
    if (!state) {
      std::filesystem::remove(state_file_, ec);
      return false;
    }
    bool ok = false;
    if (!state->metamode.empty()) {
      ok = runner_({"nvidia-settings", "-a", "CurrentMetaMode=" + state->metamode}).exit_code == 0;
    }
    if (!ok && state->mode_width > 0) {
      plan_t p;
      p.output = state->output;
      p.mode_width = state->mode_width;
      p.mode_height = state->mode_height;
      p.rate = state->rate;
      p.rotation = state->rotation;
      ok = runner_(xrandr_args(p)).exit_code == 0;
    }
    if (!ok) {
      ok = runner_({"xrandr", "--output", state->output, "--auto", "--rotate", "normal", "--scale", "1x1"}).exit_code == 0;
    }
    std::filesystem::remove(state_file_, ec);
    if (!legacy_state_.empty()) {
      std::filesystem::remove(legacy_state_, ec);
    }
    if (ok) {
      BOOST_LOG(info) << "Display modeset: "sv << state->output << " restored"sv;
    } else {
      BOOST_LOG(error) << "Display modeset: couldn't restore "sv << state->output;
    }
    return ok;
  }

  bool switcher_t::restore() {
    std::lock_guard lg {mutex_};
    return restore_locked();
  }

  bool switcher_t::recover() {
    std::lock_guard lg {mutex_};
    if (!load_state_locked()) {
      return false;
    }
    BOOST_LOG(info) << "Display modeset: restoring the display left switched by the previous run"sv;
    return restore_locked();
  }

  std::optional<plan_t> switcher_t::current() const {
    std::lock_guard lg {mutex_};
    return current_;
  }

  std::vector<output_t> switcher_t::outputs() {
    std::lock_guard lg {mutex_};
    const auto verbose = runner_({"xrandr", "--verbose"});
    const auto query = runner_({"xrandr", "--query"});
    if (query.exit_code != 0) {
      return {};
    }
    auto outputs = parse_xrandr(query.output);
    const auto names = verbose.exit_code == 0 ? parse_edid_names(verbose.output) : std::map<std::string, std::string> {};
    std::erase_if(outputs, [](const output_t &o) {
      return !o.connected;
    });
    for (auto &o : outputs) {
      if (const auto it = names.find(o.name); it != names.end()) {
        o.monitor_name = it->second;
      }
    }
    return outputs;
  }

  nlohmann::json displays_json(const std::vector<output_t> &outputs, const std::string &mirror_output, const std::optional<virtual_display::target_t> &virtual_target) {
    nlohmann::json displays = nlohmann::json::array();
    for (const auto &o : outputs) {
      const auto *mode = current_mode(o);
      displays.push_back({
        {"display_name", o.name},
        {"friendly_name", o.monitor_name.empty() ? o.name : o.monitor_name + " (" + o.name + ")"},
        {"device_id", o.name},
        {"width", o.width},
        {"height", o.height},
        {"refresh_rate", mode ? mode->current_rate : 0.0},
        {"rotation", o.rotation},
        {"primary", o.primary},
        {"kind", "mirror"},
        {"mirror", o.name == mirror_output},
      });
    }
    if (virtual_target) {
      displays.push_back({
        {"display_name", virtual_target->display},
        {"friendly_name", "Nova virtual display (" + virtual_target->display + ")"},
        {"device_id", virtual_target->display},
        {"width", virtual_target->width},
        {"height", virtual_target->height},
        {"refresh_rate", virtual_target->fps},
        {"rotation", 0},
        {"primary", false},
        {"kind", "virtual"},
        {"mirror", false},
      });
    }
    return {{"status_code", 200}, {"status_message", "OK"}, {"displays", std::move(displays)}};
  }

  nlohmann::json rotate_json(int status_code, const std::string &message) {
    return {{"status_code", status_code}, {"status_message", message}, {"success", status_code == 200}};
  }
}  // namespace display_modeset
