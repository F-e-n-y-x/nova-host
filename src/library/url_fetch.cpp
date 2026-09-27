/**
 * @file src/library/url_fetch.cpp
 * @brief SSRF-safe image download for user-supplied URLs.
 */
// standard includes
#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

// platform includes
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>

// lib includes
#include <curl/curl.h>

// local includes
#include "url_fetch.h"

namespace library::url_fetch {
  namespace {
    constexpr int max_redirects = 3;  ///< Redirects followed (each one re-checked).

    /**
     * @brief Whether the first @p bits bits of @p a equal those of @p net.
     */
    bool in_v4(const std::array<std::uint8_t, 4> &a, std::array<std::uint8_t, 4> net, int bits) {
      const std::uint32_t x = (std::uint32_t {a[0]} << 24) | (std::uint32_t {a[1]} << 16) | (std::uint32_t {a[2]} << 8) | a[3];
      const std::uint32_t n = (std::uint32_t {net[0]} << 24) | (std::uint32_t {net[1]} << 16) | (std::uint32_t {net[2]} << 8) | net[3];
      const std::uint32_t mask = bits == 0 ? 0 : ~std::uint32_t {0} << (32 - bits);
      return (x & mask) == (n & mask);
    }

    /**
     * @brief Response state collected by the curl callbacks.
     */
    struct sink_t {
      std::string body;  ///< Body so far.
      std::size_t limit = 0;  ///< Size cap.
      bool too_big = false;  ///< Set when the cap was hit.
    };

    std::size_t write_body(char *ptr, std::size_t size, std::size_t nmemb, void *userdata) {
      auto *sink = static_cast<sink_t *>(userdata);
      const auto n = size * nmemb;
      if (sink->body.size() + n > sink->limit) {
        sink->too_big = true;
        return 0;
      }
      sink->body.append(ptr, n);
      return n;
    }
  }  // namespace

  std::optional<target_t> check_url(std::string_view url, std::string &error) {
    if (url.size() > 2048) {
      error = "The URL is too long.";
      return std::nullopt;
    }
    CURLU *u = curl_url();
    const std::string text(url);
    std::optional<target_t> out;
    char *scheme = nullptr;
    char *host = nullptr;
    char *user = nullptr;
    char *password = nullptr;
    char *port = nullptr;
    char *full = nullptr;
    if (curl_url_set(u, CURLUPART_URL, text.c_str(), 0) != CURLUE_OK) {
      error = "That isn't a valid URL.";
    } else if (curl_url_get(u, CURLUPART_SCHEME, &scheme, 0) != CURLUE_OK || !scheme || std::string_view(scheme) != "https") {
      error = "Only https:// image links are allowed.";
    } else if (curl_url_get(u, CURLUPART_USER, &user, 0) == CURLUE_OK || curl_url_get(u, CURLUPART_PASSWORD, &password, 0) == CURLUE_OK) {
      error = "Links with a user name or password aren't allowed.";
    } else if (curl_url_get(u, CURLUPART_PORT, &port, 0) == CURLUE_OK && port && std::string_view(port) != "443") {
      error = "Only the standard HTTPS port is allowed.";
    } else if (curl_url_get(u, CURLUPART_HOST, &host, 0) != CURLUE_OK || !host || !*host) {
      error = "The URL has no host.";
    } else if (curl_url_get(u, CURLUPART_URL, &full, 0) == CURLUE_OK && full) {
      std::string h = host;
      if (h.size() > 2 && h.front() == '[' && h.back() == ']') {
        h = h.substr(1, h.size() - 2);
      }
      out = target_t {full, h};
    } else {
      error = "That isn't a valid URL.";
    }
    for (char *p : {scheme, host, user, password, port, full}) {
      if (p) {
        curl_free(p);
      }
    }
    curl_url_cleanup(u);
    return out;
  }

  bool is_public_ipv4(const std::array<std::uint8_t, 4> &a) {
    struct range_t {
      std::array<std::uint8_t, 4> net;  ///< Network.
      int bits;  ///< Prefix length.
    };

    static constexpr std::array<range_t, 16> blocked {{
      {{0, 0, 0, 0}, 8},  // this network
      {{10, 0, 0, 0}, 8},  // private
      {{100, 64, 0, 0}, 10},  // carrier-grade NAT
      {{127, 0, 0, 0}, 8},  // loopback
      {{169, 254, 0, 0}, 16},  // link-local (cloud metadata)
      {{172, 16, 0, 0}, 12},  // private
      {{192, 0, 0, 0}, 24},  // IETF protocol assignments
      {{192, 0, 2, 0}, 24},  // documentation
      {{192, 88, 99, 0}, 24},  // 6to4 relay
      {{192, 168, 0, 0}, 16},  // private
      {{198, 18, 0, 0}, 15},  // benchmarking
      {{198, 51, 100, 0}, 24},  // documentation
      {{203, 0, 113, 0}, 24},  // documentation
      {{224, 0, 0, 0}, 4},  // multicast
      {{240, 0, 0, 0}, 4},  // reserved and broadcast
      {{255, 255, 255, 255}, 32},  // broadcast
    }};
    return std::ranges::none_of(blocked, [&a](const range_t &r) {
      return in_v4(a, r.net, r.bits);
    });
  }

  bool is_public_ipv6(const std::array<std::uint8_t, 16> &a) {
    const auto prefix_zero = [&a](std::size_t n) {
      return std::all_of(a.begin(), a.begin() + static_cast<std::ptrdiff_t>(n), [](std::uint8_t b) {
        return b == 0;
      });
    };
    // IPv4-mapped (::ffff:a.b.c.d) and IPv4-compatible (::a.b.c.d): judge the IPv4 address.
    if (prefix_zero(10) && a[10] == 0xff && a[11] == 0xff) {
      return is_public_ipv4({a[12], a[13], a[14], a[15]});
    }
    if (prefix_zero(12)) {
      return false;  // ::, ::1 and deprecated IPv4-compatible addresses
    }
    // NAT64 64:ff9b::/96 embeds an IPv4 address.
    if (a[0] == 0x00 && a[1] == 0x64 && a[2] == 0xff && a[3] == 0x9b && std::all_of(a.begin() + 4, a.begin() + 12, [](std::uint8_t b) {
          return b == 0;
        })) {
      return is_public_ipv4({a[12], a[13], a[14], a[15]});
    }
    if ((a[0] & 0xfe) == 0xfc) {
      return false;  // unique local fc00::/7
    }
    if (a[0] == 0xfe && (a[1] & 0xc0) == 0x80) {
      return false;  // link-local fe80::/10
    }
    if (a[0] == 0xfe && (a[1] & 0xc0) == 0xc0) {
      return false;  // site-local fec0::/10 (deprecated)
    }
    if (a[0] == 0xff) {
      return false;  // multicast
    }
    if (a[0] == 0x20 && a[1] == 0x01 && a[2] == 0x0d && a[3] == 0xb8) {
      return false;  // documentation 2001:db8::/32
    }
    if (a[0] == 0x01 && std::all_of(a.begin() + 1, a.begin() + 8, [](std::uint8_t b) {
          return b == 0;
        })) {
      return false;  // discard-only 100::/64
    }
    if (a[0] == 0x20 && a[1] == 0x02) {
      return is_public_ipv4({a[2], a[3], a[4], a[5]});  // 6to4 embeds an IPv4 address
    }
    return true;
  }

  bool is_public_ip(std::string_view ip) {
    const std::string text(ip);
    std::array<std::uint8_t, 4> v4 {};
    std::array<std::uint8_t, 16> v6 {};
    if (inet_pton(AF_INET, text.c_str(), v4.data()) == 1) {
      return is_public_ipv4(v4);
    }
    if (inet_pton(AF_INET6, text.c_str(), v6.data()) == 1) {
      return is_public_ipv6(v6);
    }
    return false;
  }

  std::optional<std::string> resolve_public(const std::string &host, std::string &error) {
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *res = nullptr;
    if (getaddrinfo(host.c_str(), "443", &hints, &res) != 0 || !res) {
      error = "Couldn't find that host.";
      return std::nullopt;
    }
    std::vector<std::string> addresses;
    for (auto *p = res; p; p = p->ai_next) {
      char buf[INET6_ADDRSTRLEN] = {};
      if (p->ai_family == AF_INET) {
        inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in *>(p->ai_addr)->sin_addr, buf, sizeof(buf));
      } else if (p->ai_family == AF_INET6) {
        inet_ntop(AF_INET6, &reinterpret_cast<sockaddr_in6 *>(p->ai_addr)->sin6_addr, buf, sizeof(buf));
      } else {
        continue;
      }
      addresses.emplace_back(buf);
    }
    freeaddrinfo(res);
    if (addresses.empty()) {
      error = "Couldn't find that host.";
      return std::nullopt;
    }
    // Every address must be public, so a host can't mix in a private one to be picked later.
    if (!std::ranges::all_of(addresses, [](const std::string &a) {
          return is_public_ip(a);
        })) {
      error = "Links to local or private network addresses aren't allowed.";
      return std::nullopt;
    }
    return addresses.front();
  }

  bool is_image_type(std::string_view content_type) {
    std::string t;
    for (const char c : content_type) {
      if (c == ';') {
        break;
      }
      if (c != ' ') {
        t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
    }
    return t.starts_with("image/") && t.size() > 6 && t != "image/svg+xml";
  }

  result_t fetch_image(const std::string &url, std::size_t max_bytes) {
    result_t out;
    std::string next = url;
    for (int hop = 0; hop <= max_redirects; ++hop) {
      const auto target = check_url(next, out.error);
      if (!target) {
        return out;
      }
      const auto ip = resolve_public(target->host, out.error);
      if (!ip) {
        return out;
      }
      CURL *curl = curl_easy_init();
      if (!curl) {
        out.error = "Couldn't start the download.";
        return out;
      }
      // Pin the connection to the address that was checked (no second DNS lookup).
      const bool literal = target->host == *ip || is_public_ip(target->host);
      const auto pin = target->host + ":443:" + (ip->find(':') != std::string::npos ? "[" + *ip + "]" : *ip);
      curl_slist *resolve = literal ? nullptr : curl_slist_append(nullptr, pin.c_str());
      sink_t sink;
      sink.limit = max_bytes;
      curl_easy_setopt(curl, CURLOPT_URL, target->url.c_str());
#if LIBCURL_VERSION_NUM >= 0x075500
      curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
#else
      curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#endif
      if (resolve) {
        curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve);
      }
      curl_easy_setopt(curl, CURLOPT_PROXY, "");
      curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
      curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
      curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
      curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
      curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(max_bytes));
      curl_easy_setopt(curl, CURLOPT_USERAGENT, "Nova/1.0 (artwork)");
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
      const auto code = curl_easy_perform(curl);
      long status = 0;
      char *type = nullptr;
      char *location = nullptr;
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
      curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &type);
      curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &location);
      const std::string content_type = type ? type : "";
      const std::string redirect = location ? location : "";
      curl_slist_free_all(resolve);
      curl_easy_cleanup(curl);

      if (sink.too_big || code == CURLE_FILESIZE_EXCEEDED) {
        out.error = "The image is larger than 20 MB.";
        return out;
      }
      if (code != CURLE_OK) {
        out.error = "Couldn't download the image.";
        return out;
      }
      if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
        if (redirect.empty()) {
          out.error = "The link redirects nowhere.";
          return out;
        }
        next = redirect;
        continue;
      }
      if (status != 200) {
        out.error = "The server answered " + std::to_string(status) + ".";
        return out;
      }
      if (!is_image_type(content_type)) {
        out.error = "That link isn't an image.";
        return out;
      }
      out.body = std::move(sink.body);
      out.error.clear();
      return out;
    }
    out.error = "Too many redirects.";
    return out;
  }
}  // namespace library::url_fetch
