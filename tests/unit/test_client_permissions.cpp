/**
 * @file tests/unit/test_client_permissions.cpp
 * @brief Tests for per-client permission presets, their JSON form and input packet filtering.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

// moonlight-common-c includes
extern "C" {
#include <moonlight-common-c/src/Input.h>
}

// local includes
#include "src/client_permissions.h"
#include "src/input.h"
#include "src/utility.h"

namespace {
  namespace perm = client_permissions;

  /**
   * @brief Build the header of an input packet of the given type.
   *
   * @param magic Packet type identifier.
   * @return Header-only packet bytes (enough for permission classification).
   */
  std::vector<std::uint8_t> packet_of(const std::uint32_t magic) {
    std::vector<std::uint8_t> packet(sizeof(NV_INPUT_HEADER));
    const NV_INPUT_HEADER header {
      util::endian::big(static_cast<std::uint32_t>(sizeof(std::uint32_t))),
      util::endian::little(magic),
    };
    std::memcpy(packet.data(), &header, sizeof(header));
    return packet;
  }
}  // namespace

TEST(ClientPermissionsTest, PresetsNameTheirMasks) {
  EXPECT_EQ(perm::preset_name(perm::full), "full");
  EXPECT_EQ(perm::preset_name(perm::play), "play");
  EXPECT_EQ(perm::preset_name(perm::view_only), "view_only");
  EXPECT_EQ(perm::preset_name(perm::input_mouse), "custom");
  EXPECT_EQ(perm::preset_mask("play"), perm::play);
  EXPECT_FALSE(perm::preset_mask("admin").has_value());
  EXPECT_EQ(perm::sanitize(0xFFFFFFFFu), perm::full);
  EXPECT_FALSE(perm::has(perm::play, perm::clipboard));
  EXPECT_TRUE(perm::has(perm::play, perm::launch_apps | perm::input_controller));
}

TEST(ClientPermissionsTest, JsonRoundTripListsEveryFlagAndThePreset) {
  const auto node = perm::to_json(perm::play);
  EXPECT_EQ(node.at("preset"), "play");
  EXPECT_TRUE(node.at("input_keyboard").get<bool>());
  EXPECT_TRUE(node.at("launch_apps").get<bool>());
  EXPECT_FALSE(node.at("clipboard").get<bool>());
  EXPECT_EQ(node.size(), perm::flag_names.size() + 1);
}

TEST(ClientPermissionsTest, JsonUpdatesApplyPresetThenFlags) {
  EXPECT_EQ(perm::apply_json(perm::full, {{"preset", "view_only"}}), perm::view_only);
  EXPECT_EQ(perm::apply_json(perm::standard, {{"clipboard", false}}), perm::play);
  EXPECT_EQ(perm::apply_json(perm::full, {{"power", false}, {"host_commands", false}}), perm::standard);
  EXPECT_EQ(perm::apply_json(perm::play, {{"preset", "standard"}, {"power", true}}), perm::standard | perm::power);
  EXPECT_EQ(
    perm::apply_json(perm::full, {{"preset", "view_only"}, {"input_mouse", true}}),
    perm::input_mouse
  );
  EXPECT_EQ(perm::apply_json(perm::play, nlohmann::json::object()), perm::play);
}

TEST(ClientPermissionsTest, JsonUpdatesRejectBadInput) {
  EXPECT_THROW(perm::apply_json(perm::full, nlohmann::json::array()), std::invalid_argument);
  EXPECT_THROW(perm::apply_json(perm::full, {{"preset", "root"}}), std::invalid_argument);
  EXPECT_THROW(perm::apply_json(perm::full, {{"preset", 1}}), std::invalid_argument);
  EXPECT_THROW(perm::apply_json(perm::full, {{"camera", true}}), std::invalid_argument);
  EXPECT_THROW(perm::apply_json(perm::full, {{"clipboard", "no"}}), std::invalid_argument);
}

TEST(ClientPermissionsTest, InputPacketsNeedTheirOwnPermission) {
  const std::vector<std::pair<std::uint32_t, perm::mask_t>> cases {
    {KEY_DOWN_EVENT_MAGIC, perm::input_keyboard},
    {KEY_UP_EVENT_MAGIC, perm::input_keyboard},
    {UTF8_TEXT_EVENT_MAGIC, perm::input_keyboard},
    {MOUSE_MOVE_REL_MAGIC_GEN5, perm::input_mouse},
    {MOUSE_MOVE_ABS_MAGIC, perm::input_mouse},
    {MOUSE_BUTTON_DOWN_EVENT_MAGIC_GEN5, perm::input_mouse},
    {SCROLL_MAGIC_GEN5, perm::input_mouse},
    {SS_HSCROLL_MAGIC, perm::input_mouse},
    {MULTI_CONTROLLER_MAGIC_GEN5, perm::input_controller},
    {SS_CONTROLLER_ARRIVAL_MAGIC, perm::input_controller},
    {SS_CONTROLLER_MOTION_MAGIC, perm::input_controller},
    {SS_TOUCH_MAGIC, perm::input_touch_pen},
    {SS_PEN_MAGIC, perm::input_touch_pen},
  };

  for (const auto &[magic, needed] : cases) {
    const auto packet = packet_of(magic);
    EXPECT_TRUE(input::is_packet_permitted(packet, needed)) << std::hex << magic;
    EXPECT_TRUE(input::is_packet_permitted(packet, perm::full)) << std::hex << magic;
    EXPECT_FALSE(input::is_packet_permitted(packet, perm::full & ~needed)) << std::hex << magic;
    EXPECT_FALSE(input::is_packet_permitted(packet, perm::view_only)) << std::hex << magic;
  }
}

TEST(ClientPermissionsTest, UnknownAndShortPackets) {
  const auto unknown = packet_of(0x7F7F7F7F);
  EXPECT_TRUE(input::is_packet_permitted(unknown, perm::input_all));
  EXPECT_FALSE(input::is_packet_permitted(unknown, perm::input_all & ~perm::input_touch_pen));

  // Too short to classify: let passthrough() reject it as malformed.
  const std::vector<std::uint8_t> runt {0x01, 0x02};
  EXPECT_TRUE(input::is_packet_permitted(runt, perm::view_only));
}

TEST(ClientPermissionsTest, HostControlFlagsArePresetsAndDefaultOff) {
  EXPECT_EQ(perm::preset_name(perm::standard), "standard");
  EXPECT_EQ(perm::preset_mask("standard"), perm::standard);
  EXPECT_EQ(perm::paired_default, perm::standard);
  EXPECT_FALSE(perm::has(perm::paired_default, perm::power));
  EXPECT_FALSE(perm::has(perm::paired_default, perm::host_commands));
  EXPECT_FALSE(perm::has(perm::play, perm::host_commands));
  EXPECT_TRUE(perm::has(perm::full, perm::power | perm::host_commands));
  EXPECT_FALSE(perm::default_when_missing(perm::power));
  EXPECT_FALSE(perm::default_when_missing(perm::host_commands));
  EXPECT_TRUE(perm::default_when_missing(perm::clipboard));
  EXPECT_EQ(perm::preset_name(perm::standard | perm::power), "custom");
}

TEST(ClientPermissionsTest, AppProfilesFollowLaunchAppsWhenMissing) {
  EXPECT_TRUE(perm::default_when_missing(perm::app_profiles, true));
  EXPECT_FALSE(perm::default_when_missing(perm::app_profiles, false));
  EXPECT_TRUE(perm::default_when_missing(perm::clipboard, false));
  EXPECT_FALSE(perm::default_when_missing(perm::power, true));
  EXPECT_TRUE(perm::has(perm::standard, perm::app_profiles));
  EXPECT_TRUE(perm::has(perm::play, perm::app_profiles));
  EXPECT_FALSE(perm::has(perm::view_only, perm::app_profiles));
}
