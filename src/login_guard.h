/**
 * @file src/login_guard.h
 * @brief Failed-login tracking with exponential lockout for the web UI and its REST API.
 */
#pragma once

// standard includes
#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace login_guard {
  /**
   * @brief How many failures are tolerated and how long lockouts last.
   */
  struct policy_t {
    int free_attempts = 5;  ///< Failures allowed before the first lockout.
    std::chrono::seconds base_lockout {30};  ///< First lockout; doubles with each further failure.
    std::chrono::seconds max_lockout {15 * 60};  ///< Upper bound for one lockout.
    std::chrono::seconds forget_after {60 * 60};  ///< Idle time after which a source's failures are forgotten.
    std::size_t max_tracked = 4096;  ///< Bound on remembered sources; the stalest are dropped first.
  };

  /**
   * @brief Per-source failure counter.
   *
   * A source is normally the client IP address (IPv6 is grouped by /64, see source_key()).
   * Repeating the exact same wrong credentials does not count again: a browser tab with a stale
   * cached password fires many parallel requests, which must not lock the owner out on its own.
   * A guessing attacker sends different passwords, and each one counts.
   */
  class limiter_t {
  public:
    using clock = std::chrono::steady_clock;  ///< Monotonic clock used for all timing.

    /**
     * @brief Create a limiter.
     * @param policy Thresholds and durations.
     */
    explicit limiter_t(policy_t policy = {});

    /**
     * @brief Remaining lockout for a source.
     * @param source Source key.
     * @param now Current time.
     * @return Zero when the source may try to sign in.
     */
    std::chrono::seconds locked_for(std::string_view source, clock::time_point now);

    /**
     * @brief Record a failed sign-in.
     * @param source Source key.
     * @param fingerprint Opaque digest of the attempted credentials (never the password itself).
     * @param now Current time.
     * @return The lockout that now applies (zero while free attempts remain).
     */
    std::chrono::seconds record_failure(std::string_view source, std::string_view fingerprint, clock::time_point now);

    /**
     * @brief Forget a source's failures after it signed in.
     * @param source Source key.
     */
    void record_success(std::string_view source);

    /**
     * @brief Counted failures for a source (for logs and tests).
     * @param source Source key.
     * @return Number of distinct failed attempts still remembered.
     */
    int failures(std::string_view source);

    /**
     * @brief Forget everything.
     */
    void reset();

  private:
    struct entry_t {
      int failures = 0;
      std::string last_fingerprint;
      clock::time_point last_failure {};
      clock::time_point locked_until {};
    };

    void prune(clock::time_point now);

    policy_t policy_;
    std::mutex mutex_;
    std::unordered_map<std::string, entry_t> entries_;
  };

  /**
   * @brief Key under which a client address is tracked.
   *
   * IPv4 addresses are used as-is; IPv6 addresses are reduced to their /64 prefix, because one
   * host usually controls a whole /64.
   *
   * @param address Normalized textual address.
   * @return The source key.
   */
  std::string source_key(std::string_view address);

  /**
   * @brief The process-wide limiter used by confighttp::authenticate().
   * @return The limiter.
   */
  limiter_t &web_ui();
}  // namespace login_guard
