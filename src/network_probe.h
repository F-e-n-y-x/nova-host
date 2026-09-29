/**
 * @file src/network_probe.h
 * @brief Declarations for the paired-client network probe (`/api/network/capabilities`, `/api/network/probe`).
 *
 * Wire-compatible with Sunshine-Foundation's bandwidth probe (moonlight-vplus
 * `NvHTTP.runNetworkProbe()`): the client times a few `GET /api/network/capabilities` calls for
 * RTT and jitter, then downloads `bytes` of random data from `GET /api/network/probe?bytes=&nonce=`
 * for throughput. Nova adds `GET /api/network/probe/result?nonce=`, which reports the TCP
 * retransmissions the host saw while sending the burst, so a client can show packet loss too.
 */
#pragma once

// standard includes
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

// lib includes
#include <boost/asio/ip/tcp.hpp>
#include <nlohmann/json.hpp>

/**
 * @brief Bandwidth probe: request validation, rate limiting and TCP loss sampling.
 */
namespace network_probe {
  using clock = std::chrono::steady_clock;  ///< Clock used for limits.

  constexpr std::size_t MIN_BYTES = 64 * 1024;  ///< Smallest burst a client may ask for.
  constexpr std::size_t MAX_BYTES = 4 * 1024 * 1024;  ///< Largest burst (the V+ client caps a sample at 4 MiB too).
  constexpr std::size_t CHUNK_BYTES = 64 * 1024;  ///< Bytes written per send.
  constexpr std::uint32_t COOLDOWN_MS = 5000;  ///< Least time between two bursts of one device.
  constexpr auto MAX_TRANSFER = std::chrono::seconds {3};  ///< A burst is cut off after this long.
  constexpr std::size_t CLIENT_QUOTA_BYTES = 16 * 1024 * 1024;  ///< Burst bytes one device may use per minute.
  constexpr std::size_t GLOBAL_CONCURRENCY = 2;  ///< Bursts in flight across all devices.
  constexpr auto QUOTA_WINDOW = std::chrono::minutes {1};  ///< Window of the per-device quota.
  constexpr std::size_t RESULTS_KEPT = 32;  ///< Finished bursts whose result can still be fetched.

  /**
   * @brief Whether a nonce is acceptable: 1-64 characters of `[A-Za-z0-9._-]`.
   *
   * @param nonce Client nonce.
   * @return True when valid.
   */
  bool valid_nonce(std::string_view nonce);

  /**
   * @brief Parse the `bytes` argument.
   *
   * @param value Decimal digits only.
   * @return The size, or nullopt when it isn't a number within [MIN_BYTES, MAX_BYTES].
   */
  std::optional<std::size_t> parse_bytes(std::string_view value);

  /**
   * @brief Body of `GET /api/network/capabilities`.
   *
   * @return `{"version":1,"features":["bandwidth-probe-v1","tcp-loss-v1"],"bandwidthProbe":{...}}`.
   */
  nlohmann::json capabilities_json();

  /**
   * @brief Error body (`{"error","retryAfterMs"?}`).
   *
   * @param error Machine-readable reason, e.g. "rate_limited".
   * @param retry_after_ms Retry hint, 0 for none.
   * @return JSON object.
   */
  nlohmann::json error_json(std::string_view error, std::uint32_t retry_after_ms = 0);

  /**
   * @brief Outcome of asking to start a burst.
   */
  struct admission_t {
    std::string error;  ///< Empty when admitted; otherwise "busy", "cooldown", "nonce_reused" or "quota".
    std::uint32_t retry_after_ms = 0;  ///< Retry hint when refused.
    std::uint64_t id = 0;  ///< Admission id to pass to `complete()`.

    /**
     * @brief Whether the burst may start.
     * @return True when admitted.
     */
    explicit operator bool() const noexcept {
      return error.empty();
    }
  };

  /**
   * @brief Loss measured on the host while a burst was sent.
   */
  struct tcp_sample_t {
    std::uint64_t bytes_sent = 0;  ///< Payload bytes sent on the connection, retransmissions included.
    std::uint64_t bytes_retrans = 0;  ///< Payload bytes retransmitted.
    std::uint64_t segments_out = 0;  ///< Segments sent on the connection.
    std::uint64_t retransmits = 0;  ///< Segments retransmitted on the connection.
    double rtt_ms = 0;  ///< Kernel smoothed RTT.
    double rtt_var_ms = 0;  ///< Kernel RTT variance.
  };

  /**
   * @brief Result of one finished burst, for `GET /api/network/probe/result`.
   */
  struct result_t {
    std::string nonce;  ///< Burst nonce.
    std::size_t requested_bytes = 0;  ///< Bytes asked for.
    std::size_t sent_bytes = 0;  ///< Bytes handed to the socket.
    std::int64_t duration_ms = 0;  ///< Time from the first write to the last completion.
    std::string outcome;  ///< "completed", "timeout" or "cancelled".
    std::optional<tcp_sample_t> tcp;  ///< Connection counters for the burst (Linux only).
  };

  /**
   * @brief Loss percentage from TCP counters.
   *
   * @param sample Counters for the burst (deltas from before it started).
   * @return Retransmitted share of sent bytes (or of segments when byte counters are missing) in
   *         percent, 0 when nothing was sent.
   */
  double loss_pct(const tcp_sample_t &sample);

  /**
   * @brief JSON body of a result.
   *
   * @param result Result.
   * @return `{"nonce","requestedBytes","sentBytes","durationMs","result","lossPct"?,"retransmits"?,
   *          "segmentsOut"?,"tcpRttMs"?,"tcpRttVarMs"?}`.
   */
  nlohmann::json result_json(const result_t &result);

  /**
   * @brief Rate limiting and result bookkeeping shared by every connection.
   *
   * One burst per device at a time, a cooldown between bursts, one-time nonces, a per-minute byte
   * quota per device and a global concurrency cap.
   */
  class limiter_t {
  public:
    /**
     * @brief Ask to start a burst.
     *
     * @param client Device key (certificate).
     * @param nonce Burst nonce.
     * @param bytes Burst size.
     * @param now Current time.
     * @return Admission, refused with a reason and retry hint.
     */
    admission_t admit(const std::string &client, const std::string &nonce, std::size_t bytes, clock::time_point now = clock::now());

    /**
     * @brief Mark a burst finished and keep its result.
     *
     * @param client Device key.
     * @param id Admission id.
     * @param result Result to keep for `result()`.
     * @param now Current time.
     */
    void complete(const std::string &client, std::uint64_t id, result_t result, clock::time_point now = clock::now());

    /**
     * @brief Result of a device's finished burst.
     *
     * @param client Device key.
     * @param nonce Burst nonce.
     * @return The result, or nullopt when unknown or still running.
     */
    std::optional<result_t> result(const std::string &client, const std::string &nonce) const;

    /**
     * @brief Whether a device has a burst in flight with this nonce.
     *
     * @param client Device key.
     * @param nonce Burst nonce.
     * @return True while it runs.
     */
    bool running(const std::string &client, const std::string &nonce) const;

  private:
    /**
     * @brief Per-device state.
     */
    struct client_t {
      std::uint64_t in_flight_id = 0;  ///< Admission id of the running burst, 0 when idle.
      std::string in_flight_nonce;  ///< Nonce of the running burst.
      clock::time_point cooldown_until {};  ///< No new burst before this.
      std::deque<std::pair<clock::time_point, std::size_t>> charges;  ///< Bytes admitted in the quota window.
      std::deque<std::pair<clock::time_point, std::string>> used_nonces;  ///< Nonces seen in the quota window.
      std::deque<result_t> results;  ///< Recent results, oldest first.
    };

    mutable std::mutex mutex_;  ///< Guards everything below.
    std::unordered_map<std::string, client_t> clients_;  ///< State by device.
    std::size_t in_flight_ = 0;  ///< Bursts running across devices.
    std::uint64_t next_id_ = 1;  ///< Next admission id.
  };

  /**
   * @brief The random bytes a burst repeats (generated once, incompressible).
   *
   * @return `CHUNK_BYTES` bytes.
   */
  const std::string &payload_chunk();

  /**
   * @brief TCP counters of this process's connection between two endpoints.
   *
   * @param local Host end of the connection.
   * @param remote Client end of the connection.
   * @return Counters, or nullopt when not on Linux or the socket isn't found.
   */
  std::optional<tcp_sample_t> sample_tcp(const boost::asio::ip::tcp::endpoint &local, const boost::asio::ip::tcp::endpoint &remote);

  /**
   * @brief Counters accumulated between two samples of one connection.
   *
   * @param before Sample taken when the burst started.
   * @param after Sample taken when it ended.
   * @return Deltas for bytes, segments and retransmits (0 when a counter went backwards); RTT values from @p after.
   */
  tcp_sample_t delta(const tcp_sample_t &before, const tcp_sample_t &after);
}  // namespace network_probe
