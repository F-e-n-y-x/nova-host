/**
 * @file tests/unit/test_abr_game_cap.cpp
 * @brief Tests that a game's bitrate cap (nova-perf `bitrate_kbps`) holds for adaptive bitrate
 * and for live `/bitrate` changes.
 *
 * ABR decisions go straight to `rtsp_stream::request_bitrate_by_cert` and skip the `/bitrate`
 * clamp, so the cap must already be in the range `abr_configure` resolves; and `/bitrate` must
 * cap the value before it reaches `abr::note_bitrate`, which makes it the new ABR ceiling.
 */
// test includes
#include "../tests_common.h"

// standard includes
#include <chrono>

// local includes
#include <src/abr.h>
#include <src/nova_client_api.h>
#include <src/nova_perf.h>

using namespace std::chrono_literals;

namespace {
  const abr::clock::time_point T0 = abr::clock::time_point {} + 1000s;  ///< Arbitrary start time.

  /**
   * @brief Feed @p reports clean reports, one a second, and return the highest bitrate chosen.
   *
   * @param c Controller.
   * @param from First report time.
   * @param reports Number of reports.
   * @return Highest bitrate the controller asked for (its start value when it never stepped).
   */
  int highest_after_clean_reports(abr::controller_t &c, abr::clock::time_point from, int reports) {
    int highest = c.current_kbps();
    for (int i = 0; i < reports; ++i) {
      abr::feedback_t f;
      if (const auto d = c.update(f, from + std::chrono::seconds {i}); d.new_bitrate_kbps > highest) {
        highest = d.new_bitrate_kbps;
      }
    }
    return highest;
  }
}  // namespace

TEST(StreamBitrateCapTest, LowerOfHostMaxAndGameCap) {
  EXPECT_EQ(nova_api::stream_bitrate_cap(0, 0), 0);  // neither set
  EXPECT_EQ(nova_api::stream_bitrate_cap(-1, -1), 0);
  EXPECT_EQ(nova_api::stream_bitrate_cap(100000, 0), 100000);  // host only
  EXPECT_EQ(nova_api::stream_bitrate_cap(0, 30000), 30000);  // game only
  EXPECT_EQ(nova_api::stream_bitrate_cap(100000, 30000), 30000);  // the game's cap is lower
  EXPECT_EQ(nova_api::stream_bitrate_cap(20000, 30000), 20000);  // the host's is lower
}

TEST(StreamBitrateCapTest, AbrRangeStaysUnderTheGameCap) {
  // A 60 Mbps stream of a game capped at 25 Mbps, on a host allowing 100 Mbps.
  const int host_cap = nova_api::stream_bitrate_cap(100000, 25000);
  const auto range = abr::resolve_range(abr::mode_e::quality, 60000, 0, 0, 0, host_cap);
  EXPECT_EQ(range.max_kbps, 25000);
  EXPECT_LE(range.min_kbps, range.max_kbps);

  // The client's own maxBitrate can't lift it either.
  EXPECT_EQ(abr::resolve_range(abr::mode_e::balanced, 60000, 0, 80000, 0, host_cap).max_kbps, 25000);
  // A cap under the preset floor still gives a valid range.
  const auto tight = abr::resolve_range(abr::mode_e::quality, 60000, 0, 0, 0, nova_api::stream_bitrate_cap(0, 3000));
  EXPECT_EQ(tight.max_kbps, 3000);
  EXPECT_LE(tight.min_kbps, tight.max_kbps);

  // Without a game cap, ABR still follows max_bitrate alone (as before).
  EXPECT_EQ(abr::resolve_range(abr::mode_e::quality, 60000, 0, 0, 0, nova_api::stream_bitrate_cap(50000, 0)).max_kbps, 50000);
}

TEST(StreamBitrateCapTest, AbrDecisionsNeverClimbAboveTheGameCap) {
  const int host_cap = nova_api::stream_bitrate_cap(0, 25000);
  const auto range = abr::resolve_range(abr::mode_e::quality, 60000, 0, 0, 0, host_cap);
  // Start low so the controller has room to climb, and let it run for minutes of clean reports.
  abr::controller_t c {abr::mode_e::quality, range, range.min_kbps, T0, false};
  EXPECT_LE(highest_after_clean_reports(c, T0, 600), 25000);
  EXPECT_LE(c.current_kbps(), 25000);
}

TEST(LiveBitrateTest, ClampsToHostThenGameCap) {
  EXPECT_FALSE(nova_api::live_bitrate(0, 0, 25000));
  EXPECT_FALSE(nova_api::live_bitrate(-5, 0, 25000));
  EXPECT_EQ(nova_api::live_bitrate(60000, 0, 0), 60000);  // no caps
  EXPECT_EQ(nova_api::live_bitrate(60000, 0, 25000), 25000);  // game cap
  EXPECT_EQ(nova_api::live_bitrate(60000, 40000, 0), 40000);  // host cap
  EXPECT_EQ(nova_api::live_bitrate(60000, 40000, 25000), 25000);  // the lower wins
  EXPECT_EQ(nova_api::live_bitrate(60000, 20000, 25000), 20000);
  EXPECT_EQ(nova_api::live_bitrate(10000, 40000, 25000), 10000);  // under both: unchanged
  EXPECT_EQ(nova_api::live_bitrate(100, 0, 25000), nova_api::MIN_BITRATE_KBPS);  // floor still holds
  EXPECT_EQ(nova_api::live_bitrate(10'000'000, 0, 0), nova_api::MAX_BITRATE_KBPS);
  // Every value a profile accepts is at or above the /bitrate floor, so the cap never drops below it.
  EXPECT_GE(nova_perf::MIN_BITRATE_KBPS, nova_api::MIN_BITRATE_KBPS);
}

TEST(LiveBitrateTest, CappedManualChangeKeepsTheAbrCeilingUnderTheGameCap) {
  // What the /bitrate handler does: cap first, then note the value as the user's new ABR cap.
  const int game_cap = 25000;
  const auto range = abr::resolve_range(abr::mode_e::balanced, 20000, 0, 0, 0, nova_api::stream_bitrate_cap(0, game_cap));
  abr::controller_t c {abr::mode_e::balanced, range, 20000, T0, false};

  const auto kbps = nova_api::live_bitrate(80000, 0, game_cap);
  ASSERT_TRUE(kbps);
  c.note_bitrate(*kbps, T0 + 1s);
  EXPECT_EQ(c.range().max_kbps, game_cap);
  EXPECT_EQ(c.current_kbps(), game_cap);
  EXPECT_LE(highest_after_clean_reports(c, T0 + 2s, 600), game_cap);

  // Through the registry too, the way /bitrate calls it.
  const std::string cert = "test-game-cap-cert";
  abr::enable(cert, abr::mode_e::balanced, range, 20000, false);
  abr::note_bitrate(cert, *nova_api::live_bitrate(80000, 0, game_cap));
  const auto st = abr::status(cert);
  ASSERT_TRUE(st);
  EXPECT_LE(st->range.max_kbps, game_cap);
  EXPECT_LE(st->current_kbps, game_cap);
  abr::disable(cert);
}
