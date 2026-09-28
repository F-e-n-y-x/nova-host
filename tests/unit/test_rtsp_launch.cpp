/**
 * @file tests/unit/test_rtsp_launch.cpp
 * @brief Which /launch or /resume may replace a launch session still waiting for its RTSP handshake.
 */
// test includes
#include "../tests_common.h"

// local includes
#include <src/rtsp.h>

namespace {
  rtsp_stream::launch_session_t session(const std::string &cert, const std::string &unique_id = "0123456789ABCDEF") {
    rtsp_stream::launch_session_t s {};
    s.client_cert = cert;
    s.unique_id = unique_id;
    return s;
  }
}  // namespace

// Nebula's live resolution switch: an attempt that timed out left its launch session pending,
// and the reconnect from the same phone (new rikey) must take its place, or its encrypted RTSP
// handshake fails with "Failed to verify RTSP message tag".
TEST(RtspPendingLaunch, TheSameDeviceReplacesItsAbandonedAttempt) {
  EXPECT_TRUE(rtsp_stream::replaces_pending_launch(session("PHONE-CERT"), session("PHONE-CERT")));
}

TEST(RtspPendingLaunch, AnotherDeviceNeverTakesTheSlot) {
  EXPECT_FALSE(rtsp_stream::replaces_pending_launch(session("PHONE-CERT"), session("TABLET-CERT")));
  EXPECT_FALSE(rtsp_stream::replaces_pending_launch(session("PHONE-CERT"), session("")));
  EXPECT_FALSE(rtsp_stream::replaces_pending_launch(session(""), session("TABLET-CERT")));
}

TEST(RtspPendingLaunch, WithoutCertificatesTheUniqueIdDecides) {
  EXPECT_TRUE(rtsp_stream::replaces_pending_launch(session("", "AAAA"), session("", "AAAA")));
  EXPECT_FALSE(rtsp_stream::replaces_pending_launch(session("", "AAAA"), session("", "BBBB")));
  EXPECT_FALSE(rtsp_stream::replaces_pending_launch(session("", ""), session("", "")));
}
