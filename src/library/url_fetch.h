/**
 * @file src/library/url_fetch.h
 * @brief Fetch an image from a URL the user pasted, with SSRF protection: HTTPS only, public
 * addresses only (checked after DNS resolution and pinned for the connection), redirects
 * re-checked, a size cap and an image content type.
 */
#pragma once

// standard includes
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace library::url_fetch {

  /**
   * @brief Largest image accepted from a URL.
   */
  inline constexpr std::size_t max_image_bytes = 20 * 1024 * 1024;

  /**
   * @brief A URL that passed the syntax checks.
   */
  struct target_t {
    std::string url;  ///< Normalised URL.
    std::string host;  ///< Host name or IP literal (without brackets).
  };

  /**
   * @brief Check a user URL: https scheme, a host, no user name or password, default port.
   *
   * @param url URL.
   * @param error Set to the reason when rejected.
   * @return Target, or nullopt.
   */
  std::optional<target_t> check_url(std::string_view url, std::string &error);

  /**
   * @brief Whether an IPv4 address (network byte order bytes) is publicly routable.
   *
   * Rejects this-network, private, carrier-grade NAT, loopback, link-local, documentation,
   * benchmarking, multicast, reserved and broadcast ranges.
   *
   * @param a Address bytes.
   * @return True when public.
   */
  bool is_public_ipv4(const std::array<std::uint8_t, 4> &a);

  /**
   * @brief Whether an IPv6 address is publicly routable (IPv4-mapped/NAT64 addresses are checked as IPv4).
   *
   * @param a Address bytes.
   * @return True when public.
   */
  bool is_public_ipv6(const std::array<std::uint8_t, 16> &a);

  /**
   * @brief Whether a textual IP address is public.
   *
   * @param ip "203.0.113.5" or "2001:db8::1".
   * @return True when it parses and is public.
   */
  bool is_public_ip(std::string_view ip);

  /**
   * @brief Resolve a host and make sure every address is public.
   *
   * @param host Host name or IP literal.
   * @param error Set to the reason on failure.
   * @return One public address to connect to, or nullopt.
   */
  std::optional<std::string> resolve_public(const std::string &host, std::string &error);

  /**
   * @brief Whether a Content-Type header names an image.
   *
   * @param content_type Header value (parameters allowed).
   * @return True for image types other than SVG.
   */
  bool is_image_type(std::string_view content_type);

  /**
   * @brief Result of @ref fetch_image.
   */
  struct result_t {
    std::optional<std::string> body;  ///< Image bytes on success.
    std::string error;  ///< Why it failed, for the user.
  };

  /**
   * @brief Download an image from a user URL (at most 3 redirects, each re-checked).
   *
   * @param url URL.
   * @param max_bytes Size cap.
   * @return Body or error.
   */
  result_t fetch_image(const std::string &url, std::size_t max_bytes = max_image_bytes);
}  // namespace library::url_fetch
