/**
 * @file tests/unit/test_host_info.cpp
 * @brief Tests for the web UI's host facts: health rules, JSON shapes, preview helpers.
 */
// test includes
#include "../tests_common.h"

// standard includes
#include <algorithm>
#include <chrono>

// local includes
#include <src/host_info.h>

using namespace std::literals;

namespace {
  /**
   * @brief Facts for a healthy NVIDIA/X11 host like the reference PC.
   *
   * @return Healthy probes.
   */
  host_info::health_probes_t healthy() {
    host_info::health_probes_t probes;
    probes.encoder_probed = true;
    probes.encoder_name = "nvenc";
    probes.capture_method = "nvfbc";
    probes.zero_copy = true;
    probes.window_system = "x11";
    probes.clipboard_enabled = true;
    probes.xclip_found = true;
    probes.check_input_devices = true;
    probes.uinput_writable = true;
    probes.uhid_writable = true;
    probes.udev_rules_installed = true;
    probes.check_audio_server = true;
    probes.audio_server_reachable = true;
    probes.origin_web_ui_allowed = "lan";
    platf::capture_output_t output;
    output.name = "HDMI-0";
    output.connected = true;
    output.width = output.mode_width = 1920;
    output.height = output.mode_height = 1080;
    probes.outputs.push_back(output);
    return probes;
  }

  /**
   * @brief Find a check by id.
   *
   * @param checks Checks to search.
   * @param id Check id.
   * @return Pointer to the check, or null.
   */
  const host_info::health_check_t *find(const std::vector<host_info::health_check_t> &checks, const std::string &id) {
    const auto it = std::ranges::find(checks, id, &host_info::health_check_t::id);
    return it == checks.end() ? nullptr : &*it;
  }
}  // namespace

TEST(HostInfoHealthTests, HealthyHostHasNoProblems) {
  const auto checks = host_info::evaluate_health(healthy());
  for (const auto &check : checks) {
    EXPECT_EQ(check.status, host_info::health_status_e::ok) << check.id;
  }
  ASSERT_NE(find(checks, "encoder"), nullptr);
  ASSERT_NE(find(checks, "capture"), nullptr);
  ASSERT_NE(find(checks, "clipboard"), nullptr);
  ASSERT_NE(find(checks, "input"), nullptr);
  ASSERT_NE(find(checks, "audio"), nullptr);
  EXPECT_EQ(find(checks, "web-ui-exposure"), nullptr);
}

TEST(HostInfoHealthTests, SoftwareEncoderWarns) {
  auto probes = healthy();
  probes.encoder_name = "software";
  const auto checks = host_info::evaluate_health(probes);
  const auto *check = find(checks, "encoder");
  ASSERT_NE(check, nullptr);
  EXPECT_EQ(check->status, host_info::health_status_e::warn);
  EXPECT_EQ(check->fix_kind, host_info::fix_kind_e::setting);
}

TEST(HostInfoHealthTests, NoEncoderIsAnError) {
  auto probes = healthy();
  probes.encoder_probed = false;
  const auto checks = host_info::evaluate_health(probes);
  const auto *check = find(checks, "encoder");
  ASSERT_NE(check, nullptr);
  EXPECT_EQ(check->status, host_info::health_status_e::error);
}

TEST(HostInfoHealthTests, CpuCopyCaptureWarns) {
  auto probes = healthy();
  probes.capture_method = "x11";
  probes.zero_copy = false;
  const auto checks = host_info::evaluate_health(probes);
  const auto *check = find(checks, "capture");
  ASSERT_NE(check, nullptr);
  EXPECT_EQ(check->status, host_info::health_status_e::warn);
}

TEST(HostInfoHealthTests, MissingXclipOffersInstallCommand) {
  auto probes = healthy();
  probes.xclip_found = false;
  const auto checks = host_info::evaluate_health(probes);
  const auto *check = find(checks, "clipboard");
  ASSERT_NE(check, nullptr);
  EXPECT_EQ(check->status, host_info::health_status_e::warn);
  EXPECT_EQ(check->fix_kind, host_info::fix_kind_e::command);
  EXPECT_EQ(check->fix_value, "sudo apt install xclip");
}

TEST(HostInfoHealthTests, WaylandNeedsWlClipboard) {
  auto probes = healthy();
  probes.window_system = "wayland";
  probes.wl_clipboard_found = false;
  const auto checks = host_info::evaluate_health(probes);
  const auto *check = find(checks, "clipboard");
  ASSERT_NE(check, nullptr);
  EXPECT_EQ(check->fix_value, "sudo apt install wl-clipboard");
}

TEST(HostInfoHealthTests, ClipboardDisabledSkipsCheck) {
  auto probes = healthy();
  probes.clipboard_enabled = false;
  probes.xclip_found = false;
  const auto checks = host_info::evaluate_health(probes);
  EXPECT_EQ(find(checks, "clipboard"), nullptr);
}

TEST(HostInfoHealthTests, BlockedUinputIsAnError) {
  auto probes = healthy();
  probes.uinput_writable = false;
  const auto checks = host_info::evaluate_health(probes);
  const auto *check = find(checks, "input");
  ASSERT_NE(check, nullptr);
  EXPECT_EQ(check->status, host_info::health_status_e::error);
  EXPECT_NE(check->detail.find("/dev/uinput"), std::string::npos);
}

TEST(HostInfoHealthTests, MissingUdevRulesSuggestsReload) {
  auto probes = healthy();
  probes.uhid_writable = false;
  probes.udev_rules_installed = false;
  const auto checks = host_info::evaluate_health(probes);
  const auto *check = find(checks, "input");
  ASSERT_NE(check, nullptr);
  EXPECT_NE(check->fix_value.find("udevadm"), std::string::npos);
}

TEST(HostInfoHealthTests, ReachableSoundServerIsOkWithoutHardware) {
  // Nova makes its own sinks per stream, so no physical output device is fine.
  const auto checks = host_info::evaluate_health(healthy());
  const auto *check = find(checks, "audio");
  ASSERT_NE(check, nullptr);
  EXPECT_EQ(check->status, host_info::health_status_e::ok);
}

TEST(HostInfoHealthTests, UnreachableSoundServerIsAnError) {
  auto probes = healthy();
  probes.audio_server_reachable = false;
  const auto checks = host_info::evaluate_health(probes);
  const auto *check = find(checks, "audio");
  ASSERT_NE(check, nullptr);
  EXPECT_EQ(check->status, host_info::health_status_e::error);
}

TEST(HostInfoHealthTests, ViewportMismatchWarnsWhenIdle) {
  auto probes = healthy();
  probes.outputs[0].height = 994;
  const auto checks = host_info::evaluate_health(probes);
  const auto *check = find(checks, "display-HDMI-0");
  ASSERT_NE(check, nullptr);
  EXPECT_EQ(check->status, host_info::health_status_e::warn);
  EXPECT_NE(check->detail.find("1920×994"), std::string::npos);
  EXPECT_EQ(check->fix_value, "xrandr --output HDMI-0 --auto");
}

TEST(HostInfoHealthTests, ViewportMismatchIgnoredWhileAppRuns) {
  auto probes = healthy();
  probes.outputs[0].height = 994;
  probes.app_running = true;
  const auto checks = host_info::evaluate_health(probes);
  EXPECT_EQ(find(checks, "display-HDMI-0"), nullptr);
}

TEST(HostInfoHealthTests, ModesetIsInformational) {
  auto probes = healthy();
  probes.nvidia_drm_modeset = false;
  const auto checks = host_info::evaluate_health(probes);
  const auto *check = find(checks, "nvidia-drm-modeset");
  ASSERT_NE(check, nullptr);
  EXPECT_EQ(check->status, host_info::health_status_e::ok);
}

TEST(HostInfoHealthTests, WanExposureWarns) {
  auto probes = healthy();
  probes.origin_web_ui_allowed = "wan";
  const auto checks = host_info::evaluate_health(probes);
  const auto *check = find(checks, "web-ui-exposure");
  ASSERT_NE(check, nullptr);
  EXPECT_EQ(check->status, host_info::health_status_e::warn);
  EXPECT_EQ(check->fix_value, "origin_web_ui_allowed");
}

TEST(HostInfoJsonTests, HealthShape) {
  auto probes = healthy();
  probes.xclip_found = false;
  const auto json = host_info::health_to_json(host_info::evaluate_health(probes));
  ASSERT_TRUE(json.is_array());
  bool saw_fix = false;
  for (const auto &item : json) {
    EXPECT_TRUE(item.contains("id"));
    EXPECT_TRUE(item["status"] == "ok" || item["status"] == "warn" || item["status"] == "error");
    EXPECT_TRUE(item["title"].is_string());
    EXPECT_TRUE(item["detail"].is_string());
    if (item["fix"].is_object()) {
      saw_fix = true;
      EXPECT_EQ(item["fix"]["kind"], "command");
      EXPECT_TRUE(item["fix"]["value"].is_string());
    } else {
      EXPECT_TRUE(item["fix"].is_null());
    }
  }
  EXPECT_TRUE(saw_fix);
}

TEST(HostInfoJsonTests, DisplaysShape) {
  platf::capture_output_t output;
  output.name = "HDMI-0";
  output.index = 0;
  output.connected = true;
  output.primary = true;
  output.width = output.mode_width = 1920;
  output.height = output.mode_height = 1080;
  output.refresh_hz = 60.0;
  const auto json = host_info::displays_json({output});
  ASSERT_EQ(json.size(), 1u);
  EXPECT_EQ(json[0]["name"], "HDMI-0");
  EXPECT_EQ(json[0]["width"], 1920);
  EXPECT_EQ(json[0]["mode_height"], 1080);
  EXPECT_DOUBLE_EQ(json[0]["refresh_hz"].get<double>(), 60.0);
  EXPECT_TRUE(json[0]["primary"].get<bool>());
  EXPECT_TRUE(json[0].contains("configured"));
}

TEST(HostInfoJsonTests, AudioSinksMarkVirtualAndConfigured) {
  const std::vector<platf::sink_desc_t> sinks {
    {.name = "alsa_output.pci", .description = "Speakers", .is_virtual = false},
    {.name = "sink-sunshine-stereo", .description = "Nova stereo", .is_virtual = true},
  };
  const auto json = host_info::audio_sinks_json(sinks, "alsa_output.pci");
  EXPECT_EQ(json["configured"], "alsa_output.pci");
  ASSERT_EQ(json["sinks"].size(), 2u);
  EXPECT_TRUE(json["sinks"][0]["configured"].get<bool>());
  EXPECT_FALSE(json["sinks"][0]["virtual"].get<bool>());
  EXPECT_TRUE(json["sinks"][1]["virtual"].get<bool>());
  EXPECT_FALSE(json["sinks"][1]["configured"].get<bool>());
}

TEST(HostInfoJsonTests, HostInfoHasContract) {
  const auto json = host_info::host_info_json();
  for (const auto *key : {"name", "version", "platform", "os_pretty", "kernel", "desktop_session", "gpu", "encoders", "capture", "uptime_s"}) {
    EXPECT_TRUE(json.contains(key)) << key;
  }
  EXPECT_TRUE(json["gpu"].is_array());
  EXPECT_TRUE(json["encoders"]["codecs"].is_array());
  EXPECT_TRUE(json["capture"].contains("zero_copy"));
}

TEST(HostInfoPreviewTests, DownscaleKeepsAspectAndAverages) {
  platf::preview_frame_t frame;
  frame.width = 4;
  frame.height = 2;
  frame.bgra.assign(4 * 2 * 4, 0);
  // Left half blue, right half red (BGRA).
  for (int y = 0; y < 2; ++y) {
    for (int x = 0; x < 4; ++x) {
      auto *px = frame.bgra.data() + (y * 4 + x) * 4;
      if (x < 2) {
        px[0] = 200;
      } else {
        px[2] = 100;
      }
      px[3] = 255;
    }
  }
  int width = 0;
  int height = 0;
  const auto rgb = host_info::downscale_to_rgb(frame, 2, width, height);
  EXPECT_EQ(width, 2);
  EXPECT_EQ(height, 1);
  ASSERT_EQ(rgb.size(), 6u);
  EXPECT_EQ(rgb[2], 200);  // left pixel blue
  EXPECT_EQ(rgb[0], 0);
  EXPECT_EQ(rgb[3], 100);  // right pixel red
}

TEST(HostInfoPreviewTests, DownscaleNeverUpscales) {
  platf::preview_frame_t frame;
  frame.width = 10;
  frame.height = 5;
  frame.bgra.assign(10 * 5 * 4, 128);
  int width = 0;
  int height = 0;
  host_info::downscale_to_rgb(frame, 1280, width, height);
  EXPECT_EQ(width, 10);
  EXPECT_EQ(height, 5);
}

TEST(HostInfoPreviewTests, DownscaleRejectsShortBuffer) {
  platf::preview_frame_t frame;
  frame.width = 10;
  frame.height = 10;
  frame.bgra.assign(10, 0);
  int width = 1;
  int height = 1;
  EXPECT_TRUE(host_info::downscale_to_rgb(frame, 100, width, height).empty());
  EXPECT_EQ(width, 0);
}

TEST(HostInfoPreviewTests, EncodesJpeg) {
  const std::vector<std::uint8_t> rgb(16 * 8 * 3, 90);
  const auto jpeg = host_info::encode_jpeg(rgb, 16, 8, 80);
  ASSERT_GE(jpeg.size(), 4u);
  EXPECT_EQ(static_cast<unsigned char>(jpeg[0]), 0xFF);
  EXPECT_EQ(static_cast<unsigned char>(jpeg[1]), 0xD8);
  EXPECT_EQ(static_cast<unsigned char>(jpeg[jpeg.size() - 2]), 0xFF);
  EXPECT_EQ(static_cast<unsigned char>(jpeg[jpeg.size() - 1]), 0xD9);
}

TEST(HostInfoPreviewTests, EncodeRejectsBadInput) {
  EXPECT_TRUE(host_info::encode_jpeg({}, 16, 8, 80).empty());
  EXPECT_TRUE(host_info::encode_jpeg(std::vector<std::uint8_t>(3, 0), 0, 1, 80).empty());
}

TEST(HostInfoRateLimiterTests, AllowsBurstThenBlocks) {
  host_info::rate_limiter_t limiter {2, 1s};
  const auto t0 = std::chrono::steady_clock::time_point {} + 10s;
  EXPECT_TRUE(limiter.try_acquire(t0));
  EXPECT_TRUE(limiter.try_acquire(t0 + 100ms));
  EXPECT_FALSE(limiter.try_acquire(t0 + 200ms));
  EXPECT_TRUE(limiter.try_acquire(t0 + 1100ms));
}
