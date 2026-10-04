/**
 * @file tests/unit/test_input.cpp
 * @brief Tests for retained stream input and virtual gamepad lifecycle behavior.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

// moonlight-common-c includes
extern "C" {
#include <moonlight-common-c/src/Input.h>
}

// local includes
#include "src/config.h"
#include "src/input.h"
#include "src/platform/virtualhid_input.h"
#include "src/utility.h"

namespace {
  /**
   * @brief Protocol metadata for a fixed-size input packet.
   */
  struct input_packet_spec_t {
    std::uint32_t magic;  ///< Packet type identifier.
    std::size_t size;  ///< Complete fixed packet size.
  };

  /**
   * @brief Create raw input bytes with a caller-selected header and buffer size.
   *
   * @param magic Packet type identifier.
   * @param declared_size Packet size declared after the size field.
   * @param actual_size Number of bytes available in the packet buffer.
   * @return Raw packet bytes.
   */
  std::vector<std::uint8_t> make_input_packet(std::uint32_t magic, std::uint32_t declared_size, std::size_t actual_size) {
    std::vector<std::uint8_t> packet(actual_size);
    const NV_INPUT_HEADER header {
      util::endian::big(declared_size),
      util::endian::little(magic),
    };
    if (!packet.empty()) {
      std::memcpy(packet.data(), &header, std::min(packet.size(), sizeof(header)));
    }
    return packet;
  }

  /**
   * @brief Fixture that installs a fake global virtual input backend.
   */
  class InputGamepadSessionTest: public ::testing::Test {
  protected:
    /**
     * @brief Preserve configuration and install observable fake devices.
     */
    void SetUp() override {
      original_input_ = config::input;
      config::input.controller = true;
      config::input.gamepad = "xseries";

      auto platform_input = platf::input();
      ASSERT_TRUE(platform_input);
      auto &context = platf::virtualhid::get_input_context(platform_input);
      context = platf::virtualhid::input_context_t {lvh::BackendKind::fake};
      context_ = &context;
      runtime_ = context.runtime.get();
      ASSERT_NE(runtime_, nullptr);
      input::testing::set_platform_input(std::move(platform_input));
    }

    /**
     * @brief Destroy retained test sessions and restore configuration.
     */
    void TearDown() override {
      input::terminate_gamepads();
      input::testing::set_platform_input({});
      context_ = nullptr;
      runtime_ = nullptr;
      config::input = std::move(original_input_);
    }

    /**
     * @brief Access the fake runtime installed for the current test.
     *
     * @return Fake libvirtualhid runtime.
     */
    lvh::Runtime &runtime() const {
      return *runtime_;
    }

    /**
     * @brief Access the shared libvirtualhid input context installed for the test.
     *
     * @return Fake input context.
     */
    platf::virtualhid::input_context_t &context() const {
      return *context_;
    }

  private:
    platf::virtualhid::input_context_t *context_ = nullptr;  ///< Fake input context installed in the global backend.
    lvh::Runtime *runtime_ = nullptr;  ///< Fake runtime installed in the global input backend.
    config::input_t original_input_;  ///< Input configuration restored after each test.
  };
}  // namespace

TEST(InputPacketValidationTest, RejectsEveryBufferShorterThanTheHeader) {
  for (std::size_t actual_size = 0; actual_size < sizeof(NV_INPUT_HEADER); ++actual_size) {
    const auto packet = make_input_packet(UTF8_TEXT_EVENT_MAGIC, sizeof(std::uint32_t), actual_size);
    EXPECT_FALSE(input::testing::is_valid_input_packet(packet)) << "actual_size=" << actual_size;
  }
}

TEST(InputPacketValidationTest, RejectsDeclaredSizesOutsideTheAvailableBuffer) {
  constexpr std::uint32_t unknown_magic = 0x12345678;
  for (std::uint32_t declared_size = 0; declared_size < sizeof(std::uint32_t); ++declared_size) {
    const auto packet = make_input_packet(unknown_magic, declared_size, sizeof(NV_INPUT_HEADER));
    EXPECT_FALSE(input::testing::is_valid_input_packet(packet)) << "declared_size=" << declared_size;
  }

  const auto truncated_packet = make_input_packet(unknown_magic, sizeof(std::uint32_t) + 1, sizeof(NV_INPUT_HEADER));
  EXPECT_FALSE(input::testing::is_valid_input_packet(truncated_packet));

  const auto overflowing_size = make_input_packet(
    unknown_magic,
    std::numeric_limits<std::uint32_t>::max(),
    sizeof(NV_INPUT_HEADER)
  );
  EXPECT_FALSE(input::testing::is_valid_input_packet(overflowing_size));
}

TEST(InputPacketValidationTest, ValidatesUnicodePacketBoundsWithoutOverflow) {
  const auto empty_packet = make_input_packet(UTF8_TEXT_EVENT_MAGIC, sizeof(std::uint32_t), sizeof(NV_INPUT_HEADER));
  EXPECT_TRUE(input::testing::is_valid_input_packet(empty_packet));

  constexpr std::uint32_t maximum_declared_size = sizeof(std::uint32_t) + UTF8_TEXT_EVENT_MAX_COUNT;
  const auto maximum_packet = make_input_packet(
    UTF8_TEXT_EVENT_MAGIC,
    maximum_declared_size,
    sizeof(std::uint32_t) + maximum_declared_size
  );
  EXPECT_TRUE(input::testing::is_valid_input_packet(maximum_packet));

  const auto oversized_text = make_input_packet(
    UTF8_TEXT_EVENT_MAGIC,
    maximum_declared_size + 1,
    sizeof(std::uint32_t) + maximum_declared_size + 1
  );
  EXPECT_FALSE(input::testing::is_valid_input_packet(oversized_text));

  const auto truncated_text = make_input_packet(UTF8_TEXT_EVENT_MAGIC, maximum_declared_size, sizeof(NV_INPUT_HEADER));
  EXPECT_FALSE(input::testing::is_valid_input_packet(truncated_text));
}

TEST(InputPacketValidationTest, EnforcesEveryFixedPacketSize) {
  constexpr std::array packet_specs {
    input_packet_spec_t {MOUSE_MOVE_REL_MAGIC_GEN5, sizeof(NV_REL_MOUSE_MOVE_PACKET)},
    input_packet_spec_t {MOUSE_MOVE_ABS_MAGIC, sizeof(NV_ABS_MOUSE_MOVE_PACKET)},
    input_packet_spec_t {MOUSE_BUTTON_DOWN_EVENT_MAGIC_GEN5, sizeof(NV_MOUSE_BUTTON_PACKET)},
    input_packet_spec_t {MOUSE_BUTTON_UP_EVENT_MAGIC_GEN5, sizeof(NV_MOUSE_BUTTON_PACKET)},
    input_packet_spec_t {SCROLL_MAGIC_GEN5, sizeof(NV_SCROLL_PACKET)},
    input_packet_spec_t {SS_HSCROLL_MAGIC, sizeof(SS_HSCROLL_PACKET)},
    input_packet_spec_t {KEY_DOWN_EVENT_MAGIC, sizeof(NV_KEYBOARD_PACKET)},
    input_packet_spec_t {KEY_UP_EVENT_MAGIC, sizeof(NV_KEYBOARD_PACKET)},
    input_packet_spec_t {MULTI_CONTROLLER_MAGIC_GEN5, sizeof(NV_MULTI_CONTROLLER_PACKET)},
    input_packet_spec_t {SS_TOUCH_MAGIC, sizeof(SS_TOUCH_PACKET)},
    input_packet_spec_t {SS_PEN_MAGIC, sizeof(SS_PEN_PACKET)},
    input_packet_spec_t {SS_CONTROLLER_ARRIVAL_MAGIC, sizeof(SS_CONTROLLER_ARRIVAL_PACKET)},
    input_packet_spec_t {SS_CONTROLLER_TOUCH_MAGIC, sizeof(SS_CONTROLLER_TOUCH_PACKET)},
    input_packet_spec_t {SS_CONTROLLER_MOTION_MAGIC, sizeof(SS_CONTROLLER_MOTION_PACKET)},
    input_packet_spec_t {SS_CONTROLLER_BATTERY_MAGIC, sizeof(SS_CONTROLLER_BATTERY_PACKET)},
  };

  for (const auto &[magic, size] : packet_specs) {
    const auto declared_size = static_cast<std::uint32_t>(size - sizeof(std::uint32_t));
    const auto valid_packet = make_input_packet(magic, declared_size, size);
    EXPECT_TRUE(input::testing::is_valid_input_packet(valid_packet)) << "magic=" << magic;

    const auto trailing_bytes = make_input_packet(magic, declared_size, size + 8);
    EXPECT_TRUE(input::testing::is_valid_input_packet(trailing_bytes)) << "magic=" << magic;

    const auto wrong_declared_size = make_input_packet(magic, declared_size - 1, size);
    EXPECT_FALSE(input::testing::is_valid_input_packet(wrong_declared_size)) << "magic=" << magic;

    const auto oversized_declared_size = make_input_packet(magic, declared_size + 1, size + 1);
    EXPECT_FALSE(input::testing::is_valid_input_packet(oversized_declared_size)) << "magic=" << magic;

    const auto truncated_packet = make_input_packet(magic, declared_size, size - 1);
    EXPECT_FALSE(input::testing::is_valid_input_packet(truncated_packet)) << "magic=" << magic;
  }
}

TEST(InputPacketValidationTest, PreservesUnknownPacketHandlingWithinDeclaredBounds) {
  constexpr std::uint32_t unknown_magic = 0x12345678;
  const auto packet = make_input_packet(unknown_magic, sizeof(std::uint32_t), sizeof(NV_INPUT_HEADER));
  EXPECT_TRUE(input::testing::is_valid_input_packet(packet));
}

TEST_F(InputGamepadSessionTest, RejectsMalformedBatchablePacketsAtQueueIngress) {
  ASSERT_FALSE(task_pool.running());
  const std::shared_ptr<input::input_t> empty_input;
  EXPECT_EQ(input::testing::queued_input_packet_count(empty_input), 0);

  auto stream_input = input::alloc(std::make_shared<safe::mail_raw_t>(), "packet-validation-client");
  ASSERT_NE(stream_input, nullptr);

  auto short_header = make_input_packet(SS_PEN_MAGIC, sizeof(std::uint32_t), sizeof(NV_INPUT_HEADER) - 1);
  input::passthrough(stream_input, std::move(short_header));
  EXPECT_EQ(input::testing::queued_input_packet_count(stream_input), 0);

  constexpr std::array batchable_packet_specs {
    input_packet_spec_t {MOUSE_MOVE_REL_MAGIC_GEN5, sizeof(NV_REL_MOUSE_MOVE_PACKET)},
    input_packet_spec_t {MOUSE_MOVE_ABS_MAGIC, sizeof(NV_ABS_MOUSE_MOVE_PACKET)},
    input_packet_spec_t {SCROLL_MAGIC_GEN5, sizeof(NV_SCROLL_PACKET)},
    input_packet_spec_t {SS_HSCROLL_MAGIC, sizeof(SS_HSCROLL_PACKET)},
    input_packet_spec_t {MULTI_CONTROLLER_MAGIC_GEN5, sizeof(NV_MULTI_CONTROLLER_PACKET)},
    input_packet_spec_t {SS_TOUCH_MAGIC, sizeof(SS_TOUCH_PACKET)},
    input_packet_spec_t {SS_PEN_MAGIC, sizeof(SS_PEN_PACKET)},
    input_packet_spec_t {SS_CONTROLLER_TOUCH_MAGIC, sizeof(SS_CONTROLLER_TOUCH_PACKET)},
    input_packet_spec_t {SS_CONTROLLER_MOTION_MAGIC, sizeof(SS_CONTROLLER_MOTION_PACKET)},
  };

  std::size_t valid_packet_count = 0;
  for (const auto &[magic, size] : batchable_packet_specs) {
    const auto declared_size = static_cast<std::uint32_t>(size - sizeof(std::uint32_t));

    auto truncated_packet = make_input_packet(magic, declared_size, size - 1);
    input::passthrough(stream_input, std::move(truncated_packet));
    EXPECT_EQ(input::testing::queued_input_packet_count(stream_input), valid_packet_count) << "magic=" << magic;

    auto valid_packet = make_input_packet(magic, declared_size, size);
    input::passthrough(stream_input, std::move(valid_packet));
    ++valid_packet_count;
    EXPECT_EQ(input::testing::queued_input_packet_count(stream_input), valid_packet_count) << "magic=" << magic;

    auto oversized_declared_size = make_input_packet(magic, declared_size + 1, size + 1);
    input::passthrough(stream_input, std::move(oversized_declared_size));
    EXPECT_EQ(input::testing::queued_input_packet_count(stream_input), valid_packet_count) << "magic=" << magic;
  }
}

TEST_F(InputGamepadSessionTest, ReusesGamepadsAcrossPauseAndDestroysThemOnTermination) {
  const std::string session_id = "paired-client-certificate";
  auto first_mail = std::make_shared<safe::mail_raw_t>();
  auto first = input::alloc(first_mail, session_id);
  ASSERT_NE(first, nullptr);
  const auto active_devices_before_gamepad = runtime().active_device_count();

  const platf::gamepad_arrival_t metadata {LI_CTYPE_XBOX, 0, 0};
  const auto original_id = input::testing::alloc_gamepad(first, 0, metadata);
  ASSERT_GE(original_id, 0);
  EXPECT_EQ(runtime().active_device_count(), active_devices_before_gamepad + 1);

  const std::weak_ptr<input::input_t> paused = first;
  first.reset();
  EXPECT_FALSE(paused.expired());
  EXPECT_EQ(runtime().active_device_count(), active_devices_before_gamepad + 1);

  auto resumed_mail = std::make_shared<safe::mail_raw_t>();
  auto resumed = input::alloc(resumed_mail, session_id);
  ASSERT_NE(resumed, nullptr);
  EXPECT_EQ(paused.lock(), resumed);
  EXPECT_EQ(input::testing::gamepad_id(resumed, 0), original_id);
  EXPECT_EQ(runtime().active_device_count(), active_devices_before_gamepad + 1);

  input::terminate_gamepads(session_id);
  EXPECT_EQ(input::testing::gamepad_id(resumed, 0), -1);
  EXPECT_EQ(runtime().active_device_count(), active_devices_before_gamepad);

  auto replacement = input::alloc(std::make_shared<safe::mail_raw_t>(), session_id);
  EXPECT_NE(replacement, resumed);
}

TEST_F(InputGamepadSessionTest, AnnouncedControllerGetsAPadOnlyOnItsFirstRealInput) {
  auto stream_input = input::alloc(std::make_shared<safe::mail_raw_t>(), "lazy-pad-client");
  ASSERT_NE(stream_input, nullptr);
  const auto devices_before = runtime().active_device_count();

  // Arrival events are followed by an all-zero state: neither creates a pad.
  input::testing::send_controller_arrival(stream_input, 0, LI_CTYPE_XBOX, LI_CCAP_ANALOG_TRIGGERS);
  input::testing::send_controller_state(stream_input, 0, 0x1, 0);
  // Stick noise below 1/8 doesn't either.
  input::testing::send_controller_state(stream_input, 0, 0x1, 0, 0, 0, 1200, -900);
  EXPECT_EQ(input::testing::gamepad_id(stream_input, 0), -1);
  EXPECT_EQ(runtime().active_device_count(), devices_before);

  input::testing::send_controller_state(stream_input, 0, 0x1, platf::A);
  EXPECT_EQ(input::testing::gamepad_id(stream_input, 0), 0);
  EXPECT_EQ(runtime().active_device_count(), devices_before + 1);
}

TEST_F(InputGamepadSessionTest, PhantomArrivalDoesNotTakePlayerOneFromThePlayedPad) {
  auto stream_input = input::alloc(std::make_shared<safe::mail_raw_t>(), "phantom-client");
  ASSERT_NE(stream_input, nullptr);
  const auto devices_before = runtime().active_device_count();

  // A client announces two controllers (one of them a phantom) but only plays controller 1.
  input::testing::send_controller_arrival(stream_input, 0, LI_CTYPE_UNKNOWN, 0);
  input::testing::send_controller_state(stream_input, 0, 0x3, 0);
  input::testing::send_controller_arrival(stream_input, 1, LI_CTYPE_XBOX, LI_CCAP_ANALOG_TRIGGERS);
  input::testing::send_controller_state(stream_input, 1, 0x3, 0, 0, 0, 20000, 0);

  EXPECT_EQ(input::testing::gamepad_id(stream_input, 0), -1);
  EXPECT_EQ(input::testing::gamepad_id(stream_input, 1), 0) << "the played pad must be the first virtual pad (XInput player 1)";
  EXPECT_EQ(runtime().active_device_count(), devices_before + 1);
}

TEST_F(InputGamepadSessionTest, ReconnectFromTheSameDeviceReusesItsPadInsteadOfAddingAGhost) {
  const std::string session_id = "same-device-certificate";
  auto first = input::alloc(std::make_shared<safe::mail_raw_t>(), session_id);
  const auto devices_before = runtime().active_device_count();
  input::testing::send_controller_arrival(first, 0, LI_CTYPE_XBOX, 0);
  input::testing::send_controller_state(first, 0, 0x1, platf::A);
  ASSERT_EQ(input::testing::gamepad_id(first, 0), 0);
  first.reset();  // session A ends; its input state is retained for a resume

  auto second = input::alloc(std::make_shared<safe::mail_raw_t>(), session_id);
  input::testing::send_controller_arrival(second, 0, LI_CTYPE_XBOX, 0);
  input::testing::send_controller_state(second, 0, 0x1, platf::B);
  input::testing::release_stale_gamepads(second);  // the grace period passes

  EXPECT_EQ(input::testing::gamepad_id(second, 0), 0);
  EXPECT_EQ(runtime().active_device_count(), devices_before + 1) << "exactly one virtual pad";
}

TEST_F(InputGamepadSessionTest, ReconnectUsingAnotherControllerNumberTakesOverTheLeftoverPad) {
  const std::string session_id = "renumbered-device-certificate";
  auto first = input::alloc(std::make_shared<safe::mail_raw_t>(), session_id);
  const auto devices_before = runtime().active_device_count();
  input::testing::send_controller_state(first, 0, 0x1, platf::A);
  ASSERT_EQ(input::testing::gamepad_id(first, 0), 0);
  first.reset();

  auto second = input::alloc(std::make_shared<safe::mail_raw_t>(), session_id);
  input::testing::send_controller_arrival(second, 2, LI_CTYPE_XBOX, 0);
  input::testing::send_controller_state(second, 2, 0x4, platf::A);
  input::testing::release_stale_gamepads(second);

  EXPECT_EQ(input::testing::gamepad_id(second, 0), -1);
  EXPECT_EQ(input::testing::gamepad_id(second, 2), 0) << "the leftover pad keeps player 1";
  EXPECT_EQ(runtime().active_device_count(), devices_before + 1);
}

TEST_F(InputGamepadSessionTest, LeftoverPadsTheReconnectedClientNeverUsesAreRemoved) {
  const std::string session_id = "ghost-device-certificate";
  auto first = input::alloc(std::make_shared<safe::mail_raw_t>(), session_id);
  const auto devices_before = runtime().active_device_count();
  input::testing::send_controller_state(first, 0, 0x3, platf::A);
  input::testing::send_controller_state(first, 1, 0x3, platf::A);
  ASSERT_EQ(runtime().active_device_count(), devices_before + 2);
  first.reset();

  auto second = input::alloc(std::make_shared<safe::mail_raw_t>(), session_id);
  input::testing::send_controller_state(second, 1, 0x2, platf::X);
  input::testing::release_stale_gamepads(second);

  EXPECT_EQ(input::testing::gamepad_id(second, 0), -1);
  EXPECT_EQ(input::testing::gamepad_id(second, 1), 1);
  EXPECT_EQ(runtime().active_device_count(), devices_before + 1) << "the unused leftover pad is gone";
}

TEST_F(InputGamepadSessionTest, ReconnectedClientThatPlaysNoPadYetKeepsTheGamesPad) {
  // The game bound the pad on the first connection. The client comes back with its on-screen
  // controls off, so nothing is played for longer than the grace period; the pad must still be
  // there when the controls are turned on mid-stream, not replaced by a new one the running game
  // has to pick up as a hot-plugged controller.
  const std::string session_id = "controls-off-on-reconnect";
  auto first = input::alloc(std::make_shared<safe::mail_raw_t>(), session_id);
  const auto devices_before = runtime().active_device_count();
  input::testing::send_controller_arrival(first, 0, LI_CTYPE_XBOX, 0);
  input::testing::send_controller_state(first, 0, 0x1, platf::A);
  ASSERT_EQ(input::testing::gamepad_id(first, 0), 0);
  first.reset();

  auto second = input::alloc(std::make_shared<safe::mail_raw_t>(), session_id);
  input::testing::release_stale_gamepads(second);  // the grace period passes with nothing played
  EXPECT_EQ(runtime().active_device_count(), devices_before + 1) << "the game's pad stays";

  // The controls are turned on mid-stream: the first press lands on the pad the game knows.
  input::testing::send_controller_arrival(second, 0, LI_CTYPE_XBOX, 0);
  input::testing::send_controller_state(second, 0, 0x1, 0);
  input::testing::send_controller_state(second, 0, 0x1, platf::A);
  EXPECT_EQ(input::testing::gamepad_id(second, 0), 0);
  EXPECT_EQ(runtime().active_device_count(), devices_before + 1) << "no second pad";
}

TEST_F(InputGamepadSessionTest, LeftoversGoOnceTheReconnectedClientPlaysAnotherPad) {
  const std::string session_id = "late-player-certificate";
  auto first = input::alloc(std::make_shared<safe::mail_raw_t>(), session_id);
  const auto devices_before = runtime().active_device_count();
  input::testing::send_controller_state(first, 0, 0x3, platf::A);
  input::testing::send_controller_state(first, 1, 0x3, platf::A);
  ASSERT_EQ(runtime().active_device_count(), devices_before + 2);
  first.reset();

  auto second = input::alloc(std::make_shared<safe::mail_raw_t>(), session_id);
  input::testing::release_stale_gamepads(second);  // nothing played yet: both stay
  EXPECT_EQ(runtime().active_device_count(), devices_before + 2);

  // Later only controller 0 is played: the other leftover goes after the grace period.
  input::testing::send_controller_state(second, 0, 0x1, platf::B);
  input::testing::release_stale_gamepads(second);
  EXPECT_EQ(input::testing::gamepad_id(second, 0), 0);
  EXPECT_EQ(input::testing::gamepad_id(second, 1), -1);
  EXPECT_EQ(runtime().active_device_count(), devices_before + 1);
}

TEST_F(InputGamepadSessionTest, RefreshesSharedVirtualInputAfterLicenseStateChanges) {
  ASSERT_NE(context().keyboard, nullptr);
  ASSERT_NE(context().mouse, nullptr);
  const auto original_keyboard_id = context().keyboard->device_id();
  const auto original_mouse_id = context().mouse->device_id();
  const auto active_devices = runtime().active_device_count();

  input::refresh_virtual_input();

  ASSERT_NE(context().keyboard, nullptr);
  ASSERT_NE(context().mouse, nullptr);
  EXPECT_NE(context().keyboard->device_id(), original_keyboard_id);
  EXPECT_NE(context().mouse->device_id(), original_mouse_id);
  EXPECT_EQ(runtime().active_device_count(), active_devices);
}
