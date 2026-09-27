/**
 * @file tests/unit/test_stream_stats.cpp
 * @brief Tests for live stream telemetry, 1 Hz samples, session history and their JSON form.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>

// local includes
#include <src/config.h>
#include <src/stream_stats.h>

namespace fs = std::filesystem;
using namespace std::chrono_literals;
using ss_clock = stream_stats::clock;

namespace {
  /**
   * @brief Build a timed frame sample sent at `sent`.
   *
   * @param sent When the last packet left.
   * @param capture_us Capture stage.
   * @param encode_us Encode stage.
   * @param send_us Send stage.
   * @param bytes Encoded size.
   * @return Frame sample.
   */
  stream_stats::frame_sample_t timed_frame(ss_clock::time_point sent, std::uint32_t capture_us, std::uint32_t encode_us, std::uint32_t send_us, std::uint32_t bytes = 1000) {
    const auto encode_done = sent - std::chrono::microseconds {send_us};
    const auto encode_start = encode_done - std::chrono::microseconds {encode_us};
    const auto captured = encode_start - std::chrono::microseconds {capture_us};
    return stream_stats::make_frame_sample(captured, encode_start, encode_done, sent, bytes, 10, 2);
  }

  /**
   * @brief Session info with typical values.
   *
   * @return Info.
   */
  stream_stats::session_info_t sample_info() {
    stream_stats::session_info_t info;
    info.id = 7;
    info.client_cert = "CERT";
    info.client_name = "Phone";
    info.app_name = "Desktop";
    info.started_at = 1'800'000'000;
    info.width = 2400;
    info.height = 1080;
    info.fps_requested = 60;
    info.video_format = 1;
    return info;
  }
}  // namespace

TEST(StreamStatsSummaryTest, AverageAndNearestRankP95) {
  std::vector<std::uint32_t> values;
  for (std::uint32_t i = 1; i <= 100; ++i) {
    values.push_back(i * 1000);  // 1..100 ms
  }
  const auto stat = stream_stats::summarize_us(values);
  EXPECT_DOUBLE_EQ(stat.avg, 50.5);
  EXPECT_DOUBLE_EQ(stat.p95, 95.0);
}

TEST(StreamStatsSummaryTest, EmptyAndSingle) {
  std::vector<std::uint32_t> none;
  EXPECT_DOUBLE_EQ(stream_stats::summarize_us(none).avg, 0.0);
  EXPECT_DOUBLE_EQ(stream_stats::summarize_us(none).p95, 0.0);
  std::vector<std::uint32_t> one {4200};
  const auto stat = stream_stats::summarize_us(one);
  EXPECT_DOUBLE_EQ(stat.avg, 4.2);
  EXPECT_DOUBLE_EQ(stat.p95, 4.2);
}

TEST(StreamStatsFrameSampleTest, TimedWhenAllStagesOrdered) {
  const auto now = ss_clock::now();
  const auto f = timed_frame(now, 1200, 3800, 1000, 5000);
  EXPECT_TRUE(f.timed);
  EXPECT_FALSE(f.duplicate);
  EXPECT_EQ(f.capture_us, 1200u);
  EXPECT_EQ(f.encode_us, 3800u);
  EXPECT_EQ(f.send_us, 1000u);
  EXPECT_EQ(f.bytes, 5000u);
  EXPECT_EQ(f.packets, 10);
  EXPECT_EQ(f.fec_packets, 2);
}

TEST(StreamStatsFrameSampleTest, DuplicateAndOutOfOrderAreUntimed) {
  const auto now = ss_clock::now();
  const auto dup = stream_stats::make_frame_sample(std::nullopt, now - 2ms, now - 1ms, now, 10, 1, 0);
  EXPECT_TRUE(dup.duplicate);
  EXPECT_FALSE(dup.timed);

  const auto missing_start = stream_stats::make_frame_sample(now - 3ms, std::nullopt, now - 1ms, now, 10, 1, 0);
  EXPECT_FALSE(missing_start.duplicate);
  EXPECT_FALSE(missing_start.timed);

  const auto backwards = stream_stats::make_frame_sample(now - 1ms, now - 3ms, now - 2ms, now, 10, 1, 0);
  EXPECT_FALSE(backwards.timed);
}

TEST(StreamStatsFrameSampleTest, ClampsCounters) {
  const auto now = ss_clock::now();
  const auto f = stream_stats::make_frame_sample(std::nullopt, std::nullopt, std::nullopt, now, 10, 70000, 90000);
  EXPECT_EQ(f.packets, UINT16_MAX);
  EXPECT_EQ(f.fec_packets, UINT16_MAX);  // never more parity than packets
}

TEST(StreamStatsSessionTest, SnapshotRatesAndLatencyWindows) {
  const auto t0 = ss_clock::now();
  stream_stats::session_stats_t stats {sample_info(), t0};
  // 60 frames over the last second, 12500 bytes each -> 6000 kbit/s.
  for (int i = 0; i < 60; ++i) {
    stats.record_frame(timed_frame(t0 + 5s + std::chrono::milliseconds {i * 16}, 1000, 4000, 1000, 12500));
  }
  // An old frame outside every window must not count.
  stats.record_frame(timed_frame(t0 + 1s, 90000, 90000, 90000, 999999));

  const auto snap = stats.snapshot(t0 + 5s + 960ms, false);
  EXPECT_DOUBLE_EQ(snap.fps_actual, 60);
  EXPECT_DOUBLE_EQ(snap.bitrate_kbps, 6000.0);
  EXPECT_DOUBLE_EQ(snap.capture.avg, 1.0);
  EXPECT_DOUBLE_EQ(snap.encode.avg, 4.0);
  EXPECT_DOUBLE_EQ(snap.send.avg, 1.0);
  EXPECT_DOUBLE_EQ(snap.total.avg, 6.0);
  EXPECT_DOUBLE_EQ(snap.fec_pct, 20.0);
  EXPECT_EQ(snap.frames_total, 61u);
  EXPECT_FALSE(snap.loss_pct.has_value());
  EXPECT_FALSE(snap.rtt_ms.has_value());
  EXPECT_TRUE(snap.samples.empty());
}

TEST(StreamStatsSessionTest, DuplicatesCountedButUntimed) {
  const auto t0 = ss_clock::now();
  stream_stats::session_stats_t stats {sample_info(), t0};
  stats.record_frame(timed_frame(t0 + 100ms, 1000, 1000, 1000));
  stats.record_frame(stream_stats::make_frame_sample(std::nullopt, std::nullopt, std::nullopt, t0 + 116ms, 200, 1, 0));
  const auto snap = stats.snapshot(t0 + 200ms, false);
  EXPECT_EQ(snap.frames_total, 2u);
  EXPECT_EQ(snap.frames_duplicated, 1u);
  EXPECT_DOUBLE_EQ(snap.total.avg, 3.0);  // only the timed frame
}

TEST(StreamStatsSessionTest, OneHertzSamplesForSparklines) {
  const auto t0 = ss_clock::now();
  stream_stats::session_stats_t stats {sample_info(), t0};
  for (int sec = 0; sec < 3; ++sec) {
    for (int i = 0; i < 30; ++i) {
      stats.record_frame(timed_frame(t0 + std::chrono::seconds {sec} + std::chrono::milliseconds {i * 30}, 500, 500, 1000, 1000));
    }
  }
  // Frame in second 3 closes the first three buckets.
  stats.record_frame(timed_frame(t0 + 3s + 10ms, 500, 500, 1000));
  const auto snap = stats.snapshot(t0 + 3s + 20ms, true);
  ASSERT_EQ(snap.samples.size(), 3u);
  EXPECT_EQ(snap.samples[0].t, sample_info().started_at + 1);
  EXPECT_EQ(snap.samples[2].t, sample_info().started_at + 3);
  EXPECT_DOUBLE_EQ(snap.samples[1].fps, 30);
  EXPECT_DOUBLE_EQ(snap.samples[1].bitrate_kbps, 240.0);  // 30 * 1000 B * 8 / 1000
  EXPECT_DOUBLE_EQ(snap.samples[1].latency_ms, 2.0);
  EXPECT_FALSE(snap.samples[1].loss_pct.has_value());
}

TEST(StreamStatsSessionTest, SampleRingKeepsLastTwoMinutes) {
  const auto t0 = ss_clock::now();
  stream_stats::session_stats_t stats {sample_info(), t0};
  for (int sec = 0; sec <= 200; ++sec) {
    stats.record_frame(timed_frame(t0 + std::chrono::seconds {sec} + 1ms, 100, 100, 100));
  }
  const auto snap = stats.snapshot(t0 + 200s + 2ms, true);
  EXPECT_EQ(snap.samples.size(), stream_stats::SECOND_RING_SIZE);
  EXPECT_LT(snap.samples.front().t, snap.samples.back().t);
}

TEST(StreamStatsSessionTest, LossPercentFromClientReports) {
  const auto t0 = ss_clock::now();
  stream_stats::session_stats_t stats {sample_info(), t0};
  for (int i = 0; i < 9; ++i) {
    stats.record_frame(timed_frame(t0 + std::chrono::milliseconds {i * 100}, 100, 100, 100));  // 10 packets each
  }
  stats.record_loss(10, t0 + 900ms);  // 10 lost of 90 sent + 10 lost
  const auto snap = stats.snapshot(t0 + 950ms, false);
  ASSERT_TRUE(snap.loss_pct.has_value());
  EXPECT_DOUBLE_EQ(*snap.loss_pct, 10.0);

  // Reports older than the loss window stop counting.
  const auto later = stats.snapshot(t0 + 20s, false);
  ASSERT_TRUE(later.loss_pct.has_value());
  EXPECT_DOUBLE_EQ(*later.loss_pct, 0.0);
}

TEST(StreamStatsSessionTest, RttAndFirstEndReasonWins) {
  const auto t0 = ss_clock::now();
  stream_stats::session_stats_t stats {sample_info(), t0};
  stats.set_rtt(12);
  stats.set_end_reason("timeout");
  stats.set_end_reason("host");
  EXPECT_EQ(stats.snapshot(t0, false).rtt_ms.value_or(0), 12u);
  EXPECT_EQ(stats.finish(t0 + 10s, sample_info().started_at + 10).end_reason, "timeout");
}

TEST(StreamStatsSessionTest, FinishAverages) {
  const auto t0 = ss_clock::now();
  stream_stats::session_stats_t stats {sample_info(), t0};
  for (int i = 0; i < 100; ++i) {
    stats.record_frame(timed_frame(t0 + std::chrono::milliseconds {i * 100}, 1000, 2000, 1000, 1250));
  }
  const auto e = stats.finish(t0 + 10s, sample_info().started_at + 10);
  EXPECT_EQ(e.duration_s, 10);
  EXPECT_DOUBLE_EQ(e.avg_fps, 10.0);
  EXPECT_DOUBLE_EQ(e.avg_bitrate_kbps, 100.0);  // 100 * 1250 B * 8 / 1000 / 10 s
  ASSERT_TRUE(e.avg_latency_ms.has_value());
  EXPECT_DOUBLE_EQ(*e.avg_latency_ms, 4.0);
  EXPECT_EQ(e.end_reason, "ended");
}

TEST(StreamStatsJsonTest, SnapshotApiShape) {
  const auto t0 = ss_clock::now();
  stream_stats::session_stats_t stats {sample_info(), t0};
  stats.record_frame(timed_frame(t0 + 100ms, 1000, 2000, 1000));
  stats.record_frame(timed_frame(t0 + 1100ms, 1000, 2000, 1000));
  const auto snap = stats.snapshot(t0 + 1200ms, true);

  const auto j = stream_stats::snapshot_to_api_json(snap, {"uuid-1", "Pixel"}, true);
  EXPECT_EQ(j["id"], 7);
  EXPECT_EQ(j["client_uuid"], "uuid-1");
  EXPECT_EQ(j["client_name"], "Pixel");
  EXPECT_EQ(j["codec"], "HEVC");
  EXPECT_EQ(j["resolution"]["w"], 2400);
  EXPECT_EQ(j["resolution"]["h"], 1080);
  EXPECT_EQ(j["fps_requested"], 60);
  for (const auto *key : {"capture", "encode", "send", "total"}) {
    EXPECT_TRUE(j["latency_ms"].contains(key)) << key;
    EXPECT_TRUE(j["latency_ms"]["p95"].contains(key)) << key;
  }
  EXPECT_TRUE(j["loss_pct"].is_null());
  EXPECT_TRUE(j["rtt_ms"].is_null());
  ASSERT_TRUE(j["samples"].is_array());
  ASSERT_EQ(j["samples"].size(), 1u);
  EXPECT_TRUE(j["samples"][0].contains("bitrate_kbps"));
  EXPECT_FALSE(j.contains("client_cert"));

  const auto no_samples = stream_stats::snapshot_to_api_json(snap, {}, false);
  EXPECT_FALSE(no_samples.contains("samples"));
  EXPECT_TRUE(no_samples["client_uuid"].is_null());
  EXPECT_EQ(no_samples["client_name"], "Phone");  // falls back to the launch name
}

TEST(StreamStatsJsonTest, CodecNames) {
  EXPECT_EQ(stream_stats::codec_name(0), "H.264");
  EXPECT_EQ(stream_stats::codec_name(1), "HEVC");
  EXPECT_EQ(stream_stats::codec_name(2), "AV1");
}

/**
 * @brief Isolate session-history tests from the user's state directory.
 */
class StreamStatsHistoryTest: public BaseTest {
protected:
  /**
   * @brief Point the state file at a temp directory.
   */
  void SetUp() override {
    BaseTest::SetUp();
    original_state_file = config::nvhttp.file_state;
    original_fresh_state = config::sunshine.flags[config::flag::FRESH_STATE];
    dir = fs::temp_directory_path() / fs::path {"nova_stream_stats_test"};
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    config::nvhttp.file_state = (dir / "state.json").string();
    config::sunshine.flags[config::flag::FRESH_STATE] = false;
    stream_stats::test_support::reset();
  }

  /**
   * @brief Restore configuration and remove temp files.
   */
  void TearDown() override {
    stream_stats::test_support::reset();
    std::error_code ec;
    fs::remove_all(dir, ec);
    config::nvhttp.file_state = original_state_file;
    config::sunshine.flags[config::flag::FRESH_STATE] = original_fresh_state;
    BaseTest::TearDown();
  }

  fs::path dir;  ///< Temp state directory.

private:
  std::string original_state_file;  ///< State file before the test.
  bool original_fresh_state = false;  ///< FRESH_STATE flag before the test.
};

TEST_F(StreamStatsHistoryTest, MissingOrCorruptFileYieldsEmpty) {
  EXPECT_TRUE(stream_stats::load_history(dir / "none.json").empty());
  {
    std::ofstream out {dir / "bad.json"};
    out << "{ not json";
  }
  EXPECT_TRUE(stream_stats::load_history(dir / "bad.json").empty());
  {
    std::ofstream out {dir / "wrong.json"};
    out << R"({"sessions":[{"app_name":"x"},{"started_at":"soon"}]})";
  }
  EXPECT_TRUE(stream_stats::load_history(dir / "wrong.json").empty());
}

TEST_F(StreamStatsHistoryTest, SaveAndLoadRoundTrip) {
  stream_stats::history_entry_t e;
  e.id = 3;
  e.client_cert = "CERT";
  e.client_name = "Tablet";
  e.app_name = "Far Cry 5";
  e.started_at = 1'800'000'100;
  e.duration_s = 3600;
  e.video_format = 1;
  e.width = 1920;
  e.height = 1080;
  e.avg_fps = 59.9;
  e.avg_bitrate_kbps = 30000;
  e.avg_latency_ms = 6.2;
  e.end_reason = "client";
  const auto path = dir / "history.json";
  ASSERT_TRUE(stream_stats::save_history(path, {e}));
  EXPECT_FALSE(fs::exists(dir / "history.json.tmp"));

  const auto loaded = stream_stats::load_history(path);
  ASSERT_EQ(loaded.size(), 1u);
  EXPECT_EQ(loaded[0].app_name, "Far Cry 5");
  EXPECT_EQ(loaded[0].client_cert, "CERT");
  EXPECT_EQ(loaded[0].duration_s, 3600);
  EXPECT_DOUBLE_EQ(loaded[0].avg_latency_ms.value_or(0), 6.2);
  EXPECT_EQ(loaded[0].end_reason, "client");

  const auto api = stream_stats::history_to_api_json(loaded[0], {"u", "Tablet 2"});
  EXPECT_FALSE(api.contains("client_cert"));
  EXPECT_EQ(api["client_name"], "Tablet 2");
  EXPECT_EQ(api["codec"], "HEVC");
}

TEST_F(StreamStatsHistoryTest, RegistryLifecycleAndLimit) {
  EXPECT_TRUE(stream_stats::active_sessions(false).empty());

  auto info = sample_info();
  auto stats = stream_stats::start_session(info);
  ASSERT_EQ(stream_stats::active_sessions(false).size(), 1u);
  EXPECT_EQ(stream_stats::active_sessions(false)[0].info.app_name, "Desktop");

  stream_stats::end_session(stats);
  stream_stats::end_session(stats);  // a second join must not duplicate the entry
  EXPECT_TRUE(stream_stats::active_sessions(false).empty());
  ASSERT_EQ(stream_stats::history().size(), 1u);
  EXPECT_TRUE(fs::exists(stream_stats::history_path()));

  for (int i = 0; i < 60; ++i) {
    info.id = 100 + i;
    stream_stats::end_session(stream_stats::start_session(info));
  }
  const auto hist = stream_stats::history();
  ASSERT_EQ(hist.size(), stream_stats::HISTORY_LIMIT);
  EXPECT_EQ(hist.front().id, 159u);  // newest first

  // A fresh process reads the persisted file.
  stream_stats::test_support::reset();
  EXPECT_EQ(stream_stats::history().size(), stream_stats::HISTORY_LIMIT);
}

TEST_F(StreamStatsHistoryTest, FreshStateDoesNotWrite) {
  config::sunshine.flags[config::flag::FRESH_STATE] = true;
  stream_stats::end_session(stream_stats::start_session(sample_info()));
  EXPECT_FALSE(fs::exists(stream_stats::history_path()));
  EXPECT_EQ(stream_stats::history().size(), 1u);
}
