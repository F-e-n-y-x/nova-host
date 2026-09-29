/**
 * @file src/network_probe.cpp
 * @brief Definitions for the paired-client network probe (`/api/network/capabilities`, `/api/network/probe`).
 */
// class header include
#include "network_probe.h"

// standard includes
#include <algorithm>
#include <limits>

// lib includes
#include <openssl/rand.h>

#ifdef __linux__
  // local includes
  #include "platform/linux/tcp_stats.h"
#endif

namespace network_probe {
  namespace {
    /**
     * @brief Drop entries of a timed deque older than the quota window.
     *
     * @tparam T Entry payload.
     * @param entries Oldest-first entries.
     * @param now Current time.
     */
    template<typename T>
    void prune(std::deque<std::pair<clock::time_point, T>> &entries, clock::time_point now) {
      while (!entries.empty() && now - entries.front().first >= QUOTA_WINDOW) {
        entries.pop_front();
      }
    }

    /**
     * @brief Milliseconds from @p now until @p then, at least 1.
     *
     * @param then Future time.
     * @param now Current time.
     * @return Retry hint.
     */
    std::uint32_t ms_until(clock::time_point then, clock::time_point now) {
      const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(then - now).count();
      return static_cast<std::uint32_t>(std::clamp<std::int64_t>(ms, 1, std::numeric_limits<std::uint32_t>::max()));
    }

    /**
     * @brief Subtract counters, 0 when the later value is smaller (socket reused or wrapped).
     *
     * @param before Earlier value.
     * @param after Later value.
     * @return Difference.
     */
    std::uint64_t grew(std::uint64_t before, std::uint64_t after) {
      return after >= before ? after - before : 0;
    }
  }  // namespace

  bool valid_nonce(std::string_view nonce) {
    if (nonce.empty() || nonce.size() > 64) {
      return false;
    }
    return std::ranges::all_of(nonce, [](unsigned char c) {
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
    });
  }

  std::optional<std::size_t> parse_bytes(std::string_view value) {
    if (value.empty() || value.size() > 12) {
      return std::nullopt;
    }
    std::size_t parsed = 0;
    for (const char c : value) {
      if (c < '0' || c > '9') {
        return std::nullopt;
      }
      parsed = parsed * 10 + static_cast<std::size_t>(c - '0');
    }
    if (parsed < MIN_BYTES || parsed > MAX_BYTES) {
      return std::nullopt;
    }
    return parsed;
  }

  nlohmann::json capabilities_json() {
    return {
      {"version", 1},
      {"features", {"bandwidth-probe-v1", "tcp-loss-v1"}},
      {"bandwidthProbe",
       {
         {"version", 1},
         {"endpoint", "/api/network/probe"},
         {"minBytes", MIN_BYTES},
         {"maxBytes", MAX_BYTES},
         {"cooldownMs", COOLDOWN_MS},
         {"resultEndpoint", "/api/network/probe/result"},
       }},
    };
  }

  nlohmann::json error_json(std::string_view error, std::uint32_t retry_after_ms) {
    nlohmann::json body {{"error", std::string {error}}};
    if (retry_after_ms > 0) {
      body["retryAfterMs"] = retry_after_ms;
    }
    return body;
  }

  double loss_pct(const tcp_sample_t &sample) {
    if (sample.bytes_sent > 0) {
      return std::min(100.0, 100.0 * static_cast<double>(sample.bytes_retrans) / static_cast<double>(sample.bytes_sent));
    }
    if (sample.segments_out > 0) {
      return std::min(100.0, 100.0 * static_cast<double>(sample.retransmits) / static_cast<double>(sample.segments_out));
    }
    return 0.0;
  }

  nlohmann::json result_json(const result_t &result) {
    nlohmann::json body {
      {"nonce", result.nonce},
      {"requestedBytes", result.requested_bytes},
      {"sentBytes", result.sent_bytes},
      {"durationMs", result.duration_ms},
      {"result", result.outcome},
    };
    if (result.tcp) {
      body["lossPct"] = loss_pct(*result.tcp);
      body["retransmits"] = result.tcp->retransmits;
      body["segmentsOut"] = result.tcp->segments_out;
      body["tcpRttMs"] = result.tcp->rtt_ms;
      body["tcpRttVarMs"] = result.tcp->rtt_var_ms;
    }
    return body;
  }

  admission_t limiter_t::admit(const std::string &client, const std::string &nonce, std::size_t bytes, clock::time_point now) {
    std::lock_guard lg {mutex_};
    auto &state = clients_[client];
    prune(state.charges, now);
    prune(state.used_nonces, now);

    if (state.in_flight_id != 0) {
      return {"busy", COOLDOWN_MS};
    }
    if (in_flight_ >= GLOBAL_CONCURRENCY) {
      return {"busy", COOLDOWN_MS};
    }
    if (std::ranges::any_of(state.used_nonces, [&](const auto &used) {
          return used.second == nonce;
        })) {
      return {"nonce_reused", COOLDOWN_MS};
    }
    if (now < state.cooldown_until) {
      return {"rate_limited", ms_until(state.cooldown_until, now)};
    }
    std::size_t charged = 0;
    for (const auto &[at, n] : state.charges) {
      charged += n;
    }
    if (charged + bytes > CLIENT_QUOTA_BYTES) {
      return {"rate_limited", ms_until(state.charges.front().first + QUOTA_WINDOW, now)};
    }

    state.charges.emplace_back(now, bytes);
    state.used_nonces.emplace_back(now, nonce);
    state.in_flight_id = next_id_++;
    state.in_flight_nonce = nonce;
    ++in_flight_;
    return {{}, 0, state.in_flight_id};
  }

  void limiter_t::complete(const std::string &client, std::uint64_t id, result_t result, clock::time_point now) {
    std::lock_guard lg {mutex_};
    const auto it = clients_.find(client);
    if (it == clients_.end() || it->second.in_flight_id != id || id == 0) {
      return;
    }
    auto &state = it->second;
    state.in_flight_id = 0;
    state.in_flight_nonce.clear();
    state.cooldown_until = now + std::chrono::milliseconds {COOLDOWN_MS};
    if (in_flight_ > 0) {
      --in_flight_;
    }
    state.results.push_back(std::move(result));
    while (state.results.size() > RESULTS_KEPT) {
      state.results.pop_front();
    }
    // Forget devices with nothing left to remember, so the map stays small.
    std::erase_if(clients_, [&](const auto &entry) {
      const auto &c = entry.second;
      return c.in_flight_id == 0 && c.results.empty() && c.charges.empty() && c.used_nonces.empty() && now >= c.cooldown_until;
    });
  }

  std::optional<result_t> limiter_t::result(const std::string &client, const std::string &nonce) const {
    std::lock_guard lg {mutex_};
    const auto it = clients_.find(client);
    if (it == clients_.end()) {
      return std::nullopt;
    }
    for (auto r = it->second.results.rbegin(); r != it->second.results.rend(); ++r) {
      if (r->nonce == nonce) {
        return *r;
      }
    }
    return std::nullopt;
  }

  bool limiter_t::running(const std::string &client, const std::string &nonce) const {
    std::lock_guard lg {mutex_};
    const auto it = clients_.find(client);
    return it != clients_.end() && it->second.in_flight_id != 0 && it->second.in_flight_nonce == nonce;
  }

  const std::string &payload_chunk() {
    static const std::string chunk = [] {
      std::string data(CHUNK_BYTES, '\0');
      if (RAND_bytes(reinterpret_cast<unsigned char *>(data.data()), static_cast<int>(data.size())) != 1) {
        // Incompressible enough without OpenSSL's RNG: xorshift bytes.
        std::uint32_t x = 0x9e3779b9U;
        for (auto &byte : data) {
          x ^= x << 13;
          x ^= x >> 17;
          x ^= x << 5;
          byte = static_cast<char>(x & 0xffU);
        }
      }
      return data;
    }();
    return chunk;
  }

  std::optional<tcp_sample_t> sample_tcp(const boost::asio::ip::tcp::endpoint &local, const boost::asio::ip::tcp::endpoint &remote) {
#ifdef __linux__
    const auto counters = tcp_stats::find(local.data(), local.size(), remote.data(), remote.size());
    if (!counters) {
      return std::nullopt;
    }
    return tcp_sample_t {
      .bytes_sent = counters->bytes_sent,
      .bytes_retrans = counters->bytes_retrans,
      .segments_out = counters->segments_out,
      .retransmits = counters->retransmits,
      .rtt_ms = counters->rtt_us / 1000.0,
      .rtt_var_ms = counters->rtt_var_us / 1000.0,
    };
#else
    (void) local;
    (void) remote;
    return std::nullopt;
#endif
  }

  tcp_sample_t delta(const tcp_sample_t &before, const tcp_sample_t &after) {
    return {
      .bytes_sent = grew(before.bytes_sent, after.bytes_sent),
      .bytes_retrans = grew(before.bytes_retrans, after.bytes_retrans),
      .segments_out = grew(before.segments_out, after.segments_out),
      .retransmits = grew(before.retransmits, after.retransmits),
      .rtt_ms = after.rtt_ms,
      .rtt_var_ms = after.rtt_var_ms,
    };
  }
}  // namespace network_probe
