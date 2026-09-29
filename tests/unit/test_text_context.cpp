/**
 * @file tests/unit/test_text_context.cpp
 * @brief Remote text context (control message 24) tests: the 76-byte wire format against the
 *        client decoder's layout and trust policy, AT-SPI classification, click/focus
 *        correlation and debounce in the bridge, input sniffing and the source supervisor
 *        (src/text_context.h).
 */
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>

#include "src/config.h"
#include "src/client_permissions.h"
#include "src/nova_client_api.h"
#include "src/text_context.h"
#include "src/utility.h"

#include "../tests_common.h"

extern "C" {
#include <moonlight-common-c/src/Input.h>
#include <moonlight-common-c/src/Limelight.h>
}

using namespace std::literals;
namespace tc = text_context;

namespace {
  using clock_t_ = tc::bridge_t::clock;

  /**
   * @brief Mirror of the client (moonlight-common-c `RemoteTextContextStream.c` decode plus
   *        Nebula/V+ `RemoteTextContextPolicy`): every packet the host sends must pass it.
   */
  struct client_t {
    struct context_t {
      std::uint16_t flags;
      std::uint32_t revision;
      std::uint64_t activation;
      std::uint64_t token;
      std::uint8_t source;
      std::uint8_t cause;
      std::int32_t ax, ay;
      std::int32_t el, et, er, eb;
      std::int32_t cl, ct, cr, cb;
      std::uint32_t cw, ch;
    };

    static std::optional<context_t> decode(const std::vector<std::uint8_t> &p) {
      if (p.size() != 76 || p[0] != 1 || p[1] != 76 || p[26] != 0 || p[27] != 0) {
        return std::nullopt;
      }
      auto u32 = [&](std::size_t at) {
        return (std::uint32_t) p[at] | ((std::uint32_t) p[at + 1] << 8) | ((std::uint32_t) p[at + 2] << 16) | ((std::uint32_t) p[at + 3] << 24);
      };
      auto i32 = [&](std::size_t at) {
        return (std::int32_t) u32(at);
      };
      context_t c {};
      c.flags = (std::uint16_t) (p[2] | (p[3] << 8));
      c.revision = u32(4);
      c.activation = (std::uint64_t) u32(8) | ((std::uint64_t) u32(12) << 32);
      c.token = (std::uint64_t) u32(16) | ((std::uint64_t) u32(20) << 32);
      c.source = p[24];
      c.cause = p[25];
      c.ax = i32(28);
      c.ay = i32(32);
      c.el = i32(36);
      c.et = i32(40);
      c.er = i32(44);
      c.eb = i32(48);
      c.cl = i32(52);
      c.ct = i32(56);
      c.cr = i32(60);
      c.cb = i32(64);
      c.cw = u32(68);
      c.ch = u32(72);
      if (c.cw == 0 || c.ch == 0 || c.cw > 1000000 || c.ch > 1000000) {
        return std::nullopt;
      }
      return c;
    }

    static bool trusted_activation(const context_t &c) {
      if (!(c.flags & tc::kFlagInputMatched) || c.source != 2) {
        return false;
      }
      return (c.cause == 1 || c.cause == 2) && (c.flags & tc::kFlagActive) && (c.flags & tc::kFlagEditable) &&
             (c.flags & tc::kFlagElementRect) && c.el < c.er && c.et < c.eb;
    }

    static bool trusted_deactivation(const context_t &c, std::uint64_t active) {
      if (active == 0 || c.activation != active || !(c.flags & tc::kFlagInputMatched) || c.source != 2) {
        return false;
      }
      if (c.cause != 1 && c.cause != 2) {
        return false;
      }
      return !(c.flags & tc::kFlagActive) || !(c.flags & tc::kFlagEditable);
    }

    static bool newer(std::uint32_t candidate, std::uint32_t previous) {
      const auto delta = candidate - previous;
      return delta >= 1 && delta <= 0x7fffffffu;
    }

    /// Keyboard state the client ends up in.
    std::uint64_t active = 0;
    std::optional<std::uint32_t> last_revision;
    int opens = 0;
    int closes = 0;
    int ignored = 0;
    std::optional<context_t> last;

    void push(const std::vector<std::uint8_t> &bytes) {
      auto c = decode(bytes);
      if (!c) {
        ignored++;
        return;
      }
      if (last_revision && !newer(c->revision, *last_revision)) {
        ignored++;
        return;
      }
      last_revision = c->revision;
      last = c;
      if (trusted_activation(*c)) {
        active = c->activation;
        opens++;
      } else if (trusted_deactivation(*c, active)) {
        active = 0;
        closes++;
      } else {
        ignored++;
      }
    }
  };

  tc::observation_t field(std::string id, tc::rect_t rect, bool focused = true) {
    tc::observation_t o;
    o.element = std::move(id);
    o.focused = focused;
    o.editable = true;
    o.element_rect = rect;
    return o;
  }

  tc::observation_t blur(std::string id) {
    tc::observation_t o;
    o.element = std::move(id);
    o.focused = false;
    return o;
  }

  /**
   * @brief A bridge with a 1920x1080 capture at (100, 50), driven on a fake clock.
   */
  struct BridgeFixture: ::testing::Test {
    tc::bridge_t bridge;
    clock_t_::time_point t = clock_t_::time_point {} + 1h;
    client_t client;

    void SetUp() override {
      bridge.set_capture_geometry({100, 50, 1920, 1080});
      bridge.session_started(1);
    }

    void advance(std::chrono::milliseconds d) {
      t += d;
      bridge.tick(t);
    }

    std::vector<tc::outbound_t> flush(std::uint32_t session = 1) {
      bridge.tick(t);
      std::vector<tc::outbound_t> mine;
      for (auto &o : bridge.drain()) {
        if (o.session == session) {
          client.push(o.bytes);
        }
        mine.push_back(std::move(o));
      }
      return mine;
    }

    void click(std::uint32_t session, tc::point_t at, tc::cause_e cause = tc::cause_e::remote_mouse) {
      bridge.on_pointer(session, cause, tc::phase_e::down, 7, at, 0, t);
      t += 40ms;
      bridge.on_pointer(session, cause, tc::phase_e::up, 7, at, 0, t);
    }
  };

  // ---- Input packets as the client sends them ---------------------------------------------

  template<class T>
  std::vector<std::uint8_t> bytes_of(const T &packet) {
    std::vector<std::uint8_t> out(sizeof(T));
    std::memcpy(out.data(), &packet, sizeof(T));
    return out;
  }

  void header(NV_INPUT_HEADER &h, std::uint32_t magic, std::size_t size) {
    h.size = util::endian::big<std::uint32_t>(static_cast<std::uint32_t>(size - sizeof(std::uint32_t)));
    h.magic = util::endian::little<std::uint32_t>(magic);
  }

  std::vector<std::uint8_t> touch(std::uint8_t type, std::uint32_t id, float x, float y) {
    SS_TOUCH_PACKET p {};
    header(p.header, SS_TOUCH_MAGIC, sizeof(p));
    p.eventType = type;
    p.pointerId = util::endian::little(id);
    std::memcpy(p.x, &x, 4);
    std::memcpy(p.y, &y, 4);
    return bytes_of(p);
  }

  std::vector<std::uint8_t> button(bool down, std::uint8_t which = BUTTON_LEFT) {
    NV_MOUSE_BUTTON_PACKET p {};
    header(p.header, down ? MOUSE_BUTTON_DOWN_EVENT_MAGIC_GEN5 : MOUSE_BUTTON_UP_EVENT_MAGIC_GEN5, sizeof(p));
    p.button = which;
    return bytes_of(p);
  }

  std::vector<std::uint8_t> abs_move(short x, short y, short w, short h) {
    NV_ABS_MOUSE_MOVE_PACKET p {};
    header(p.header, MOUSE_MOVE_ABS_MAGIC, sizeof(p));
    p.x = util::endian::big(x);
    p.y = util::endian::big(y);
    p.width = util::endian::big(w);
    p.height = util::endian::big(h);
    return bytes_of(p);
  }

  /**
   * @brief Focus source double: records its target and lets the test post observations.
   */
  struct fake_source_t: tc::source_t {
    struct shared_t {
      std::mutex mutex;
      std::vector<tc::target_t> started;
      int stopped = 0;
      bool refuse = false;
      std::atomic<bool> broken {false};
      callback_t callback;
      std::optional<tc::point_t> pointer;
    };

    explicit fake_source_t(std::shared_ptr<shared_t> shared, tc::target_t target):
        shared_ {std::move(shared)},
        target_ {std::move(target)} {
    }

    bool start(callback_t callback) override {
      std::lock_guard lock {shared_->mutex};
      if (shared_->refuse) {
        return false;
      }
      shared_->started.push_back(target_);
      shared_->callback = std::move(callback);
      return true;
    }

    void stop() override {
      std::lock_guard lock {shared_->mutex};
      shared_->stopped++;
      shared_->callback = nullptr;
    }

    std::optional<tc::point_t> pointer() override {
      std::lock_guard lock {shared_->mutex};
      return shared_->pointer;
    }

    bool broken() const override {
      return shared_->broken.load();
    }

    std::shared_ptr<shared_t> shared_;
    tc::target_t target_;
  };

  template<class Pred>
  bool eventually(Pred pred, std::chrono::milliseconds timeout = 3s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      if (pred()) {
        return true;
      }
      std::this_thread::sleep_for(10ms);
    }
    return pred();
  }
}  // namespace

// ---- Wire format ------------------------------------------------------------------------

TEST(TextContextWire, EncodesTheClientLayoutByteForByte) {
  tc::packet_t p;
  p.flags = tc::kFlagActive | tc::kFlagEditable | tc::kFlagElementRect | tc::kFlagInputMatched;
  p.revision = 0x04030201;
  p.activation_id = 0x0102030405060708ull;
  p.input_token = 0x1112131415161718ull;
  p.source = tc::source_e::uia;
  p.cause = tc::cause_e::remote_touch;
  p.anchor_x = -2;
  p.anchor_y = 300;
  p.element = {10, 20, 110, 60};
  p.caret = {0, 0, 0, 0};
  p.capture_width = 1920;
  p.capture_height = 1080;
  const auto b = tc::encode(p);
  ASSERT_EQ(b.size(), 76u);
  EXPECT_EQ(b[0], 1);
  EXPECT_EQ(b[1], 76);
  EXPECT_EQ(b[2], 0xA3);  // 0x0001|0x0002|0x0020|0x0080
  EXPECT_EQ(b[3], 0x00);
  EXPECT_EQ((std::vector<std::uint8_t> {b.begin() + 4, b.begin() + 8}), (std::vector<std::uint8_t> {1, 2, 3, 4}));
  EXPECT_EQ((std::vector<std::uint8_t> {b.begin() + 8, b.begin() + 16}), (std::vector<std::uint8_t> {8, 7, 6, 5, 4, 3, 2, 1}));
  EXPECT_EQ((std::vector<std::uint8_t> {b.begin() + 16, b.begin() + 24}), (std::vector<std::uint8_t> {0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11}));
  EXPECT_EQ(b[24], 2);
  EXPECT_EQ(b[25], 1);
  EXPECT_EQ(b[26], 0);
  EXPECT_EQ(b[27], 0);
  EXPECT_EQ((std::vector<std::uint8_t> {b.begin() + 28, b.begin() + 32}), (std::vector<std::uint8_t> {0xFE, 0xFF, 0xFF, 0xFF}));
  EXPECT_EQ((std::vector<std::uint8_t> {b.begin() + 68, b.begin() + 76}), (std::vector<std::uint8_t> {0x80, 0x07, 0, 0, 0x38, 0x04, 0, 0}));

  auto c = client_t::decode(b);
  ASSERT_TRUE(c);
  EXPECT_EQ(c->el, 10);
  EXPECT_EQ(c->et, 20);
  EXPECT_EQ(c->er, 110);
  EXPECT_EQ(c->eb, 60);
  EXPECT_EQ(c->ax, -2);
  EXPECT_TRUE(client_t::trusted_activation(*c));

  auto round = tc::decode(b);
  ASSERT_TRUE(round);
  EXPECT_EQ(round->flags, p.flags);
  EXPECT_EQ(round->activation_id, p.activation_id);
  EXPECT_EQ(round->input_token, p.input_token);
  EXPECT_EQ(round->element, p.element);
}

TEST(TextContextWire, DecodeRejectsWhatTheClientRejects) {
  tc::packet_t p;
  p.capture_width = 100;
  p.capture_height = 100;
  auto good = tc::encode(p);
  EXPECT_TRUE(tc::decode(good));

  auto bad = good;
  bad[0] = 2;
  EXPECT_FALSE(tc::decode(bad));
  EXPECT_FALSE(client_t::decode(bad));
  bad = good;
  bad[26] = 1;
  EXPECT_FALSE(tc::decode(bad));
  bad = good;
  bad.pop_back();
  EXPECT_FALSE(tc::decode(bad));
  p.capture_width = 0;
  EXPECT_FALSE(tc::decode(tc::encode(p)));
  p.capture_width = 1'000'001;
  EXPECT_FALSE(client_t::decode(tc::encode(p)));
}

TEST(TextContextWire, NegotiationBitsMatchTheClient) {
  EXPECT_EQ(tc::kControlPacketType, 0x550C);
  EXPECT_EQ(tc::kHostFeatureFlag, 0x200u);  // LI_FF_REMOTE_TEXT_CONTEXT
  EXPECT_EQ(tc::kMlFeatureFlag, 0x10u);  // ML_FF_REMOTE_TEXT_CONTEXT
}

// ---- AT-SPI classification ----------------------------------------------------------------

TEST(TextContextAtspi, ClassifiesTextRolesAndStates) {
  namespace a = tc::atspi;
  auto states = [](std::initializer_list<std::uint32_t> bits) {
    std::vector<std::uint32_t> words(2, 0);
    for (auto b : bits) {
      words[b / 32] |= 1u << (b % 32);
    }
    return words;
  };
  const std::array<std::int32_t, 4> extents {10, 20, 200, 30};
  const std::array<std::int32_t, 4> caret {50, 22, 0, 26};

  auto entry = a::classify("app/1", a::kRoleEntry, states({a::kStateFocused, a::kStateEditable, a::kStateShowing}), extents, caret);
  EXPECT_TRUE(entry.focused);
  EXPECT_TRUE(entry.editable);
  EXPECT_FALSE(entry.password);
  EXPECT_FALSE(entry.multiline);
  ASSERT_TRUE(entry.element_rect);
  EXPECT_EQ(*entry.element_rect, (tc::rect_t {10, 20, 210, 50}));
  ASSERT_TRUE(entry.caret_rect);
  EXPECT_EQ(*entry.caret_rect, (tc::rect_t {50, 22, 51, 48}));  // Zero-width caret gets 1 px.

  auto password = a::classify("app/2", a::kRolePasswordText, states({a::kStateFocused, a::kStateEditable}), extents, caret);
  EXPECT_TRUE(password.password);
  EXPECT_TRUE(password.editable);
  EXPECT_FALSE(password.caret_rect) << "a password caret reveals the length";

  auto view = a::classify("app/3", a::kRoleText, states({a::kStateFocused, a::kStateEditable, a::kStateMultiLine}), extents, std::nullopt);
  EXPECT_TRUE(view.editable);
  EXPECT_TRUE(view.multiline);

  auto label = a::classify("app/4", a::kRoleText, states({a::kStateFocused}), extents, std::nullopt);
  EXPECT_FALSE(label.editable) << "a non-editable text role (a label) takes no text";

  auto read_only = a::classify("app/5", a::kRoleEntry, states({a::kStateFocused, a::kStateEditable, a::kStateReadOnly}), extents, std::nullopt);
  EXPECT_FALSE(read_only.editable);

  auto button = a::classify("app/6", 43 /* PUSH_BUTTON */, states({a::kStateFocused}), extents, std::nullopt);
  EXPECT_FALSE(button.editable);

  auto terminal = a::classify("app/7", a::kRoleTerminal, states({a::kStateFocused}), extents, std::nullopt);
  EXPECT_TRUE(terminal.editable);
  EXPECT_TRUE(terminal.multiline);

  auto offscreen = a::classify("app/8", a::kRoleEntry, states({a::kStateFocused, a::kStateEditable}), std::array<std::int32_t, 4> {-1, -1, 0, 0}, std::nullopt);
  EXPECT_FALSE(offscreen.element_rect);
}

// ---- Bridge -------------------------------------------------------------------------------

TEST_F(BridgeFixture, ClickThenFocusOpensAndBlurCloses) {
  const tc::rect_t rect {400, 300, 800, 340};
  click(1, {500, 320});
  t += 20ms;
  auto o = field("app/1", rect);
  o.caret_rect = tc::rect_t {510, 305, 511, 335};
  bridge.observe(o, t);
  EXPECT_TRUE(flush().empty()) << "waits for focus to settle";
  advance(70ms);
  auto out = flush();
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(client.opens, 1);
  ASSERT_TRUE(client.last);
  EXPECT_EQ(client.last->cause, 2);
  EXPECT_EQ(client.last->el, 300);  // capture-relative: 400 - 100
  EXPECT_EQ(client.last->et, 250);
  EXPECT_EQ(client.last->cw, 1920u);
  EXPECT_EQ(client.last->ax, 400);
  EXPECT_TRUE(client.last->flags & tc::kFlagAnchorPoint);
  EXPECT_TRUE(client.last->flags & tc::kFlagCaretRect);
  EXPECT_EQ(client.last->cl, 410);

  t += 1s;
  bridge.observe(blur("app/1"), t);
  EXPECT_TRUE(flush().empty()) << "grace period: focus may come back";
  advance(350ms);
  ASSERT_EQ(flush().size(), 1u);
  EXPECT_EQ(client.closes, 1);
  EXPECT_EQ(client.active, 0u);
  EXPECT_EQ(client.ignored, 0);
}

TEST_F(BridgeFixture, FocusWithoutAClientClickIsNeverSent) {
  bridge.observe(field("app/1", {400, 300, 800, 340}), t);
  advance(500ms);
  EXPECT_TRUE(flush().empty());

  // A click long before the focus change does not count either.
  click(1, {500, 320});
  t += 2s;
  bridge.observe(blur("app/1"), t);
  bridge.observe(field("app/2", {400, 300, 800, 340}), t);
  advance(500ms);
  EXPECT_TRUE(flush().empty());
}

TEST_F(BridgeFixture, ClickOutsideTheFieldOrADragDoesNotOpen) {
  click(1, {100, 100});
  bridge.observe(field("app/1", {400, 300, 800, 340}), t);
  advance(200ms);
  EXPECT_TRUE(flush().empty());

  bridge.on_pointer(1, tc::cause_e::remote_touch, tc::phase_e::down, 3, tc::point_t {500, 320}, 0, t);
  bridge.on_pointer(1, tc::cause_e::remote_touch, tc::phase_e::move, 3, tc::point_t {600, 320}, 100, t);
  bridge.on_pointer(1, tc::cause_e::remote_touch, tc::phase_e::up, 3, tc::point_t {600, 320}, 100, t);
  bridge.observe(blur("app/1"), t);
  bridge.observe(field("app/2", {400, 300, 800, 340}), t);
  advance(200ms);
  EXPECT_TRUE(flush().empty()) << "a drag selects text, it does not ask for a keyboard";
}

TEST_F(BridgeFixture, ClickIntoTheAlreadyFocusedFieldOpens) {
  bridge.observe(field("app/1", {400, 300, 800, 340}), t);  // Focused before the stream started.
  advance(200ms);
  EXPECT_TRUE(flush().empty());
  click(1, {450, 310}, tc::cause_e::remote_touch);
  advance(100ms);
  ASSERT_EQ(flush().size(), 1u);
  EXPECT_EQ(client.opens, 1);
  EXPECT_EQ(client.last->cause, 1);
}

TEST_F(BridgeFixture, PasswordFieldIsFlaggedWithoutCaret) {
  click(1, {500, 320});
  auto o = field("app/pw", {400, 300, 800, 340});
  o.password = true;
  o.caret_rect = tc::rect_t {510, 305, 511, 335};
  bridge.observe(o, t);
  advance(100ms);
  flush();
  ASSERT_EQ(client.opens, 1);
  EXPECT_TRUE(client.last->flags & tc::kFlagPassword);
  EXPECT_FALSE(client.last->flags & tc::kFlagCaretRect);
  EXPECT_EQ(client.last->cl, 0);
}

TEST_F(BridgeFixture, TabbingToTheNextFieldKeepsTheKeyboardOpen) {
  click(1, {500, 320});
  bridge.observe(field("app/1", {400, 300, 800, 340}), t);
  advance(100ms);
  flush();
  ASSERT_EQ(client.opens, 1);

  t += 1s;
  // Tab: the new field's focus may come before or after the old one's blur.
  bridge.observe(field("app/2", {400, 400, 800, 440}), t);
  bridge.observe(blur("app/1"), t);
  advance(500ms);
  EXPECT_TRUE(flush().empty());
  EXPECT_NE(client.active, 0u);

  // Leaving the second field for a button closes.
  t += 1s;
  bridge.observe(blur("app/2"), t);
  tc::observation_t button;
  button.element = "app/button";
  button.focused = true;
  bridge.observe(button, t);
  advance(400ms);
  flush();
  EXPECT_EQ(client.closes, 1);
}

TEST_F(BridgeFixture, FocusBouncingBackWithinTheGraceSendsNothing) {
  click(1, {500, 320});
  bridge.observe(field("app/1", {400, 300, 800, 340}), t);
  advance(100ms);
  flush();
  t += 1s;
  bridge.observe(blur("app/1"), t);
  t += 100ms;
  bridge.observe(field("app/1", {400, 300, 800, 340}), t);  // A popup took focus for a moment.
  advance(500ms);
  EXPECT_TRUE(flush().empty());
  EXPECT_NE(client.active, 0u);
}

TEST_F(BridgeFixture, OpenedAndLeftBeforeSendingSendsNothing) {
  click(1, {500, 320});
  bridge.observe(field("app/1", {400, 300, 800, 340}), t);
  t += 10ms;
  bridge.observe(blur("app/1"), t);
  advance(500ms);
  EXPECT_TRUE(flush().empty());
}

TEST_F(BridgeFixture, FocusMovingToAnotherClientClosesTheFirst) {
  bridge.session_started(2);
  click(1, {500, 320});
  bridge.observe(field("app/1", {400, 300, 800, 340}), t);
  advance(100ms);
  auto out = flush();
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].session, 1u);

  t += 1s;
  click(2, {500, 420});
  bridge.observe(blur("app/1"), t);
  bridge.observe(field("app/2", {400, 400, 800, 440}), t);
  advance(100ms);
  out = flush();
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(client.closes, 1) << "session 1 closes its keyboard";
  EXPECT_TRUE(std::ranges::any_of(out, [](const auto &o) {
    return o.session == 2 && client_t::decode(o.bytes) && client_t::trusted_activation(*client_t::decode(o.bytes));
  }));
}

TEST_F(BridgeFixture, RevisionsIncreaseAndActivationsCount) {
  for (int i = 0; i < 3; ++i) {
    click(1, {500, 320});
    bridge.observe(field("app/" + std::to_string(i), {400, 300, 800, 340}), t);
    advance(100ms);
    flush();
    t += 1s;
    bridge.observe(blur("app/" + std::to_string(i)), t);
    advance(400ms);
    flush();
    t += 1s;
  }
  EXPECT_EQ(client.opens, 3);
  EXPECT_EQ(client.closes, 3);
  EXPECT_EQ(client.ignored, 0);
  EXPECT_EQ(client.last->activation, 3u);
  EXPECT_EQ(client.last->revision, 6u);
}

TEST_F(BridgeFixture, StoppedSessionGetsNothing) {
  click(1, {500, 320});
  bridge.observe(field("app/1", {400, 300, 800, 340}), t);
  bridge.session_stopped(1);
  advance(200ms);
  EXPECT_TRUE(flush().empty());
  EXPECT_EQ(bridge.session_count(), 0u);
}

TEST_F(BridgeFixture, ResetClosesAnOpenKeyboardAtOnce) {
  click(1, {500, 320});
  bridge.observe(field("app/1", {400, 300, 800, 340}), t);
  advance(100ms);
  flush();
  ASSERT_EQ(client.opens, 1);
  t += 1s;
  bridge.observe_reset();
  bridge.tick(clock_t_::now() + 1h);
  for (auto &o : bridge.drain()) {
    client.push(o.bytes);
  }
  EXPECT_EQ(client.closes, 1);
}

TEST_F(BridgeFixture, NoGeometryNoPacket) {
  bridge.set_capture_geometry({});
  click(1, {500, 320});
  bridge.observe(field("app/1", {400, 300, 800, 340}), t);
  advance(200ms);
  EXPECT_TRUE(flush().empty());
}

// ---- Service: input sniffing and the supervisor --------------------------------------------

namespace {
  struct ServiceFixture: ::testing::Test {
    std::shared_ptr<fake_source_t::shared_t> shared = std::make_shared<fake_source_t::shared_t>();
    std::mutex target_mutex;
    tc::target_t target;
    std::unique_ptr<tc::service_t> service;

    void SetUp() override {
      service = std::make_unique<tc::service_t>(
        [this](const tc::target_t &t) {
          return std::make_unique<fake_source_t>(shared, t);
        },
        [] {
          return clock_t_::now();
        },
        [this] {
          std::lock_guard lock {target_mutex};
          return target;
        },
        tc::tuning_t {.activation_settle = 0ms, .deactivation_grace = 0ms, .min_packet_gap = 0ms}
      );
      service->set_capture_geometry({0, 0, 1000, 500});
    }

    void TearDown() override {
      service.reset();
    }

    void post(const tc::observation_t &o) {
      fake_source_t::callback_t cb;
      {
        std::lock_guard lock {shared->mutex};
        cb = shared->callback;
      }
      ASSERT_TRUE(cb);
      cb(o);
    }

    std::vector<tc::outbound_t> drain_soon() {
      std::vector<tc::outbound_t> out;
      eventually([&] {
        for (auto &o : service->drain_outbound()) {
          out.push_back(std::move(o));
        }
        return !out.empty();
      },
                 1s);
      return out;
    }
  };
}  // namespace

TEST_F(ServiceFixture, OnlyCapableClientsRegister) {
  EXPECT_FALSE(service->session_started(1, 0x03, true)) << "client without ML_FF_REMOTE_TEXT_CONTEXT";
  EXPECT_FALSE(service->session_started(2, tc::kMlFeatureFlag, false)) << "host setting off";
  EXPECT_FALSE(service->active());
  EXPECT_TRUE(service->session_started(3, tc::kMlFeatureFlag | 0x03, true));
  EXPECT_TRUE(service->active());
  EXPECT_TRUE(service->wait_settled(2s));
  EXPECT_TRUE(service->source_running());
  service->session_stopped(3);
  EXPECT_FALSE(service->active());
  EXPECT_TRUE(eventually([&] {
    return !service->source_running();
  }));
  std::lock_guard lock {shared->mutex};
  EXPECT_EQ(shared->stopped, 1);
}

TEST_F(ServiceFixture, TouchTapOnAFieldOpensTheKeyboard) {
  ASSERT_TRUE(service->session_started(1, tc::kMlFeatureFlag, true));
  ASSERT_TRUE(service->wait_settled(2s));
  // Field at x 400..600, y 200..240 of a 1000x500 capture; tap at (0.5, 0.44) = (500, 220).
  service->on_input(1, touch(LI_TOUCH_EVENT_DOWN, 5, 0.5f, 0.44f));
  service->on_input(1, touch(LI_TOUCH_EVENT_UP, 5, 0.5f, 0.44f));
  post(field("app/1", {400, 200, 600, 240}));
  auto out = drain_soon();
  ASSERT_EQ(out.size(), 1u);
  auto c = client_t::decode(out[0].bytes);
  ASSERT_TRUE(c);
  EXPECT_TRUE(client_t::trusted_activation(*c));
  EXPECT_EQ(c->cause, 1);
  EXPECT_EQ(c->ax, 500);
  EXPECT_EQ(c->ay, 220);

  post(blur("app/1"));
  out = drain_soon();
  ASSERT_EQ(out.size(), 1u);
  EXPECT_TRUE(client_t::trusted_deactivation(*client_t::decode(out[0].bytes), c->activation));
}

TEST_F(ServiceFixture, AbsoluteMouseClickUsesTheClientPosition) {
  ASSERT_TRUE(service->session_started(1, tc::kMlFeatureFlag, true));
  ASSERT_TRUE(service->wait_settled(2s));
  {
    std::lock_guard lock {shared->mutex};
    shared->pointer = tc::point_t {5, 5};  // Where X says the pointer is (lagging).
  }
  service->on_input(1, abs_move(960, 400, 1920, 1000));  // (500, 200) on the capture.
  service->on_input(1, button(true));
  service->on_input(1, button(false));
  post(field("app/1", {400, 180, 600, 220}));
  auto out = drain_soon();
  ASSERT_EQ(out.size(), 1u);
  auto c = client_t::decode(out[0].bytes);
  ASSERT_TRUE(c);
  EXPECT_EQ(c->cause, 2);
  EXPECT_EQ(c->ax, 500);
  EXPECT_EQ(c->ay, 200);
}

TEST_F(ServiceFixture, RelativeMouseClickAsksTheSourceWhereThePointerIs) {
  ASSERT_TRUE(service->session_started(1, tc::kMlFeatureFlag, true));
  ASSERT_TRUE(service->wait_settled(2s));
  {
    std::lock_guard lock {shared->mutex};
    shared->pointer = tc::point_t {450, 210};
  }
  service->on_input(1, button(true));
  service->on_input(1, button(false));
  post(field("app/1", {400, 180, 600, 220}));
  auto out = drain_soon();
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(client_t::decode(out[0].bytes)->ax, 450);

  // A right click does not count.
  post(blur("app/1"));
  drain_soon();
  service->on_input(1, button(true, BUTTON_RIGHT));
  service->on_input(1, button(false, BUTTON_RIGHT));
  post(field("app/2", {400, 180, 600, 220}));
  std::this_thread::sleep_for(50ms);
  EXPECT_TRUE(service->drain_outbound().empty());
}

TEST_F(ServiceFixture, InputFromUnregisteredSessionsIsIgnored) {
  ASSERT_TRUE(service->session_started(1, tc::kMlFeatureFlag, true));
  ASSERT_TRUE(service->wait_settled(2s));
  service->on_input(9, touch(LI_TOUCH_EVENT_DOWN, 1, 0.5f, 0.44f));
  service->on_input(9, touch(LI_TOUCH_EVENT_UP, 1, 0.5f, 0.44f));
  post(field("app/1", {400, 200, 600, 240}));
  std::this_thread::sleep_for(50ms);
  EXPECT_TRUE(service->drain_outbound().empty());
  // Short or garbage packets are ignored.
  service->on_input(1, std::vector<std::uint8_t> {1, 2, 3});
}

TEST_F(ServiceFixture, SourceFollowsTheCaptureToAnotherDisplay) {
  ASSERT_TRUE(service->session_started(1, tc::kMlFeatureFlag, true));
  ASSERT_TRUE(service->wait_settled(2s));
  {
    std::lock_guard lock {target_mutex};
    target = {":21", "/tmp/xauth", "/tmp/session"};
  }
  EXPECT_TRUE(eventually([&] {
    std::lock_guard lock {shared->mutex};
    return shared->started.size() == 2;
  }));
  std::lock_guard lock {shared->mutex};
  EXPECT_EQ(shared->started[0].display, "");
  EXPECT_EQ(shared->started[1].display, ":21");
  EXPECT_EQ(shared->started[1].session_dir, "/tmp/session");
  EXPECT_EQ(shared->stopped, 1);
}

TEST_F(ServiceFixture, BrokenSourceIsReplaced) {
  ASSERT_TRUE(service->session_started(1, tc::kMlFeatureFlag, true));
  ASSERT_TRUE(service->wait_settled(2s));
  shared->broken = true;
  EXPECT_TRUE(eventually([&] {
    std::lock_guard lock {shared->mutex};
    if (shared->stopped >= 1) {
      shared->broken = false;
    }
    return shared->started.size() >= 2;
  }));
}

TEST_F(ServiceFixture, NoAccessibilityBusMeansNoSourceButNoHarm) {
  {
    std::lock_guard lock {shared->mutex};
    shared->refuse = true;
  }
  ASSERT_TRUE(service->session_started(1, tc::kMlFeatureFlag, true));
  ASSERT_TRUE(service->wait_settled(2s));
  EXPECT_FALSE(service->source_running());
  service->on_input(1, touch(LI_TOUCH_EVENT_DOWN, 1, 0.5f, 0.44f));
  EXPECT_TRUE(service->drain_outbound().empty());
  service->session_stopped(1);
}

// ---- Settings and capabilities -------------------------------------------------------------

TEST(TextContextConfig, OnByDefaultAndAdvertisedAsCapability) {
  EXPECT_TRUE(config::input.remote_text_context);
  nova_api::host_features_t features {.text_context = true};
  auto caps = nova_api::capabilities("0.3.0", features, client_permissions::standard);
  const auto &list = caps["features"];
  EXPECT_NE(std::find(list.begin(), list.end(), nlohmann::json("text_context")), list.end());
  auto without = nova_api::capabilities("0.3.0", nova_api::host_features_t {}, client_permissions::standard);
  EXPECT_EQ(std::find(without["features"].begin(), without["features"].end(), nlohmann::json("text_context")), without["features"].end());
}

TEST(TextContextConfig, OffWhenTheSettingOrTheMouseIsOff) {
  const auto saved = config::input;
  config::input.remote_text_context = false;
  EXPECT_FALSE(tc::available());
  config::input.remote_text_context = true;
  config::input.mouse = false;
  EXPECT_FALSE(tc::available());
  config::input = saved;
}
