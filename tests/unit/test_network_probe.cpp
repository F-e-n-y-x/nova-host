/**
 * @file tests/unit/test_network_probe.cpp
 * @brief Tests for the network probe: request parsing, capabilities, rate limiting, results and TCP counters.
 */
// test includes
#include "../tests_common.h"

// standard includes
#include <chrono>
#include <string>

// lib includes
#include <boost/asio.hpp>

// local includes
#include <src/network_probe.h>

using namespace std::chrono_literals;

namespace {
  const network_probe::clock::time_point T0 = network_probe::clock::time_point {} + 1000s;  ///< Arbitrary start time.

  /**
   * @brief A finished result for a nonce.
   * @param nonce Burst nonce.
   * @return Result.
   */
  network_probe::result_t done(const std::string &nonce) {
    network_probe::result_t r;
    r.nonce = nonce;
    r.requested_bytes = network_probe::MAX_BYTES;
    r.sent_bytes = network_probe::MAX_BYTES;
    r.duration_ms = 120;
    r.outcome = "completed";
    return r;
  }
}  // namespace

TEST(NetworkProbeTest, ValidatesNonces) {
  EXPECT_TRUE(network_probe::valid_nonce("0f8fad5b-d9cb-469f-a165-70867728950e"));
  EXPECT_TRUE(network_probe::valid_nonce("a.b_c-D"));
  EXPECT_FALSE(network_probe::valid_nonce(""));
  EXPECT_FALSE(network_probe::valid_nonce(std::string(65, 'a')));
  EXPECT_TRUE(network_probe::valid_nonce(std::string(64, 'a')));
  EXPECT_FALSE(network_probe::valid_nonce("has space"));
  EXPECT_FALSE(network_probe::valid_nonce("semi;colon"));
  EXPECT_FALSE(network_probe::valid_nonce("../etc"));
}

TEST(NetworkProbeTest, ParsesByteCounts) {
  EXPECT_EQ(network_probe::parse_bytes("65536"), network_probe::MIN_BYTES);
  EXPECT_EQ(network_probe::parse_bytes("4194304"), network_probe::MAX_BYTES);
  EXPECT_EQ(network_probe::parse_bytes("1000000"), 1000000U);
  EXPECT_FALSE(network_probe::parse_bytes("65535"));
  EXPECT_FALSE(network_probe::parse_bytes("4194305"));
  EXPECT_FALSE(network_probe::parse_bytes(""));
  EXPECT_FALSE(network_probe::parse_bytes("-1"));
  EXPECT_FALSE(network_probe::parse_bytes("1e6"));
  EXPECT_FALSE(network_probe::parse_bytes(" 100000"));
  EXPECT_FALSE(network_probe::parse_bytes("99999999999999999999999"));
}

TEST(NetworkProbeTest, CapabilitiesMatchWhatVplusAccepts) {
  // NvHTTP.parseNetworkProbeCapabilities: version >= 1, bandwidthProbe.version >= 1,
  // endpoint == "/api/network/probe", 0 < minBytes <= maxBytes, and minBytes <= 4 MiB.
  const auto caps = network_probe::capabilities_json();
  EXPECT_GE(caps["version"].get<int>(), 1);
  const auto &probe = caps["bandwidthProbe"];
  EXPECT_GE(probe["version"].get<int>(), 1);
  EXPECT_EQ(probe["endpoint"], "/api/network/probe");
  EXPECT_GT(probe["minBytes"].get<long long>(), 0);
  EXPECT_LE(probe["minBytes"].get<long long>(), probe["maxBytes"].get<long long>());
  EXPECT_LE(probe["maxBytes"].get<long long>(), 4LL * 1024 * 1024);
  EXPECT_EQ(probe["cooldownMs"], network_probe::COOLDOWN_MS);
  EXPECT_EQ(probe["resultEndpoint"], "/api/network/probe/result");
  EXPECT_NE(std::ranges::find(caps["features"], "bandwidth-probe-v1"), caps["features"].end());
  EXPECT_NE(std::ranges::find(caps["features"], "tcp-loss-v1"), caps["features"].end());
}

TEST(NetworkProbeTest, ErrorBodies) {
  EXPECT_EQ(network_probe::error_json("not_paired"), nlohmann::json({{"error", "not_paired"}}));
  EXPECT_EQ(network_probe::error_json("rate_limited", 1500)["retryAfterMs"], 1500);
}

TEST(NetworkProbeLimiterTest, OneBurstAtATimeThenCooldown) {
  network_probe::limiter_t limiter;
  const auto first = limiter.admit("dev", "n1", network_probe::MAX_BYTES, T0);
  ASSERT_TRUE(first);
  EXPECT_TRUE(limiter.running("dev", "n1"));
  EXPECT_FALSE(limiter.result("dev", "n1"));

  const auto busy = limiter.admit("dev", "n2", network_probe::MAX_BYTES, T0 + 100ms);
  EXPECT_FALSE(busy);
  EXPECT_EQ(busy.error, "busy");

  limiter.complete("dev", first.id, done("n1"), T0 + 1s);
  EXPECT_FALSE(limiter.running("dev", "n1"));
  const auto cooling = limiter.admit("dev", "n2", network_probe::MAX_BYTES, T0 + 2s);
  EXPECT_FALSE(cooling);
  EXPECT_EQ(cooling.error, "rate_limited");
  EXPECT_EQ(cooling.retry_after_ms, 4000U);

  EXPECT_TRUE(limiter.admit("dev", "n2", network_probe::MAX_BYTES, T0 + 6s));
}

TEST(NetworkProbeLimiterTest, NoncesAreOneTime) {
  network_probe::limiter_t limiter;
  const auto a = limiter.admit("dev", "same", network_probe::MIN_BYTES, T0);
  ASSERT_TRUE(a);
  limiter.complete("dev", a.id, done("same"), T0 + 1s);
  const auto again = limiter.admit("dev", "same", network_probe::MIN_BYTES, T0 + 10s);
  EXPECT_FALSE(again);
  EXPECT_EQ(again.error, "nonce_reused");
}

TEST(NetworkProbeLimiterTest, PerDeviceQuota) {
  network_probe::limiter_t limiter;
  auto t = T0;
  // 16 MiB per minute: four 4 MiB bursts fit, the fifth waits for the window.
  for (int i = 0; i < 4; ++i) {
    const auto a = limiter.admit("dev", "q" + std::to_string(i), network_probe::MAX_BYTES, t);
    ASSERT_TRUE(a) << i << " " << a.error;
    limiter.complete("dev", a.id, done("q" + std::to_string(i)), t + 1s);
    t += 6s;
  }
  const auto over = limiter.admit("dev", "q4", network_probe::MAX_BYTES, t);
  EXPECT_FALSE(over);
  EXPECT_EQ(over.error, "rate_limited");
  EXPECT_GT(over.retry_after_ms, 0U);
  EXPECT_TRUE(limiter.admit("dev", "q5", network_probe::MAX_BYTES, T0 + 61s));
  // Another device has its own quota.
  EXPECT_TRUE(limiter.admit("other", "q0", network_probe::MAX_BYTES, t));
}

TEST(NetworkProbeLimiterTest, GlobalConcurrency) {
  network_probe::limiter_t limiter;
  for (std::size_t i = 0; i < network_probe::GLOBAL_CONCURRENCY; ++i) {
    ASSERT_TRUE(limiter.admit("dev" + std::to_string(i), "n", network_probe::MIN_BYTES, T0));
  }
  const auto full = limiter.admit("late", "n", network_probe::MIN_BYTES, T0);
  EXPECT_FALSE(full);
  EXPECT_EQ(full.error, "busy");
}

TEST(NetworkProbeLimiterTest, KeepsResultsAndIgnoresStaleCompletions) {
  network_probe::limiter_t limiter;
  const auto a = limiter.admit("dev", "r1", network_probe::MIN_BYTES, T0);
  ASSERT_TRUE(a);
  limiter.complete("dev", a.id + 99, done("wrong"), T0 + 1s);  // unknown id: ignored
  EXPECT_TRUE(limiter.running("dev", "r1"));
  limiter.complete("dev", a.id, done("r1"), T0 + 1s);
  limiter.complete("dev", a.id, done("dup"), T0 + 1s);  // second completion: ignored
  const auto r = limiter.result("dev", "r1");
  ASSERT_TRUE(r);
  EXPECT_EQ(r->outcome, "completed");
  EXPECT_FALSE(limiter.result("dev", "dup"));
  EXPECT_FALSE(limiter.result("other", "r1"));
  limiter.complete("nobody", 1, done("x"), T0);  // unknown device: ignored
}

TEST(NetworkProbeResultTest, LossFromTcpCounters) {
  network_probe::tcp_sample_t s;
  EXPECT_EQ(network_probe::loss_pct(s), 0.0);
  s.segments_out = 1000;
  s.retransmits = 5;
  EXPECT_DOUBLE_EQ(network_probe::loss_pct(s), 0.5);
  // Byte counters are preferred when the kernel has them.
  s.bytes_sent = 4'000'000;
  s.bytes_retrans = 40'000;
  EXPECT_DOUBLE_EQ(network_probe::loss_pct(s), 1.0);
  s.bytes_retrans = 8'000'000;
  EXPECT_DOUBLE_EQ(network_probe::loss_pct(s), 100.0);
}

TEST(NetworkProbeResultTest, DeltaAndJson) {
  network_probe::tcp_sample_t before {.bytes_sent = 100, .bytes_retrans = 0, .segments_out = 10, .retransmits = 1, .rtt_ms = 9, .rtt_var_ms = 2};
  network_probe::tcp_sample_t after {.bytes_sent = 1100, .bytes_retrans = 20, .segments_out = 110, .retransmits = 3, .rtt_ms = 11.5, .rtt_var_ms = 1.25};
  const auto d = network_probe::delta(before, after);
  EXPECT_EQ(d.bytes_sent, 1000U);
  EXPECT_EQ(d.bytes_retrans, 20U);
  EXPECT_EQ(d.segments_out, 100U);
  EXPECT_EQ(d.retransmits, 2U);
  EXPECT_DOUBLE_EQ(d.rtt_ms, 11.5);
  // A counter going backwards (socket reuse) reads as 0, never as a huge number.
  EXPECT_EQ(network_probe::delta(after, before).bytes_sent, 0U);

  auto r = done("abc");
  auto j = network_probe::result_json(r);
  EXPECT_EQ(j["nonce"], "abc");
  EXPECT_EQ(j["sentBytes"], network_probe::MAX_BYTES);
  EXPECT_EQ(j["durationMs"], 120);
  EXPECT_EQ(j["result"], "completed");
  EXPECT_FALSE(j.contains("lossPct"));
  r.tcp = d;
  j = network_probe::result_json(r);
  EXPECT_DOUBLE_EQ(j["lossPct"].get<double>(), 2.0);
  EXPECT_EQ(j["retransmits"], 2);
  EXPECT_DOUBLE_EQ(j["tcpRttMs"].get<double>(), 11.5);
}

TEST(NetworkProbeResultTest, PayloadIsARandomChunk) {
  const auto &chunk = network_probe::payload_chunk();
  EXPECT_EQ(chunk.size(), network_probe::CHUNK_BYTES);
  EXPECT_EQ(&chunk, &network_probe::payload_chunk());
  // Not a constant fill: the transfer must not compress.
  EXPECT_NE(chunk.find_first_not_of(chunk[0]), std::string::npos);
}

#ifdef __linux__
TEST(NetworkProbeTcpTest, SamplesALoopbackConnection) {
  namespace asio = boost::asio;
  asio::io_context io;
  asio::ip::tcp::acceptor acceptor {io, asio::ip::tcp::endpoint {asio::ip::make_address("127.0.0.1"), 0}};
  asio::ip::tcp::socket client {io};
  client.connect(acceptor.local_endpoint());
  asio::ip::tcp::socket server = acceptor.accept();

  const auto before = network_probe::sample_tcp(server.local_endpoint(), server.remote_endpoint());
  ASSERT_TRUE(before);
  const std::string data(256 * 1024, 'x');
  asio::write(server, asio::buffer(data));
  std::string sink(data.size(), '\0');
  asio::read(client, asio::buffer(sink));
  const auto after = network_probe::sample_tcp(server.local_endpoint(), server.remote_endpoint());
  ASSERT_TRUE(after);
  const auto d = network_probe::delta(*before, *after);
  EXPECT_GE(d.bytes_sent, data.size());
  EXPECT_GT(d.segments_out, 0U);
  EXPECT_LT(network_probe::loss_pct(d), 1.0);

  // An endpoint pair this process doesn't own is not found.
  EXPECT_FALSE(network_probe::sample_tcp(asio::ip::tcp::endpoint {asio::ip::make_address("127.0.0.1"), 1}, server.remote_endpoint()));
}
#endif
