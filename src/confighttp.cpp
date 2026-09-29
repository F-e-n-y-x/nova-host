/**
 * @file src/confighttp.cpp
 * @brief Definitions for the Web UI Config HTTP server.
 *
 * @todo Authentication, better handling of routes common to nvhttp, cleanup
 */
#define BOOST_BIND_GLOBAL_PLACEHOLDERS

// standard includes
#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <cstdint>
#include <format>
#include <fstream>
#include <iterator>
#include <new>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

// lib includes
#include <boost/algorithm/string.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/filesystem.hpp>
#include <lizardbyte/common/env.h>
#include <nlohmann/json.hpp>
#include <Simple-Web-Server/crypto.hpp>
#include <Simple-Web-Server/server_https.hpp>

#ifdef _WIN32
  #include "platform/virtualhid_input.h"
  #include "platform/windows/misc.h"
  #include "platform/windows/utf_utils.h"

  #include <Windows.h>
#endif

// local includes
#include "abr.h"
#include "app_lifecycle.h"
#include "client_permissions.h"
#include "clipboard.h"
#include "config.h"
#include "confighttp.h"
#include "crypto.h"
#include "display_device.h"
#include "display_follow.h"
#include "file_handler.h"
#include "globals.h"
#include "host_commands.h"
#include "host_info.h"
#include "httpcommon.h"
#include "input.h"
#include "library/artwork.h"
#include "library/library.h"
#include "library/match.h"
#include "library/metadata.h"
#include "library/steam_catalog.h"
#include "library/title.h"
#include "library/url_fetch.h"
#include "logging.h"
#include "login_guard.h"
#include "network.h"
#include "nova_client_api.h"
#include "nova_perf.h"
#include "nova_update_http.h"
#include "nvhttp.h"
#include "platform/common.h"
#include "process.h"
#include "rtsp.h"
#include "stream_stats.h"
#include "system_tray.h"
#include "utility.h"
#include "uuid.h"
#include "web_session.h"

using namespace std::literals;

namespace confighttp {
  namespace fs = std::filesystem;

  /**
   * @brief HTTPS server type used for Sunshine's configuration UI.
   */
  using https_server_t = SimpleWeb::Server<SimpleWeb::HTTPS>;

  /**
   * @brief Case-insensitive map used for HTTP headers and query parameters.
   */
  using args_t = SimpleWeb::CaseInsensitiveMultimap;
  /**
   * @brief Shared HTTPS response object passed to configuration handlers.
   */
  using resp_https_t = std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response>;
  /**
   * @brief Shared HTTPS request object received by configuration handlers.
   */
  using req_https_t = std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request>;
  /**
   * @brief Handler signature for configuration UI HTTPS routes.
   */
  using https_handler_t = std::function<void(resp_https_t, req_https_t)>;

  namespace {
    using license_status_provider_t = std::function<lvh::LicenseResult()>;  ///< Provider for the current libvirtualhid license status.
#if defined(linux) || defined(__FreeBSD__) || defined(SUNSHINE_TESTS)
    using portal_token_path_provider_t = std::function<fs::path()>;  ///< Provider for the XDG Portal token path.
#endif

    /**
     * @brief Return the current libvirtualhid license status provider.
     *
     * Unit-test builds expose a mutable provider so the HTTP fixture can avoid
     * contacting an installed Windows broker. Production builds keep the
     * provider const and always call libvirtualhid directly.
     *
     * @return License status provider for the current build.
     */
    auto &virtual_input_license_status_provider() {
#ifdef SUNSHINE_TESTS
      static license_status_provider_t status_provider = lvh::get_license_status;
#else
      static const license_status_provider_t status_provider = lvh::get_license_status;
#endif
      return status_provider;
    }

#if defined(linux) || defined(__FreeBSD__) || defined(SUNSHINE_TESTS)
    /**
     * @brief Return the path provider for the saved XDG Portal restore token.
     *
     * @return Path provider for the current build.
     */
    auto &portal_token_path_provider() {
  #ifdef SUNSHINE_TESTS
      static portal_token_path_provider_t path_provider = []() {
        return platf::appdata() / "portal_token";
      };
  #else
      static const portal_token_path_provider_t path_provider = []() {
        return platf::appdata() / "portal_token";
      };
  #endif
      return path_provider;
    }
#endif
  }  // namespace

  /**
   * @brief Client certificate operations accepted by the configuration API.
   */
  enum class op_e {
    ADD,  ///< Add client
    REMOVE  ///< Remove client
  };

  /**
   * @brief Overwrite a request-local sensitive string when leaving scope.
   */
  class scoped_sensitive_string_clear_t {
  public:
    /**
     * @brief Register a sensitive string for best-effort clearing.
     *
     * @param value Mutable sensitive string.
     */
    explicit scoped_sensitive_string_clear_t(std::string &value):
        value_ {value} {}

    scoped_sensitive_string_clear_t(const scoped_sensitive_string_clear_t &) = delete;
    scoped_sensitive_string_clear_t &operator=(const scoped_sensitive_string_clear_t &) = delete;

    /**
     * @brief Overwrite and clear the registered string.
     */
    ~scoped_sensitive_string_clear_t() {
      std::fill(value_.begin(), value_.end(), '\0');
      value_.clear();
    }

  private:
    std::string &value_;  ///< Sensitive request-local string.
  };

#ifdef SUNSHINE_TESTS
  void set_virtual_input_license_status_provider_for_testing(virtual_input_license_status_provider_t status_provider) {
    virtual_input_license_status_provider() = std::move(status_provider);
  }

  void reset_virtual_input_license_status_provider_for_testing() {
    virtual_input_license_status_provider() = lvh::get_license_status;
  }

  void set_portal_token_path_provider_for_testing(confighttp::portal_token_path_provider_t path_provider) {
    portal_token_path_provider() = std::move(path_provider);
  }

  void reset_portal_token_path_provider_for_testing() {
    portal_token_path_provider() = []() {
      return platf::appdata() / "portal_token";
    };
  }

  void clear_sensitive_string_for_testing(std::string &value) {
    const scoped_sensitive_string_clear_t clear_value {value};
  }
#endif

  // CSRF token management
  /**
   * @brief CSRF token value and its expiration deadline.
   */
  constexpr std::string_view secret_placeholder = "********";  ///< Shown instead of stored secrets such as the SteamGridDB key.

  void mask_secret_config(nlohmann::json &tree) {
    for (const auto key : secret_config_keys) {
      if (tree.contains(key) && tree[key].is_string() && !tree[key].get<std::string>().empty()) {
        tree[key] = std::string(secret_placeholder);
      }
    }
  }

  void restore_secret_config(nlohmann::json &input, const std::unordered_map<std::string, std::string> &current) {
    for (const auto key : secret_config_keys) {
      if (input.contains(key) && input[key] == std::string(secret_placeholder)) {
        const auto it = current.find(std::string(key));
        input[key] = it == current.end() ? std::string {} : it->second;
      }
    }
  }

  struct csrf_token_t {
    std::string token;  ///< Random token value that must be echoed by the client.
    std::chrono::steady_clock::time_point expiration;  ///< Monotonic deadline after which the token is rejected.
  };

  std::map<std::string, csrf_token_t, std::less<>> csrf_tokens;  ///< CSRF tokens by client identifier. NOSONAR(cpp:S5421) - intentionally mutable global
  std::mutex csrf_tokens_mutex;  ///< Mutex protecting CSRF token storage. NOSONAR(cpp:S5421) - intentionally mutable global

  // CSRF token configuration
  /**
   * @brief Number of random bytes used when generating a CSRF token.
   */
  constexpr auto CSRF_TOKEN_SIZE = 32;  // 32 bytes = 256 bits
  /**
   * @brief Amount of time a generated CSRF token remains valid.
   */
  constexpr auto CSRF_TOKEN_LIFETIME = std::chrono::hours(1);  // Tokens valid for 1 hour

  constexpr std::string_view libvirtualhid_minimum_version = LIBVIRTUALHID_MINIMUM_VERSION;  ///< Minimum supported libvirtualhid driver version.
  constexpr auto VIGEMBUS_MINIMUM_VERSION = "1.17.0.0"sv;  ///< Minimum supported ViGEmBus fallback driver version.  // NOSONAR(cpp:S1313): not an IP address

  /**
   * @brief Parse one dotted driver-version component.
   *
   * @param part Version component text.
   * @return Parsed component value, or empty when invalid.
   */
  std::optional<unsigned int> parse_driver_version_part(std::string_view part) {
    if (part.empty()) {
      return std::nullopt;
    }

    unsigned int value = 0;
    const auto *begin = part.data();
    const auto *end = part.data() + part.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc {} || ptr != end) {
      return std::nullopt;
    }

    return value;
  }

  /**
   * @brief Parse a dotted driver version into numeric components.
   *
   * @param version Driver version text.
   * @return Parsed version parts, or empty when invalid.
   */
  std::optional<std::vector<unsigned int>> parse_driver_version(std::string_view version) {
    if (version.empty()) {
      return std::nullopt;
    }

    std::vector<unsigned int> parts;
    std::size_t start = 0;
    while (start <= version.size()) {
      const auto dot = version.find('.', start);
      const auto length = dot == std::string_view::npos ? std::string_view::npos : dot - start;
      const auto part = parse_driver_version_part(version.substr(start, length));
      if (!part.has_value()) {
        return std::nullopt;
      }

      parts.push_back(*part);
      if (dot == std::string_view::npos) {
        break;
      }
      start = dot + 1;
    }

    return parts;
  }

  bool is_driver_version_development(std::string_view version) {
    const auto version_parts = parse_driver_version(version);
    return version_parts && version_parts->size() >= 3U && (*version_parts)[0] == 0U && (*version_parts)[1] == 0U;
  }

  bool is_driver_version_supported(std::string_view version, std::string_view minimum_version) {
    if (minimum_version.empty() || is_driver_version_development(version)) {
      return true;
    }

    const auto version_parts = parse_driver_version(version);
    const auto minimum_parts = parse_driver_version(minimum_version);
    if (!version_parts || !minimum_parts) {
      return false;
    }

    const auto part_count = std::max(version_parts->size(), minimum_parts->size());
    for (std::size_t i = 0; i < part_count; ++i) {
      const auto version_part = i < version_parts->size() ? (*version_parts)[i] : 0U;
      const auto minimum_part = i < minimum_parts->size() ? (*minimum_parts)[i] : 0U;
      if (version_part != minimum_part) {
        return version_part > minimum_part;
      }
    }

    return true;
  }

  nlohmann::json build_driver_status(bool installed, const std::string &version, std::string_view minimum_version) {
    const auto minimum_version_text = std::string {minimum_version};

    nlohmann::json output_tree;
    output_tree["installed"] = installed;
    output_tree["version"] = version;
    output_tree["minimum_version"] = minimum_version_text;
    output_tree["supported_versions"] = minimum_version.empty() ? "Any" : std::format(">= {}", minimum_version_text);
    output_tree["development_version"] = installed && is_driver_version_development(version);
    output_tree["version_compatible"] = installed && is_driver_version_supported(version, minimum_version);

    return output_tree;
  }

  /**
   * @brief Return a stable Web UI name for a libvirtualhid license state.
   *
   * @param state License state.
   * @return Lowercase state name.
   */
  std::string_view virtualhid_license_state_name(lvh::LicenseState state) {
    using enum lvh::LicenseState;

    switch (state) {
      case unlicensed:
        return "unlicensed";
      case licensed:
        return "licensed";
      case expired:
        return "expired";
      case disabled:
        return "disabled";
      case invalid:
        return "invalid";
      case unavailable:
      default:
        return "unavailable";
    }
  }

  nlohmann::json build_virtualhid_license_status(const lvh::LicenseResult &result) {
    const auto &license = result.license;
    nlohmann::json output_tree;
    output_tree["operation_ok"] = result.status.ok();
    output_tree["service_available"] = license.service_available;
    output_tree["state"] = virtualhid_license_state_name(license.state);
    output_tree["licensed"] = license.licensed();
    output_tree["active_devices"] = license.active_devices;
    output_tree["activation_limit"] = license.activation_limit;
    output_tree["activation_usage"] = license.activation_usage;
    output_tree["plan_name"] = license.plan_name;
    output_tree["customer_email"] = license.customer_email;
    output_tree["message"] = license.message;
    output_tree["purchase_url"] = license.purchase_url;
    output_tree["manage_account_url"] = license.manage_account_url;
    output_tree["error"] = result.status.ok() ? "" : result.status.message();
    return output_tree;
  }

  namespace {
    /**
     * @brief Handle a virtual-input license request using the configured status provider.
     *
     * @param response HTTP response object.
     * @param request Authenticated HTTP request.
     */
    void get_virtual_input_license(const resp_https_t &response, const req_https_t &request) {
      if (!authenticate(response, request)) {
        return;
      }

      print_req(request);
      send_response(response, build_virtualhid_license_status(virtual_input_license_status_provider()()));
    }
  }  // namespace

#ifdef _WIN32
  /**
   * @brief RAII wrapper for a Windows registry key handle.
   */
  class registry_key_t {
  public:
    /**
     * @brief Construct an empty registry key wrapper.
     */
    registry_key_t() = default;

    /**
     * @brief Copy construction is disabled because the wrapper owns a handle.
     */
    registry_key_t(const registry_key_t &) = delete;

    /**
     * @brief Copy assignment is disabled because the wrapper owns a handle.
     *
     * @return This registry key wrapper.
     */
    registry_key_t &operator=(const registry_key_t &) = delete;

    /**
     * @brief Close the owned registry key handle.
     */
    ~registry_key_t() {
      close();
    }

    /**
     * @brief Get the owned registry key handle.
     *
     * @return Registry key handle.
     */
    HKEY get() const {
      return handle;
    }

    /**
     * @brief Prepare the wrapper to receive a registry key handle.
     *
     * @return Address of the wrapped handle.
     */
    HKEY *put() {
      close();
      return &handle;
    }

  private:
    /**
     * @brief Close the owned registry key handle if one is open.
     */
    void close() {
      if (handle) {
        RegCloseKey(handle);
        handle = nullptr;
      }
    }

    HKEY handle = nullptr;  ///< Owned Windows registry key handle.
  };

  /**
   * @brief Read a string value from a Windows registry key.
   *
   * @param key Registry key to query.
   * @param value_name Registry value name.
   * @return Registry string value, or empty when unavailable.
   */
  std::optional<std::wstring> read_registry_string_value(HKEY key, const wchar_t *value_name) {
    DWORD value_type = 0;
    DWORD value_size = 0;
    if (RegGetValueW(key, nullptr, value_name, RRF_RT_REG_SZ, &value_type, nullptr, &value_size) != ERROR_SUCCESS || value_size == 0) {
      return std::nullopt;
    }

    std::wstring value(value_size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(key, nullptr, value_name, RRF_RT_REG_SZ, &value_type, value.data(), &value_size) != ERROR_SUCCESS) {
      return std::nullopt;
    }

    while (!value.empty() && value.back() == L'\0') {
      value.pop_back();
    }
    return value;
  }

  /**
   * @brief Read the installed libvirtualhid driver version from the Windows device registry.
   *
   * @return Driver version string, or empty when unavailable.
   */
  std::string read_libvirtualhid_driver_version() {
    registry_key_t root_key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Enum\\ROOT\\LIBVIRTUALHID", 0, KEY_READ, root_key.put()) != ERROR_SUCCESS) {
      return {};
    }

    for (DWORD index = 0;; ++index) {
      std::wstring subkey_name(256, L'\0');
      auto subkey_name_size = static_cast<DWORD>(subkey_name.size());
      const auto enum_status = RegEnumKeyExW(root_key.get(), index, subkey_name.data(), &subkey_name_size, nullptr, nullptr, nullptr, nullptr);
      if (enum_status == ERROR_NO_MORE_ITEMS) {
        break;
      }
      if (enum_status != ERROR_SUCCESS) {
        continue;
      }

      std::wstring device_key_path = L"SYSTEM\\CurrentControlSet\\Enum\\ROOT\\LIBVIRTUALHID\\";
      device_key_path.append(subkey_name, 0, subkey_name_size);

      registry_key_t device_key;
      if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, device_key_path.c_str(), 0, KEY_READ, device_key.put()) != ERROR_SUCCESS) {
        continue;
      }

      const auto driver_key_suffix = read_registry_string_value(device_key.get(), L"Driver");
      if (!driver_key_suffix) {
        continue;
      }

      std::wstring driver_key_path = L"SYSTEM\\CurrentControlSet\\Control\\Class\\";
      driver_key_path += *driver_key_suffix;

      registry_key_t driver_key;
      if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, driver_key_path.c_str(), 0, KEY_READ, driver_key.put()) != ERROR_SUCCESS) {
        continue;
      }

      if (const auto version = read_registry_string_value(driver_key.get(), L"DriverVersion")) {
        return utf_utils::to_utf8(*version);
      }
    }

    return {};
  }

#endif

  /**
   * @brief Log the request details.
   * @param request The HTTP request object.
   */
  void print_req(const req_https_t &request) {
    BOOST_LOG(debug) << "METHOD :: "sv << request->method;
    BOOST_LOG(debug) << "DESTINATION :: "sv << request->path;

    for (auto &[name, val] : request->header) {
      BOOST_LOG(debug) << name << " -- " << (name == "Authorization" ? "CREDENTIALS REDACTED" : val);
    }

    BOOST_LOG(debug) << " [--] "sv;

    for (auto &[name, val] : request->parse_query_string()) {
      BOOST_LOG(debug) << name << " -- " << val;
    }

    BOOST_LOG(debug) << " [--] "sv;
  }

  /**
   * @brief Send a response.
   * @param response The HTTP response object.
   * @param output_tree The JSON tree to send.
   */
  /**
   * @brief Identifies the Web UI build being served: FNV-1a of index.html (which names every
   *        hashed asset), read once per process.
   *
   * API responses carry it as X-Nova-Build; a page loaded from an older build (a tab left open
   * across a package upgrade and restart) sees a different value and reloads, instead of running
   * old code against the new API.
   *
   * @return 16 hex characters, or empty when index.html can't be read.
   */
  const std::string &web_build_id() {
    static const std::string id = [] {
      std::ifstream in(WEB_DIR "index.html", std::ios::binary);
      if (!in) {
        return std::string {};
      }
      const std::string content {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
      std::uint64_t hash = 0xcbf29ce484222325ULL;
      for (const unsigned char c : content) {
        hash ^= c;
        hash *= 0x100000001b3ULL;
      }
      return std::format("{:016x}", hash);
    }();
    return id;
  }

  void send_response(const resp_https_t &response, const nlohmann::json &output_tree) {
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "application/json");
    if (!web_build_id().empty()) {
      headers.emplace("X-Nova-Build", web_build_id());
    }
    headers.emplace("X-Frame-Options", "DENY");
    headers.emplace("Content-Security-Policy", "frame-ancestors 'none';");
    response->write(output_tree.dump(), headers);
  }

  namespace {
    /**
     * @brief Where a request says it came from, compared with the host it was sent to.
     */
    enum class origin_e {
      absent,  ///< Neither Origin nor Referer (curl, scripts; or a browser told not to send them).
      same,  ///< Origin/Referer is this host, or one of csrf_allowed_origins.
      cross,  ///< Another site.
    };

    /**
     * @brief Authority ("host:port") of an absolute URL, or empty.
     * @param url Absolute URL.
     * @return The authority.
     */
    std::string_view authority_of(const std::string_view url) {
      const auto scheme = url.find("://");
      if (scheme == std::string_view::npos) {
        return {};
      }
      const auto rest = url.substr(scheme + 3);
      return rest.substr(0, rest.find_first_of("/?#"));
    }

    /**
     * @brief Whether a URL starts with one of csrf_allowed_origins (followed by ':' or '/' or the end).
     * @param url Origin or Referer value.
     * @return True when allowed.
     */
    bool is_allowed_origin(const std::string_view url) {
      return std::ranges::any_of(config::sunshine.csrf_allowed_origins, [&url](const std::string &allowed_origin) {
        if (!url.starts_with(allowed_origin)) {
          return false;
        }
        const size_t len = allowed_origin.length();
        return url.length() == len || url[len] == ':' || url[len] == '/';
      });
    }

    /**
     * @brief Compare Origin (or, without it, Referer) with the Host header and csrf_allowed_origins.
     *
     * Same-origin by construction: when the Origin/Referer authority equals the Host header the
     * browser used to reach us, the request can't be cross-site, whichever name, IP or tailnet
     * address the host is reached by. csrf_allowed_origins covers reverse proxies.
     *
     * @param request The HTTP request.
     * @return Where the request came from.
     */
    origin_e request_origin(const req_https_t &request) {
      const auto host_it = request->header.find("Host");
      const auto judge = [&](const std::string_view url) {
        if (host_it != request->header.end() && !authority_of(url).empty() && authority_of(url) == host_it->second) {
          return origin_e::same;
        }
        return is_allowed_origin(url) ? origin_e::same : origin_e::cross;
      };
      // "Origin: null" (sandboxed or privacy-restricted contexts) says nothing about the site.
      if (const auto origin_it = request->header.find("Origin"); origin_it != request->header.end() && origin_it->second != "null") {
        return judge(origin_it->second);
      }
      if (const auto referer_it = request->header.find("Referer"); referer_it != request->header.end()) {
        return judge(referer_it->second);
      }
      return origin_e::absent;
    }

    /**
     * @brief Whether a method can change state (anything but GET, HEAD and OPTIONS).
     * @param method HTTP method.
     * @return True for POST, PUT, PATCH, DELETE and unknown methods.
     */
    bool is_state_changing(const std::string_view method) {
      return method != "GET" && method != "HEAD" && method != "OPTIONS";
    }

    /**
     * @brief Case-insensitive "header contains" check.
     */
    bool header_has(const req_https_t &request, const std::string_view name, const std::string_view needle) {
      const auto it = request->header.find(std::string {name});
      return it != request->header.end() && boost::icontains(it->second, needle);
    }

    /**
     * @brief The request came from a web browser rather than curl or a script.
     *
     * Browsers get a 401 without WWW-Authenticate (so no native sign-in popup) or a redirect to
     * /login. Fetch metadata (Sec-Fetch-*) is sent by every current browser; Accept: text/html,
     * X-Requested-With and a session cookie cover the rest.
     *
     * @param request The HTTP request.
     * @return True for browser requests.
     */
    bool from_browser(const req_https_t &request) {
      const auto &h = request->header;
      return h.find("Sec-Fetch-Site") != h.end() || h.find("Sec-Fetch-Mode") != h.end() || h.find("Sec-Fetch-Dest") != h.end() ||
             h.find("X-Requested-With") != h.end() || header_has(request, "Accept", "text/html") ||
             header_has(request, "Cookie", web_session::cookie_name);
    }

    /**
     * @brief A browser engine sent this request (fetch metadata present).
     *
     * Such requests sign in with the session cookie only. HTTP Basic credentials a browser
     * cached from the old sign-in popup are ignored, so signing out really signs out, and a
     * cross-site page can't ride on them. curl, scripts and other API clients send no fetch
     * metadata and keep using Basic.
     *
     * @param request The HTTP request.
     * @return True when Sec-Fetch-Site is present.
     */
    bool from_browser_engine(const req_https_t &request) {
      return request->header.find("Sec-Fetch-Site") != request->header.end();
    }

    /**
     * @brief The request is a page load (navigation) rather than an API call.
     * @param request The HTTP request.
     * @return True for GET navigations outside /api/.
     */
    bool is_page_load(const req_https_t &request) {
      if (request->method != "GET" || request->path.starts_with("/api/"sv) || request->path == "/api") {
        return false;
      }
      return header_has(request, "Sec-Fetch-Mode", "navigate") || header_has(request, "Sec-Fetch-Dest", "document") ||
             header_has(request, "Accept", "text/html");
    }

    /**
     * @brief The session cookie's value, or empty.
     * @param request The HTTP request.
     * @return Token.
     */
    std::string session_cookie(const req_https_t &request) {
      const auto [first, last] = request->header.equal_range("Cookie");
      for (auto it = first; it != last; ++it) {
        if (auto value = web_session::cookie_value(it->second, web_session::cookie_name); !value.empty()) {
          return value;
        }
      }
      return {};
    }

    /**
     * @brief Tag of the credentials in effect; sessions made under other credentials are void.
     * @return The tag.
     */
    std::string current_cred_tag() {
      return web_session::credential_tag(config::sunshine.password, config::sunshine.salt);
    }

    /**
     * @brief The signed-in session this request carries, if any.
     * @param request The HTTP request.
     * @param had_cookie Set when a session cookie was present (valid or not).
     * @return The session.
     */
    std::optional<web_session::session_t> request_session(const req_https_t &request, bool *had_cookie = nullptr) {
      const auto token = session_cookie(request);
      if (had_cookie) {
        *had_cookie = !token.empty();
      }
      if (token.empty() || config::sunshine.username.empty()) {
        return std::nullopt;
      }
      return web_session::web_ui().validate(token, current_cred_tag(), web_session::clock::now());
    }

    /**
     * @brief Headers every auth response carries.
     */
    SimpleWeb::CaseInsensitiveMultimap auth_headers(const std::string_view content_type = "application/json") {
      return {
        {"Content-Type", std::string {content_type}},
        {"Cache-Control", "no-store"},
        {"X-Frame-Options", "DENY"},
        {"Content-Security-Policy", "frame-ancestors 'none';"}
      };
    }

    /**
     * @brief The request's path and query, as sent.
     */
    std::string request_target(const req_https_t &request) {
      return request->query_string.empty() ? request->path : request->path + "?" + request->query_string;
    }

    /**
     * @brief 401 (or, for a page load, a redirect to /login) without asking for Basic credentials
     *        when a browser is asking.
     * @param clear_cookie Also delete a stale session cookie.
     */
    void send_unauthorized_impl(const resp_https_t &response, const req_https_t &request, const bool clear_cookie) {
      auto address = net::addr_to_normalized_string(request->remote_endpoint().address());
      BOOST_LOG(info) << "Web UI: ["sv << address << "] -- not authorized"sv;

      if (is_page_load(request)) {
        auto headers = auth_headers("text/html; charset=utf-8");
        headers.emplace("Location", web_session::login_redirect(request_target(request)));
        if (clear_cookie) {
          headers.emplace("Set-Cookie", web_session::clear_cookie());
        }
        response->write(SimpleWeb::StatusCode::redirection_see_other, headers);
        return;
      }

      constexpr auto code = SimpleWeb::StatusCode::client_error_unauthorized;
      nlohmann::json tree;
      tree["status_code"] = code;
      tree["status"] = false;
      tree["error"] = "Unauthorized";

      auto headers = auth_headers();
      if (!from_browser(request)) {
        // API clients (curl, scripts) may still be challenged for Basic credentials.
        headers.emplace("WWW-Authenticate", R"(Basic realm="Nova", charset="UTF-8")");
      }
      if (clear_cookie) {
        headers.emplace("Set-Cookie", web_session::clear_cookie());
      }
      response->write(code, tree.dump(), headers);
    }

    /**
     * @brief CSRF check for a state-changing request signed in with the session cookie.
     *
     * Passes when Origin (or Referer) is this host or an allowed origin, or when X-CSRF-Token
     * carries this session's token. A token that is sent must be right. Unlike Basic-auth
     * requests, a cookie request with neither header nor token is refused: the cookie is sent
     * automatically, so its presence proves nothing about who made the request.
     *
     * @param response The HTTP response object.
     * @param request The HTTP request object.
     * @param session The session the request carries.
     * @return True when the request may proceed.
     */
    bool session_csrf_ok(const resp_https_t &response, const req_https_t &request, const web_session::session_t &session) {
      const auto address = net::addr_to_normalized_string(request->remote_endpoint().address());
      bool token_ok = false;
      if (const auto token_it = request->header.find("X-CSRF-Token"); token_it != request->header.end()) {
        token_ok = web_session::equal_ct(token_it->second, session.csrf_token);
        if (!token_ok) {
          BOOST_LOG(error) << "Web UI: ["sv << address << "] -- CSRF token mismatch on "sv << request->method << ' ' << request->path;
          bad_request(response, request, "Invalid CSRF token");
          return false;
        }
      }
      switch (request_origin(request)) {
        case origin_e::same:
          return true;
        case origin_e::cross:
          if (token_ok) {
            return true;
          }
          BOOST_LOG(error) << "Web UI: ["sv << address << "] -- CSRF protection blocked a cross-site "sv << request->method << ' ' << request->path;
          bad_request(response, request, "Missing CSRF token");
          return false;
        case origin_e::absent:
          if (token_ok) {
            return true;
          }
          BOOST_LOG(error) << "Web UI: ["sv << address << "] -- CSRF protection blocked "sv << request->method << ' ' << request->path << " without Origin or token"sv;
          bad_request(response, request, "Missing CSRF token");
          return false;
      }
      return false;
    }
  }  // namespace

  /**
   * @brief Send a 401 Unauthorized response.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   */
  void send_unauthorized(const resp_https_t &response, const req_https_t &request) {
    send_unauthorized_impl(response, request, false);
  }

  /**
   * @brief Send a redirect response.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * @param path The path to redirect to.
   */
  void send_redirect(const resp_https_t &response, const req_https_t &request, const char *path) {
    auto address = net::addr_to_normalized_string(request->remote_endpoint().address());
    BOOST_LOG(info) << "Web UI: ["sv << address << "] -- not authorized"sv;
    const SimpleWeb::CaseInsensitiveMultimap headers {
      {"Location", path},
      {"X-Frame-Options", "DENY"},
      {"Content-Security-Policy", "frame-ancestors 'none';"}
    };
    response->write(SimpleWeb::StatusCode::redirection_temporary_redirect, headers);
  }

  /**
   * @brief Refuse (403) a request whose source address is outside origin_web_ui_allowed.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * @return True when the source may use the web UI.
   */
  bool origin_allowed(const resp_https_t &response, const req_https_t &request) {
    const auto address = net::addr_to_normalized_string(request->remote_endpoint().address());
    if (const auto ip_type = net::from_address(address); ip_type > http::origin_web_ui_allowed) {
      BOOST_LOG(info) << "Web UI: ["sv << address << "] -- denied"sv;
      response->write(SimpleWeb::StatusCode::client_error_forbidden);
      return false;
    }
    return true;
  }

  /**
   * @brief Authenticate the user: a valid session cookie, or HTTP Basic for API clients.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * @return True if the user is authenticated, false otherwise (the response has been sent).
   */
  bool authenticate(const resp_https_t &response, const req_https_t &request) {
    auto address = net::addr_to_normalized_string(request->remote_endpoint().address());

    if (!origin_allowed(response, request)) {
      return false;
    }

    // If credentials are shown, redirect the user to a /welcome page
    if (config::sunshine.username.empty()) {
      send_redirect(response, request, "/welcome");
      return false;
    }

    // 1. The session cookie set by /api/auth/login.
    bool had_cookie = false;
    if (const auto session = request_session(request, &had_cookie)) {
      return !is_state_changing(request->method) || session_csrf_ok(response, request, *session);
    }

    // 2. HTTP Basic, for curl and other API clients. Browsers use the sign-in page instead.
    const auto auth = request->header.find("authorization");
    if (auth == request->header.end() || from_browser_engine(request)) {
      // No credentials yet: not a failed attempt.
      send_unauthorized_impl(response, request, had_cookie);
      return false;
    }

    // Too many wrong passwords from this source: refuse without even checking, so guessing
    // stays slow whatever the password.
    const auto source = login_guard::source_key(address);
    const auto now = login_guard::limiter_t::clock::now();
    if (const auto wait = login_guard::web_ui().locked_for(source, now); wait.count() > 0) {
      send_locked_out(response, request, wait);
      return false;
    }

    const auto &rawAuth = auth->second;
    const auto fail = [&]() {
      // Fingerprint the attempt so a stale client repeating one wrong password counts once.
      const auto fingerprint = util::hex(crypto::hash(rawAuth + config::sunshine.salt)).to_string();
      if (const auto lockout = login_guard::web_ui().record_failure(source, fingerprint, now); lockout.count() > 0) {
        BOOST_LOG(warning) << "Web UI: ["sv << address << "] -- "sv << login_guard::web_ui().failures(source) << " failed sign-ins, locked out for "sv << lockout.count() << " s"sv;
        send_locked_out(response, request, lockout);
      } else {
        send_unauthorized_impl(response, request, had_cookie);
      }
      return false;
    };

    if (rawAuth.size() <= "Basic "sv.length() || !boost::istarts_with(rawAuth, "Basic "sv)) {
      return fail();
    }
    auto authData = SimpleWeb::Crypto::Base64::decode(rawAuth.substr("Basic "sv.length()));

    const auto index = authData.find(':');
    if (index == std::string::npos || index + 1 >= authData.size()) {
      return fail();
    }

    const auto username = authData.substr(0, index);
    const auto password = authData.substr(index + 1);

    if (const auto hash = util::hex(crypto::hash(password + config::sunshine.salt)).to_string(); !boost::iequals(username, config::sunshine.username) || !web_session::equal_ct(hash, config::sunshine.password)) {
      return fail();
    }

    login_guard::web_ui().record_success(source);
    return true;
  }

  void send_locked_out(const resp_https_t &response, const req_https_t &request, const std::chrono::seconds wait) {
    const auto address = net::addr_to_normalized_string(request->remote_endpoint().address());
    BOOST_LOG(info) << "Web UI: ["sv << address << "] -- locked out for another "sv << wait.count() << " s"sv;

    const auto message = std::format("Too many failed sign-in attempts. Try again in {} seconds.", wait.count());
    const bool wants_json = request->path.starts_with("/api/"sv);
    nlohmann::json tree;
    tree["status_code"] = 429;
    tree["status"] = false;
    tree["error"] = message;
    tree["retry_after"] = wait.count();

    const SimpleWeb::CaseInsensitiveMultimap headers {
      {"Content-Type", wants_json ? "application/json" : "text/html; charset=utf-8"},
      {"Retry-After", std::to_string(wait.count())},
      {"Cache-Control", "no-store"},
      {"X-Frame-Options", "DENY"},
      {"Content-Security-Policy", "frame-ancestors 'none';"}
    };
    const auto body = wants_json ? tree.dump() : std::format("<!doctype html><meta charset=\"utf-8\"><title>Nova</title><p style=\"font:16px system-ui;margin:3rem\">{}</p>", message);
    response->write(SimpleWeb::StatusCode::client_error_too_many_requests, body, headers);
  }

  /**
   * @brief Send a 404 Not Found response.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * @param error_message The error message to include in the response.
   */
  void not_found(const resp_https_t &response, [[maybe_unused]] const req_https_t &request, const std::string &error_message) {
    constexpr auto code = SimpleWeb::StatusCode::client_error_not_found;

    nlohmann::json tree;
    tree["status_code"] = code;
    tree["error"] = error_message;

    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "application/json");
    headers.emplace("X-Frame-Options", "DENY");
    headers.emplace("Content-Security-Policy", "frame-ancestors 'none';");

    response->write(code, tree.dump(), headers);
  }

  /**
   * @brief Send a 400 Bad Request response.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * @param error_message The error message to include in the response.
   */
  void bad_request(const resp_https_t &response, [[maybe_unused]] const req_https_t &request, const std::string &error_message) {
    constexpr auto code = SimpleWeb::StatusCode::client_error_bad_request;

    nlohmann::json tree;
    tree["status_code"] = code;
    tree["status"] = false;
    tree["error"] = error_message;

    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "application/json");
    headers.emplace("X-Frame-Options", "DENY");
    headers.emplace("Content-Security-Policy", "frame-ancestors 'none';");

    response->write(code, tree.dump(), headers);
  }

  /**
   * @brief Validate the request content type and send a bad request when mismatched.
   */
  bool check_content_type(const resp_https_t &response, const req_https_t &request, const std::string_view &contentType) {
    const auto requestContentType = request->header.find("content-type");
    if (requestContentType == request->header.end()) {
      bad_request(response, request, "Content type not provided");
      return false;
    }
    // Extract the media type part before any parameters (e.g., charset)
    std::string actualContentType = requestContentType->second;
    if (const size_t semicolonPos = actualContentType.find(';'); semicolonPos != std::string::npos) {
      actualContentType = actualContentType.substr(0, semicolonPos);
    }

    // Trim whitespace and convert to lowercase for case-insensitive comparison
    boost::algorithm::trim(actualContentType);
    boost::algorithm::to_lower(actualContentType);

    std::string expectedContentType(contentType);
    boost::algorithm::to_lower(expectedContentType);

    if (actualContentType != expectedContentType) {
      bad_request(response, request, "Content type mismatch");
      return false;
    }
    return true;
  }

  /**
   * @brief Get a unique client identifier for CSRF token management.
   * @param request The HTTP request object.
   * @return A unique identifier based on username or IP address.
   */
  std::string get_client_id(const req_https_t &request) {
    // A signed-in browser: its session, so CSRF tokens are per session.
    if (const auto session = request_session(request)) {
      return "session:" + session->id;
    }

    // Try to use the authenticated username as client ID
    if (const auto auth = request->header.find("authorization"); !config::sunshine.username.empty() && auth != request->header.end()) {
      if (const auto &rawAuth = auth->second; rawAuth.rfind("Basic "sv, 0) == 0) {
        auto authData = SimpleWeb::Crypto::Base64::decode(rawAuth.substr("Basic "sv.length()));
        if (const auto index = static_cast<int>(authData.find(':')); index < authData.size() - 1) {
          return authData.substr(0, index);  // Return username
        }
      }
    }

    // Fall back to IP address if no username
    return net::addr_to_normalized_string(request->remote_endpoint().address());
  }

  /**
   * @brief Generate a new CSRF token for a client.
   * @param client_id A unique identifier for the client (e.g., session ID or username).
   * @return The generated CSRF token.
   */
  std::string generate_csrf_token(const std::string &client_id) {
    // Generate a cryptographically secure random token
    std::string token = crypto::rand_alphabet(CSRF_TOKEN_SIZE);

    std::scoped_lock lock(csrf_tokens_mutex);

    // Clean up expired tokens first
    const auto now = std::chrono::steady_clock::now();
    std::erase_if(csrf_tokens, [&now](const auto &entry) {
      return entry.second.expiration < now;
    });

    // Store the token with expiration
    csrf_tokens[client_id] = csrf_token_t {
      token,
      now + CSRF_TOKEN_LIFETIME
    };

    return token;
  }

  /**
   * @brief Validate a stored CSRF token for a client against a provided token string.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * @param client_id A unique identifier for the client.
   * @param provided_token The token string to validate.
   * @return True if the token is valid, false otherwise.
   */
  bool validate_stored_csrf_token(const resp_https_t &response, const req_https_t &request, const std::string_view client_id, const std::string_view provided_token) {
    std::scoped_lock lock(csrf_tokens_mutex);
    const auto token_it = csrf_tokens.find(client_id);

    if (token_it == csrf_tokens.end()) {
      auto address = net::addr_to_normalized_string(request->remote_endpoint().address());
      BOOST_LOG(error) << "Web UI: ["sv << address << "] -- CSRF token validation failed: no token found for client"sv;
      bad_request(response, request, "Invalid CSRF token");
      return false;
    }

    if (const auto now = std::chrono::steady_clock::now(); token_it->second.expiration < now) {
      csrf_tokens.erase(token_it);
      auto address = net::addr_to_normalized_string(request->remote_endpoint().address());
      BOOST_LOG(error) << "Web UI: ["sv << address << "] -- CSRF token validation failed: token expired"sv;
      bad_request(response, request, "CSRF token expired");
      return false;
    }

    if (token_it->second.token != provided_token) {
      auto address = net::addr_to_normalized_string(request->remote_endpoint().address());
      BOOST_LOG(error) << "Web UI: ["sv << address << "] -- CSRF token validation failed: token mismatch"sv;
      bad_request(response, request, "Invalid CSRF token");
      return false;
    }

    return true;
  }

  /**
   * @brief Validate CSRF token.
   */
  bool validate_csrf_token(const resp_https_t &response, const req_https_t &request, const std::string &client_id) {
    // Signed in with the session cookie: the stricter per-session check (a same-origin header
    // or this session's token is required, because the browser attaches the cookie by itself).
    if (const auto session = request_session(request)) {
      return session_csrf_ok(response, request, *session);
    }

    const auto origin = request_origin(request);
    // Same origin, or no Origin/Referer at all: a request without them can't be browser-initiated
    // CSRF against Basic credentials (curl and scripts never send them, and browsers don't send
    // Basic credentials here any more, see from_browser_engine()).
    if (origin != origin_e::cross) {
      return true;
    }

    // A browser-like request arrived with an Origin/Referer that doesn't match an allowed origin.
    // Require a CSRF token (header, or the query string as a fallback).
    const auto header_it = request->header.find("X-CSRF-Token");
    if (header_it == request->header.end()) {
      auto query_params = request->parse_query_string();
      const auto query_it = query_params.find("csrf_token");
      if (query_it == query_params.end()) {
        const auto origin_it = request->header.find("Origin");
        const auto referer_it = request->header.find("Referer");
        const std::string_view blocked_origin = origin_it != request->header.end() ? std::string_view {origin_it->second} : referer_it != request->header.end() ? std::string_view {referer_it->second} : ""sv;
        auto address = net::addr_to_normalized_string(request->remote_endpoint().address());
        BOOST_LOG(error) << "Web UI: ["sv << address << "] -- CSRF protection blocked request from origin: "sv << blocked_origin;
        BOOST_LOG(error) << "Web UI: To allow this origin, add it to the 'csrf_allowed_origins' option in your Sunshine configuration"sv;
        bad_request(response, request, "Missing CSRF token");
        return false;
      }

      return validate_stored_csrf_token(response, request, client_id, query_it->second);
    }

    // Validate token from header
    return validate_stored_csrf_token(response, request, client_id, header_it->second);
  }

  /**
   * @brief Validates the application index and sends an error response if invalid.
   */
  bool check_app_index(const resp_https_t &response, const req_https_t &request, int index) {
    std::string file = file_handler::read_file(config::stream.file_apps.c_str());
    nlohmann::json file_tree = nlohmann::json::parse(file);
    if (const auto &apps = file_tree["apps"]; index < 0 || index >= static_cast<int>(apps.size())) {
      std::string error;
      if (const int max_index = static_cast<int>(apps.size()) - 1; max_index < 0) {
        error = "No applications found";
      } else {
        error = std::format("'index' {} out of range, max index is {}", index, max_index);
      }
      bad_request(response, request, error);
      return false;
    }
    return true;
  }

  void getPage(const resp_https_t &response, const req_https_t &request, const bool require_auth, const bool redirect_if_username) {
    // Special handling for welcome page: redirect if the username is already set
    if (redirect_if_username && !config::sunshine.username.empty()) {
      send_redirect(response, request, "/");
      return;
    }

    if (require_auth && !authenticate(response, request)) {
      return;
    }

    print_req(request);

    const std::string content = file_handler::read_file(WEB_DIR "index.html");
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "text/html; charset=utf-8");

    // prevent click jacking
    headers.emplace("X-Frame-Options", "DENY");
    headers.emplace("Content-Security-Policy", "frame-ancestors 'none';");

    response->write(content, headers);
  }

  void getFallbackPage(const resp_https_t &response, const req_https_t &request) {
    const std::string_view path = request->path;
    const auto has_server_prefix = [path](const std::string_view prefix) {
      return path == prefix || (path.starts_with(prefix) && path.length() > prefix.length() && path[prefix.length()] == '/');
    };

    if (has_server_prefix("/api") || has_server_prefix("/assets") || has_server_prefix("/images")) {
      not_found(response, request);
      return;
    }

    getPage(response, request);
  }

  /**
   * @brief Get the favicon image.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * @todo combine function with getSunshineLogoImage and possibly getNodeModules
   * @todo use mime_types map
   */
  void getFaviconImage(const resp_https_t &response, const req_https_t &request) {
    print_req(request);

    std::ifstream in(WEB_DIR "images/sunshine.ico", std::ios::binary);
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "image/x-icon");
    headers.emplace("X-Frame-Options", "DENY");
    headers.emplace("Content-Security-Policy", "frame-ancestors 'none';");
    response->write(SimpleWeb::StatusCode::success_ok, in, headers);
  }

  /**
   * @brief Get the Sunshine logo image.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * @todo combine function with getFaviconImage and possibly getNodeModules
   * @todo use mime_types map
   */
  void getSunshineLogoImage(const resp_https_t &response, const req_https_t &request) {
    print_req(request);

    std::ifstream in(WEB_DIR "images/logo-sunshine-45.png", std::ios::binary);
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "image/png");
    headers.emplace("X-Frame-Options", "DENY");
    headers.emplace("Content-Security-Policy", "frame-ancestors 'none';");
    response->write(SimpleWeb::StatusCode::success_ok, in, headers);
  }

  /**
   * @brief Check if a path is a child of another path.
   * @param base The base path.
   * @param query The path to check.
   * @return True if the path is a child of the base path, false otherwise.
   */
  bool isChildPath(fs::path const &base, fs::path const &query) {
    auto relPath = fs::relative(base, query);
    return *(relPath.begin()) != fs::path("..");
  }

  /**
   * @brief Get an asset.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   */
  void getAsset(const resp_https_t &response, const req_https_t &request) {
    print_req(request);
    fs::path webDirPath(WEB_DIR);
    fs::path nodeModulesPath(webDirPath / "assets");

    // .relative_path is needed to shed any leading slash that might exist in the request path
    auto filePath = fs::weakly_canonical(webDirPath / fs::path(request->path).relative_path());

    // Don't do anything if the file does not exist or is outside the assets directory
    if (!isChildPath(filePath, nodeModulesPath)) {
      BOOST_LOG(warning) << "Someone requested a path " << filePath << " that is outside the assets folder";
      bad_request(response, request);
      return;
    }
    if (!fs::exists(filePath)) {
      not_found(response, request);
      return;
    }

    auto relPath = fs::relative(filePath, webDirPath);
    // get the mime type from the file extension mime_types map
    // remove the leading period from the extension
    auto mimeType = mime_types.find(relPath.extension().string().substr(1));
    // check if the extension is in the map at the x position
    if (mimeType == mime_types.end()) {
      bad_request(response, request);
      return;
    }

    // if it is, set the content type to the mime type
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", mimeType->second);
    headers.emplace("X-Frame-Options", "DENY");
    headers.emplace("Content-Security-Policy", "frame-ancestors 'none';");
    std::ifstream in(filePath.string(), std::ios::binary);
    response->write(SimpleWeb::StatusCode::success_ok, in, headers);
  }

  /**
   * @brief Get a CSRF token for the authenticated user.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/csrf-token|:| GET|:| null}
   */
  void getCSRFToken(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    nlohmann::json output_tree;
    if (const auto session = request_session(request)) {
      // Session-cookie requests are checked against their session's token.
      output_tree["csrf_token"] = session->csrf_token;
    } else {
      output_tree["csrf_token"] = generate_csrf_token(get_client_id(request));
    }
    send_response(response, output_tree);
  }

  namespace {
    /**
     * @brief Write JSON with the auth headers plus optional extras (Set-Cookie).
     */
    void send_auth_json(const resp_https_t &response, const SimpleWeb::StatusCode code, const nlohmann::json &tree, const std::vector<std::pair<std::string, std::string>> &extra = {}) {
      auto headers = auth_headers();
      if (!web_build_id().empty()) {
        headers.emplace("X-Nova-Build", web_build_id());
      }
      for (const auto &[name, value] : extra) {
        headers.emplace(name, value);
      }
      response->write(code, tree.dump(), headers);
    }

    /**
     * @brief Public description of a session (never the token or its hash).
     */
    nlohmann::json session_json(const web_session::session_t &s, const std::string_view current_id) {
      return {
        {"id", s.id},
        {"created", s.created},
        {"last_seen", s.last_seen},
        {"expires", s.expires},
        {"remember", s.remember},
        {"user_agent", s.user_agent},
        {"address", s.address},
        {"current", s.id == current_id},
      };
    }

    /**
     * @brief Sign this browser in: store a new session and build its Set-Cookie header.
     * @param request The HTTP request (User-Agent and address are recorded).
     * @param remember 30-day cookie instead of a browser-session cookie.
     * @param body Receives username, csrf_token, expires and remember.
     * @return The Set-Cookie header value.
     */
    std::string start_session(const req_https_t &request, const bool remember, nlohmann::json &body) {
      const auto ua_it = request->header.find("User-Agent");
      const auto address = net::addr_to_normalized_string(request->remote_endpoint().address());
      const auto created = web_session::web_ui().create(remember, current_cred_tag(), ua_it == request->header.end() ? ""sv : std::string_view {ua_it->second}, address, web_session::clock::now());
      body["username"] = config::sunshine.username;
      body["csrf_token"] = created.session.csrf_token;
      body["expires"] = created.session.expires;
      body["remember"] = remember;
      return web_session::set_cookie(created.token, remember ? std::optional {web_session::remembered_lifetime} : std::nullopt);
    }

    /**
     * @brief Refuse auth requests that a browser sent from another site (login CSRF).
     * @return True when the request may proceed.
     */
    bool auth_origin_ok(const resp_https_t &response, const req_https_t &request) {
      if (request_origin(request) == origin_e::cross) {
        auto address = net::addr_to_normalized_string(request->remote_endpoint().address());
        BOOST_LOG(error) << "Web UI: ["sv << address << "] -- cross-site "sv << request->path << " refused"sv;
        send_auth_json(response, SimpleWeb::StatusCode::client_error_forbidden, {{"status_code", 403}, {"status", false}, {"error", "Cross-site request refused"}});
        return false;
      }
      return true;
    }
  }  // namespace

  /**
   * @brief Sign in with the web UI username and password; sets the nova_session cookie.
   *
   * Wrong passwords count toward the same per-source lockout as HTTP Basic (429 + Retry-After).
   *
   * @api_examples{/api/auth/login|:| POST|:| {"username":"admin","password":"secret","remember":true}}
   */
  void postAuthLogin(const resp_https_t &response, const req_https_t &request) {
    if (!origin_allowed(response, request) || !auth_origin_ok(response, request) || !check_content_type(response, request, "application/json")) {
      return;
    }
    if (config::sunshine.username.empty()) {
      send_auth_json(response, SimpleWeb::StatusCode::client_error_conflict, {{"status_code", 409}, {"status", false}, {"error", "setup_required"}});
      return;
    }

    const auto address = net::addr_to_normalized_string(request->remote_endpoint().address());
    const auto source = login_guard::source_key(address);
    const auto now = login_guard::limiter_t::clock::now();
    if (const auto wait = login_guard::web_ui().locked_for(source, now); wait.count() > 0) {
      send_locked_out(response, request, wait);
      return;
    }

    std::string username;
    std::string password;
    const scoped_sensitive_string_clear_t clear_password {password};
    bool remember = false;
    try {
      const auto input = nlohmann::json::parse(request->content.string());
      username = input.at("username").get<std::string>();
      password = input.at("password").get<std::string>();
      if (const auto it = input.find("remember"); it != input.end() && it->is_boolean()) {
        remember = it->get<bool>();
      }
    } catch (const std::exception &) {
      bad_request(response, request, "Expected {\"username\": string, \"password\": string, \"remember\": boolean}");
      return;
    }

    const auto hash = util::hex(crypto::hash(password + config::sunshine.salt)).to_string();
    const bool ok = !username.empty() && !password.empty() && boost::iequals(username, config::sunshine.username) && web_session::equal_ct(hash, config::sunshine.password);
    if (!ok) {
      const auto fingerprint = util::hex(crypto::hash("login\n" + username + "\n" + password + config::sunshine.salt)).to_string();
      if (const auto lockout = login_guard::web_ui().record_failure(source, fingerprint, now); lockout.count() > 0) {
        BOOST_LOG(warning) << "Web UI: ["sv << address << "] -- "sv << login_guard::web_ui().failures(source) << " failed sign-ins, locked out for "sv << lockout.count() << " s"sv;
        send_locked_out(response, request, lockout);
      } else {
        BOOST_LOG(info) << "Web UI: ["sv << address << "] -- wrong username or password"sv;
        send_auth_json(response, SimpleWeb::StatusCode::client_error_unauthorized, {{"status_code", 401}, {"status", false}, {"error", "Wrong username or password"}});
      }
      return;
    }
    login_guard::web_ui().record_success(source);

    // Never reuse a session id the browser already had (session fixation).
    if (const auto old = session_cookie(request); !old.empty()) {
      web_session::web_ui().revoke_token(old);
    }

    nlohmann::json body {{"status", true}};
    const auto cookie = start_session(request, remember, body);
    BOOST_LOG(info) << "Web UI: ["sv << address << "] -- signed in"sv << (remember ? " (kept for 30 days)"sv : ""sv);
    send_auth_json(response, SimpleWeb::StatusCode::success_ok, body, {{"Set-Cookie", cookie}});
  }

  /**
   * @brief Sign this browser out: forget its session and delete the cookie.
   *
   * @api_examples{/api/auth/logout|:| POST|:| null}
   */
  void postAuthLogout(const resp_https_t &response, const req_https_t &request) {
    if (!origin_allowed(response, request) || !auth_origin_ok(response, request)) {
      return;
    }
    if (const auto token = session_cookie(request); !token.empty() && web_session::web_ui().revoke_token(token)) {
      const auto address = net::addr_to_normalized_string(request->remote_endpoint().address());
      BOOST_LOG(info) << "Web UI: ["sv << address << "] -- signed out"sv;
    }
    send_auth_json(response, SimpleWeb::StatusCode::success_ok, {{"status", true}}, {{"Set-Cookie", web_session::clear_cookie()}});
  }

  /**
   * @brief Whether this browser is signed in (never 401, so the sign-in page can ask).
   *
   * @api_examples{/api/auth/session|:| GET|:| null}
   */
  void getAuthSession(const resp_https_t &response, const req_https_t &request) {
    if (!origin_allowed(response, request)) {
      return;
    }
    bool had_cookie = false;
    const auto session = request_session(request, &had_cookie);
    nlohmann::json body {
      {"authenticated", session.has_value()},
      {"setup_required", config::sunshine.username.empty()},
    };
    if (session) {
      body["username"] = config::sunshine.username;
      body["csrf_token"] = session->csrf_token;
      body["expires"] = session->expires;
      body["remember"] = session->remember;
      body["id"] = session->id;
    }
    std::vector<std::pair<std::string, std::string>> extra;
    if (had_cookie && !session) {
      extra.emplace_back("Set-Cookie", web_session::clear_cookie());
    }
    send_auth_json(response, SimpleWeb::StatusCode::success_ok, body, extra);
  }

  /**
   * @brief List signed-in browsers.
   *
   * @api_examples{/api/auth/sessions|:| GET|:| null}
   */
  void getAuthSessions(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }
    const auto current = request_session(request);
    const auto current_id = current ? current->id : ""s;
    auto items = nlohmann::json::array();
    for (const auto &s : web_session::web_ui().list(web_session::clock::now())) {
      items.push_back(session_json(s, current_id));
    }
    send_auth_json(response, SimpleWeb::StatusCode::success_ok, {{"status", true}, {"sessions", std::move(items)}});
  }

  /**
   * @brief Sign out one browser ({"id": "..."}) or every other one ({"others": true}).
   *
   * @api_examples{/api/auth/sessions/revoke|:| POST|:| {"id":"0123456789abcdef01"}}
   */
  void postAuthSessionsRevoke(const resp_https_t &response, const req_https_t &request) {
    if (!check_content_type(response, request, "application/json") || !authenticate(response, request) || !validate_csrf_token(response, request, get_client_id(request))) {
      return;
    }
    const auto current = request_session(request);
    std::string id;
    bool others = false;
    try {
      const auto input = nlohmann::json::parse(request->content.string());
      id = input.value("id", ""s);
      others = input.value("others", false);
    } catch (const std::exception &) {
      bad_request(response, request, "Expected {\"id\": string} or {\"others\": true}");
      return;
    }

    auto &store = web_session::web_ui();
    int revoked = 0;
    if (others) {
      for (const auto &s : store.list(web_session::clock::now())) {
        if ((!current || s.id != current->id) && store.revoke_id(s.id)) {
          ++revoked;
        }
      }
    } else if (!id.empty()) {
      revoked = store.revoke_id(id) ? 1 : 0;
    } else {
      bad_request(response, request, "Expected {\"id\": string} or {\"others\": true}");
      return;
    }

    std::vector<std::pair<std::string, std::string>> extra;
    if (current && !others && id == current->id) {
      extra.emplace_back("Set-Cookie", web_session::clear_cookie());
    }
    send_auth_json(response, SimpleWeb::StatusCode::success_ok, {{"status", true}, {"revoked", revoked}}, extra);
  }

  /**
   * @brief Get the list of available applications.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * Besides the `apps` array, the reply carries `running_index` (position in `apps` of the running
   * application, or null) and `running_name` (its name, or null).
   *
   * @api_examples{/api/apps|:| GET|:| null}
   */
  void getApps(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    try {
      std::string content = file_handler::read_file(config::stream.file_apps.c_str());
      nlohmann::json file_tree = nlohmann::json::parse(content);

      // Legacy versions of Sunshine used strings for boolean and integers, let's convert them
      // List of keys to convert to boolean
      const std::vector<std::string> boolean_keys = {
        "exclude-global-prep-cmd",
        "elevated",
        "auto-detach",
        "wait-all"
      };

      // List of keys to convert to integers
      std::vector<std::string> integer_keys = {
        "exit-timeout"
      };

      // Walk fileTree and convert true/false strings to boolean or integer values
      for (auto &app : file_tree["apps"]) {
        for (const auto &key : boolean_keys) {
          if (app.contains(key) && app[key].is_string()) {
            app[key] = app[key] == "true";
          }
        }
        for (const auto &key : integer_keys) {
          if (app.contains(key) && app[key].is_string()) {
            app[key] = std::stoi(app[key].get<std::string>());
          }
        }
        if (app.contains("prep-cmd")) {
          for (auto &prep : app["prep-cmd"]) {
            if (prep.contains("elevated") && prep["elevated"].is_string()) {
              prep["elevated"] = prep["elevated"] == "true";
            }
          }
        }
      }

      // Which entry (if any) is running now, so the UI can show it and offer "Close".
      file_tree["running_index"] = nullptr;
      file_tree["running_name"] = nullptr;
      if (const auto running_id = proc::proc.running(); running_id > 0) {
        const auto &apps = proc::proc.get_apps();
        for (std::size_t i = 0; i < apps.size(); ++i) {
          if (apps[i].id == std::to_string(running_id)) {
            file_tree["running_index"] = i;
            break;
          }
        }
        file_tree["running_name"] = proc::proc.get_last_run_app_name();
      }

      send_response(response, file_tree);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "GetApps: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Save an application. To save a new application, the index must be `-1`. To update an existing application, you must provide the current index of the application.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * The body for the post request should be JSON serialized in the following format:
   * @code{.json}
   * {
   *   "name": "Application Name",
   *   "output": "Log Output Path",
   *   "cmd": "Command to run the application",
   *   "index": -1,
   *   "exclude-global-prep-cmd": false,
   *   "elevated": false,
   *   "auto-detach": true,
   *   "wait-all": true,
   *   "exit-timeout": 5,
   *   "prep-cmd": [
   *     {
   *       "do": "Command to prepare",
   *       "undo": "Command to undo preparation",
   *       "elevated": false
   *     }
   *   ],
   *   "detached": [
   *     "Detached command"
   *   ],
   *   "image-path": "Full path to the application image. Must be a png file."
   * }
   * @endcode
   *
   * @api_examples{/api/apps|:| POST|:| {"name":"Hello, World!","index":-1}}
   */
  void saveApp(const resp_https_t &response, const req_https_t &request) {
    if (!check_content_type(response, request, "application/json")) {
      return;
    }
    if (!authenticate(response, request)) {
      return;
    }

    std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    std::stringstream ss;
    ss << request->content.rdbuf();
    try {
      // TODO: Input Validation
      nlohmann::json output_tree;
      nlohmann::json input_tree = nlohmann::json::parse(ss);
      std::scoped_lock apps_lock(library::apps_file_mutex());
      std::string file = file_handler::read_file(config::stream.file_apps.c_str());
      nlohmann::json file_tree = nlohmann::json::parse(file);

      // Host commands the web UI edits for this app (Foundation's "menu-cmd" key).
      if (const auto it = input_tree.find("menu-cmd"); it != input_tree.end()) {
        std::vector<std::string> errors;
        auto normalized = host_commands::normalize(*it, &errors);
        if (!errors.empty()) {
          bad_request(response, request, "Host commands: " + errors.front());
          return;
        }
        if (normalized.empty()) {
          input_tree.erase("menu-cmd");
        } else {
          *it = std::move(normalized);
        }
      }

      // Nova: keep the performance profile in one clean "nova-perf" object (clamped, defaults
      // dropped, legacy "nova-compat" FSR/frame cap/MangoHud keys moved into it).
      if (input_tree.is_object() && (input_tree.contains("nova-perf") || input_tree.contains("nova-compat"))) {
        nova_perf::store(input_tree, nova_perf::for_app(input_tree));
      }

      if (input_tree["prep-cmd"].empty()) {
        input_tree.erase("prep-cmd");
      }

      if (input_tree["detached"].empty()) {
        input_tree.erase("detached");
      }

      auto &apps_node = file_tree["apps"];
      int index = input_tree["index"].get<int>();  // this will intentionally cause an exception if the provided value is the wrong type

      input_tree.erase("index");

      if (index == -1) {
        apps_node.push_back(input_tree);
      } else {
        nlohmann::json newApps = nlohmann::json::array();
        for (size_t i = 0; i < apps_node.size(); ++i) {
          if (i == index) {
            newApps.push_back(input_tree);
          } else {
            newApps.push_back(apps_node[i]);
          }
        }
        file_tree["apps"] = newApps;
      }

      // Sort the apps array by name
      std::sort(apps_node.begin(), apps_node.end(), [](const nlohmann::json &a, const nlohmann::json &b) {
        return a["name"].get<std::string>() < b["name"].get<std::string>();
      });

      file_handler::write_file(config::stream.file_apps.c_str(), file_tree.dump(4));
      proc::refresh(config::stream.file_apps);

      output_tree["status"] = true;
      send_response(response, output_tree);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "SaveApp: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief What runs on the host: the app, since when, on which display, and how many devices stream.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * Same body as the device API's GET /nova/v1/running: `running`, `app` ({id, index, appid, name}
   * or null), `since` (Unix seconds), `display` ("virtual" or "mirror"), `connected_clients`,
   * `idle_quit_hours`, `idle_quit_at` (Unix seconds, or null) and `tracked` (false when the app detached).
   *
   * @api_examples{/api/apps/running|:| GET|:| null}
   */
  void getRunningApp(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    send_response(response, app_lifecycle::running_json(app_lifecycle::current()));
  }

  /**
   * @brief Close the currently running application.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/apps/close|:| POST|:| null}
   */
  void closeApp(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    const app_lifecycle::busy_guard_t busy;  // the app watcher waits while the app is ended
    proc::proc.terminate();
    if (rtsp_stream::session_count() == 0) {
      display_follow::app_closed();
    }

    nlohmann::json output_tree;
    output_tree["status"] = true;
    send_response(response, output_tree);
  }

  /**
   * @brief Delete an application.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/apps/9999|:| DELETE|:| null}
   */
  void deleteApp(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    try {
      nlohmann::json output_tree;
      nlohmann::json new_apps = nlohmann::json::array();
      const int index = std::stoi(request->path_match[1]);

      if (!check_app_index(response, request, index)) {
        return;
      }

      std::scoped_lock apps_lock(library::apps_file_mutex());
      std::string file = file_handler::read_file(config::stream.file_apps.c_str());
      nlohmann::json file_tree = nlohmann::json::parse(file);
      auto &apps = file_tree["apps"];

      for (size_t i = 0; i < apps.size(); ++i) {
        if (i != index) {
          new_apps.push_back(apps[i]);
        }
      }
      file_tree["apps"] = new_apps;

      file_handler::write_file(config::stream.file_apps.c_str(), file_tree.dump(4));
      proc::refresh(config::stream.file_apps);

      output_tree["status"] = true;
      output_tree["result"] = std::format("application {} deleted", index);
      send_response(response, output_tree);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "DeleteApp: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Get the list of paired clients.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/clients/list|:| GET|:| null}
   */
  void getClients(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    const nlohmann::json named_certs = nvhttp::get_all_clients();

    nlohmann::json output_tree;
    output_tree["named_certs"] = named_certs;
    output_tree["status"] = true;
    send_response(response, output_tree);
  }

  // ---- Nova: live stream sessions and session history (/api/sessions*) ----

  /**
   * @brief Resolve a session's client certificate to its current UUID and name.
   * @param cert_pem PEM certificate recorded for the session.
   * @return Identity; empty strings when the client is no longer paired.
   */
  stream_stats::client_identity_t session_client_identity(const std::string &cert_pem) {
    stream_stats::client_identity_t who;
    if (auto id = nvhttp::get_client_identity(cert_pem)) {
      who.uuid = std::move(id->first);
      who.name = std::move(id->second);
    }
    return who;
  }

  /**
   * @brief Get live statistics for every active stream.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * Add `?samples=1` to include the last 120 one-second samples per session (for sparklines).
   * Latencies are host-side milliseconds: `capture` is capture timestamp to encoder pickup, `encode` is
   * colour conversion plus encoding, `send` is packetizing, FEC, encryption and pacing until the last
   * packet leaves the socket. `loss_pct` is null until the client reports loss. `abr` is present while the
   * device runs adaptive bitrate: `{"mode","min_kbps","max_kbps","current_kbps","last_reason","changes"}`.
   *
   * @api_examples{/api/sessions|:| GET|:| null}
   */
  void getSessions(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    const auto query = request->parse_query_string();
    const auto samples_it = query.find("samples");
    const bool with_samples = samples_it != query.end() && (samples_it->second == "1" || samples_it->second == "true");

    nlohmann::json sessions = nlohmann::json::array();
    for (const auto &snap : stream_stats::active_sessions(with_samples)) {
      auto entry = stream_stats::snapshot_to_api_json(snap, session_client_identity(snap.info.client_cert), with_samples);
      if (const auto abr_state = abr::status(snap.info.client_cert)) {
        entry["abr"] = abr::status_json(*abr_state);
      }
      sessions.push_back(std::move(entry));
    }

    nlohmann::json output_tree;
    output_tree["sessions"] = std::move(sessions);
    output_tree["status"] = true;
    send_response(response, output_tree);
  }

  /**
   * @brief Get the most recent ended streams (up to 50), newest first.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * `end_reason` is "client" (the device disconnected), "host" (stopped from Nova or the app exited),
   * "timeout" (the device stopped responding) or "ended".
   *
   * @api_examples{/api/sessions/history|:| GET|:| null}
   */
  void getSessionHistory(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    nlohmann::json sessions = nlohmann::json::array();
    for (const auto &entry : stream_stats::history()) {
      sessions.push_back(stream_stats::history_to_api_json(entry, session_client_identity(entry.client_cert)));
    }

    nlohmann::json output_tree;
    output_tree["sessions"] = std::move(sessions);
    output_tree["status"] = true;
    send_response(response, output_tree);
  }

  // ---- end Nova sessions ----

  /**
   * @brief Update a paired client: enable or disable it, rename it, or change its permissions.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * The body for the POST request should be JSON serialized in the following format; every
   * field except "uuid" is optional and only the fields present are changed:
   * @code{.json}
   * {
   *   "uuid": "<uuid>",
   *   "enabled": true,
   *   "name": "Living room TV",
   *   "permissions": {"preset": "play", "clipboard": false}
   * }
   * @endcode
   * All fields are validated before anything changes. Replies `{"status": true}` on success,
   * 404 when no client has that UUID and 400 for invalid input.
   *
   * @api_examples{/api/clients/update|:| POST|:| {"uuid":"<uuid>","enabled":true}}
   */
  void updateClient(resp_https_t response, req_https_t request) {
    if (!check_content_type(response, request, "application/json")) {
      return;
    }
    if (!authenticate(response, request)) {
      return;
    }
    std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    std::stringstream ss;
    ss << request->content.rdbuf();
    try {
      const nlohmann::json input_tree = nlohmann::json::parse(ss.str());
      if (!input_tree.is_object()) {
        bad_request(response, request, "Body must be a JSON object");
        return;
      }
      const std::string uuid = input_tree.value("uuid", "");
      const auto current_permissions = nvhttp::get_client_permissions_by_uuid(uuid);
      if (uuid.empty() || !current_permissions) {
        not_found(response, request, "Unknown device");
        return;
      }

      std::optional<bool> enabled;
      if (const auto it = input_tree.find("enabled"); it != input_tree.end()) {
        if (!it->is_boolean()) {
          bad_request(response, request, "enabled must be a boolean");
          return;
        }
        enabled = it->get<bool>();
      }

      std::optional<std::string> name;
      if (const auto it = input_tree.find("name"); it != input_tree.end()) {
        if (!it->is_string() || !nvhttp::is_valid_client_name(it->get<std::string>())) {
          bad_request(response, request, "name must be 1-64 characters without leading or trailing spaces or control characters");
          return;
        }
        name = it->get<std::string>();
      }

      std::optional<client_permissions::mask_t> permissions;
      if (const auto it = input_tree.find("permissions"); it != input_tree.end()) {
        try {
          permissions = client_permissions::apply_json(*current_permissions, *it);
        } catch (const std::invalid_argument &e) {
          bad_request(response, request, e.what());
          return;
        }
      }

      if (!enabled && !name && !permissions) {
        bad_request(response, request, "Nothing to update: send enabled, name or permissions");
        return;
      }

      bool status = true;
      if (name) {
        status = nvhttp::set_client_name(uuid, *name) && status;
      }
      if (permissions) {
        status = nvhttp::set_client_permissions(uuid, *permissions) && status;
      }
      if (enabled) {
        status = nvhttp::set_client_enabled(uuid, *enabled) && status;
        if (!*enabled && status) {
          auto cert = nvhttp::get_cert_by_uuid(uuid);
          if (!cert.empty()) {
            rtsp_stream::terminate_sessions_by_cert(cert);
          }

          const app_lifecycle::busy_guard_t busy;  // the app watcher waits while the app is ended
          if (rtsp_stream::session_count() == 0 && proc::proc.running() > 0) {
            proc::proc.terminate();
            display_follow::app_closed();  // a virtual display kept for it goes too
          }
        }
      }

      nlohmann::json output_tree;
      output_tree["status"] = status;
      send_response(response, output_tree);
    } catch (nlohmann::json::exception &e) {
      BOOST_LOG(warning) << "Update Client: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief End the active stream of one paired client, leaving its app running.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * The body for the POST request should be JSON serialized in the following format:
   * @code{.json}
   * {
   *   "uuid": "<uuid>"
   * }
   * @endcode
   * Replies `{"status": true}` when a stream was ended and 404 when the client is unknown or
   * not streaming.
   *
   * @api_examples{/api/clients/disconnect|:| POST|:| {"uuid":"<uuid>"}}
   */
  void disconnectClient(const resp_https_t &response, const req_https_t &request) {
    if (!check_content_type(response, request, "application/json")) {
      return;
    }
    if (!authenticate(response, request)) {
      return;
    }
    const std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    std::stringstream ss;
    ss << request->content.rdbuf();
    try {
      const nlohmann::json input_tree = nlohmann::json::parse(ss.str());
      const std::string uuid = input_tree.is_object() ? input_tree.value("uuid", "") : "";
      const auto cert = uuid.empty() ? std::string {} : nvhttp::get_cert_by_uuid(uuid);
      if (cert.empty()) {
        not_found(response, request, "Unknown device");
        return;
      }
      if (!rtsp_stream::has_session_for_cert(cert)) {
        not_found(response, request, "Device is not streaming");
        return;
      }

      BOOST_LOG(info) << "Ended the stream from device ["sv << nvhttp::get_client_name_by_uuid(uuid) << "] ("sv << uuid << ')';
      rtsp_stream::terminate_sessions_by_cert(cert);

      nlohmann::json output_tree;
      output_tree["status"] = true;
      send_response(response, output_tree);
    } catch (nlohmann::json::exception &e) {
      BOOST_LOG(warning) << "Disconnect Client: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Unpair a client.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * The body for the POST request should be JSON serialized in the following format:
   * @code{.json}
   * {
   *  "uuid": "<uuid>"
   * }
   * @endcode
   *
   * @api_examples{/api/unpair|:| POST|:| {"uuid":"1234"}}
   */
  void unpair(const resp_https_t &response, const req_https_t &request) {
    if (!check_content_type(response, request, "application/json")) {
      return;
    }
    if (!authenticate(response, request)) {
      return;
    }

    std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    std::stringstream ss;
    ss << request->content.rdbuf();

    try {
      // TODO: Input Validation
      nlohmann::json output_tree;
      const nlohmann::json input_tree = nlohmann::json::parse(ss);
      const std::string uuid = input_tree.value("uuid", "");
      const bool removed = nvhttp::unpair_client(uuid);
      output_tree["status"] = removed;

      if (removed && nvhttp::get_all_clients().empty()) {
        proc::proc.terminate();
      }

      send_response(response, output_tree);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "Unpair: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Unpair all clients.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/clients/unpair-all|:| POST|:| null}
   */
  void unpairAll(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    nvhttp::erase_all_clients();
    proc::proc.terminate();

    nlohmann::json output_tree;
    output_tree["status"] = true;
    send_response(response, output_tree);
  }

  /**
   * @brief Get the configuration settings.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/config|:| GET|:| null}
   */
  void getConfig(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    nlohmann::json output_tree;
    output_tree["status"] = true;
    output_tree["platform"] = SUNSHINE_PLATFORM;
    output_tree["version"] = PROJECT_VERSION;
    // The web UI greets the signed-in user by name; this endpoint is
    // authenticated, so the username is not exposed to anonymous callers.
    output_tree["username"] = config::sunshine.username;

    auto vars = config::parse_config(file_handler::read_file(config::sunshine.config_file.c_str()));

    for (auto &[name, value] : vars) {
      output_tree[name] = std::move(value);
    }
    // Secrets are write-only: report that one is stored without revealing it.
    mask_secret_config(output_tree);

    send_response(response, output_tree);
  }

  /**
   * @brief Get the locale setting. This endpoint does not require authentication.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/configLocale|:| GET|:| null}
   */
  void getLocale(const resp_https_t &response, const req_https_t &request) {
    // we need to return the locale whether authenticated or not

    print_req(request);

    nlohmann::json output_tree;
    output_tree["status"] = true;
    output_tree["locale"] = config::sunshine.locale;
    send_response(response, output_tree);
  }

  /**
   * @brief Save the configuration settings.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * The body for the POST request should be JSON serialized in the following format:
   * @code{.json}
   * {
   *   "key": "value"
   * }
   * @endcode
   *
   * @attention{It is recommended to ONLY save the config settings that differ from the default behavior.}
   *
   * @api_examples{/api/config|:| POST|:| {"key":"value"}}
   */
  void saveConfig(const resp_https_t &response, const req_https_t &request) {
    if (!check_content_type(response, request, "application/json")) {
      return;
    }
    if (!authenticate(response, request)) {
      return;
    }

    std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    std::stringstream ss;
    ss << request->content.rdbuf();
    try {
      // TODO: Input Validation
      std::stringstream config_stream;
      nlohmann::json output_tree;
      nlohmann::json input_tree = nlohmann::json::parse(ss);
      // The Web UI sends the placeholder back unchanged when the user didn't edit a secret.
      restore_secret_config(input_tree, config::parse_config(file_handler::read_file(config::sunshine.config_file.c_str())));
      if (const auto it = input_tree.find("host_commands"); it != input_tree.end() && !it->is_null()) {
        const auto list = it->is_string() ? nlohmann::json::parse(it->get<std::string>().empty() ? "[]"s : it->get<std::string>()) : *it;
        std::vector<std::string> errors;
        auto normalized = host_commands::normalize(list, &errors);
        if (!errors.empty()) {
          bad_request(response, request, "Host commands: " + errors.front());
          return;
        }
        *it = normalized.empty() ? nlohmann::json(nullptr) : nlohmann::json(normalized.dump());
      }
      for (const auto &[k, v] : input_tree.items()) {
        if (v.is_null() || (v.is_string() && v.get<std::string>().empty())) {
          continue;
        }

        // v.dump() will dump valid json, which we do not want for strings in the config, right now
        // we should migrate the config file to straight JSON and get rid of all this nonsense
        const auto value = v.is_string() ? v.get<std::string>() : v.dump();
        // One setting per line: a newline in a key or value would smuggle in another setting.
        if (k.empty() || k.find_first_of("=\r\n# ") != std::string::npos || value.find_first_of("\r\n") != std::string::npos) {
          bad_request(response, request, "Invalid setting: " + k);
          return;
        }
        config_stream << k << " = " << value << std::endl;
      }
      file_handler::write_file(config::sunshine.config_file.c_str(), config_stream.str());
      output_tree["status"] = true;
      send_response(response, output_tree);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "SaveConfig: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Get an application's image.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @note{The index in the url path is the application index.}
   *
   * @api_examples{/api/covers/9999 |:| GET|:| null}
   */
  void getCover(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    try {
      const int index = std::stoi(request->path_match[1]);
      if (!check_app_index(response, request, index)) {
        return;
      }

      std::string file = file_handler::read_file(config::stream.file_apps.c_str());
      nlohmann::json file_tree = nlohmann::json::parse(file);
      auto &apps = file_tree["apps"];

      auto &app = apps[index];

      // Get the image path from the app configuration
      std::string app_image_path;
      if (app.contains("image-path") && !app["image-path"].is_null()) {
        app_image_path = app["image-path"];
      }

      // Use validate_app_image_path to resolve and validate the path
      // This handles extension validation, PNG signature validation, and path resolution
      std::string validated_path = proc::validate_app_image_path(app_image_path);

      // Check if we got the default image path (means validation failed or no image configured)
      if (validated_path == DEFAULT_APP_IMAGE_PATH) {
        BOOST_LOG(debug) << "Application at index " << index << " does not have a valid cover image";
        not_found(response, request, "Cover image not found");
        return;
      }

      // Open and stream the validated file
      std::ifstream in(validated_path, std::ios::binary);
      if (!in) {
        BOOST_LOG(warning) << "Unable to read cover image file: " << validated_path;
        bad_request(response, request, "Unable to read cover image file");
        return;
      }

      SimpleWeb::CaseInsensitiveMultimap headers;
      headers.emplace("Content-Type", "image/png");
      headers.emplace("X-Frame-Options", "DENY");
      headers.emplace("Content-Security-Policy", "frame-ancestors 'none';");

      response->write(SimpleWeb::StatusCode::success_ok, in, headers);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "GetCover: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Upload a cover image.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * The body for the post request should be JSON serialized in the following format:
   * @code{.json}
   * {
   *   "key": "igdb_<game_id>",
   *   "url": "https://images.igdb.com/igdb/image/upload/t_cover_big_2x/<slug>.png"
   * }
   * @endcode
   *
   * @api_examples{/api/covers/upload|:| POST|:| {"key":"igdb_1234","url":"https://images.igdb.com/igdb/image/upload/t_cover_big_2x/abc123.png"}}
   */
  void uploadCover(const resp_https_t &response, const req_https_t &request) {
    if (!check_content_type(response, request, "application/json")) {
      return;
    }
    if (!authenticate(response, request)) {
      return;
    }
    if (!validate_csrf_token(response, request, get_client_id(request))) {
      return;
    }

    std::stringstream ss;
    ss << request->content.rdbuf();
    try {
      nlohmann::json output_tree;
      nlohmann::json input_tree = nlohmann::json::parse(ss);

      std::string key = input_tree.value("key", "");
      if (key.empty()) {
        bad_request(response, request, "Cover key is required");
        return;
      }
      std::string url = input_tree.value("url", "");

      const std::string coverdir = platf::appdata().string() + "/covers/";
      file_handler::make_directory(coverdir);

      std::basic_string path = coverdir + http::url_escape(key) + ".png";
      if (!url.empty()) {
        if (http::url_get_host(url) != "images.igdb.com") {
          bad_request(response, request, "Only images.igdb.com is allowed");
          return;
        }
        if (!http::download_file(url, path)) {
          bad_request(response, request, "Failed to download cover");
          return;
        }
      } else {
        auto data = SimpleWeb::Crypto::Base64::decode(input_tree.value("data", ""));

        std::ofstream imgfile(path);
        imgfile.write(data.data(), static_cast<int>(data.size()));
      }
      output_tree["status"] = true;
      output_tree["path"] = path;
      send_response(response, output_tree);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "UploadCover: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Get the logs from the log file.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/logs|:| GET|:| null}
   */
  void getLogs(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    std::string content = file_handler::read_file(config::sunshine.log_file.c_str());
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "text/plain");
    headers.emplace("X-Frame-Options", "DENY");
    headers.emplace("Content-Security-Policy", "frame-ancestors 'none';");
    response->write(SimpleWeb::StatusCode::success_ok, content, headers);
  }

  /**
   * @brief Update existing credentials.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * The body for the post request should be JSON serialized in the following format:
   * @code{.json}
   * {
   *   "currentUsername": "Current Username",
   *   "currentPassword": "Current Password",
   *   "newUsername": "New Username",
   *   "newPassword": "New Password",
   *   "confirmNewPassword": "Confirm New Password"
   * }
   * @endcode
   *
   * @api_examples{/api/password|:| POST|:| {"currentUsername":"admin","currentPassword":"admin","newUsername":"admin","newPassword":"admin","confirmNewPassword":"admin"}}
   */
  void savePassword(const resp_https_t &response, const req_https_t &request) {
    if (!check_content_type(response, request, "application/json")) {
      return;
    }
    // Before the first account exists there is nothing to authenticate against, but the
    // origin policy still applies: otherwise anyone who can reach the port claims the host.
    if (config::sunshine.username.empty() ? !origin_allowed(response, request) : !authenticate(response, request)) {
      return;
    }

    std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    std::vector<std::string> errors = {};
    std::stringstream ss;
    std::stringstream config_stream;
    std::string session_cookie_header;
    ss << request->content.rdbuf();
    try {
      // TODO: Input Validation
      nlohmann::json output_tree;
      nlohmann::json input_tree = nlohmann::json::parse(ss);
      std::string username = input_tree.value("currentUsername", "");
      std::string newUsername = input_tree.value("newUsername", "");
      std::string password = input_tree.value("currentPassword", "");
      std::string newPassword = input_tree.value("newPassword", "");
      std::string confirmPassword = input_tree.value("confirmNewPassword", "");
      if (newUsername.empty()) {
        newUsername = username;
      }
      if (newUsername.empty()) {
        errors.emplace_back("Invalid Username");
      } else {
        auto hash = util::hex(crypto::hash(password + config::sunshine.salt)).to_string();
        if (config::sunshine.username.empty() || (boost::iequals(username, config::sunshine.username) && web_session::equal_ct(hash, config::sunshine.password))) {
          if (newPassword.empty() || newPassword != confirmPassword) {
            errors.emplace_back("Password Mismatch");
          } else {
            const auto previous = request_session(request);
            http::save_user_creds(config::sunshine.credentials_file, newUsername, newPassword);
            http::reload_user_creds(config::sunshine.credentials_file);
            // New credentials sign every browser out; this one gets a fresh session so the
            // owner isn't sent to the sign-in page right after choosing the password.
            web_session::web_ui().revoke_all();
            output_tree["status"] = true;
            if (from_browser(request)) {
              session_cookie_header = start_session(request, previous && previous->remember, output_tree);
            }
          }
        } else {
          errors.emplace_back("Invalid Current Credentials");
        }
      }

      if (!errors.empty()) {
        // join the errors array
        std::string error = std::accumulate(errors.begin(), errors.end(), std::string(), [](const std::string &a, const std::string &b) {
          return a.empty() ? b : a + ", " + b;
        });
        bad_request(response, request, error);
        return;
      }

      if (session_cookie_header.empty()) {
        send_response(response, output_tree);
      } else {
        send_auth_json(response, SimpleWeb::StatusCode::success_ok, output_tree, {{"Set-Cookie", session_cookie_header}});
      }
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "SavePassword: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief List client pairing requests that are waiting for PIN approval.
   *
   * @api_examples{/api/pin|:| GET|:| null}
   */
  void getPendingPairings(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    nlohmann::json output_tree;
    output_tree["pairings"] = nlohmann::json::array();
    for (const auto &pairing : nvhttp::get_pending_pairings()) {
      output_tree["pairings"].push_back({
        {"id", pairing.id},
        {"name", pairing.name},
        {"address", pairing.address},
        {"app", pairing.app},
        {"form", pairing.form},
        {"suggested_name", pairing.suggested_name},
      });
    }
    send_response(response, output_tree);
  }

  /**
   * @brief Cancel a client pairing request that is waiting for PIN approval.
   * The body for the delete request should be JSON serialized in the following format:
   * @code{.json}
   * {
   *   "pairing_id": "<pairing_id>"
   * }
   * @endcode
   *
   * @api_examples{/api/pin|:| DELETE|:| {"pairing_id":"0123456789abcdef0123456789abcdef"}}
   */
  void cancelPairing(const resp_https_t &response, const req_https_t &request) {
    if (!check_content_type(response, request, "application/json")) {
      return;
    }
    if (!authenticate(response, request)) {
      return;
    }

    const std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    std::stringstream ss;
    ss << request->content.rdbuf();
    try {
      const nlohmann::json input_tree = nlohmann::json::parse(ss);
      const std::string pairing_id = input_tree.value("pairing_id", "");
      if (!nvhttp::is_valid_pairing_id(pairing_id)) {
        bad_request(response, request, "pairing_id must contain exactly 32 hexadecimal characters");
        return;
      }

      nlohmann::json output_tree;
      output_tree["status"] = nvhttp::cancel_pairing(pairing_id);
      send_response(response, output_tree);
    } catch (nlohmann::json::exception &e) {
      BOOST_LOG(warning) << "CancelPairing: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Submit a PIN and return whether the selected client completes pairing.
   *
   * The request remains open for up to the configured `ping_timeout` while
   * Moonlight completes the cryptographic handshake. A wrong PIN, protocol
   * failure, cancellation, or timeout returns `{"status":false}`.
   * The body for the post request should be JSON serialized in the following format:
   * @code{.json}
   * {
   *   "pairing_id": "<pairing_id>",
   *   "pin": "<pin>",
   *   "name": "Friendly Client Name"
   * }
   * @endcode
   *
   * @api_examples{/api/pin|:| POST|:| {"pairing_id":"0123456789abcdef0123456789abcdef","pin":"1234","name":"My PC"}}
   */
  void savePin(const resp_https_t &response, const req_https_t &request) {
    if (!check_content_type(response, request, "application/json")) {
      return;
    }
    if (!authenticate(response, request)) {
      return;
    }

    std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    std::stringstream ss;
    ss << request->content.rdbuf();
    try {
      nlohmann::json output_tree;
      nlohmann::json input_tree = nlohmann::json::parse(ss);
      const std::string name = input_tree.value("name", "");
      const std::string pairing_id = input_tree.value("pairing_id", "");
      const std::string pin = input_tree.value("pin", "");
      if (!nvhttp::is_valid_pairing_id(pairing_id)) {
        bad_request(response, request, "pairing_id must contain exactly 32 hexadecimal characters");
        return;
      }
      if (!nvhttp::is_valid_pairing_pin(pin)) {
        bad_request(response, request, "PIN must contain exactly 4 numeric digits");
        return;
      }
      if (!nvhttp::is_valid_pairing_name(name)) {
        bad_request(response, request, "Client name must contain between 1 and 128 bytes");
        return;
      }

      output_tree["status"] = nvhttp::pin(pairing_id, pin, name);
      send_response(response, output_tree);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "SavePin: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Send a host file to the connected client(s) as a file-transfer
   *        offer (clipboard kind=4). The client fetches the bytes over the
   *        paired HTTPS connection.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/clipboard/send-file| POST| {"path":"/home/me/photo.jpg"}}
   */
  void sendFileToClient(const resp_https_t &response, const req_https_t &request) {
    if (!check_content_type(response, request, "application/json")) {
      return;
    }
    if (!authenticate(response, request)) {
      return;
    }

    std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    std::stringstream ss;
    ss << request->content.rdbuf();
    try {
      nlohmann::json input_tree = nlohmann::json::parse(ss);
      const std::string path = input_tree.value("path", "");

      auto offer = clipboard::offer_file(path);
      if (offer.empty()) {
        bad_request(response, request, "path is not a readable file");
        return;
      }
      nlohmann::json output_tree = nlohmann::json::parse(offer);
      output_tree["status"] = true;
      send_response(response, output_tree);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "SendFileToClient: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Reset the display device persistence.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/reset-display-device-persistence|:| POST|:| null}
   */
  void resetDisplayDevicePersistence(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    nlohmann::json output_tree;
    output_tree["status"] = display_device::reset_persistence();
    send_response(response, output_tree);
  }

  /**
   * @brief Authenticate a Web UI request and delete the saved XDG Portal restore token.
   * @details On platforms without XDG Portal capture, this operation succeeds without changing the filesystem.
   *
   * @param response HTTP response used for authentication, CSRF, and status output.
   * @param request HTTP request carrying the client identity and CSRF token.
   *
   * @api_examples{/api/reset-portal-token|:| POST|:| null}
   */
  void resetPortalToken(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    bool status = true;
#if defined(linux) || defined(__FreeBSD__)
    std::error_code ec;
    fs::remove(portal_token_path_provider()(), ec);
    if (ec) {
      BOOST_LOG(error) << "Failed to delete XDG Portal restore token: "sv << ec.message();
      status = false;
    }
#endif

    nlohmann::json output_tree;
    output_tree["status"] = status;
    send_response(response, output_tree);
  }

  /**
   * @brief Authenticate a Web UI request and restart the Sunshine process.
   *
   * @param response HTTP response used for authentication or CSRF failures.
   * @param request HTTP request carrying the client identity and CSRF token.
   *
   * @api_examples{/api/restart|:| POST|:| null}
   */
  void restart(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    // We may not return from this call
    platf::restart();
  }

  /**
   * @brief Build libvirtualhid driver version and installation status.
   *
   * @return libvirtualhid driver status JSON.
   */
  nlohmann::json get_virtualhid_driver_status() {
#ifdef _WIN32
    const auto version_str = read_libvirtualhid_driver_version();
    const auto driver_detected = !version_str.empty();
    auto output_tree = build_driver_status(driver_detected, version_str, libvirtualhid_minimum_version);
    bool requires_installed_driver = true;
    std::string backend_name;
    std::string runtime_error_message;

    try {
      const auto runtime = platf::virtualhid::create_runtime();
      if (runtime) {
        const auto &capabilities = runtime->capabilities();
        backend_name = capabilities.backend_name;
        requires_installed_driver = capabilities.requires_installed_driver;
        output_tree = build_driver_status(driver_detected || capabilities.supports_gamepad, version_str, libvirtualhid_minimum_version);
      }
    } catch (const std::bad_alloc &exception) {
      runtime_error_message = exception.what();
    }

    output_tree["backend_name"] = backend_name;
    output_tree["requires_installed_driver"] = requires_installed_driver;
    if (!runtime_error_message.empty()) {
      output_tree["error"] = runtime_error_message;
    }
#else
    auto output_tree = build_driver_status(false, "", libvirtualhid_minimum_version);
    output_tree["error"] = "libvirtualhid driver status is only available on Windows";
    output_tree["backend_name"] = "";
    output_tree["requires_installed_driver"] = false;
#endif

    return output_tree;
  }

  /**
   * @brief Build ViGEmBus fallback driver version and installation status.
   *
   * @return ViGEmBus fallback driver status JSON.
   */
  nlohmann::json get_vigembus_driver_status() {
#ifdef _WIN32
    std::string version_str;

    // Check if ViGEmBus driver exists
    std::string system_root;
    if (!lizardbyte::common::get_env("SystemRoot", system_root)) {
      system_root = "C:\\Windows";
    }
    const std::filesystem::path driver_path = std::filesystem::path(system_root) / "System32" / "drivers" / "ViGEmBus.sys";
    const auto installed = std::filesystem::exists(driver_path);
    if (installed) {
      platf::getFileVersionInfo(driver_path, version_str);
    }

    auto output_tree = build_driver_status(installed, version_str, VIGEMBUS_MINIMUM_VERSION);
#else
    auto output_tree = build_driver_status(false, "", VIGEMBUS_MINIMUM_VERSION);
    output_tree["error"] = "ViGEmBus is only available on Windows";
#endif

    return output_tree;
  }

  /**
   * @brief Get virtual input driver version and installation status.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/virtual-input/status|:| GET|:| null}
   */
  void getVirtualInputStatus(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    nlohmann::json output_tree;
    output_tree["virtualhid"] = get_virtualhid_driver_status();
    output_tree["vigembus"] = get_vigembus_driver_status();
    send_response(response, output_tree);
  }

  /**
   * @brief Get the current libvirtualhid machine license status.
   *
   * @param response HTTP response object.
   * @param request Authenticated HTTP request.
   *
   * @api_examples{/api/virtual-input/license|:| GET|:| null}
   */
  void getVirtualInputLicense(const resp_https_t &response, const req_https_t &request) {
    get_virtual_input_license(response, request);
  }

  /**
   * @brief Activate, validate, or deactivate the libvirtualhid machine license.
   *
   * Submitted license keys are used only for the synchronous broker call. They are
   * never logged or saved in Sunshine's configuration, and extracted mutable copies
   * are overwritten before the handler returns.
   *
   * @param response HTTP response object.
   * @param request Authenticated HTTP request with a JSON action.
   *
   * @api_examples{/api/virtual-input/license|:| POST|:| {"action":"validate"}}
   */
  void updateVirtualInputLicense(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    const auto client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);
    try {
      std::stringstream content;
      content << request->content.rdbuf();
      auto input_tree = nlohmann::json::parse(content);
      const auto action = input_tree.value("action", "");

      lvh::LicenseResult result;
      if (action == "activate") {
        auto license_key = input_tree.value("license_key", "");
        input_tree["license_key"] = "";
        if (license_key.empty()) {
          bad_request(response, request, "License key is required");
          return;
        }

        const scoped_sensitive_string_clear_t clear_license_key {license_key};
        result = lvh::activate_license(license_key);
      } else if (action == "validate") {
        result = lvh::validate_license();
      } else if (action == "deactivate") {
        result = lvh::deactivate_license();
      } else {
        bad_request(response, request, "Unknown license action");
        return;
      }

#ifdef _WIN32
      config::select_all_gamepad_drivers_if_licensed(result.license.licensed());
#endif
#if defined(_WIN32) && defined(SUNSHINE_TRAY) && SUNSHINE_TRAY >= 1
      system_tray::update_tray_virtualhid_license(result.license, false);
#endif
#ifdef _WIN32
      if (result.status.ok()) {
        input::refresh_virtual_input();
      }
#endif
      send_response(response, build_virtualhid_license_status(result));
    } catch (const nlohmann::json::exception &) {
      bad_request(response, request, "Invalid license request");
    }
  }

  /**
   * @brief Checks whether a directory entry qualifies as an executable file.
   * @param entry The directory entry to check.
   * @param status The cached file status for the entry.
   * @return True if the file should be included in an executable-type listing.
   */
  bool is_browsable_executable([[maybe_unused]] const fs::directory_entry &entry, [[maybe_unused]] const fs::file_status &status) {
#ifdef _WIN32
    auto ext = entry.path().extension().string();
    boost::algorithm::to_lower(ext);
    return ext == ".exe" || ext == ".bat" || ext == ".cmd" || ext == ".com" || ext == ".ps1";
#else
    const auto perms = status.permissions();
    return (perms & fs::perms::owner_exec) != fs::perms::none ||
           (perms & fs::perms::group_exec) != fs::perms::none ||
           (perms & fs::perms::others_exec) != fs::perms::none;
#endif
  }

#ifdef _WIN32
  /**
   * @brief Builds a JSON array of available Windows drive letters.
   * @return JSON array of drive-letter entries.
   */
  nlohmann::json get_windows_drives() {
    nlohmann::json entries = nlohmann::json::array();
    const DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
      if (drives & (1 << i)) {
        const auto drive_letter = static_cast<char>('A' + i);
        const auto drive_path = std::string(1, drive_letter) + ":\\";
        nlohmann::json entry;
        entry["name"] = drive_path;
        entry["type"] = "directory";
        entry["path"] = drive_path;
        entries.push_back(entry);
      }
    }
    return entries;
  }
#endif

  /**
   * @brief Lists, filters, and sorts the entries of a directory for the browse API.
   * @param dir_path The directory to list.
   * @param type_str Filter type: "directory", "executable", "file", or "any".
   * @return Sorted JSON array of entry objects with name/type/path fields.
   */
  nlohmann::json build_browse_entries(const fs::path &dir_path, const std::string &type_str) {
    nlohmann::json entries = nlohmann::json::array();

    std::error_code iter_ec;
    for (auto it = fs::directory_iterator(dir_path, fs::directory_options::skip_permission_denied, iter_ec);
         !iter_ec && it != fs::directory_iterator();
         it.increment(iter_ec)) {
      try {
        const auto status = it->status();
        const bool is_dir = fs::is_directory(status);

        if (const bool is_regular = fs::is_regular_file(status); !is_dir && !is_regular) {
          continue;
        }

        // Apply type filter (directories are always included for navigation)
        if (type_str == "directory" && !is_dir) {
          continue;
        }

        if (type_str == "executable" && !is_dir && !is_browsable_executable(*it, status)) {
          continue;
        }

        nlohmann::json file_entry;
        file_entry["name"] = it->path().filename().string();
        file_entry["path"] = it->path().string();
        file_entry["type"] = is_dir ? "directory" : "file";
        entries.push_back(file_entry);
      } catch (const fs::filesystem_error &e) {
        BOOST_LOG(debug) << "BrowseDirectory: skipping entry due to error: "sv << e.what();
      }
    }

    if (iter_ec) {
      BOOST_LOG(debug) << "BrowseDirectory: directory iteration error: "sv << iter_ec.message();
    }

    // Sort: directories first, then files; both case-insensitively alphabetical
    std::sort(entries.begin(), entries.end(), [](const nlohmann::json &a, const nlohmann::json &b) {
      const bool a_dir = (a["type"] == "directory");
      if (const bool b_dir = (b["type"] == "directory"); a_dir != b_dir) {
        return a_dir && !b_dir;
      }
      auto a_name = a["name"].get<std::string>();
      auto b_name = b["name"].get<std::string>();
      boost::algorithm::to_lower(a_name);
      boost::algorithm::to_lower(b_name);
      return a_name < b_name;
    });

    return entries;
  }

  /**
   * @brief Browse the server filesystem.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * @note On Windows, an empty or root path returns the list of available drive letters.
   * @note On non-Windows, an empty path defaults to the filesystem root ("/").
   *
   * @api_examples{/api/browse?path=/home/user&type=directory|:| GET|:| null}
   */
  void browseDirectory(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    try {
      const auto query_params = request->parse_query_string();

      std::string path_str;
      if (const auto path_it = query_params.find("path"); path_it != query_params.end()) {
        path_str = path_it->second;
      }

      std::string type_str = "any";
      if (const auto type_it = query_params.find("type"); type_it != query_params.end() && !type_it->second.empty()) {
        type_str = type_it->second;
      }

      nlohmann::json output_tree;

#ifdef _WIN32
      // On Windows with an empty or root path, return the list of available drive letters
      if (path_str.empty() || path_str == "/" || path_str == "\\") {
        output_tree["path"] = "";
        output_tree["parent"] = "";
        output_tree["entries"] = get_windows_drives();
        send_response(response, output_tree);
        return;
      }
#else
      // On non-Windows, default an empty path to the filesystem root
      if (path_str.empty()) {
        path_str = "/";
      }
#endif

      // Normalize the path
      fs::path dir_path = fs::weakly_canonical(fs::path(path_str));

      // If the path points to a file, use its parent directory
      std::error_code ec;
      if (fs::is_regular_file(dir_path, ec)) {
        dir_path = dir_path.parent_path();
      }

      // If the path doesn't exist, try the parent
      if (!fs::exists(dir_path, ec)) {
        dir_path = dir_path.parent_path();
      }

      if (!fs::is_directory(dir_path, ec)) {
        bad_request(response, request, "Path is not a directory");
        return;
      }

      output_tree["path"] = dir_path.string();

      // Determine the parent path for the "Up" navigation
      const fs::path parent = dir_path.parent_path();
#ifdef _WIN32
      // At a drive root (e.g., C:\) the parent equals itself; signal the drive list with an empty string
      output_tree["parent"] = (parent == dir_path) ? "" : parent.string();
#else
      output_tree["parent"] = parent.string();
#endif

      output_tree["entries"] = build_browse_entries(dir_path, type_str);
      send_response(response, output_tree);
    } catch (const fs::filesystem_error &e) {
      BOOST_LOG(warning) << "BrowseDirectory: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  // ---------------------------------------------------------------------------
  // Nova host facts: /api/host/info, /api/displays, /api/audio/sinks, /api/preview,
  // /api/health. Kept together so the sessions/telemetry routes can live apart.
  // ---------------------------------------------------------------------------

  /**
   * @brief Describe the host: name, versions, GPUs, encoder and capture method.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/host/info|:| GET|:| null}
   */
  void getHostInfo(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }
    print_req(request);
    send_response(response, host_info::host_info_json());
  }

  /**
   * @brief List the display outputs Nova can capture.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * Each entry has `name`, `index` (legacy numeric `output_name`), `connected`, `primary`,
   * `x`/`y`, `width`/`height` (current scanout), `mode_width`/`mode_height`, `refresh_hz`
   * and `configured`.
   *
   * @api_examples{/api/displays|:| GET|:| null}
   */
  void getDisplays(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }
    print_req(request);
    nlohmann::json output_tree;
    output_tree["displays"] = host_info::displays_json(host_info::cached_outputs());
    output_tree["status"] = true;
    send_response(response, output_tree);
  }

  /**
   * @brief List the sound server's output devices.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * Nova's own per-stream sinks are marked `virtual`; `configured` echoes `audio_sink`.
   *
   * @api_examples{/api/audio/sinks|:| GET|:| null}
   */
  void getAudioSinks(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }
    print_req(request);
    std::vector<platf::sink_desc_t> sinks;
    if (auto control = platf::audio_control()) {
      sinks = control->list_sinks();
    }
    auto output_tree = host_info::audio_sinks_json(sinks, config::audio.sink);
    output_tree["status"] = true;
    send_response(response, output_tree);
  }

  /**
   * @brief Return a JPEG snapshot of a display.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * Query: `display` (output name, optional) and `w` (width, 160–1280, default 640).
   * Replies 429 when more than two captures are requested per second and 503 when the
   * desktop can't be captured without prompting. Never cached by the browser.
   *
   * @api_examples{/api/preview?display=HDMI-0&w=640|:| GET|:| null}
   */
  void getPreview(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    std::string display;
    int width = 640;
    for (const auto &[name, value] : request->parse_query_string()) {
      if (name == "display") {
        display = value;
      } else if (name == "w") {
        std::from_chars(value.data(), value.data() + value.size(), width);
      }
    }
    if (display.size() > 64 || !std::ranges::all_of(display, [](char c) {
          return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == ':';
        })) {
      bad_request(response, request, "Invalid display name");
      return;
    }

    const auto result = host_info::preview_jpeg(display, width);
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("X-Frame-Options", "DENY");
    headers.emplace("Content-Security-Policy", "frame-ancestors 'none';");
    headers.emplace("Cache-Control", "no-store");
    if (result.jpeg.empty()) {
      nlohmann::json tree;
      tree["status"] = false;
      tree["status_code"] = result.http_status;
      tree["error"] = result.error;
      headers.emplace("Content-Type", "application/json");
      response->write(static_cast<SimpleWeb::StatusCode>(result.http_status), tree.dump(), headers);
      return;
    }
    headers.emplace("Content-Type", "image/jpeg");
    response->write(SimpleWeb::StatusCode::success_ok, result.jpeg, headers);
  }

  /**
   * @brief Run the setup-doctor checks.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * Each check is `{id, status: ok|warn|error, title, detail, fix: null|{kind, value}}`
   * with `kind` one of `command`, `setting` or `doc`. Results are cached for ten seconds.
   *
   * @api_examples{/api/health|:| GET|:| null}
   */
  void getHealth(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }
    print_req(request);
    nlohmann::json output_tree;
    output_tree["checks"] = host_info::health_to_json(host_info::cached_health());
    output_tree["status"] = true;
    send_response(response, output_tree);
  }


  // ---------------------------------------------------------------------------
  // Nova game library: scan folders and launchers, match titles, import with artwork.
  // ---------------------------------------------------------------------------

  /**
   * @brief Send an image file with the right content type.
   *
   * @param response The HTTP response object.
   * @param path Image file (PNG or JPEG).
   */
  void send_image_file(const resp_https_t &response, const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    auto ext = path.extension().string();
    boost::to_lower(ext);
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", ext == ".png" ? "image/png" : "image/jpeg");
    headers.emplace("Cache-Control", "private, max-age=300");
    headers.emplace("X-Content-Type-Options", "nosniff");
    headers.emplace("X-Frame-Options", "DENY");
    headers.emplace("Content-Security-Policy", "frame-ancestors 'none';");
    response->write(SimpleWeb::StatusCode::success_ok, in, headers);
  }

  /**
   * @brief Start scanning for games.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * The body is JSON: `{"source": "folder"|"lutris"|"steam"|"heroic", "path": "/abs/folder"}` (path only for folder).
   * The response is `{"status": true, "job_id": "<id>"}`; poll `/api/library/jobs/<id>`.
   *
   * @api_examples{/api/library/scan|:| POST|:| {"source":"folder","path":"/home/user/Games"}}
   */
  void postLibraryScan(const resp_https_t &response, const req_https_t &request) {
    if (!check_content_type(response, request, "application/json")) {
      return;
    }
    if (!authenticate(response, request)) {
      return;
    }
    if (!validate_csrf_token(response, request, get_client_id(request))) {
      return;
    }
    print_req(request);

    std::stringstream ss;
    ss << request->content.rdbuf();
    try {
      const auto input = nlohmann::json::parse(ss);
      const auto source = library::parse_source(input.value("source", std::string {}));
      if (!source) {
        bad_request(response, request, "'source' must be folder, lutris, steam or heroic");
        return;
      }
      fs::path path;
      if (*source == library::source_e::folder) {
        path = input.value("path", std::string {});
        if (!library::safe_scan_root(path)) {
          bad_request(response, request, "'path' must be an existing folder (absolute path)");
          return;
        }
      }
      const auto job = library::start_scan(*source, path, library::current_settings());
      if (!job) {
        bad_request(response, request, "Another scan or import is already running. Try again when it finishes.");
        return;
      }
      send_response(response, {{"status", true}, {"job_id", *job}});
    } catch (const std::exception &e) {
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Get the progress and result of a scan or import job.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * Scan results: `result.items[]` = {temp_id, title, source, source_id, launch_cmd, working_dir,
   * matched: {appid, name, confidence} | null, artwork: {poster|hero|logo|icon: [{id, label, url?}]},
   * already_in_library} and `result.skipped[]` = {path, reason}. Import results: `result.imported[]`,
   * `result.duplicates[]` and `result.failed[]`.
   *
   * @api_examples{/api/library/jobs/0123456789abcdef|:| GET|:| null}
   */
  void getLibraryJob(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }
    print_req(request);
    const auto status = library::job_status(request->path_match[1].str());
    if (!status) {
      not_found(response, request, "Job not found");
      return;
    }
    send_response(response, *status);
  }

  /**
   * @brief Import games found by a scan as apps, downloading their artwork.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * The body is JSON: `{"items": [{"temp_id": "...", "title"?: "...", "poster"?: "<candidate id>"|"none",
   * "hero"?: ..., "logo"?: ..., "icon"?: ..., "launch_cmd"?: "...", "working_dir"?: "..."}]}`. Omitted artwork uses
   * the best candidate. The response is `{"status": true, "job_id": "<id>"}`.
   *
   * @api_examples{/api/library/import|:| POST|:| {"items":[{"temp_id":"0123456789abcdef:0"}]}}
   */
  void postLibraryImport(const resp_https_t &response, const req_https_t &request) {
    if (!check_content_type(response, request, "application/json")) {
      return;
    }
    if (!authenticate(response, request)) {
      return;
    }
    if (!validate_csrf_token(response, request, get_client_id(request))) {
      return;
    }
    print_req(request);

    std::stringstream ss;
    ss << request->content.rdbuf();
    try {
      const auto input = nlohmann::json::parse(ss);
      if (!input.contains("items") || !input["items"].is_array() || input["items"].empty() || input["items"].size() > 500) {
        bad_request(response, request, "'items' must be a list of 1 to 500 games");
        return;
      }
      const auto job = library::start_import(input["items"], library::current_settings());
      if (!job) {
        bad_request(response, request, "Another scan or import is already running. Try again when it finishes.");
        return;
      }
      send_response(response, {{"status", true}, {"job_id", *job}});
    } catch (const std::exception &e) {
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Ask a running library scan, import or artwork job to stop.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * Cancellation is cooperative: the job stops at its next checkpoint and its state becomes
   * `cancelled`. Responds 404 for unknown ids and 400 when the job already finished.
   *
   * @api_examples{/api/library/jobs/0123456789abcdef/cancel|:| POST|:| null}
   */
  void postLibraryJobCancel(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }
    if (!validate_csrf_token(response, request, get_client_id(request))) {
      return;
    }
    print_req(request);
    const auto id = request->path_match[1].str();
    if (!library::job_status(id)) {
      not_found(response, request, "Job not found");
      return;
    }
    if (!library::cancel_job(id)) {
      bad_request(response, request, "The job already finished");
      return;
    }
    send_response(response, {{"status", true}});
  }

  /**
   * @brief Download chosen artwork for an app already in the library and save it.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * The body is JSON: `{"app_index": 3, "poster"?: "<candidate id>", "hero"?: ..., "logo"?: ..., "icon"?: ...}`
   * with candidate ids from `/api/library/artwork/search`. Runs as a background job; the response is
   * `{"status": true, "job_id": "<id>"}` and the job result lists the `applied` kinds.
   *
   * @api_examples{/api/library/artwork/apply|:| POST|:| {"app_index":0,"poster":"c1a2b3c4"}}
   */
  void postLibraryArtworkApply(const resp_https_t &response, const req_https_t &request) {
    if (!check_content_type(response, request, "application/json")) {
      return;
    }
    if (!authenticate(response, request)) {
      return;
    }
    if (!validate_csrf_token(response, request, get_client_id(request))) {
      return;
    }
    print_req(request);

    std::stringstream ss;
    ss << request->content.rdbuf();
    try {
      const auto input = nlohmann::json::parse(ss);
      if (!input.contains("app_index") || !input["app_index"].is_number_unsigned()) {
        bad_request(response, request, "'app_index' must be a non-negative integer");
        return;
      }
      const auto job = library::start_apply_artwork(input["app_index"].get<std::size_t>(), input, library::current_settings());
      if (!job) {
        bad_request(response, request, "Another scan or import is already running. Try again when it finishes.");
        return;
      }
      send_response(response, {{"status", true}, {"job_id", *job}});
    } catch (const std::exception &e) {
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Set one kind of an app's artwork from an upload, an image URL, or back to automatic.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * The body is `{"app_index": 3, "kind": "poster|hero|logo|icon|background"}` plus one of `"data": "<base64 PNG/JPEG>"`,
   * `"url": "https://…"` (downloaded with SSRF protection, at most 20 MB) or `"reset": true`. The reply is
   * `{"status": true, "job_id": "<id>"}`; the job result is `{"app_index", "kind", "applied", "cleared"}`.
   *
   * @api_examples{/api/library/artwork/custom|:| POST|:| {"app_index":3,"kind":"hero","url":"https://example.com/hero.jpg"}}
   */
  void postLibraryArtworkCustom(const resp_https_t &response, const req_https_t &request) {
    if (!check_content_type(response, request, "application/json")) {
      return;
    }
    if (!authenticate(response, request)) {
      return;
    }
    if (!validate_csrf_token(response, request, get_client_id(request))) {
      return;
    }
    print_req(request);
    std::stringstream ss;
    ss << request->content.rdbuf();
    const auto input = nlohmann::json::parse(ss.str(), nullptr, false);
    if (!input.is_object() || !input.contains("app_index") || !input["app_index"].is_number_unsigned()) {
      bad_request(response, request, "'app_index' must be a non-negative integer");
      return;
    }
    const auto kind = library::parse_kind(input.value("kind", std::string {}));
    if (!kind) {
      bad_request(response, request, "'kind' must be poster, hero, logo, icon or background");
      return;
    }
    library::custom_art_t source;
    const int given = (input.contains("data") ? 1 : 0) + (input.contains("url") ? 1 : 0) + (input.value("reset", false) ? 1 : 0);
    if (given != 1) {
      bad_request(response, request, "Give exactly one of 'data', 'url' or 'reset': true");
      return;
    }
    if (input.contains("data")) {
      if (!input["data"].is_string() || input["data"].get<std::string>().size() > 28 * 1024 * 1024) {
        bad_request(response, request, "'data' must be a base64 image of at most 20 MB");
        return;
      }
      try {
        source.bytes = SimpleWeb::Crypto::Base64::decode(input["data"].get<std::string>());
      } catch (const std::exception &) {
        bad_request(response, request, "'data' isn't valid base64");
        return;
      }
    } else if (input.contains("url")) {
      std::string error;
      if (!input["url"].is_string() || !library::url_fetch::check_url(input["url"].get<std::string>(), error)) {
        bad_request(response, request, error.empty() ? "'url' must be an https:// image link" : error);
        return;
      }
      source.url = input["url"].get<std::string>();
    } else {
      source.reset = true;
    }
    const auto job = library::start_custom_artwork(input["app_index"].get<std::size_t>(), *kind, std::move(source), library::current_settings());
    if (!job) {
      bad_request(response, request, "Another library job is already running. Try again when it finishes.");
      return;
    }
    send_response(response, {{"status", true}, {"job_id", *job}});
  }

  /**
   * @brief Parse a JSON request body after the usual content-type, auth and CSRF checks.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * @return The parsed body, or nullopt when a check failed (an error was already sent).
   */
  std::optional<nlohmann::json> checked_json_body(const resp_https_t &response, const req_https_t &request) {
    if (!check_content_type(response, request, "application/json")) {
      return std::nullopt;
    }
    if (!authenticate(response, request)) {
      return std::nullopt;
    }
    if (!validate_csrf_token(response, request, get_client_id(request))) {
      return std::nullopt;
    }
    print_req(request);
    std::stringstream ss;
    ss << request->content.rdbuf();
    auto body = nlohmann::json::parse(ss.str(), nullptr, false);
    if (body.is_discarded()) {
      bad_request(response, request, "The request body isn't valid JSON");
      return std::nullopt;
    }
    return body.is_object() ? body : nlohmann::json::object();
  }

  /**
   * @brief Start a background job that re-fetches game details for some apps.
   * @param indices App indexes, or empty for every app.
   * @return Job id, or nullopt when too many jobs are running.
   */
  std::optional<std::string> start_metadata_refresh(std::vector<std::size_t> indices) {
    const bool all = indices.empty();
    return library::start_task("metadata", [indices = std::move(indices), all](const library::task_progress_t &progress, const std::function<bool()> &cancelled) {
      const auto tree = library::load_apps(config::stream.file_apps);
      const auto &apps = tree["apps"];
      std::vector<std::size_t> todo = indices;
      if (all) {
        for (std::size_t i = 0; i < apps.size(); ++i) {
          todo.push_back(i);
        }
      }
      std::size_t done = 0;
      std::size_t matched = 0;
      std::size_t considered = 0;
      for (const auto i : todo) {
        if (cancelled()) {
          break;
        }
        progress(done, todo.size(), "Fetching game details");
        if (i < apps.size() && apps[i].is_object() && !apps[i].value("cmd", std::string {}).empty() && !apps[i].value("name", std::string {}).starts_with("Desktop")) {
          ++considered;
          matched += nova_api::store_details(apps[i], true, true) ? 1 : 0;
        }
        progress(++done, todo.size(), "Fetching game details");
      }
      if (all && !cancelled()) {
        nova_api::record_refresh(matched, considered);
      }
      return nlohmann::json {{"refreshed", considered}, {"matched", matched}};
    });
  }

  /**
   * @brief Re-fetch game details in the background.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * The body is `{"app_index": 3}` for one app or `{"all": true}` for the whole library. The response is
   * `{"status": true, "job_id": "<id>"}`; the job result is `{"refreshed": n, "matched": m}`.
   *
   * @api_examples{/api/library/metadata/refresh|:| POST|:| {"all":true}}
   */
  void postLibraryMetadataRefresh(const resp_https_t &response, const req_https_t &request) {
    const auto body = checked_json_body(response, request);
    if (!body) {
      return;
    }
    std::vector<std::size_t> indices;
    if (body->contains("app_index")) {
      if (!(*body)["app_index"].is_number_unsigned()) {
        bad_request(response, request, "'app_index' must be a non-negative integer");
        return;
      }
      indices.push_back((*body)["app_index"].get<std::size_t>());
    } else if (!body->value("all", false)) {
      bad_request(response, request, "Give 'app_index' or 'all': true");
      return;
    }
    const auto job = start_metadata_refresh(std::move(indices));
    if (!job) {
      bad_request(response, request, "Another library job is already running. Try again when it finishes.");
      return;
    }
    send_response(response, {{"status", true}, {"job_id", *job}});
  }

  /**
   * @brief Delete cached game details, matches and screenshots (play statistics are kept).
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/library/metadata/clear-cache|:| POST|:| {}}
   */
  void postLibraryMetadataClearCache(const resp_https_t &response, const req_https_t &request) {
    if (!checked_json_body(response, request)) {
      return;
    }
    send_response(response, {{"status", true}, {"removed", nova_api::clear_metadata_cache()}});
  }

  /**
   * @brief Summary for the "Library & artwork" settings: `{"total", "matched", "last_refresh_at"}`.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/library/metadata/status|:| GET|:| null}
   */
  void getLibraryMetadataStatus(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }
    print_req(request);
    auto out = nova_api::metadata_summary(library::load_apps(config::stream.file_apps)["apps"]);
    out["catalog"] = library::steam_catalog::status();
    out["status"] = true;
    send_response(response, out);
  }

  /**
   * @brief Current metadata match of one app.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/library/metadata/3|:| GET|:| null}
   */
  void getLibraryMetadata(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }
    print_req(request);
    const auto tree = library::load_apps(config::stream.file_apps);
    std::size_t index = 0;
    const auto text = request->path_match[1].str();
    std::from_chars(text.data(), text.data() + text.size(), index);
    if (index >= tree["apps"].size()) {
      not_found(response, request, "No app with that index");
      return;
    }
    auto out = nova_api::metadata_status(tree["apps"][index]);
    out["status"] = true;
    out["app_index"] = index;
    send_response(response, out);
  }

  /**
   * @brief Set or clear an app's metadata match.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * The body may hold `"steam_appid": n|null`, `"igdb_id": n|null`, `"reset": true` (drop both and go
   * back to automatic matching) and `"refetch": true` (fetch details now). The response is the new
   * status, plus `job_id` when a refetch started.
   *
   * @api_examples{/api/library/metadata/3|:| POST|:| {"steam_appid":552520,"refetch":true}}
   */
  void postLibraryMetadata(const resp_https_t &response, const req_https_t &request) {
    const auto body = checked_json_body(response, request);
    if (!body) {
      return;
    }
    std::size_t index = 0;
    const auto text = request->path_match[1].str();
    std::from_chars(text.data(), text.data() + text.size(), index);
    const auto id_field = [&body](const char *key, std::optional<std::optional<std::uint64_t>> &out) {
      if (!body->contains(key)) {
        return true;
      }
      const auto &v = (*body)[key];
      if (v.is_null()) {
        out = std::optional<std::uint64_t> {};
        return true;
      }
      if (!v.is_number_unsigned() || v.get<std::uint64_t>() == 0 || v.get<std::uint64_t>() > 0xFFFFFFFFFFULL) {
        return false;
      }
      out = v.get<std::uint64_t>();
      return true;
    };
    std::optional<std::optional<std::uint64_t>> steam;
    std::optional<std::optional<std::uint64_t>> igdb;
    if (!id_field("steam_appid", steam) || !id_field("igdb_id", igdb)) {
      bad_request(response, request, "'steam_appid' and 'igdb_id' must be positive integers or null");
      return;
    }
    if (steam && *steam && **steam > 0xFFFFFFFFULL) {
      bad_request(response, request, "'steam_appid' is out of range");
      return;
    }
    const bool reset = body->value("reset", false);
    const auto before = library::load_apps(config::stream.file_apps);
    if (index >= before["apps"].size()) {
      not_found(response, request, "No app with that index");
      return;
    }
    nova_api::forget_app_metadata(before["apps"][index]);
    const auto updated = library::update_app(config::stream.file_apps, index, [&](nlohmann::json &app) {
      if (reset) {
        app.erase("nova-igdb-id");
        if (app.value("nova-source", std::string {}) != "steam") {
          app.erase("nova-steam-appid");
        }
      }
      if (steam) {
        if (*steam) {
          app["nova-steam-appid"] = static_cast<std::uint32_t>(**steam);
          app.erase("nova-igdb-id");
        } else {
          app.erase("nova-steam-appid");
        }
      }
      if (igdb) {
        if (*igdb) {
          app["nova-igdb-id"] = **igdb;
        } else {
          app.erase("nova-igdb-id");
        }
      }
    });
    if (!updated) {
      bad_request(response, request, "Couldn't save apps.json");
      return;
    }
    nova_api::forget_app_metadata(*updated);
    auto out = nova_api::metadata_status(*updated);
    out["status"] = true;
    out["app_index"] = index;
    if (body->value("refetch", false)) {
      if (const auto job = start_metadata_refresh({index})) {
        out["job_id"] = *job;
      }
    }
    send_response(response, out);
  }

  /**
   * @brief Search candidate matches for "Change match": Steam store (several spellings), the local
   * Steam catalogue, SteamGridDB and IGDB when configured, merged and ranked.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * Query: `q=<title>`, an app id or a Steam store URL. Reply: `{"candidates": [{source, appid, igdb_id,
   * sgdb_id, name, year, edition, type, unlisted, confidence, poster}], "igdb": bool, "steamgriddb": bool, "catalog": {...}}`.
   *
   * @api_examples{/api/library/metadata/search?q=Far%20Cry%205|:| GET|:| null}
   */
  void getLibraryMetadataSearch(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }
    print_req(request);
    const auto query = request->parse_query_string();
    const auto it = query.find("q");
    const auto q = it == query.end() ? std::string {} : it->second.substr(0, 128);
    if (q.empty()) {
      bad_request(response, request, "Give 'q'");
      return;
    }
    const auto meta = library::metadata::from_config();
    nlohmann::json candidates = nlohmann::json::array();
    for (const auto &c : library::match::search(q, library::match::live_sources(meta))) {
      candidates.push_back(library::match_json(c));
    }
    send_response(response, {{"status", true}, {"candidates", std::move(candidates)}, {"igdb", meta.igdb_enabled()}, {"steamgriddb", !meta.steamgriddb_api_key.empty()}, {"catalog", library::steam_catalog::status()}});
  }


  /**
   * @brief Search store matches and artwork for a game ("Change match" / "Choose artwork").
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * Query: `q=<title>` or `appid=<steam app id>`.
   *
   * @api_examples{/api/library/artwork/search?q=Far%20Cry%205|:| GET|:| null}
   */
  void getLibraryArtworkSearch(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }
    print_req(request);
    const auto query = request->parse_query_string();
    std::string q;
    std::uint32_t appid = 0;
    if (const auto it = query.find("q"); it != query.end()) {
      q = it->second.substr(0, 128);
    }
    if (const auto it = query.find("appid"); it != query.end()) {
      std::from_chars(it->second.data(), it->second.data() + it->second.size(), appid);
    }
    if (q.empty() && appid == 0) {
      bad_request(response, request, "Give 'q' or 'appid'");
      return;
    }
    auto result = library::artwork_search(q, appid, library::current_settings());
    result["status"] = true;
    send_response(response, result);
  }

  /**
   * @brief Preview an artwork candidate (re-encoded as a small PNG).
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/library/candidates/c1a2b3c4|:| GET|:| null}
   */
  void getLibraryCandidate(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }
    print_req(request);
    const auto ref = library::candidate(request->path_match[1].str());
    if (!ref) {
      not_found(response, request, "Artwork not found");
      return;
    }
    // Previews are throwaway: keep them in the temp folder, one per candidate.
    std::error_code ec;
    const auto preview_dir = fs::temp_directory_path(ec) / "nova-host-previews" / request->path_match[1].str();
    auto preview_ref = *ref;
    const bool wide = ref->kind == library::art_kind_e::hero || ref->kind == library::art_kind_e::background;
    preview_ref.kind = wide ? library::art_kind_e::hero : library::art_kind_e::poster;
    const auto cached = preview_dir / (preview_ref.kind == library::art_kind_e::hero ? "hero.jpg" : "poster.png");
    const auto stored = fs::is_regular_file(cached, ec) ? std::optional<fs::path> {cached} : library::artwork::store(preview_ref, preview_dir);
    if (!stored) {
      not_found(response, request, "Couldn't load that image");
      return;
    }
    send_image_file(response, *stored);
  }

  /**
   * @brief Get one piece of an app's artwork.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * Path: `/api/covers/<app index>/<poster|hero|logo|icon>`.
   *
   * @api_examples{/api/covers/0/hero|:| GET|:| null}
   */
  void getAppArt(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }
    print_req(request);
    try {
      const auto index = std::stoul(request->path_match[1].str());
      const auto kind = library::parse_kind(request->path_match[2].str());
      nlohmann::json app;
      {
        std::scoped_lock lock(library::apps_file_mutex());
        const auto tree = nlohmann::json::parse(file_handler::read_file(config::stream.file_apps.c_str()));
        if (!tree.contains("apps") || index >= tree["apps"].size()) {
          not_found(response, request, "Application not found");
          return;
        }
        app = tree["apps"][index];
      }
      const auto file = kind ? library::app_art(app, *kind, platf::appdata() / "covers" / "library") : std::nullopt;
      if (!file) {
        not_found(response, request, "Artwork not found");
        return;
      }
      send_image_file(response, *file);
    } catch (const std::exception &e) {
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Start the HTTPS configuration server.
   */
  /**
   * @brief GET /api/host-commands: the saved global and per-app host commands.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   */
  void getHostCommands(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }
    print_req(request);
    nlohmann::json global = nlohmann::json::array();
    for (const auto &command : host_commands::global()) {
      global.push_back(host_commands::to_json(command));
    }
    nlohmann::json apps = nlohmann::json::array();
    try {
      const auto tree = nlohmann::json::parse(file_handler::read_file(config::stream.file_apps.c_str()));
      if (tree.contains("apps") && tree["apps"].is_array()) {
        for (std::size_t i = 0; i < tree["apps"].size(); ++i) {
          nlohmann::json commands = nlohmann::json::array();
          for (const auto &command : host_commands::for_app(tree["apps"][i])) {
            commands.push_back(host_commands::to_json(command));
          }
          if (!commands.empty()) {
            apps.push_back({{"index", i}, {"name", tree["apps"][i].value("name", ""s)}, {"commands", std::move(commands)}});
          }
        }
      }
    } catch (const std::exception &) {
      // An unreadable apps.json just means no per-app commands.
    }
    send_response(response, {{"status", true}, {"global", std::move(global)}, {"apps", std::move(apps)}});
  }

  /**
   * @brief GET /api/host-commands/runs: the last run of every host command, with its output.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   */
  void getHostCommandRuns(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }
    print_req(request);
    nlohmann::json runs = nlohmann::json::array();
    for (const auto &record : host_commands::all_runs()) {
      runs.push_back(host_commands::run_to_json(record));
    }
    send_response(response, {{"status", true}, {"runs", std::move(runs)}});
  }

  /**
   * @brief POST /api/host-commands/run `{"id", "app": <index>|null}`: run a saved host command now.
   *
   * Only saved commands run (by id), exactly as a device would run them; the output shows up in
   * /api/host-commands/runs.
   *
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   */
  void postHostCommandRun(const resp_https_t &response, const req_https_t &request) {
    if (!check_content_type(response, request, "application/json")) {
      return;
    }
    if (!authenticate(response, request)) {
      return;
    }
    if (!validate_csrf_token(response, request, get_client_id(request))) {
      return;
    }
    print_req(request);
    try {
      const auto input = nlohmann::json::parse(request->content.string());
      const auto id = input.value("id", ""s);
      std::vector<host_commands::command_t> app_commands;
      if (const auto app = input.find("app"); app != input.end() && app->is_number_integer()) {
        const auto tree = nlohmann::json::parse(file_handler::read_file(config::stream.file_apps.c_str()));
        const auto index = app->get<long long>();
        if (index < 0 || !tree.contains("apps") || index >= static_cast<long long>(tree["apps"].size())) {
          bad_request(response, request, "No such app");
          return;
        }
        app_commands = host_commands::for_app(tree["apps"][static_cast<std::size_t>(index)]);
      }
      const auto found = host_commands::resolve(id, app_commands, app_commands.empty() ? host_commands::global() : std::vector<host_commands::command_t> {});
      if (!found) {
        not_found(response, request, "No saved host command with that id");
        return;
      }
      const auto started = host_commands::start_async(found->first, host_commands::build_env({{"NOVA_COMMAND_ID", found->first.id}, {"NOVA_COMMAND_NAME", found->first.name}, {"SUNSHINE_CLIENT_NAME", "web UI"}}), "the web UI");
      send_response(response, {{"status", started == host_commands::start_e::started}, {"started", started == host_commands::start_e::started}, {"error", started == host_commands::start_e::started ? "" : "That command is already running, or too many are."}});
    } catch (const std::exception &e) {
      bad_request(response, request, e.what());
    }
  }

  void start() {
    platf::set_thread_name("confighttp");
    const auto shutdown_event = mail::man->event<bool>(mail::shutdown);

    const auto port_https = net::map_port(PORT_HTTPS);
    const auto address_family = net::af_from_enum_string(config::sunshine.address_family);

    https_server_t server {config::nvhttp.cert, config::nvhttp.pkey};

    // Helper to create SPA entry handlers without repeating the signature
    auto page_handler = [](bool require_auth = true, bool redirect_if_username = false) {
      return [require_auth, redirect_if_username](const resp_https_t &response, const req_https_t &request) {
        getPage(response, request, require_auth, redirect_if_username);
      };
    };

    // Default resource handlers
    const https_handler_t bad_request_handler = [](const resp_https_t &response, const req_https_t &request) {
      bad_request(response, request);
    };
    // error by default
    server.default_resource["DELETE"] = bad_request_handler;
    server.default_resource["PATCH"] = bad_request_handler;
    server.default_resource["POST"] = bad_request_handler;
    server.default_resource["PUT"] = bad_request_handler;
    server.default_resource["GET"] = getFallbackPage;

    // Public SPA routes with authentication behavior that differs from the default fallback
    server.resource["^/logout/?$"]["GET"] = page_handler(false);
    server.resource["^/login/?$"]["GET"] = [](const resp_https_t &response, const req_https_t &request) {
      if (config::sunshine.username.empty()) {
        send_redirect(response, request, "/welcome");
        return;
      }
      if (origin_allowed(response, request)) {
        getPage(response, request, false);
      }
    };
    server.resource["^/welcome/?$"]["GET"] = page_handler(false, true);

    // rest api
    server.resource["^/api/browse$"]["GET"] = browseDirectory;
    server.resource["^/api/apps$"]["GET"] = getApps;
    server.resource["^/api/apps$"]["POST"] = saveApp;
    server.resource["^/api/clipboard/send-file$"]["POST"] = sendFileToClient;
    server.resource["^/api/apps/([0-9]+)$"]["DELETE"] = deleteApp;
    server.resource["^/api/apps/close$"]["POST"] = closeApp;
    server.resource["^/api/apps/running$"]["GET"] = getRunningApp;
    server.resource["^/api/clients/list$"]["GET"] = getClients;
    server.resource["^/api/clients/unpair$"]["POST"] = unpair;
    server.resource["^/api/clients/unpair-all$"]["POST"] = unpairAll;
    server.resource["^/api/clients/update$"]["POST"] = updateClient;
    server.resource["^/api/clients/disconnect$"]["POST"] = disconnectClient;
    // Nova: live sessions and history
    server.resource["^/api/sessions$"]["GET"] = getSessions;
    server.resource["^/api/sessions/history$"]["GET"] = getSessionHistory;
    server.resource["^/api/config$"]["GET"] = getConfig;
    server.resource["^/api/config$"]["POST"] = saveConfig;
    server.resource["^/api/configLocale$"]["GET"] = getLocale;
    server.resource["^/api/covers/([0-9]+)$"]["GET"] = getCover;
    server.resource["^/api/covers/upload$"]["POST"] = uploadCover;
    // Nova game library
    server.resource["^/api/covers/([0-9]+)/(poster|hero|logo|icon|background)$"]["GET"] = getAppArt;
    server.resource["^/api/library/scan$"]["POST"] = postLibraryScan;
    server.resource["^/api/library/jobs/([0-9a-f]{16})$"]["GET"] = getLibraryJob;
    server.resource["^/api/library/scan/([0-9a-f]{16})$"]["GET"] = getLibraryJob;
    server.resource["^/api/library/import$"]["POST"] = postLibraryImport;
    server.resource["^/api/library/jobs/([0-9a-f]{16})/cancel$"]["POST"] = postLibraryJobCancel;
    server.resource["^/api/library/artwork/apply$"]["POST"] = postLibraryArtworkApply;
    server.resource["^/api/library/artwork/custom$"]["POST"] = postLibraryArtworkCustom;
    server.resource["^/api/library/artwork/search$"]["GET"] = getLibraryArtworkSearch;
    server.resource["^/api/library/candidates/(c[0-9]+[0-9a-f]{6})$"]["GET"] = getLibraryCandidate;
    server.resource["^/api/library/metadata/refresh$"]["POST"] = postLibraryMetadataRefresh;
    server.resource["^/api/library/metadata/clear-cache$"]["POST"] = postLibraryMetadataClearCache;
    server.resource["^/api/library/metadata/status$"]["GET"] = getLibraryMetadataStatus;
    server.resource["^/api/library/metadata/search$"]["GET"] = getLibraryMetadataSearch;
    server.resource["^/api/library/metadata/([0-9]{1,5})$"]["GET"] = getLibraryMetadata;
    server.resource["^/api/library/metadata/([0-9]{1,5})$"]["POST"] = postLibraryMetadata;
    server.resource["^/api/csrf-token$"]["GET"] = getCSRFToken;
    // Nova sign-in sessions (the page at /login)
    server.resource["^/api/auth/login$"]["POST"] = postAuthLogin;
    server.resource["^/api/auth/logout$"]["POST"] = postAuthLogout;
    server.resource["^/api/auth/session$"]["GET"] = getAuthSession;
    server.resource["^/api/auth/sessions$"]["GET"] = getAuthSessions;
    server.resource["^/api/auth/sessions/revoke$"]["POST"] = postAuthSessionsRevoke;
    server.resource["^/api/password$"]["POST"] = savePassword;
    server.resource["^/api/pin$"]["DELETE"] = cancelPairing;
    server.resource["^/api/pin$"]["GET"] = getPendingPairings;
    server.resource["^/api/pin$"]["POST"] = savePin;
    server.resource["^/api/logs$"]["GET"] = getLogs;
    server.resource["^/api/reset-display-device-persistence$"]["POST"] = resetDisplayDevicePersistence;
    server.resource["^/api/reset-portal-token$"]["POST"] = resetPortalToken;
    server.resource["^/api/restart$"]["POST"] = restart;
    server.resource["^/api/virtual-input/license$"]["GET"] = getVirtualInputLicense;
    server.resource["^/api/virtual-input/license$"]["POST"] = updateVirtualInputLicense;
    server.resource["^/api/virtual-input/status$"]["GET"] = getVirtualInputStatus;
    // Nova host facts
    server.resource["^/api/host/info$"]["GET"] = getHostInfo;
    server.resource["^/api/displays$"]["GET"] = getDisplays;
    server.resource["^/api/audio/sinks$"]["GET"] = getAudioSinks;
    server.resource["^/api/preview$"]["GET"] = getPreview;
    server.resource["^/api/health$"]["GET"] = getHealth;
    server.resource["^/api/host-commands$"]["GET"] = getHostCommands;
    server.resource["^/api/host-commands/runs$"]["GET"] = getHostCommandRuns;
    server.resource["^/api/host-commands/run$"]["POST"] = postHostCommandRun;
    nova_update::register_routes(server);

    // static/dynamic resources
    server.resource["^/images/sunshine.ico$"]["GET"] = getFaviconImage;
    server.resource["^/images/logo-sunshine-45.png$"]["GET"] = getSunshineLogoImage;
    server.resource["^/assets\\/.+$"]["GET"] = getAsset;

    server.config.reuse_address = true;
    server.config.address = net::get_bind_address(address_family);
    server.config.port = port_https;
    // Bodies are buffered before authentication; custom artwork uploads are the largest legitimate ones.
    server.config.max_request_streambuf_size = 64 * 1024 * 1024;

    const auto display_addr = net::get_bind_address_url_host();

    auto accept_and_run = [&](auto *server) {
      try {
        platf::set_thread_name("confighttp::tcp");
        server->start([&display_addr](const unsigned short port) {
          BOOST_LOG(info) << "Configuration UI available at [https://"sv << display_addr << ":" << port << "]";
        });
      } catch (boost::system::system_error &err) {
        // It's possible the exception gets thrown after calling server->stop() from a different thread
        if (shutdown_event->peek()) {
          return;
        }

        BOOST_LOG(fatal) << "Couldn't start Configuration HTTPS server on port ["sv << port_https << "]: "sv << err.what();
        shutdown_event->raise(true);
        return;
      }
    };
    std::jthread tcp {accept_and_run, &server};

    // Wait for any event
    shutdown_event->view();

    server.stop();

    tcp.join();
  }
}  // namespace confighttp
