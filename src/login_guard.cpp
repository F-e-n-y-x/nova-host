/**
 * @file src/login_guard.cpp
 * @brief Definitions for failed-login tracking with exponential lockout.
 */
// standard includes
#include <algorithm>
#include <vector>

// lib includes
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/address_v6.hpp>

// local includes
#include "login_guard.h"

namespace login_guard {
  limiter_t::limiter_t(policy_t policy):
      policy_ {policy} {
  }

  std::chrono::seconds limiter_t::locked_for(const std::string_view source, const clock::time_point now) {
    std::lock_guard lock {mutex_};
    const auto it = entries_.find(std::string {source});
    if (it == entries_.end() || it->second.locked_until <= now) {
      return std::chrono::seconds::zero();
    }
    // Round up so a caller never sees "0 s left" while still locked.
    return std::chrono::ceil<std::chrono::seconds>(it->second.locked_until - now);
  }

  std::chrono::seconds limiter_t::record_failure(const std::string_view source, const std::string_view fingerprint, const clock::time_point now) {
    std::lock_guard lock {mutex_};
    prune(now);

    auto &entry = entries_[std::string {source}];
    if (entry.failures > 0 && !fingerprint.empty() && entry.last_fingerprint == fingerprint) {
      // The same wrong credentials again (a stale browser tab): refuse without escalating.
      entry.last_failure = now;
      return entry.locked_until > now ? std::chrono::ceil<std::chrono::seconds>(entry.locked_until - now) : std::chrono::seconds::zero();
    }

    entry.failures += 1;
    entry.last_fingerprint = std::string {fingerprint};
    entry.last_failure = now;

    if (entry.failures < policy_.free_attempts) {
      return std::chrono::seconds::zero();
    }

    // 30 s, 60 s, 120 s, ... capped at max_lockout.
    const int doublings = std::min(entry.failures - policy_.free_attempts, 20);
    auto lockout = policy_.base_lockout;
    for (int i = 0; i < doublings && lockout < policy_.max_lockout; ++i) {
      lockout *= 2;
    }
    lockout = std::min(lockout, policy_.max_lockout);
    entry.locked_until = now + lockout;
    return lockout;
  }

  void limiter_t::record_success(const std::string_view source) {
    std::lock_guard lock {mutex_};
    entries_.erase(std::string {source});
  }

  int limiter_t::failures(const std::string_view source) {
    std::lock_guard lock {mutex_};
    const auto it = entries_.find(std::string {source});
    return it == entries_.end() ? 0 : it->second.failures;
  }

  void limiter_t::reset() {
    std::lock_guard lock {mutex_};
    entries_.clear();
  }

  void limiter_t::prune(const clock::time_point now) {
    std::erase_if(entries_, [&](const auto &item) {
      const auto &entry = item.second;
      return entry.locked_until <= now && now - entry.last_failure > policy_.forget_after;
    });

    if (entries_.size() < policy_.max_tracked) {
      return;
    }
    // Still full of active sources: drop the stalest half rather than growing without bound.
    std::vector<std::pair<clock::time_point, std::string>> by_age;
    by_age.reserve(entries_.size());
    for (const auto &[key, entry] : entries_) {
      by_age.emplace_back(std::max(entry.last_failure, entry.locked_until), key);
    }
    std::ranges::sort(by_age);
    for (std::size_t i = 0; i < by_age.size() / 2; ++i) {
      entries_.erase(by_age[i].second);
    }
  }

  std::string source_key(const std::string_view address) {
    boost::system::error_code ec;
    const auto parsed = boost::asio::ip::make_address(std::string {address}, ec);
    if (ec || !parsed.is_v6() || parsed.to_v6().is_v4_mapped()) {
      return std::string {address};
    }
    auto bytes = parsed.to_v6().to_bytes();
    std::fill(bytes.begin() + 8, bytes.end(), 0);
    return boost::asio::ip::address_v6 {bytes}.to_string() + "/64";
  }

  limiter_t &web_ui() {
    static limiter_t limiter;
    return limiter;
  }
}  // namespace login_guard
