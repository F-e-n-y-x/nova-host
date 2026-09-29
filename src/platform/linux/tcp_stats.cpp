/**
 * @file src/platform/linux/tcp_stats.cpp
 * @brief Definitions for reading kernel TCP counters of one of this process's connections.
 */
// class header include
#include "tcp_stats.h"

// standard includes
#include <cstring>
#include <string>

// platform includes
#include <dirent.h>
#include <linux/tcp.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace tcp_stats {
  namespace {
    /**
     * @brief Compare two socket addresses (family, port and address; IPv4-mapped IPv6 equals IPv4).
     *
     * @param a First address.
     * @param b Second address.
     * @return True when they name the same endpoint.
     */
    bool same_endpoint(const sockaddr_storage &a, const sockaddr_storage &b) {
      const auto v4_of = [](const sockaddr_storage &s, in_addr &addr, in_port_t &port) {
        if (s.ss_family == AF_INET) {
          const auto &in = reinterpret_cast<const sockaddr_in &>(s);
          addr = in.sin_addr;
          port = in.sin_port;
          return true;
        }
        if (s.ss_family == AF_INET6) {
          const auto &in6 = reinterpret_cast<const sockaddr_in6 &>(s);
          if (IN6_IS_ADDR_V4MAPPED(&in6.sin6_addr)) {
            std::memcpy(&addr, in6.sin6_addr.s6_addr + 12, sizeof(addr));
            port = in6.sin6_port;
            return true;
          }
        }
        return false;
      };
      in_addr a4 {};
      in_addr b4 {};
      in_port_t ap = 0;
      in_port_t bp = 0;
      if (v4_of(a, a4, ap) && v4_of(b, b4, bp)) {
        return ap == bp && a4.s_addr == b4.s_addr;
      }
      if (a.ss_family == AF_INET6 && b.ss_family == AF_INET6) {
        const auto &x = reinterpret_cast<const sockaddr_in6 &>(a);
        const auto &y = reinterpret_cast<const sockaddr_in6 &>(b);
        return x.sin6_port == y.sin6_port && std::memcmp(&x.sin6_addr, &y.sin6_addr, sizeof(in6_addr)) == 0;
      }
      return false;
    }

    /**
     * @brief Copy a caller address into a `sockaddr_storage`.
     *
     * @param src Address.
     * @param len Its size.
     * @param out Destination.
     * @return False when the size is out of range.
     */
    bool load(const void *src, std::size_t len, sockaddr_storage &out) {
      if (!src || len == 0 || len > sizeof(out)) {
        return false;
      }
      std::memset(&out, 0, sizeof(out));
      std::memcpy(&out, src, len);
      return true;
    }
  }  // namespace

  std::optional<counters_t> find(const void *local, std::size_t local_len, const void *remote, std::size_t remote_len) {
    sockaddr_storage want_local {};
    sockaddr_storage want_remote {};
    if (!load(local, local_len, want_local) || !load(remote, remote_len, want_remote)) {
      return std::nullopt;
    }
    DIR *dir = opendir("/proc/self/fd");
    if (!dir) {
      return std::nullopt;
    }
    std::optional<counters_t> found;
    while (const auto *entry = readdir(dir)) {
      char *end = nullptr;
      const long fd = std::strtol(entry->d_name, &end, 10);
      if (end == entry->d_name || *end != '\0' || fd == dirfd(dir)) {
        continue;
      }
      struct stat st {};
      if (fstat(static_cast<int>(fd), &st) != 0 || !S_ISSOCK(st.st_mode)) {
        continue;
      }
      sockaddr_storage have_local {};
      sockaddr_storage have_remote {};
      socklen_t len = sizeof(have_local);
      if (getsockname(static_cast<int>(fd), reinterpret_cast<sockaddr *>(&have_local), &len) != 0) {
        continue;
      }
      len = sizeof(have_remote);
      if (getpeername(static_cast<int>(fd), reinterpret_cast<sockaddr *>(&have_remote), &len) != 0) {
        continue;
      }
      if (!same_endpoint(have_local, want_local) || !same_endpoint(have_remote, want_remote)) {
        continue;
      }
      tcp_info info {};
      len = sizeof(info);
      if (getsockopt(static_cast<int>(fd), IPPROTO_TCP, TCP_INFO, &info, &len) != 0) {
        break;
      }
      counters_t c;
      c.bytes_sent = info.tcpi_bytes_sent;
      c.bytes_retrans = info.tcpi_bytes_retrans;
      c.segments_out = info.tcpi_segs_out;
      c.retransmits = info.tcpi_total_retrans;
      c.rtt_us = info.tcpi_rtt;
      c.rtt_var_us = info.tcpi_rttvar;
      found = c;
      break;
    }
    closedir(dir);
    return found;
  }
}  // namespace tcp_stats
