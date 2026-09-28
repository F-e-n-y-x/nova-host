/**
 * @file src/nova_update.h
 * @brief Declarations for Nova's host-side update check and installer (private GitHub releases).
 */
#pragma once

// standard includes
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// lib includes
#include <nlohmann/json.hpp>

// local includes
#include "run_program.h"

/**
 * @brief Update check and one-click install for Nova, run by the host rather than the browser.
 *
 * The browser can't read a private repository's releases (it has no token), and a token in the
 * page would leak. The host asks the GitHub releases API instead, with an optional read-only
 * fine-grained token stored like the other secret settings (masked in /api/config). It picks the
 * highest `nova-vX.Y.Z` release, downloads its .deb only when the owner asks, checks the SHA-256
 * published with the release, and installs through a root helper only while no stream is active.
 */
namespace nova_update {
  inline constexpr std::string_view DEFAULT_REPO = "F-e-n-y-x/nova-host";  ///< Release repository.
  inline constexpr std::string_view TAG_PREFIX = "nova-v";  ///< Release tags look like nova-v0.3.0.
  inline constexpr std::uint64_t MAX_PACKAGE_BYTES = 512ull << 20;  ///< Refuse larger downloads.

  /**
   * @brief A parsed version: X.Y.Z plus semver pre-release identifiers.
   */
  struct version_t {
    int major = 0;  ///< Major version.
    int minor = 0;  ///< Minor version.
    int patch = 0;  ///< Patch version.
    std::vector<std::string> pre;  ///< Pre-release identifiers ("rc", "1"); empty for a release.
    bool local = false;  ///< Local build: the suffix was a git hash and/or "dirty" (X.Y.Z plus local commits).

    /**
     * @brief Format as X.Y.Z[-pre].
     * @return The version string (without the local-build suffix).
     */
    std::string str() const;
  };

  /**
   * @brief Parse a version or tag.
   *
   * Accepts "0.2.0", "v0.2.0", "nova-v0.2.0-rc.1" and "+build" metadata (ignored). When @p tag is false
   * (the running build), a suffix that is only a 7-40 digit hex commit and/or "dirty" — what local
   * builds carry, e.g. "0.1.0-e0371fee-dirty" — marks a local build that compares equal to X.Y.Z
   * instead of a pre-release that would sort below it.
   *
   * @param text Version text.
   * @param tag True for a release tag: the "nova-v" prefix is required and no local-build suffix is recognised.
   * @return The version, or nullopt when it isn't X.Y.Z[-pre].
   */
  std::optional<version_t> parse_version(std::string_view text, bool tag = false);

  /**
   * @brief Compare two versions by semver 2.0 precedence (the local flag is ignored).
   *
   * @param a First version.
   * @param b Second version.
   * @return Negative, zero or positive like strcmp.
   */
  int compare_versions(const version_t &a, const version_t &b);

  /**
   * @brief A release asset.
   */
  struct asset_t {
    std::string name;  ///< File name.
    std::string url;  ///< API URL (https://api.github.com/repos/<repo>/releases/assets/<id>).
    std::uint64_t size = 0;  ///< Size in bytes.
    std::string sha256;  ///< Lowercase hex from the asset's "digest" field, or empty.
  };

  /**
   * @brief The release chosen by pick_release().
   */
  struct release_t {
    std::string tag;  ///< Tag name, e.g. "nova-v0.3.0".
    version_t version;  ///< Parsed tag.
    std::string name;  ///< Release title.
    std::string html_url;  ///< Release page.
    std::string published_at;  ///< ISO 8601 date.
    std::string notes;  ///< Release notes (truncated).
    bool prerelease = false;  ///< GitHub pre-release flag (or a pre-release tag).
    asset_t deb;  ///< The package to install.
    std::optional<asset_t> checksum;  ///< A "<deb>.sha256" or SHA256SUMS asset, if published.
  };

  /**
   * @brief Pick the newest installable release from a GitHub /releases response.
   *
   * Drafts, tags that are not nova-vX.Y.Z[-pre], releases without a .deb for @p arch and asset URLs
   * outside the repository's API path are ignored. Pre-releases count only when @p include_prerelease.
   *
   * @param releases Parsed JSON array.
   * @param repo "owner/name", used to validate asset URLs.
   * @param include_prerelease Also consider pre-releases.
   * @param arch Debian architecture, e.g. "amd64".
   * @return The highest-version release, or nullopt.
   */
  std::optional<release_t> pick_release(const nlohmann::json &releases, std::string_view repo, bool include_prerelease, std::string_view arch);

  /**
   * @brief Find a file's SHA-256 in a checksum file ("<hex>  <name>" lines, or a bare hex digest).
   *
   * @param text Checksum file contents.
   * @param filename File to look for.
   * @return Lowercase hex digest, or nullopt.
   */
  std::optional<std::string> parse_checksum(std::string_view text, std::string_view filename);

  /**
   * @brief Whether @p latest is newer than the running version.
   *
   * @param current Running version string (PROJECT_VERSION).
   * @param latest Candidate release version.
   * @return False when @p current can't be parsed, so an odd build never shows a false banner.
   */
  bool is_newer(std::string_view current, const version_t &latest);

  /**
   * @brief Result of an HTTP request.
   */
  struct http_result_t {
    long status = 0;  ///< HTTP status, 0 on a transport error.
    std::string body;  ///< Response body (GET only).
    std::string error;  ///< Transport error text.
  };

  /**
   * @brief Injected side effects (fakes in tests).
   */
  struct deps_t {
    /// GET @p url with @p headers, keeping at most @p max_bytes of body.
    std::function<http_result_t(const std::string &url, const std::vector<std::string> &headers, std::size_t max_bytes)> get;
    /// Download @p url to @p to (at most @p max_bytes), reporting (done, total).
    std::function<http_result_t(const std::string &url, const std::vector<std::string> &headers, const std::filesystem::path &to, std::uint64_t max_bytes, const std::function<void(std::uint64_t, std::uint64_t)> &progress)> download;
    /// Run a program (dpkg-deb, sudo).
    std::function<run_program::result_t(const std::vector<std::string> &argv)> run;
    /// Whether any client is streaming right now.
    std::function<bool()> streaming;
    /// Restart Nova so the new binary runs.
    std::function<void()> restart;
    /// Unix time in seconds.
    std::function<std::int64_t()> now;
    std::filesystem::path cache_dir;  ///< Where packages are downloaded; the previous one is kept for rollback.
    std::string helper;  ///< Root install helper run through `sudo -n`.
  };

  /**
   * @brief Settings for one check or install.
   */
  struct options_t {
    bool enabled = true;  ///< Config `update_check`.
    bool prerelease = false;  ///< Config `notify_pre_releases`.
    std::string token;  ///< Config `update_github_token`.
    std::string current;  ///< Running version.
    std::string repo {DEFAULT_REPO};  ///< Release repository.
    std::string arch = "amd64";  ///< Debian architecture.
  };

  /**
   * @brief Install progress states.
   */
  enum class install_state_e {
    idle,  ///< Nothing started.
    downloading,  ///< Fetching the package.
    verifying,  ///< Checking SHA-256 and package metadata.
    installing,  ///< Running the root helper.
    restarting,  ///< Installed; restarting once no stream is active.
    manual,  ///< Downloaded and verified, but self-install isn't enabled: the owner runs `command`.
    failed,  ///< Something went wrong; see `message`.
  };

  /**
   * @brief Name used in the JSON status.
   * @param state The state.
   * @return Lowercase state name.
   */
  std::string_view to_string(install_state_e state);

  /**
   * @brief Result of starting an install.
   */
  enum class start_result_e {
    started,  ///< The install runs.
    streaming,  ///< Refused: a client is streaming.
    busy,  ///< Refused: an install is already running.
    no_update,  ///< Refused: the tag isn't the newer release from the last check.
  };

  /**
   * @brief Update checker and installer. Thread-safe; one per process (see instance()).
   */
  class service_t {
  public:
    /**
     * @brief Create a service.
     * @param deps Side effects.
     */
    explicit service_t(deps_t deps);

    /**
     * @brief Query the release API now (blocking).
     * @param options Settings.
     */
    void check(const options_t &options);

    /**
     * @brief Whether the last check is older than @p max_age_s (or never ran).
     * @param max_age_s Maximum age in seconds.
     * @return True when a new check is due.
     */
    bool stale(std::int64_t max_age_s) const;

    /**
     * @brief Validate and claim an install of @p tag; call run_install() next (on a worker thread).
     *
     * @param options Settings.
     * @param tag Tag the owner confirmed in the UI.
     * @return Whether the install may run.
     */
    start_result_e begin_install(const options_t &options, const std::string &tag);

    /**
     * @brief Download, verify and install the release claimed by begin_install() (blocking).
     * @param options Settings.
     */
    void run_install(const options_t &options);

    /**
     * @brief Status for GET /api/update/status.
     * @param options Settings.
     * @return JSON object.
     */
    nlohmann::json status_json(const options_t &options) const;

    /**
     * @brief Mark a check as running (for the UI) and return false if one already is.
     * @return True when the caller should run check().
     */
    bool claim_check();

  private:
    std::vector<std::string> api_headers(const options_t &options, bool binary) const;
    void fail(const std::string &message);
    void set_state(install_state_e state, const std::string &message = {});
    std::optional<std::string> expected_sha256(const options_t &options, const release_t &release);

    deps_t deps_;
    mutable std::mutex mutex_;
    bool checking_ = false;
    std::int64_t checked_at_ = 0;
    std::string check_error_;
    std::optional<release_t> latest_;
    install_state_e state_ = install_state_e::idle;
    std::string message_;
    std::string command_;
    std::string install_tag_;
    double progress_ = 0;
  };

  /**
   * @brief SHA-256 of a file as lowercase hex.
   * @param path File.
   * @return Digest, or empty when the file can't be read.
   */
  std::string sha256_file(const std::filesystem::path &path);

  /**
   * @brief The process-wide service with libcurl, the packaged helper and ~/.cache/nova-host/updates.
   * @return The service.
   */
  service_t &instance();

  /**
   * @brief Options from the current config and PROJECT_VERSION.
   * @return Options.
   */
  options_t current_options();
}  // namespace nova_update
