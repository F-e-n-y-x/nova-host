/**
 * @file src/platform/linux/tcp_stats.h
 * @brief Declarations for reading kernel TCP counters of one of this process's connections.
 *
 * Kept free of Boost and glibc's `<netinet/tcp.h>` so the implementation can use the full
 * `struct tcp_info` from `<linux/tcp.h>`.
 */
#pragma once

// standard includes
#include <cstddef>
#include <cstdint>
#include <optional>

/**
 * @brief Linux TCP connection counters.
 */
namespace tcp_stats {
  /**
   * @brief Counters from `TCP_INFO`.
   */
  struct counters_t {
    std::uint64_t bytes_sent = 0;  ///< Payload bytes sent, retransmissions included (`tcpi_bytes_sent`).
    std::uint64_t bytes_retrans = 0;  ///< Payload bytes retransmitted (`tcpi_bytes_retrans`).
    std::uint64_t segments_out = 0;  ///< Segments sent (`tcpi_segs_out`).
    std::uint64_t retransmits = 0;  ///< Segments retransmitted (`tcpi_total_retrans`).
    std::uint32_t rtt_us = 0;  ///< Smoothed RTT (`tcpi_rtt`).
    std::uint32_t rtt_var_us = 0;  ///< RTT variance (`tcpi_rttvar`).
  };

  /**
   * @brief Find this process's TCP socket bound to @p local and connected to @p remote and read its counters.
   *
   * @param local Host socket address (`sockaddr_in` or `sockaddr_in6`).
   * @param local_len Size of @p local.
   * @param remote Peer socket address.
   * @param remote_len Size of @p remote.
   * @return Counters, or nullopt when no such socket exists.
   */
  std::optional<counters_t> find(const void *local, std::size_t local_len, const void *remote, std::size_t remote_len);
}  // namespace tcp_stats
