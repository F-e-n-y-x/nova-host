/**
 * @file tests/unit/platform/linux/test_text_context_live.cpp
 * @brief Opt-in live test of remote text context on a spare X server (NOVA_TC_LIVE_DIR set).
 *
 * The harness (text_context_live.sh next to this file) starts a throwaway Xvfb with a window manager, a private D-Bus
 * session and a GTK test app with a button, a text entry, a password entry and a text view. The
 * app moves its own keyboard focus when told to through a command file; nothing is typed and no
 * input is injected anywhere. The test drives the real AT-SPI source and the service with the
 * client's own input packets (a tap at the field) and checks the 76-byte packets the client
 * would receive:
 *  - tap + focus on the entry opens (input matched, element rectangle around the tap);
 *  - focus moving to the button closes that activation;
 *  - the password entry is flagged, without a caret;
 *  - focus that no tap caused sends nothing;
 * once for the desktop layout (the display's own bus, as in Mirror) and once for the Virtual
 * display layout (the bus found through the desktop session's app-env.json).
 */
#if defined(__linux__)

  // test includes
  #include "../../../tests_common.h"

  // standard includes
  #include <chrono>
  #include <cstdlib>
  #include <cstring>
  #include <filesystem>
  #include <fstream>
  #include <thread>

  // lib includes
  #include <nlohmann/json.hpp>

  // local includes
  #include <src/text_context.h>
  #include <src/utility.h>

extern "C" {
  #include <moonlight-common-c/src/Input.h>
  #include <moonlight-common-c/src/Limelight.h>
}

namespace {
  namespace fs = std::filesystem;
  namespace tc = text_context;
  using namespace std::literals;

  struct decoded_t {
    std::uint16_t flags;
    std::uint64_t activation;
    std::uint8_t cause;
    std::int32_t ax, ay, el, et, er, eb, cl;
  };

  decoded_t decode(const std::vector<std::uint8_t> &b) {
    auto p = tc::decode(b);
    EXPECT_TRUE(p);
    if (!p) {
      return {};
    }
    return {p->flags, p->activation_id, static_cast<std::uint8_t>(p->cause), p->anchor_x, p->anchor_y, p->element.left, p->element.top, p->element.right, p->element.bottom, p->caret.left};
  }

  std::vector<std::uint8_t> touch(std::uint8_t type, float x, float y) {
    SS_TOUCH_PACKET p {};
    p.header.size = util::endian::big<std::uint32_t>(sizeof(p) - 4);
    p.header.magic = util::endian::little<std::uint32_t>(SS_TOUCH_MAGIC);
    p.eventType = type;
    p.pointerId = util::endian::little<std::uint32_t>(3);
    std::memcpy(p.x, &x, 4);
    std::memcpy(p.y, &y, 4);
    std::vector<std::uint8_t> out(sizeof(p));
    std::memcpy(out.data(), &p, sizeof(p));
    return out;
  }

  class TextContextLive: public ::testing::Test {
  protected:
    fs::path dir;
    nlohmann::json geometry;
    std::ofstream report;
    int serial = 0;

    void SetUp() override {
      const char *d = std::getenv("NOVA_TC_LIVE_DIR");
      if (!d || !*d) {
        GTEST_SKIP() << "live test: set NOVA_TC_LIVE_DIR (see tc-live.sh)";
      }
      dir = d;
      for (int i = 0; i < 100 && !fs::exists(dir / "geometry.json"); ++i) {
        std::this_thread::sleep_for(100ms);
      }
      ASSERT_TRUE(fs::exists(dir / "geometry.json")) << "the test app never started";
      geometry = nlohmann::json::parse(std::ifstream {dir / "geometry.json"});
      report.open(dir / "report.txt", std::ios::app);
    }

    void command(const std::string &name) {
      std::ofstream {dir / "cmd.tmp"} << name << ':' << ++serial;
      fs::rename(dir / "cmd.tmp", dir / "cmd");
    }

    std::vector<tc::outbound_t> wait_packets(tc::service_t &service, std::chrono::milliseconds timeout) {
      std::vector<tc::outbound_t> out;
      const auto deadline = std::chrono::steady_clock::now() + timeout;
      while (std::chrono::steady_clock::now() < deadline) {
        for (auto &o : service.drain_outbound()) {
          out.push_back(std::move(o));
        }
        if (!out.empty()) {
          // Let a burst finish.
          std::this_thread::sleep_for(150ms);
          for (auto &o : service.drain_outbound()) {
            out.push_back(std::move(o));
          }
          break;
        }
        std::this_thread::sleep_for(20ms);
      }
      return out;
    }

    void tap(tc::service_t &service, const std::string &widget) {
      const auto r = geometry[widget];
      const float x = (r[0].get<float>() + r[2].get<float>() / 2) / 1280.0f;
      const float y = (r[1].get<float>() + r[3].get<float>() / 2) / 720.0f;
      service.on_input(1, touch(LI_TOUCH_EVENT_DOWN, x, y));
      service.on_input(1, touch(LI_TOUCH_EVENT_UP, x, y));
    }

    void run(const std::string &label, tc::target_t target) {
      tc::service_t service {
        [](const tc::target_t &t) {
          return tc::make_platform_source(t);
        },
        [] {
          return tc::bridge_t::clock::now();
        },
        [target] {
          return target;
        }
      };
      service.set_capture_geometry({0, 0, 1280, 720});
      ASSERT_TRUE(tc::platform_source_available(target)) << label << ": no accessibility bus";
      ASSERT_TRUE(service.session_started(1, tc::kMlFeatureFlag, true));
      ASSERT_TRUE(service.wait_settled(10s));
      ASSERT_TRUE(service.source_running()) << label << ": the AT-SPI source did not start";

      command("button");
      std::this_thread::sleep_for(600ms);
      service.drain_outbound();

      // 1. Tap the entry; the app focuses it.
      tap(service, "entry");
      command("entry");
      auto out = wait_packets(service, 3s);
      ASSERT_EQ(out.size(), 1u) << label << ": expected one open packet";
      auto open = decode(out[0].bytes);
      EXPECT_TRUE(open.flags & tc::kFlagActive);
      EXPECT_TRUE(open.flags & tc::kFlagEditable);
      EXPECT_TRUE(open.flags & tc::kFlagInputMatched);
      EXPECT_TRUE(open.flags & tc::kFlagElementRect);
      EXPECT_FALSE(open.flags & tc::kFlagPassword);
      EXPECT_EQ(open.cause, 1);
      EXPECT_LE(open.el, open.ax);
      EXPECT_GE(open.er, open.ax);
      EXPECT_LE(open.et, open.ay);
      EXPECT_GE(open.eb, open.ay);
      report << label << ": open activation " << open.activation << " element [" << open.el << ',' << open.et << ' ' << open.er << ',' << open.eb
             << "] tap (" << open.ax << ',' << open.ay << ") caret " << ((open.flags & tc::kFlagCaretRect) ? "yes" : "no") << '\n';

      // 2. Focus moves to the button: close.
      command("button");
      out = wait_packets(service, 3s);
      ASSERT_EQ(out.size(), 1u) << label << ": expected one close packet";
      auto close = decode(out[0].bytes);
      EXPECT_EQ(close.activation, open.activation);
      EXPECT_FALSE(close.flags & tc::kFlagActive);
      report << label << ": close activation " << close.activation << '\n';

      // 3. The password entry: flagged, no caret.
      tap(service, "password");
      command("password");
      out = wait_packets(service, 3s);
      ASSERT_EQ(out.size(), 1u) << label << ": expected one password open";
      auto pw = decode(out[0].bytes);
      EXPECT_TRUE(pw.flags & tc::kFlagPassword);
      EXPECT_FALSE(pw.flags & tc::kFlagCaretRect);
      EXPECT_EQ(pw.cl, 0);
      report << label << ": password open, caret " << ((pw.flags & tc::kFlagCaretRect) ? "SENT" : "withheld") << '\n';
      command("button");
      wait_packets(service, 2s);

      // 4. Focus moved by the app alone (no tap): nothing.
      std::this_thread::sleep_for(1500ms);  // past the match window of the last tap
      command("view");
      out = wait_packets(service, 1500ms);
      EXPECT_TRUE(out.empty()) << label << ": focus without a tap must not open a keyboard";
      report << label << ": focus without a tap sent " << out.size() << " packets\n";
      command("button");

      service.session_stopped(1);
      EXPECT_TRUE(service.wait_settled(5s));
    }
  };
}  // namespace

TEST_F(TextContextLive, DesktopDisplayOwnBus) {
  // Mirror: Nova's own DISPLAY and session bus.
  run("desktop", {});
}

TEST_F(TextContextLive, VirtualDisplaySessionBus) {
  // Virtual display with a desktop session: the private bus comes from app-env.json.
  const char *session = std::getenv("NOVA_TC_SESSION_DIR");
  if (!session || !*session) {
    GTEST_SKIP() << "NOVA_TC_SESSION_DIR not set";
  }
  const char *display = std::getenv("DISPLAY");
  run("virtual-display", {display ? display : "", "", session});
}

#endif
