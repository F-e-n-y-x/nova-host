/**
 * @file tests/unit/test_abr.cpp
 * @brief Tests for the rule-based adaptive bitrate controller, its range presets, parsing and registry.
 */
// test includes
#include "../tests_common.h"

// standard includes
#include <chrono>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <vector>

// local includes
#include <src/abr.h>

using namespace std::chrono_literals;

namespace {
  const abr::clock::time_point T0 = abr::clock::time_point {} + 1000s;  ///< Arbitrary start time.

  /**
   * @brief A report with only loss (and optionally RTT) set.
   *
   * @param loss Packet loss in percent.
   * @param rtt RTT in ms (0 = unknown).
   * @return Feedback.
   */
  abr::feedback_t report(double loss, double rtt = 0) {
    abr::feedback_t f;
    f.packet_loss_pct = loss;
    f.rtt_ms = rtt;
    return f;
  }

  /**
   * @brief Feed clean reports once a second from @p from until the controller steps.
   *
   * @param c Controller.
   * @param from First report time.
   * @param max_reports Give up after this many.
   * @return Time and bitrate of the first step, or nullopt.
   */
  std::optional<std::pair<abr::clock::time_point, int>> first_step(abr::controller_t &c, abr::clock::time_point from, int max_reports = 120) {
    for (int i = 0; i < max_reports; ++i) {
      const auto t = from + std::chrono::seconds {i};
      if (const auto d = c.update(report(0.0), t); d.new_bitrate_kbps > 0) {
        return std::make_pair(t, d.new_bitrate_kbps);
      }
    }
    return std::nullopt;
  }

  constexpr abr::range_t RANGE {6000, 40000};  ///< Range used by most controller tests.
}  // namespace

TEST(AbrModeTest, ParsesFoundationNamesAndNebulaAliases) {
  EXPECT_EQ(abr::parse_mode("quality"), abr::mode_e::quality);
  EXPECT_EQ(abr::parse_mode("aggressive"), abr::mode_e::quality);
  EXPECT_EQ(abr::parse_mode("balanced"), abr::mode_e::balanced);
  EXPECT_EQ(abr::parse_mode("lowLatency"), abr::mode_e::low_latency);
  EXPECT_EQ(abr::parse_mode("low_latency"), abr::mode_e::low_latency);
  EXPECT_EQ(abr::parse_mode("conservative"), abr::mode_e::low_latency);
  EXPECT_FALSE(abr::parse_mode("fast"));
  EXPECT_FALSE(abr::parse_mode(""));
  EXPECT_EQ(abr::mode_name(abr::mode_e::quality), "quality");
  EXPECT_EQ(abr::mode_name(abr::mode_e::balanced), "balanced");
  EXPECT_EQ(abr::mode_name(abr::mode_e::low_latency), "lowLatency");
}

TEST(AbrRangeTest, PresetsSetTheFloorAndTheUserBitrateIsTheCap) {
  // Floors are Foundation's presets; the ceiling is always the bitrate the stream runs at.
  const auto balanced = abr::resolve_range(abr::mode_e::balanced, 20000, 0, 0, 0, 0);
  EXPECT_EQ(balanced.min_kbps, 6000);
  EXPECT_EQ(balanced.max_kbps, 20000);
  const auto quality = abr::resolve_range(abr::mode_e::quality, 20000, 0, 0, 0, 0);
  EXPECT_EQ(quality.min_kbps, 10000);
  EXPECT_EQ(quality.max_kbps, 20000);
  const auto low = abr::resolve_range(abr::mode_e::low_latency, 20000, 0, 0, 0, 0);
  EXPECT_EQ(low.min_kbps, 2000);
  EXPECT_EQ(low.max_kbps, 20000);
  EXPECT_EQ(abr::resolve_range(abr::mode_e::balanced, 5000, 0, 0, 0, 0).min_kbps, 3000);
  EXPECT_EQ(abr::resolve_range(abr::mode_e::balanced, 5000, 0, 0, 0, 0).max_kbps, 5000);
  EXPECT_EQ(abr::resolve_range(abr::mode_e::balanced, 300000, 0, 0, 0, 0).max_kbps, 300000);
}

TEST(AbrRangeTest, ClientBoundsHostCapAndFloor) {
  const auto requested = abr::resolve_range(abr::mode_e::balanced, 20000, 8000, 15000, 0, 0);
  EXPECT_EQ(requested.min_kbps, 8000);
  EXPECT_EQ(requested.max_kbps, 15000);
  // A client maximum above the stream bitrate never raises the cap.
  EXPECT_EQ(abr::resolve_range(abr::mode_e::balanced, 20000, 8000, 50000, 0, 0).max_kbps, 20000);
  // Only the missing bound comes from the preset.
  EXPECT_EQ(abr::resolve_range(abr::mode_e::balanced, 20000, 0, 15000, 0, 0).min_kbps, 6000);
  // The host cap always wins.
  EXPECT_EQ(abr::resolve_range(abr::mode_e::balanced, 20000, 0, 50000, 0, 12000).max_kbps, 12000);
  // The host floor raises the minimum.
  EXPECT_EQ(abr::resolve_range(abr::mode_e::low_latency, 20000, 0, 0, 8000, 0).min_kbps, 8000);
  // Never inverted: a minimum above the cap collapses onto it.
  const auto inverted = abr::resolve_range(abr::mode_e::balanced, 20000, 50000, 0, 0, 25000);
  EXPECT_EQ(inverted.min_kbps, 20000);
  EXPECT_EQ(inverted.max_kbps, 20000);
  // Never below the absolute floor.
  const auto tiny = abr::resolve_range(abr::mode_e::low_latency, 0, 100, 200, 0, 0);
  EXPECT_EQ(tiny.min_kbps, abr::FLOOR_KBPS);
  EXPECT_EQ(tiny.max_kbps, abr::FLOOR_KBPS);
  EXPECT_LE(abr::resolve_range(abr::mode_e::balanced, 5'000'000, 0, 0, 0, 0).max_kbps, abr::CEILING_KBPS);
}

TEST(AbrControllerTest, StartsInsideTheRange) {
  EXPECT_EQ(abr::controller_t(abr::mode_e::balanced, RANGE, 50000, T0).current_kbps(), 40000);
  EXPECT_EQ(abr::controller_t(abr::mode_e::balanced, RANGE, 1000, T0).current_kbps(), 6000);
  EXPECT_EQ(abr::controller_t(abr::mode_e::balanced, RANGE, 20000, T0).current_kbps(), 20000);
}

TEST(AbrControllerTest, EmergencyLossDropsAtOnceButSpaced) {
  abr::controller_t c {abr::mode_e::balanced, RANGE, 20000, T0};
  auto d = c.update(report(6.0), T0);
  EXPECT_EQ(d.new_bitrate_kbps, 14000);
  EXPECT_EQ(d.reason, "loss 6.0% (emergency)");
  // The loss figure lags the drop: a report 1 s later must not compound it.
  EXPECT_EQ(c.update(report(6.0), T0 + 1s).new_bitrate_kbps, 0);
  EXPECT_EQ(c.update(report(6.0), T0 + 2s).new_bitrate_kbps, 9800);
  EXPECT_EQ(c.update(report(6.0), T0 + 4s).new_bitrate_kbps, 6860);
  // Clamped to the minimum, then nothing left to drop.
  EXPECT_EQ(c.update(report(6.0), T0 + 6s).new_bitrate_kbps, 6000);
  EXPECT_EQ(c.update(report(6.0), T0 + 8s).new_bitrate_kbps, 0);
  EXPECT_EQ(c.current_kbps(), 6000);
  EXPECT_EQ(c.changes(), 4);
}

TEST(AbrControllerTest, ModerateLossMustBeSustained) {
  abr::controller_t c {abr::mode_e::balanced, RANGE, 20000, T0};
  EXPECT_EQ(c.update(report(3.0), T0).new_bitrate_kbps, 0);
  const auto d = c.update(report(3.0), T0 + 1s);
  EXPECT_EQ(d.new_bitrate_kbps, 18000);
  EXPECT_EQ(d.reason, "loss 3.0% (sustained)");
  // Still lossy, but a regular step waits for the 3 s decision interval.
  EXPECT_EQ(c.update(report(3.0), T0 + 2s).new_bitrate_kbps, 0);
  EXPECT_EQ(c.update(report(3.0), T0 + 3s).new_bitrate_kbps, 0);
  EXPECT_EQ(c.update(report(3.0), T0 + 4s).new_bitrate_kbps, 16200);
}

TEST(AbrControllerTest, SingleLossyReportDoesNothing) {
  abr::controller_t c {abr::mode_e::balanced, RANGE, 20000, T0};
  for (int i = 0; i < 20; ++i) {
    // Alternating clean and lossy reports never sustain.
    EXPECT_EQ(c.update(report(i % 2 ? 3.0 : 0.0), T0 + std::chrono::seconds {i}).new_bitrate_kbps, 0) << i;
  }
}

TEST(AbrControllerTest, HysteresisBandHoldsTheBitrate) {
  abr::controller_t c {abr::mode_e::balanced, RANGE, 20000, T0};
  for (int i = 0; i < 60; ++i) {
    // 1 % is above the clear threshold and below the drop threshold: neither climb nor drop.
    EXPECT_EQ(c.update(report(1.0), T0 + std::chrono::seconds {i}).new_bitrate_kbps, 0) << i;
  }
  EXPECT_EQ(c.current_kbps(), 20000);
}

TEST(AbrControllerTest, ClimbsAfterStableReports) {
  abr::controller_t c {abr::mode_e::balanced, RANGE, 20000, T0};
  const auto first = first_step(c, T0);
  ASSERT_TRUE(first);
  EXPECT_EQ(first->first, T0 + 4s);  // the 5th clean report
  EXPECT_EQ(first->second, 21000);
  EXPECT_EQ(c.last_reason(), "stable, probe");
  const auto second = first_step(c, T0 + 5s);
  ASSERT_TRUE(second);
  EXPECT_EQ(second->first, T0 + 9s);
  EXPECT_EQ(second->second, 22050);
}

TEST(AbrControllerTest, WaitsAfterADropBeforeClimbing) {
  abr::controller_t c {abr::mode_e::balanced, RANGE, 20000, T0};
  ASSERT_EQ(c.update(report(6.0), T0).new_bitrate_kbps, 14000);
  const auto up = first_step(c, T0 + 1s);
  ASSERT_TRUE(up);
  EXPECT_EQ(up->first, T0 + 10s);  // hold_after_drop = 10 s in balanced mode
  EXPECT_EQ(up->second, 14700);
}

TEST(AbrControllerTest, ClimbsCarefullyNearTheBitrateThatCausedLoss) {
  abr::controller_t c {abr::mode_e::balanced, RANGE, 20000, T0};
  EXPECT_EQ(c.update(report(3.0), T0).new_bitrate_kbps, 0);
  ASSERT_EQ(c.update(report(3.0), T0 + 1s).new_bitrate_kbps, 18000);
  const auto up = first_step(c, T0 + 2s);
  ASSERT_TRUE(up);
  EXPECT_EQ(up->first, T0 + 11s);  // hold_after_drop = 10 s
  EXPECT_EQ(up->second, 18900);  // ×1.05, still below 95 % of 20000
  const auto near = first_step(c, T0 + 12s);
  ASSERT_TRUE(near);
  EXPECT_EQ(near->second, 19278);  // ×1.02, not ×1.05 = 19845 (past 95 % of 20000)
  EXPECT_EQ(c.last_reason(), "stable, careful probe");
}

TEST(AbrControllerTest, ForgetsTheCeilingAfterAWhile) {
  abr::controller_t c {abr::mode_e::balanced, RANGE, 20000, T0};
  c.update(report(3.0), T0);
  ASSERT_EQ(c.update(report(3.0), T0 + 1s).new_bitrate_kbps, 18000);
  ASSERT_EQ(first_step(c, T0 + 2s)->second, 18900);
  const auto up = first_step(c, T0 + 62s);
  ASSERT_TRUE(up);
  EXPECT_EQ(up->second, 19845);
  EXPECT_EQ(c.last_reason(), "stable, probe");
}

TEST(AbrControllerTest, LaggingHostLossDoesNotCompoundADrop) {
  abr::controller_t c {abr::mode_e::balanced, RANGE, 20000, T0};
  auto f = report(6.0);
  ASSERT_EQ(c.update(f, T0).new_bitrate_kbps, 14000);
  // The client's per-second loss is clean again, but the host's 5 s window still shows the old loss.
  auto g = report(0.0);
  g.host_loss_pct = 6.0;
  EXPECT_EQ(c.update(g, T0 + 2s).new_bitrate_kbps, 0);
  EXPECT_EQ(c.update(g, T0 + 4s).new_bitrate_kbps, 0);
  // Once the window has moved past the drop, the host figure counts again.
  EXPECT_EQ(c.update(g, T0 + 6s).new_bitrate_kbps, 9800);
}

TEST(AbrControllerTest, RisingRttDropsWhenSustained) {
  abr::controller_t c {abr::mode_e::balanced, RANGE, 20000, T0};
  EXPECT_EQ(c.update(report(0.0, 10), T0).new_bitrate_kbps, 0);
  EXPECT_EQ(c.rtt_baseline_ms(), 10.0);
  EXPECT_EQ(c.update(report(0.0, 70), T0 + 1s).new_bitrate_kbps, 0);
  const auto d = c.update(report(0.0, 70), T0 + 2s);
  EXPECT_EQ(d.new_bitrate_kbps, 18000);
  EXPECT_EQ(d.reason, "rtt 70 ms (+60)");
  // Congestion is not learned as the new normal.
  EXPECT_EQ(c.rtt_baseline_ms(), 10.0);
}

TEST(AbrControllerTest, RttSpikeBlocksClimbingButDoesNotDrop) {
  abr::controller_t c {abr::mode_e::balanced, RANGE, 20000, T0};
  for (int i = 0; i < 30; ++i) {
    // Every 4th report spikes: never sustained, and the stable run never reaches 5.
    const double rtt = i % 4 == 3 ? 90 : 10;
    EXPECT_EQ(c.update(report(0.0, rtt), T0 + std::chrono::seconds {i}).new_bitrate_kbps, 0) << i;
  }
}

TEST(AbrControllerTest, RttBaselineFollowsASlowlyRisingRoute) {
  abr::controller_t c {abr::mode_e::balanced, RANGE, 20000, T0};
  c.update(report(0.0, 10), T0);
  for (int i = 1; i <= 200; ++i) {
    c.update(report(0.0, 40), T0 + std::chrono::seconds {i});
  }
  ASSERT_TRUE(c.rtt_baseline_ms());
  EXPECT_GT(*c.rtt_baseline_ms(), 35.0);
  EXPECT_LE(*c.rtt_baseline_ms(), 40.0);
}

TEST(AbrControllerTest, FusesHostLossAndRtt) {
  abr::controller_t c {abr::mode_e::balanced, RANGE, 20000, T0};
  auto f = report(0.0);
  f.host_loss_pct = 7.0;
  const auto d = c.update(f, T0);
  EXPECT_EQ(d.new_bitrate_kbps, 14000);
  EXPECT_EQ(d.reason, "loss 7.0% (emergency)");

  // With no client RTT, the host's control-channel RTT drives the latency rule.
  abr::controller_t r {abr::mode_e::balanced, RANGE, 20000, T0};
  auto g = report(0.0);
  g.host_rtt_ms = 12.0;
  r.update(g, T0);
  EXPECT_EQ(r.rtt_baseline_ms(), 12.0);
}

TEST(AbrControllerTest, NeverLeavesTheRange) {
  abr::controller_t c {abr::mode_e::balanced, {6000, 21000}, 20000, T0};
  const auto up = first_step(c, T0);
  ASSERT_TRUE(up);
  EXPECT_EQ(up->second, 21000);
  EXPECT_FALSE(first_step(c, T0 + 10s));
  EXPECT_EQ(c.current_kbps(), 21000);
}

TEST(AbrControllerTest, SkipsTinySteps) {
  abr::controller_t c {abr::mode_e::balanced, RANGE, 39900, T0};
  // ×1.05 clamps to 40000: 100 kbps is under 1 % of the bitrate, so the encoder is left alone.
  EXPECT_FALSE(first_step(c, T0, 30));
  EXPECT_EQ(c.current_kbps(), 39900);
}

TEST(AbrControllerTest, EchoedBitrateKeepsTheStableRun) {
  abr::controller_t c {abr::mode_e::balanced, RANGE, 20000, T0};
  ASSERT_EQ(first_step(c, T0)->second, 21000);  // at T0 + 4 s
  c.note_bitrate(21000, T0 + 5s);  // the client applying the decision through /bitrate
  const auto next = first_step(c, T0 + 5s);
  ASSERT_TRUE(next);
  EXPECT_EQ(next->first, T0 + 9s);
}

TEST(AbrControllerTest, ManualChangeIsTheNewCap) {
  abr::controller_t c {abr::mode_e::balanced, RANGE, 20000, T0};
  ASSERT_EQ(first_step(c, T0)->second, 21000);
  // The user lowers the slider: ABR stays at or below it.
  c.note_bitrate(15000, T0 + 6s);
  EXPECT_EQ(c.current_kbps(), 15000);
  EXPECT_EQ(c.range().max_kbps, 15000);
  EXPECT_FALSE(first_step(c, T0 + 7s, 30));
  // Non-positive values are ignored.
  c.note_bitrate(-1, T0 + 40s);
  EXPECT_EQ(c.current_kbps(), 15000);
  // Raising the slider raises the cap; after a drop ABR climbs back to it and no further.
  c.note_bitrate(30000, T0 + 40s);
  EXPECT_EQ(c.range().max_kbps, 30000);
  ASSERT_EQ(c.update(report(6.0), T0 + 41s).new_bitrate_kbps, 21000);
  int last = 0;
  for (auto t = T0 + 42s; t < T0 + 400s; t += 1s) {
    if (const auto d = c.update(report(0.0), t); d.new_bitrate_kbps > 0) {
      EXPECT_GT(d.new_bitrate_kbps, last);
      EXPECT_LE(d.new_bitrate_kbps, 30000);
      last = d.new_bitrate_kbps;
    }
  }
  // Within a small step of the cap (steps under 1 % are skipped), never past it.
  EXPECT_GT(c.current_kbps(), 29500);
  // A minimum above a lowered cap folds onto it.
  c.note_bitrate(3000, T0 + 401s);
  EXPECT_EQ(c.range().min_kbps, 3000);
  EXPECT_EQ(c.range().max_kbps, 3000);
}

TEST(AbrControllerTest, ClientReportedBitrateIsIgnored) {
  abr::controller_t c {abr::mode_e::balanced, RANGE, 20000, T0};
  auto f = report(0.0);
  f.current_bitrate_kbps = 30000;  // stale or out of range: the host's own value wins
  EXPECT_EQ(c.update(f, T0).new_bitrate_kbps, 0);
  EXPECT_EQ(c.current_kbps(), 20000);
}

TEST(AbrControllerTest, CostlyChangesAreFewerAndLarger) {
  // FFmpeg NVENC starts a keyframe on every change: emergency drops still happen at once, but
  // regular steps are 8 s apart and probes climb by at least 10 %, ending exactly at the cap.
  abr::controller_t c {abr::mode_e::balanced, {6000, 20000}, 20000, T0, true};
  ASSERT_EQ(c.update(report(6.0), T0).new_bitrate_kbps, 14000);
  std::vector<std::pair<abr::clock::time_point, int>> ups;
  for (auto t = T0 + 1s; t < T0 + 120s; t += 1s) {
    if (const auto d = c.update(report(0.0), t); d.new_bitrate_kbps > 0) {
      ups.emplace_back(t, d.new_bitrate_kbps);
    }
  }
  ASSERT_GE(ups.size(), 2U);
  for (std::size_t i = 1; i < ups.size(); ++i) {
    EXPECT_GE(ups[i].first - ups[i - 1].first, abr::COSTLY_INTERVAL);
    if (ups[i].second != 20000) {  // the last step to the cap may be small
      EXPECT_GE(ups[i].second, ups[i - 1].second * 1.05);
    }
  }
  EXPECT_EQ(ups.front().second, 15400);  // ×1.10
  EXPECT_EQ(c.current_kbps(), 20000);
  EXPECT_EQ(ups.size(), 5U);  // 15400, 16940, 18634, then a careful 19752 and the cap

  // Sustained loss steps are spaced too.
  abr::controller_t d {abr::mode_e::balanced, {6000, 20000}, 20000, T0, true};
  d.update(report(3.0), T0);
  ASSERT_EQ(d.update(report(3.0), T0 + 1s).new_bitrate_kbps, 18000);
  int drops = 0;
  for (auto t = T0 + 2s; t < T0 + 9s; t += 1s) {
    drops += d.update(report(3.0), t).new_bitrate_kbps > 0;
  }
  EXPECT_EQ(drops, 0);
  EXPECT_EQ(d.update(report(3.0), T0 + 9s).new_bitrate_kbps, 16200);
}

TEST(AbrControllerTest, ModesDiffer) {
  // Low latency drops at 1.6 % sustained loss, where balanced holds.
  abr::controller_t low {abr::mode_e::low_latency, RANGE, 20000, T0};
  abr::controller_t bal {abr::mode_e::balanced, RANGE, 20000, T0};
  low.update(report(1.6), T0);
  bal.update(report(1.6), T0);
  EXPECT_EQ(low.update(report(1.6), T0 + 1s).new_bitrate_kbps, 17000);
  EXPECT_EQ(bal.update(report(1.6), T0 + 1s).new_bitrate_kbps, 0);

  // Quality climbs after 3 clean reports by 8 %; low latency needs 8 and climbs 3 %.
  abr::controller_t quality {abr::mode_e::quality, RANGE, 20000, T0};
  const auto q = first_step(quality, T0);
  ASSERT_TRUE(q);
  EXPECT_EQ(q->first, T0 + 2s);
  EXPECT_EQ(q->second, 21600);
  abr::controller_t slow {abr::mode_e::low_latency, RANGE, 20000, T0};
  const auto s = first_step(slow, T0);
  ASSERT_TRUE(s);
  EXPECT_EQ(s->first, T0 + 7s);
  EXPECT_EQ(s->second, 20600);
}

TEST(AbrFeedbackTest, SanitizesBadNumbers) {
  abr::feedback_t f;
  f.packet_loss_pct = std::numeric_limits<double>::quiet_NaN();
  f.rtt_ms = -5;
  f.decode_fps = std::numeric_limits<double>::infinity();
  f.dropped_frames = -3;
  f.current_bitrate_kbps = -1;
  f.host_loss_pct = 250.0;
  const auto s = abr::sanitize(f);
  EXPECT_EQ(s.packet_loss_pct, 0.0);
  EXPECT_EQ(s.rtt_ms, 0.0);
  EXPECT_EQ(s.decode_fps, 0.0);
  EXPECT_EQ(s.dropped_frames, 0);
  EXPECT_EQ(s.current_bitrate_kbps, 0);
  EXPECT_EQ(s.host_loss_pct, 100.0);
  EXPECT_EQ(abr::sanitize(report(400.0)).packet_loss_pct, 100.0);
}

TEST(AbrFeedbackTest, ParsesTheVplusBody) {
  const auto f = abr::parse_feedback(nlohmann::json::parse(R"({"packetLoss":2.5,"rttMs":12,"decodeFps":59.9,"droppedFrames":3,"currentBitrate":20000})"));
  EXPECT_DOUBLE_EQ(f.packet_loss_pct, 2.5);
  EXPECT_DOUBLE_EQ(f.rtt_ms, 12.0);
  EXPECT_DOUBLE_EQ(f.decode_fps, 59.9);
  EXPECT_EQ(f.dropped_frames, 3);
  EXPECT_EQ(f.current_bitrate_kbps, 20000);
  EXPECT_FALSE(f.host_loss_pct);

  const auto junk = abr::parse_feedback(nlohmann::json::parse(R"({"packetLoss":"lots","rttMs":null,"currentBitrate":1e12})"));
  EXPECT_EQ(junk.packet_loss_pct, 0.0);
  EXPECT_EQ(junk.rtt_ms, 0.0);
  EXPECT_EQ(junk.current_bitrate_kbps, 2000000000);
  EXPECT_EQ(abr::parse_feedback(nlohmann::json::array()).packet_loss_pct, 0.0);
}

TEST(AbrConfigRequestTest, ParsesAndValidates) {
  std::string error;
  auto off = abr::parse_config_request(R"({"enabled":false,"minBitrate":0,"maxBitrate":0,"mode":"balanced"})", error);
  ASSERT_TRUE(off);
  EXPECT_FALSE(off->enabled);

  auto on = abr::parse_config_request(R"({"enabled":true,"minBitrate":5000,"maxBitrate":60000,"mode":"lowLatency"})", error);
  ASSERT_TRUE(on);
  EXPECT_TRUE(on->enabled);
  EXPECT_EQ(on->mode, abr::mode_e::low_latency);
  EXPECT_EQ(on->mode_text, "lowLatency");
  EXPECT_EQ(on->min_kbps, 5000);
  EXPECT_EQ(on->max_kbps, 60000);

  auto alias = abr::parse_config_request(R"({"enabled":true,"mode":"aggressive"})", error);
  ASSERT_TRUE(alias);
  EXPECT_EQ(alias->mode, abr::mode_e::quality);
  EXPECT_EQ(alias->mode_text, "aggressive");

  auto defaults = abr::parse_config_request(R"({"enabled":true})", error);
  ASSERT_TRUE(defaults);
  EXPECT_EQ(defaults->mode, abr::mode_e::balanced);
  EXPECT_EQ(defaults->min_kbps, 0);

  EXPECT_FALSE(abr::parse_config_request("not json", error));
  EXPECT_EQ(error, "Invalid JSON body");
  EXPECT_FALSE(abr::parse_config_request(R"({"enabled":true,"mode":"fast"})", error));
  EXPECT_EQ(error, "Invalid mode: must be 'balanced', 'quality', or 'lowLatency'");
  EXPECT_FALSE(abr::parse_config_request(R"({"enabled":true,"mode":7})", error));
  EXPECT_FALSE(abr::parse_config_request(R"({"enabled":true,"minBitrate":-1})", error));
  EXPECT_EQ(error, "minBitrate and maxBitrate must be non-negative");
  EXPECT_FALSE(abr::parse_config_request(R"({"enabled":true,"minBitrate":9000,"maxBitrate":8000})", error));
  EXPECT_EQ(error, "minBitrate must not exceed maxBitrate");
}

TEST(AbrCapabilitiesTest, FoundationShape) {
  const auto on = abr::capabilities_json(true, 0);
  EXPECT_TRUE(on["supported"].get<bool>());
  EXPECT_EQ(on["version"], 1);
  EXPECT_FALSE(on["llmEnabled"].get<bool>());
  EXPECT_EQ(on["hostMaxBitrate"], 0);
  EXPECT_TRUE(on["features"].is_array());
  EXPECT_FALSE(on["features"].empty());

  const auto off = abr::capabilities_json(false, 50000);
  EXPECT_FALSE(off["supported"].get<bool>());
  EXPECT_EQ(off["hostMaxBitrate"], 50000);
  EXPECT_TRUE(off["features"].empty());
}

TEST(AbrRegistryTest, EnableFeedbackStatusDisable) {
  const std::string cert = "-----BEGIN CERTIFICATE-----test-abr";
  EXPECT_FALSE(abr::is_enabled(cert));
  EXPECT_FALSE(abr::feedback(cert, report(6.0)));
  EXPECT_FALSE(abr::status(cert));

  abr::enable(cert, abr::mode_e::balanced, RANGE, 20000);
  EXPECT_TRUE(abr::is_enabled(cert));
  const auto d = abr::feedback(cert, report(6.0), T0);
  ASSERT_TRUE(d);
  EXPECT_EQ(d->new_bitrate_kbps, 14000);

  auto s = abr::status(cert);
  ASSERT_TRUE(s);
  EXPECT_EQ(s->current_kbps, 14000);
  EXPECT_EQ(s->changes, 1);
  EXPECT_EQ(s->range.max_kbps, 40000);
  const auto j = abr::status_json(*s);
  EXPECT_EQ(j["mode"], "balanced");
  EXPECT_EQ(j["current_kbps"], 14000);
  EXPECT_EQ(j["min_kbps"], 6000);
  EXPECT_EQ(j["last_reason"], "loss 6.0% (emergency)");

  abr::note_bitrate(cert, 25000);
  EXPECT_EQ(abr::status(cert)->current_kbps, 25000);
  EXPECT_EQ(abr::status(cert)->range.max_kbps, 25000);

  // Enabling again starts over.
  abr::enable(cert, abr::mode_e::quality, {10000, 30000}, 20000);
  EXPECT_EQ(abr::status(cert)->changes, 0);
  EXPECT_EQ(abr::status(cert)->mode, abr::mode_e::quality);

  abr::disable(cert);
  EXPECT_FALSE(abr::is_enabled(cert));
  EXPECT_FALSE(abr::feedback(cert, report(6.0)));
  abr::note_bitrate(cert, 1000);  // no-op when off
  EXPECT_FALSE(abr::status(cert));
}

TEST(AbrEncoderBitrateTest, MatchesTheRtspAllowance) {
  // 20 % FEC, stereo normal-quality audio: 20000 / 1.25 = 16000, - 192 = 15808, - 500 = 15308.
  EXPECT_EQ(abr::encoder_kbps(20000, 20, 192), 15308);
  // FEC above 80 % is not compensated.
  EXPECT_EQ(abr::encoder_kbps(20000, 90, 192), 19308);
  // Audio and overhead reductions are capped at 20 % and 10 %.
  EXPECT_EQ(abr::encoder_kbps(1000, 20, 512), 576);
  EXPECT_EQ(abr::encoder_kbps(0, 20, 192), 1);
  EXPECT_GE(abr::encoder_kbps(1, 0, 0), 1);
}
