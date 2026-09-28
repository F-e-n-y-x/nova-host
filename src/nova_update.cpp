/**
 * @file src/nova_update.cpp
 * @brief Definitions for Nova's host-side update check and installer.
 */
// standard includes
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

// lib includes
#include <curl/curl.h>
#include <openssl/evp.h>

// local includes
#include "config.h"
#include "entry_handler.h"
#include "logging.h"
#include "nova_update.h"
#include "platform/common.h"
#include "rtsp.h"

using namespace std::literals;

#ifndef NOVA_UPDATE_HELPER
  #define NOVA_UPDATE_HELPER "/usr/libexec/nova-host/nova-install-update"  ///< Root helper installed by the package.
#endif

namespace nova_update {
  namespace {
    constexpr std::size_t MAX_API_BYTES = 4 << 20;
    constexpr std::size_t MAX_NOTES = 4000;

    bool is_hex(std::string_view s) {
      return std::ranges::all_of(s, [](char c) {
        return std::isxdigit(static_cast<unsigned char>(c));
      });
    }

    std::string lower(std::string s) {
      std::ranges::transform(s, s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      return s;
    }

    std::optional<int> number(std::string_view s) {
      if (s.empty() || s.size() > 9 || !std::ranges::all_of(s, ::isdigit)) {
        return std::nullopt;
      }
      return std::stoi(std::string {s});
    }

    std::vector<std::string_view> split(std::string_view s, char sep) {
      std::vector<std::string_view> out;
      std::size_t start = 0;
      for (;;) {
        const auto pos = s.find(sep, start);
        out.push_back(s.substr(start, pos - start));
        if (pos == std::string_view::npos) {
          return out;
        }
        start = pos + 1;
      }
    }

    /// "e0371fee", "e0371fee-dirty", "dirty": what build_version.cmake appends to local builds.
    bool is_local_suffix(std::string_view s) {
      if (s == "dirty") {
        return true;
      }
      if (s.ends_with("-dirty")) {
        s.remove_suffix(6);
      }
      return s.size() >= 7 && s.size() <= 40 && is_hex(s) && std::ranges::all_of(s, [](char c) {
               return !std::isupper(static_cast<unsigned char>(c));
             });
    }

    std::optional<asset_t> parse_asset(const nlohmann::json &a, std::string_view repo) {
      if (!a.is_object() || !a.contains("name") || !a.contains("url") || !a["name"].is_string() || !a["url"].is_string()) {
        return std::nullopt;
      }
      asset_t asset;
      asset.name = a["name"].get<std::string>();
      asset.url = a["url"].get<std::string>();
      // Only ever send the token to this repository's asset API.
      const auto prefix = "https://api.github.com/repos/"s + std::string {repo} + "/releases/assets/";
      if (!asset.url.starts_with(prefix) || !number(std::string_view {asset.url}.substr(prefix.size()))) {
        return std::nullopt;
      }
      if (asset.name.empty() || asset.name.find('/') != std::string::npos || asset.name.starts_with('.')) {
        return std::nullopt;
      }
      if (a.contains("size") && a["size"].is_number_unsigned()) {
        asset.size = a["size"].get<std::uint64_t>();
      }
      if (a.contains("digest") && a["digest"].is_string()) {
        const auto digest = a["digest"].get<std::string>();
        if (digest.starts_with("sha256:") && digest.size() == 7 + 64 && is_hex(std::string_view {digest}.substr(7))) {
          asset.sha256 = lower(digest.substr(7));
        }
      }
      return asset;
    }

    std::string json_string(const nlohmann::json &o, const char *key) {
      return o.contains(key) && o[key].is_string() ? o[key].get<std::string>() : std::string {};
    }

    bool valid_token(const std::string &token) {
      // ghp_…, github_pat_…: letters, digits and underscores. Anything else could inject headers.
      return token.size() <= 255 && std::ranges::all_of(token, [](char c) {
               return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
             });
    }
  }  // namespace

  std::string version_t::str() const {
    std::string s = std::to_string(major) + '.' + std::to_string(minor) + '.' + std::to_string(patch);
    for (std::size_t i = 0; i < pre.size(); ++i) {
      s += (i == 0 ? '-' : '.');
      s += pre[i];
    }
    return s;
  }

  std::optional<version_t> parse_version(std::string_view text, bool tag) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
      text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
      text.remove_suffix(1);
    }
    if (tag) {
      if (!text.starts_with(TAG_PREFIX)) {
        return std::nullopt;
      }
      text.remove_prefix(TAG_PREFIX.size());
    } else {
      if (text.starts_with(TAG_PREFIX)) {
        text.remove_prefix(TAG_PREFIX.size());
      } else if (text.starts_with('v') || text.starts_with('V')) {
        text.remove_prefix(1);
      }
    }
    if (const auto plus = text.find('+'); plus != std::string_view::npos) {
      text = text.substr(0, plus);
    }
    std::string_view core = text;
    std::string_view suffix;
    const auto dash = text.find('-');
    if (dash != std::string_view::npos) {
      core = text.substr(0, dash);
      suffix = text.substr(dash + 1);
      if (suffix.empty()) {
        return std::nullopt;
      }
    }
    const auto parts = split(core, '.');
    if (parts.size() != 3) {
      return std::nullopt;
    }
    version_t v;
    const auto a = number(parts[0]);
    const auto b = number(parts[1]);
    const auto c = number(parts[2]);
    if (!a || !b || !c) {
      return std::nullopt;
    }
    v.major = *a;
    v.minor = *b;
    v.patch = *c;
    if (suffix.empty()) {
      return v;
    }
    if (!tag && is_local_suffix(suffix)) {
      v.local = true;
      return v;
    }
    for (const auto id : split(suffix, '.')) {
      if (id.empty() || !std::ranges::all_of(id, [](char ch) {
            return std::isalnum(static_cast<unsigned char>(ch)) || ch == '-';
          })) {
        return std::nullopt;
      }
      v.pre.emplace_back(id);
    }
    return v;
  }

  int compare_versions(const version_t &a, const version_t &b) {
    for (const auto &[x, y] : {std::pair {a.major, b.major}, std::pair {a.minor, b.minor}, std::pair {a.patch, b.patch}}) {
      if (x != y) {
        return x < y ? -1 : 1;
      }
    }
    // A release sorts above its pre-releases.
    if (a.pre.empty() || b.pre.empty()) {
      return a.pre.empty() == b.pre.empty() ? 0 : (a.pre.empty() ? 1 : -1);
    }
    for (std::size_t i = 0; i < std::min(a.pre.size(), b.pre.size()); ++i) {
      const auto na = number(a.pre[i]);
      const auto nb = number(b.pre[i]);
      if (na && nb) {
        if (*na != *nb) {
          return *na < *nb ? -1 : 1;
        }
      } else if (na || nb) {
        return na ? -1 : 1;  // numeric identifiers sort below alphanumeric ones
      } else if (a.pre[i] != b.pre[i]) {
        return a.pre[i] < b.pre[i] ? -1 : 1;
      }
    }
    if (a.pre.size() != b.pre.size()) {
      return a.pre.size() < b.pre.size() ? -1 : 1;
    }
    return 0;
  }

  bool is_newer(std::string_view current, const version_t &latest) {
    const auto mine = parse_version(current);
    return mine && compare_versions(latest, *mine) > 0;
  }

  std::optional<release_t> pick_release(const nlohmann::json &releases, std::string_view repo, bool include_prerelease, std::string_view arch) {
    if (!releases.is_array()) {
      return std::nullopt;
    }
    std::optional<release_t> best;
    for (const auto &r : releases) {
      if (!r.is_object() || (r.contains("draft") && r["draft"].is_boolean() && r["draft"].get<bool>())) {
        continue;
      }
      release_t rel;
      rel.tag = json_string(r, "tag_name");
      const auto version = parse_version(rel.tag, true);
      if (!version) {
        continue;
      }
      rel.version = *version;
      rel.prerelease = (r.contains("prerelease") && r["prerelease"].is_boolean() && r["prerelease"].get<bool>()) || !version->pre.empty();
      if (rel.prerelease && !include_prerelease) {
        continue;
      }
      if (!r.contains("assets") || !r["assets"].is_array()) {
        continue;
      }
      std::vector<asset_t> assets;
      for (const auto &a : r["assets"]) {
        if (auto asset = parse_asset(a, repo)) {
          assets.push_back(std::move(*asset));
        }
      }
      std::vector<const asset_t *> debs;
      for (const auto &a : assets) {
        const auto name = lower(a.name);
        if (name.ends_with(".deb") && name.find("dbgsym") == std::string::npos && name.find("-dbg") == std::string::npos) {
          debs.push_back(&a);
        }
      }
      const asset_t *deb = nullptr;
      const auto arch_tag = lower(std::string {arch});
      for (const auto *d : debs) {
        const auto name = lower(d->name);
        if (name.ends_with("_" + arch_tag + ".deb") || name.find(arch_tag) != std::string::npos) {
          deb = d;
          break;
        }
      }
      if (!deb && debs.size() == 1 && lower(debs.front()->name).ends_with("_all.deb")) {
        deb = debs.front();
      }
      if (!deb) {
        continue;
      }
      rel.deb = *deb;
      for (const auto &a : assets) {
        const auto name = lower(a.name);
        if (name == lower(deb->name) + ".sha256" || name == "sha256sums" || name == "sha256sums.txt" || name == "checksums.txt") {
          rel.checksum = a;
          break;
        }
      }
      rel.name = json_string(r, "name");
      rel.html_url = json_string(r, "html_url");
      if (!rel.html_url.starts_with("https://github.com/")) {
        rel.html_url.clear();
      }
      rel.published_at = json_string(r, "published_at");
      rel.notes = json_string(r, "body");
      if (rel.notes.size() > MAX_NOTES) {
        rel.notes.resize(MAX_NOTES);
        rel.notes += "…";
      }
      if (!best || compare_versions(rel.version, best->version) > 0) {
        best = std::move(rel);
      }
    }
    return best;
  }

  std::optional<std::string> parse_checksum(std::string_view text, std::string_view filename) {
    std::optional<std::string> bare;
    int lines = 0;
    for (auto line : split(text, '\n')) {
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
        line.remove_suffix(1);
      }
      while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
        line.remove_prefix(1);
      }
      if (line.empty() || line.starts_with('#')) {
        continue;
      }
      ++lines;
      if (line.size() < 64 || !is_hex(line.substr(0, 64)) || (line.size() > 64 && line[64] != ' ' && line[64] != '\t')) {
        continue;
      }
      const auto hex = lower(std::string {line.substr(0, 64)});
      auto name = line.substr(64);
      while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) {
        name.remove_prefix(1);
      }
      if (name.starts_with('*')) {
        name.remove_prefix(1);  // binary mode marker from sha256sum -b
      }
      if (const auto slash = name.rfind('/'); slash != std::string_view::npos) {
        name = name.substr(slash + 1);
      }
      if (name.empty()) {
        bare = hex;
      } else if (name == filename) {
        return hex;
      }
    }
    if (bare && lines == 1) {
      return bare;
    }
    return std::nullopt;
  }

  std::string sha256_file(const std::filesystem::path &path) {
    std::ifstream in {path, std::ios::binary};
    if (!in) {
      return {};
    }
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx || EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
      EVP_MD_CTX_free(ctx);
      return {};
    }
    std::vector<char> buf(1 << 16);
    while (in) {
      in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
      if (in.gcount() > 0) {
        EVP_DigestUpdate(ctx, buf.data(), static_cast<std::size_t>(in.gcount()));
      }
    }
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    const bool ok = in.eof() && EVP_DigestFinal_ex(ctx, md, &len) == 1;
    EVP_MD_CTX_free(ctx);
    if (!ok) {
      return {};
    }
    static constexpr char digits[] = "0123456789abcdef";
    std::string hex;
    for (unsigned int i = 0; i < len; ++i) {
      hex += digits[md[i] >> 4];
      hex += digits[md[i] & 0xf];
    }
    return hex;
  }

  std::string_view to_string(install_state_e state) {
    switch (state) {
      case install_state_e::idle:
        return "idle";
      case install_state_e::downloading:
        return "downloading";
      case install_state_e::verifying:
        return "verifying";
      case install_state_e::installing:
        return "installing";
      case install_state_e::restarting:
        return "restarting";
      case install_state_e::manual:
        return "manual";
      case install_state_e::failed:
        return "failed";
    }
    return "idle";
  }

  service_t::service_t(deps_t deps):
      deps_ {std::move(deps)} {
  }

  std::vector<std::string> service_t::api_headers(const options_t &options, bool binary) const {
    std::vector<std::string> headers {
      binary ? "Accept: application/octet-stream"s : "Accept: application/vnd.github+json"s,
      "X-GitHub-Api-Version: 2022-11-28"s,
    };
    if (!options.token.empty() && valid_token(options.token)) {
      headers.push_back("Authorization: Bearer " + options.token);
    }
    return headers;
  }

  bool service_t::claim_check() {
    std::lock_guard lg {mutex_};
    if (checking_) {
      return false;
    }
    checking_ = true;
    return true;
  }

  bool service_t::stale(std::int64_t max_age_s) const {
    std::lock_guard lg {mutex_};
    return !checking_ && (checked_at_ == 0 || deps_.now() - checked_at_ >= max_age_s);
  }

  void service_t::check(const options_t &options) {
    std::string error;
    std::optional<release_t> latest;
    if (!options.token.empty() && !valid_token(options.token)) {
      error = "bad_token";
    } else {
      const auto url = "https://api.github.com/repos/" + options.repo + "/releases?per_page=30";
      const auto r = deps_.get(url, api_headers(options, false), MAX_API_BYTES);
      if (r.status == 0) {
        error = "network";
      } else if (r.status == 401) {
        error = "unauthorized";
      } else if (r.status == 403 || r.status == 429) {
        error = "rate_limited";
      } else if (r.status == 404) {
        // GitHub answers 404, not 403, for a private repository without (or with a too narrow) token.
        error = options.token.empty() ? "private"s : "not_found"s;
      } else if (r.status != 200) {
        error = "http_" + std::to_string(r.status);
      } else {
        const auto json = nlohmann::json::parse(r.body, nullptr, false);
        if (json.is_discarded() || !json.is_array()) {
          error = "bad_response";
        } else {
          latest = pick_release(json, options.repo, options.prerelease, options.arch);
          if (!latest) {
            error = "no_release";
          }
        }
      }
    }
    if (!error.empty() && error != "no_release") {
      BOOST_LOG(info) << "Update check: "sv << error;
    } else if (latest) {
      BOOST_LOG(info) << "Update check: latest release "sv << latest->tag << (is_newer(options.current, latest->version) ? " (newer)"sv : ""sv);
    }
    std::lock_guard lg {mutex_};
    checking_ = false;
    checked_at_ = deps_.now();
    check_error_ = error;
    latest_ = latest;
  }

  void service_t::set_state(install_state_e state, const std::string &message) {
    std::lock_guard lg {mutex_};
    state_ = state;
    message_ = message;
  }

  void service_t::fail(const std::string &message) {
    BOOST_LOG(warning) << "Update install: "sv << message;
    set_state(install_state_e::failed, message);
  }

  start_result_e service_t::begin_install(const options_t &options, const std::string &tag) {
    std::lock_guard lg {mutex_};
    if (state_ == install_state_e::downloading || state_ == install_state_e::verifying || state_ == install_state_e::installing ||
        state_ == install_state_e::restarting) {
      return start_result_e::busy;
    }
    if (!latest_ || latest_->tag != tag || !is_newer(options.current, latest_->version)) {
      return start_result_e::no_update;
    }
    if (deps_.streaming()) {
      return start_result_e::streaming;
    }
    state_ = install_state_e::downloading;
    message_.clear();
    command_.clear();
    install_tag_ = tag;
    progress_ = 0;
    return start_result_e::started;
  }

  std::optional<std::string> service_t::expected_sha256(const options_t &options, const release_t &release) {
    std::optional<std::string> from_file;
    if (release.checksum) {
      const auto r = deps_.get(release.checksum->url, api_headers(options, true), 1 << 20);
      if (r.status != 200) {
        fail("couldn't download " + release.checksum->name + " (HTTP " + std::to_string(r.status) + ")");
        return std::nullopt;
      }
      from_file = parse_checksum(r.body, release.deb.name);
      if (!from_file) {
        fail(release.checksum->name + " has no SHA-256 for " + release.deb.name);
        return std::nullopt;
      }
    }
    if (!release.deb.sha256.empty() && from_file && *from_file != release.deb.sha256) {
      fail("the release's checksum file and GitHub's digest disagree");
      return std::nullopt;
    }
    if (from_file) {
      return from_file;
    }
    if (!release.deb.sha256.empty()) {
      return release.deb.sha256;
    }
    fail("the release publishes no SHA-256 for " + release.deb.name + "; not installing an unverified package");
    return std::nullopt;
  }

  void service_t::run_install(const options_t &options) {
    release_t release;
    {
      std::lock_guard lg {mutex_};
      if (!latest_ || latest_->tag != install_tag_) {
        state_ = install_state_e::failed;
        message_ = "the release changed; check again";
        return;
      }
      release = *latest_;
    }
    if (release.deb.size > MAX_PACKAGE_BYTES) {
      fail("the package is larger than 512 MiB");
      return;
    }
    const auto expected = expected_sha256(options, release);
    if (!expected) {
      return;
    }

    std::error_code ec;
    std::filesystem::create_directories(deps_.cache_dir, ec);
    std::filesystem::permissions(deps_.cache_dir, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, ec);
    const auto final_path = deps_.cache_dir / release.deb.name;
    const auto part = deps_.cache_dir / (release.deb.name + ".part");
    std::filesystem::remove(part, ec);

    const auto r = deps_.download(release.deb.url, api_headers(options, true), part, MAX_PACKAGE_BYTES, [this](std::uint64_t done, std::uint64_t total) {
      std::lock_guard lg {mutex_};
      progress_ = total ? static_cast<double>(done) / static_cast<double>(total) : 0;
    });
    if (r.status != 200) {
      std::filesystem::remove(part, ec);
      fail("download failed (" + (r.status ? "HTTP " + std::to_string(r.status) : r.error) + ")");
      return;
    }

    set_state(install_state_e::verifying);
    const auto actual = sha256_file(part);
    if (actual.empty() || actual != *expected) {
      std::filesystem::remove(part, ec);
      fail("SHA-256 mismatch for " + release.deb.name + ": expected " + *expected + ", got " + (actual.empty() ? "nothing"s : actual));
      return;
    }
    const auto fields = deps_.run({"dpkg-deb", "-f", part.string(), "Package", "Version"});
    std::string package;
    std::string version;
    std::istringstream lines {fields.output};
    for (std::string line; std::getline(lines, line);) {
      if (line.starts_with("Package: ")) {
        package = line.substr(9);
      } else if (line.starts_with("Version: ")) {
        version = line.substr(9);
      }
    }
    // The Debian version is X.Y.Z, optionally with a "-<revision>" or pre-release suffix; X.Y.Z must match the tag.
    const auto upstream = parse_version(version);
    const bool version_ok = upstream && upstream->major == release.version.major && upstream->minor == release.version.minor &&
                            upstream->patch == release.version.patch;
    if (fields.exit_code != 0 || package != "nova-host" || !version_ok) {
      std::filesystem::remove(part, ec);
      fail("the downloaded file is not the nova-host " + release.version.str() + " package (" + (package.empty() ? "unreadable"s : package + " " + version) + ")");
      return;
    }
    std::filesystem::rename(part, final_path, ec);
    if (ec) {
      fail("couldn't store the package: " + ec.message());
      return;
    }

    // Keep this package and the previous one (for rollback); drop older downloads.
    std::vector<std::filesystem::directory_entry> debs;
    for (const auto &e : std::filesystem::directory_iterator(deps_.cache_dir, ec)) {
      if (e.is_regular_file() && e.path().extension() == ".deb") {
        debs.push_back(e);
      }
    }
    std::ranges::sort(debs, [](const auto &a, const auto &b) {
      return a.last_write_time() > b.last_write_time();
    });
    for (std::size_t i = 2; i < debs.size(); ++i) {
      std::filesystem::remove(debs[i].path(), ec);
    }

    const std::string manual = "sudo apt-get install --allow-downgrades " + final_path.string();
    if (deps_.streaming()) {
      std::lock_guard lg {mutex_};
      state_ = install_state_e::manual;
      message_ = "A stream started; install later.";
      command_ = manual;
      return;
    }
    if (deps_.helper.empty() || !std::filesystem::exists(deps_.helper, ec)) {
      std::lock_guard lg {mutex_};
      state_ = install_state_e::manual;
      message_ = "Verified. Run the command to install.";
      command_ = manual;
      return;
    }

    set_state(install_state_e::installing);
    const auto result = deps_.run({"sudo", "-n", deps_.helper, final_path.string(), *expected});
    if (result.exit_code != 0) {
      const bool not_allowed = result.error.find("password") != std::string::npos || result.error.find("not allowed") != std::string::npos ||
                               result.error.find("may not run") != std::string::npos || result.exit_code == 127;
      std::lock_guard lg {mutex_};
      if (not_allowed) {
        state_ = install_state_e::manual;
        message_ = "Verified. One-click install isn't enabled on this PC; run the command, or enable it (see the docs).";
      } else {
        state_ = install_state_e::failed;
        message_ = "the installer failed (exit " + std::to_string(result.exit_code) + "): " + result.error.substr(result.error.size() > 400 ? result.error.size() - 400 : 0);
        BOOST_LOG(warning) << "Update install: "sv << message_;
      }
      command_ = manual;
      return;
    }
    BOOST_LOG(info) << "Update install: installed "sv << release.tag << "; restarting when no stream is active"sv;
    set_state(install_state_e::restarting, "Installed " + release.tag + ". Nova restarts when no stream is active.");
    deps_.restart();
  }

  nlohmann::json service_t::status_json(const options_t &options) const {
    std::lock_guard lg {mutex_};
    nlohmann::json out;
    out["enabled"] = options.enabled;
    out["current"] = options.current;
    out["token_set"] = !options.token.empty();
    out["repo"] = options.repo;
    out["checking"] = checking_;
    out["checked_at"] = checked_at_;
    out["error"] = check_error_;
    out["streaming"] = deps_.streaming();
    const bool newer = latest_ && is_newer(options.current, latest_->version);
    out["update_available"] = newer;
    if (latest_) {
      const auto &l = *latest_;
      out["latest"] = {
        {"tag", l.tag},
        {"version", l.version.str()},
        {"name", l.name},
        {"prerelease", l.prerelease},
        {"html_url", l.html_url},
        {"published_at", l.published_at},
        {"notes", l.notes},
        {"package", {{"name", l.deb.name}, {"size", l.deb.size}}},
        {"checksum", l.checksum ? "file"s : (l.deb.sha256.empty() ? ""s : "digest"s)},
      };
    } else {
      out["latest"] = nullptr;
    }
    out["install"] = {
      {"state", to_string(state_)},
      {"tag", install_tag_},
      {"progress", progress_},
      {"message", message_},
      {"command", command_},
    };
    return out;
  }

  namespace {
    std::size_t write_string(char *data, std::size_t size, std::size_t n, void *user) {
      auto *sink = static_cast<std::pair<std::string *, std::size_t> *>(user);
      const auto bytes = size * n;
      if (sink->first->size() + bytes > sink->second) {
        return 0;
      }
      sink->first->append(data, bytes);
      return bytes;
    }

    struct file_sink_t {
      std::ofstream out;
      std::uint64_t written = 0;
      std::uint64_t max = 0;
      const std::function<void(std::uint64_t, std::uint64_t)> *progress = nullptr;
    };

    std::size_t write_file(char *data, std::size_t size, std::size_t n, void *user) {
      auto *sink = static_cast<file_sink_t *>(user);
      const auto bytes = size * n;
      if (sink->written + bytes > sink->max) {
        return 0;
      }
      sink->out.write(data, static_cast<std::streamsize>(bytes));
      sink->written += bytes;
      return sink->out ? bytes : 0;
    }

    int on_progress(void *user, curl_off_t total, curl_off_t done, curl_off_t, curl_off_t) {
      const auto *sink = static_cast<file_sink_t *>(user);
      if (sink->progress && *sink->progress) {
        (*sink->progress)(static_cast<std::uint64_t>(done), static_cast<std::uint64_t>(total));
      }
      return 0;
    }

    CURL *make_curl(const std::string &url, curl_slist *headers, long timeout_s) {
      CURL *curl = curl_easy_init();
      if (!curl) {
        return nullptr;
      }
      curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
#if LIBCURL_VERSION_NUM >= 0x075500
      curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
      curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
      curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
      curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
#endif
      // Asset downloads redirect to GitHub's object storage. libcurl (>= 7.58) does not forward a custom
      // Authorization header to a different host, and UNRESTRICTED_AUTH stays off, so the token stays with api.github.com.
      curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
      curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
      curl_easy_setopt(curl, CURLOPT_UNRESTRICTED_AUTH, 0L);
      curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
      curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_s);
      curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
      curl_easy_setopt(curl, CURLOPT_USERAGENT, "Nova-host/" PROJECT_VERSION);
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
      return curl;
    }

    curl_slist *to_slist(const std::vector<std::string> &headers) {
      curl_slist *list = nullptr;
      for (const auto &h : headers) {
        list = curl_slist_append(list, h.c_str());
      }
      return list;
    }

    http_result_t curl_get(const std::string &url, const std::vector<std::string> &headers, std::size_t max_bytes) {
      http_result_t result;
      curl_slist *list = to_slist(headers);
      CURL *curl = make_curl(url, list, 30);
      if (!curl) {
        curl_slist_free_all(list);
        result.error = "curl init failed";
        return result;
      }
      std::pair<std::string *, std::size_t> sink {&result.body, max_bytes};
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_string);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
      const auto rc = curl_easy_perform(curl);
      if (rc == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
      } else {
        result.error = curl_easy_strerror(rc);
      }
      curl_easy_cleanup(curl);
      curl_slist_free_all(list);
      return result;
    }

    http_result_t curl_download(const std::string &url, const std::vector<std::string> &headers, const std::filesystem::path &to, std::uint64_t max_bytes, const std::function<void(std::uint64_t, std::uint64_t)> &progress) {
      http_result_t result;
      file_sink_t sink;
      sink.out.open(to, std::ios::binary | std::ios::trunc);
      sink.max = max_bytes;
      sink.progress = &progress;
      if (!sink.out) {
        result.error = "can't write " + to.string();
        return result;
      }
      curl_slist *list = to_slist(headers);
      CURL *curl = make_curl(url, list, 900);
      if (!curl) {
        curl_slist_free_all(list);
        result.error = "curl init failed";
        return result;
      }
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
      curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
      curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, on_progress);
      curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &sink);
      curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
      const auto rc = curl_easy_perform(curl);
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
      if (rc != CURLE_OK) {
        result.error = curl_easy_strerror(rc);
        if (result.status == 200) {
          result.status = 0;
        }
      }
      curl_easy_cleanup(curl);
      curl_slist_free_all(list);
      sink.out.close();
      return result;
    }

    std::filesystem::path cache_dir() {
      if (const char *xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg == '/') {
        return std::filesystem::path {xdg} / "nova-host" / "updates";
      }
      if (const char *home = std::getenv("HOME"); home && *home == '/') {
        return std::filesystem::path {home} / ".cache" / "nova-host" / "updates";
      }
      return platf::appdata() / "updates";
    }

    /// The systemd user unit Nova runs in, from /proc/self/cgroup (".../app-io.github.f_e_n_y_x.NovaHost.service").
    std::string own_unit() {
      std::ifstream in {"/proc/self/cgroup"};
      for (std::string line; std::getline(in, line);) {
        const auto slash = line.rfind('/');
        if (slash == std::string::npos) {
          continue;
        }
        auto unit = line.substr(slash + 1);
        if (unit.ends_with(".service") && std::ranges::all_of(unit, [](char c) {
              return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_' || c == '@';
            })) {
          return unit;
        }
      }
      return {};
    }

    void restart_when_idle() {
      std::thread([] {
        platf::set_thread_name("nova::update");
        while (rtsp_stream::session_count() > 0) {
          std::this_thread::sleep_for(2s);
        }
        const auto unit = own_unit();
        if (!unit.empty() && std::getenv("INVOCATION_ID")) {
          const auto r = run_program::run({"systemctl", "--user", "restart", "--no-block", unit}, 15s);
          if (r.exit_code == 0) {
            return;
          }
          BOOST_LOG(warning) << "Update install: systemctl --user restart "sv << unit << " failed; exiting so systemd restarts Nova"sv;
        } else {
          BOOST_LOG(info) << "Update install: not running under systemd; exiting so the new version can be started"sv;
        }
        // The unit uses Restart=on-failure, so a non-zero exit brings the new binary up.
        lifetime::exit_sunshine(75, true);
      }).detach();
    }
  }  // namespace

  service_t &instance() {
    static auto *service = new service_t {deps_t {
      .get = curl_get,
      .download = curl_download,
      .run = [](const std::vector<std::string> &argv) {
        return run_program::run(argv, 10min);
      },
      .streaming = [] {
        return rtsp_stream::session_count() > 0;
      },
      .restart = restart_when_idle,
      .now = [] {
        return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
      },
      .cache_dir = cache_dir(),
      .helper = NOVA_UPDATE_HELPER,
    }};
    return *service;
  }

  options_t current_options() {
    options_t o;
    o.enabled = config::nova.update_check;
    o.prerelease = config::sunshine.notify_pre_releases;
    o.token = config::nova.update_github_token;
    o.current = PROJECT_VERSION;
    return o;
  }
}  // namespace nova_update
