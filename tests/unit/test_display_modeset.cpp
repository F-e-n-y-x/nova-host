/**
 * @file tests/unit/test_display_modeset.cpp
 * @brief Tests for Nova's native Mirror mode switching, against a fake xrandr/nvidia-settings.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <string>
#include <vector>

// local includes
#include <src/display_modeset.h>

namespace fs = std::filesystem;

namespace {
  // atom's HDMI dummy (CustomEDID "Nova VDD"), captured with `xrandr --query`, plus an owner's
  // DP monitor to its right so every test also checks that monitor is left alone.
  constexpr auto hdmi_modes = R"(   1920x1080     60.00*+ 119.88    59.94
   3840x2160     59.94
   3120x1440    119.90    59.96
   2560x1440    119.88    59.95
   2340x1080    120.00    60.00
   1280x1024     75.02
   1280x720      59.94
   1024x768      75.03    70.07    60.00
   800x600       72.19
   640x480       75.00    59.93
)";

  constexpr auto nova_edid_hex = R"(	EDID:
		00ffffffffffff0038a9108001000000
		011e010380341d782aeef945a0574798
		27125054bfef01010101010101010101
		010101010101023a801871382d40582c
		450008222100001ec57130a0c0a02950
		30203a0008222100001a000000fd0017
		791ed23c000a202020202020000000fc
		004e6f7661205644440a2020202001db
)";

  constexpr auto dpys = R"(7 Display Devices on atom:0

    [0] atom:0[dpy:0] (HDMI-0) (connected, enabled)

      Has the following names:
        DFP
        DFP-0
        DPY-0
        HDMI-0

    [1] atom:0[dpy:1] (DP-0) (connected, enabled)

      Has the following names:
        DFP-1
        DPY-1
        DP-0
)";

  /**
   * @brief A tiny model of the desktop X server: xrandr and nvidia-settings change it.
   */
  class fake_x_t {
  public:
    int hdmi_w = 1920;  ///< HDMI-0 size on the screen.
    int hdmi_h = 1080;  ///< HDMI-0 height on the screen.
    std::string hdmi_rotation = "normal";  ///< HDMI-0 rotation.
    std::string metamode = "DPY-0: nvidia-auto-select @1920x1080 +0+0 {ViewPortIn=1920x1080, ViewPortOut=1920x1080+0+0}, DPY-1: 2560x1440_60 +1920+0 {ViewPortIn=2560x1440, ViewPortOut=2560x1440+0+0}";  ///< CurrentMetaMode.
    bool nvidia_settings = true;  ///< nvidia-settings is installed.
    bool metamode_fails = false;  ///< nvidia-settings -a returns an error.
    bool dp_connected = true;  ///< The DP monitor is plugged in.
    std::vector<std::string> calls;  ///< Every command, joined by spaces.

    std::string query(bool verbose) const {
      std::string out = "Screen 0: minimum 8 x 8, current 4480 x 1440, maximum 32767 x 32767\n";
      out += std::format("HDMI-0 connected primary {}x{}+0+0 {}{}(normal left inverted right x axis y axis) 520mm x 290mm\n", hdmi_w, hdmi_h, verbose ? "(0x1bd) " : "", hdmi_rotation == "normal" ? std::string {} : hdmi_rotation + " ");
      out += verbose ? std::string {nova_edid_hex} : std::string {hdmi_modes};
      if (dp_connected) {
        out += "DP-0 connected 2560x1440+1920+0 (normal left inverted right x axis y axis) 600mm x 340mm\n   2560x1440     59.95*+\n   1920x1080     60.00  \n";
      } else {
        out += "DP-0 disconnected (normal left inverted right x axis y axis)\n";
      }
      out += "DP-1 disconnected (normal left inverted right x axis y axis)\n";
      return out;
    }

    display_modeset::run_result_t run(const std::vector<std::string> &argv) {
      std::string joined;
      for (const auto &a : argv) {
        joined += (joined.empty() ? "" : " ") + a;
      }
      calls.push_back(joined);
      if (argv[0] == "xrandr") {
        if (argv.size() > 1 && argv[1] == "--query") {
          return {0, query(false)};
        }
        if (argv.size() > 1 && argv[1] == "--verbose") {
          return {0, query(true)};
        }
        // xrandr --output HDMI-0 --mode WxH [--rate R] --rotate X (--scale 1x1 | --scale-from WxH)
        int w = 0;
        int h = 0;
        std::string rotation = "normal";
        std::string from;
        for (std::size_t i = 1; i + 1 < argv.size(); ++i) {
          if (argv[i] == "--mode") {
            std::sscanf(argv[i + 1].c_str(), "%dx%d", &w, &h);
          } else if (argv[i] == "--rotate") {
            rotation = argv[i + 1];
          } else if (argv[i] == "--scale-from") {
            from = argv[i + 1];
          } else if (argv[i] == "--auto") {
            w = 1920;
            h = 1080;
          }
        }
        if (std::find(argv.begin(), argv.end(), "--auto") != argv.end()) {
          w = 1920;
          h = 1080;
        }
        if (w == 0) {
          return {1, {}};
        }
        if (rotation == "left" || rotation == "right") {
          std::swap(w, h);
        }
        if (!from.empty()) {
          std::sscanf(from.c_str(), "%dx%d", &w, &h);
        }
        hdmi_w = w;
        hdmi_h = h;
        hdmi_rotation = rotation;
        return {0, {}};
      }
      if (argv[0] == "nvidia-settings") {
        if (!nvidia_settings) {
          return {-1, {}};
        }
        if (argv[1] == "-q" && argv[2] == "CurrentMetaMode") {
          return {0, "id=50, switchable=no, source=nv-control :: " + metamode + "\n"};
        }
        if (argv[1] == "-q" && argv[2] == "dpys") {
          return {0, dpys};
        }
        if (argv[1] == "-a" && argv[2].starts_with("CurrentMetaMode=")) {
          if (metamode_fails) {
            return {1, {}};
          }
          metamode = argv[2].substr(16);
          // Apply DPY-0's ViewPortIn and rotation to the model.
          for (const auto &entry : display_modeset::split_metamode(metamode)) {
            if (!entry.starts_with("DPY-0:") && !entry.starts_with("HDMI-0:")) {
              continue;
            }
            const auto vpi = entry.find("ViewPortIn=");
            if (vpi != std::string::npos) {
              std::sscanf(entry.c_str() + vpi + 11, "%dx%d", &hdmi_w, &hdmi_h);
            }
            const auto rot = entry.find("Rotation=");
            hdmi_rotation = rot == std::string::npos ? "normal" : entry.substr(rot + 9, entry.find_first_of(",}", rot) - rot - 9);
          }
          return {0, {}};
        }
      }
      return {127, {}};
    }
  };

  class DisplayModesetTest: public ::testing::Test {
  protected:
    void SetUp() override {
      dir = fs::temp_directory_path() / ("nova-modeset-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
      fs::create_directories(dir);
      state = dir / "display_modeset.json";
      legacy = dir / "sunshine-resolution.metamode";
    }

    void TearDown() override {
      std::error_code ec;
      fs::remove_all(dir, ec);
    }

    display_modeset::switcher_t make(std::string output = "auto") {
      return display_modeset::switcher_t {
        [this](const std::vector<std::string> &argv) {
          return x.run(argv);
        },
        state,
        [output]() {
          return output;
        },
        legacy
      };
    }

    static display_follow::request_t request(int w, int h, int fps, int rotation = 0) {
      display_follow::request_t r;
      r.width = w;
      r.height = h;
      r.fps = fps;
      r.rotation = rotation;
      return r;
    }

    bool called(std::string_view prefix) const {
      return std::ranges::any_of(x.calls, [&](const std::string &c) {
        return c.starts_with(prefix);
      });
    }

    fake_x_t x;
    fs::path dir;
    fs::path state;
    fs::path legacy;
  };

  display_modeset::output_t hdmi() {
    auto outputs = display_modeset::parse_xrandr(fake_x_t {}.query(false));
    return outputs.at(0);
  }
}  // namespace

TEST(DisplayModesetParse, ReadsOutputsModesAndRates) {
  const auto outputs = display_modeset::parse_xrandr(fake_x_t {}.query(false));
  ASSERT_EQ(outputs.size(), 3u);
  const auto &o = outputs[0];
  EXPECT_EQ(o.name, "HDMI-0");
  EXPECT_TRUE(o.connected);
  EXPECT_TRUE(o.primary);
  EXPECT_TRUE(o.enabled);
  EXPECT_EQ(o.width, 1920);
  EXPECT_EQ(o.height, 1080);
  EXPECT_EQ(o.rotation, 0);
  ASSERT_EQ(o.modes.size(), 10u);
  EXPECT_EQ(o.modes[0].width, 1920);
  EXPECT_TRUE(o.modes[0].preferred);
  EXPECT_DOUBLE_EQ(o.modes[0].current_rate, 60.0);
  EXPECT_EQ(o.modes[0].rates.size(), 3u);
  EXPECT_EQ(o.modes[4].width, 2340);
  EXPECT_DOUBLE_EQ(o.modes[4].rates[0], 120.0);
  EXPECT_DOUBLE_EQ(o.modes[4].current_rate, 0.0);
  EXPECT_EQ(outputs[1].name, "DP-0");
  EXPECT_EQ(outputs[1].x, 1920);
  EXPECT_FALSE(outputs[2].connected);
}

TEST(DisplayModesetParse, ReadsRotationAndVerboseHeader) {
  fake_x_t x;
  x.hdmi_w = 1080;
  x.hdmi_h = 2340;
  x.hdmi_rotation = "left";
  const auto outputs = display_modeset::parse_xrandr(x.query(true));
  ASSERT_FALSE(outputs.empty());
  EXPECT_EQ(outputs[0].rotation, 90);
  EXPECT_EQ(outputs[0].width, 1080);
  EXPECT_EQ(outputs[0].height, 2340);
  EXPECT_TRUE(outputs[0].modes.empty());  // --verbose mode lines are not the query format
}

TEST(DisplayModesetParse, EdidMonitorNameOfTheDummy) {
  const auto names = display_modeset::parse_edid_names(fake_x_t {}.query(true));
  ASSERT_EQ(names.count("HDMI-0"), 1u);
  EXPECT_EQ(names.at("HDMI-0"), "Nova VDD");
  EXPECT_EQ(names.count("DP-0"), 0u);
}

TEST(DisplayModesetParse, DpyNamesAndMetaMode) {
  const auto map = display_modeset::parse_dpy_names(dpys);
  EXPECT_EQ(map.at("HDMI-0"), "DPY-0");
  EXPECT_EQ(map.at("DP-0"), "DPY-1");
  EXPECT_EQ(display_modeset::parse_current_metamode("id=50, switchable=no, source=nv-control :: DPY-0: nvidia-auto-select @1920x824 +0+0 {ViewPortIn=1920x824, ViewPortOut=1920x824+0+0}\n"),
            "DPY-0: nvidia-auto-select @1920x824 +0+0 {ViewPortIn=1920x824, ViewPortOut=1920x824+0+0}");
}

TEST(DisplayModesetParse, MetaModeEntriesKeepOtherDisplays) {
  const std::string mm = "DPY-0: nvidia-auto-select @1920x1080 +0+0 {ViewPortIn=1920x1080, ViewPortOut=1920x1080+0+0}, DPY-1: 2560x1440_60 +1920+0 {ViewPortIn=2560x1440, ViewPortOut=2560x1440+0+0}";
  const auto entries = display_modeset::split_metamode(mm);
  ASSERT_EQ(entries.size(), 2u);
  EXPECT_TRUE(entries[1].starts_with("DPY-1:"));
  EXPECT_EQ(display_modeset::metamode_entry_position(mm, {"DP-0", "DPY-1"}), std::make_pair(1920, 0));

  const auto replaced = display_modeset::replace_metamode_entry(mm, {"HDMI-0", "DPY-0"}, "DPY-0: 2340x1080_120 +0+0 {Rotation=left}");
  EXPECT_EQ(replaced, "DPY-0: 2340x1080_120 +0+0 {Rotation=left}, DPY-1: 2560x1440_60 +1920+0 {ViewPortIn=2560x1440, ViewPortOut=2560x1440+0+0}");
  // A display missing from the MetaMode (turned off) is appended, never dropping another one.
  EXPECT_EQ(display_modeset::replace_metamode_entry("DPY-1: nvidia-auto-select +0+0", {"DPY-0"}, "DPY-0: 1920x1080_60 +0+0"), "DPY-1: nvidia-auto-select +0+0, DPY-0: 1920x1080_60 +0+0");
}

TEST(DisplayModesetParse, PickRateLikeTheScript) {
  display_modeset::mode_t m;
  m.rates = {60.00, 119.88, 59.94};
  EXPECT_DOUBLE_EQ(*display_modeset::pick_rate(m, 60), 59.94);
  EXPECT_DOUBLE_EQ(*display_modeset::pick_rate(m, 90), 119.88);
  EXPECT_DOUBLE_EQ(*display_modeset::pick_rate(m, 144), 119.88);
  EXPECT_FALSE(display_modeset::pick_rate(display_modeset::mode_t {}, 60));
}

TEST(DisplayModesetParse, AutoOutputIsTheNovaEdidOrTheOnlyMonitor) {
  auto outputs = display_modeset::parse_xrandr(fake_x_t {}.query(false));
  // Two monitors and no EDID names: never guess.
  EXPECT_EQ(display_modeset::pick_output(outputs, "auto"), nullptr);
  outputs[0].monitor_name = "Nova VDD";
  ASSERT_NE(display_modeset::pick_output(outputs, "auto"), nullptr);
  EXPECT_EQ(display_modeset::pick_output(outputs, "auto")->name, "HDMI-0");
  // A named output must be connected.
  EXPECT_EQ(display_modeset::pick_output(outputs, "DP-0")->name, "DP-0");
  EXPECT_EQ(display_modeset::pick_output(outputs, "DP-1"), nullptr);
  EXPECT_EQ(display_modeset::pick_output(outputs, "HDMI-9"), nullptr);
  outputs[0].monitor_name.clear();
  outputs[1].connected = false;
  EXPECT_EQ(display_modeset::pick_output(outputs, "auto")->name, "HDMI-0");
}

TEST(DisplayModesetPlan, ExactModeWhenTheEdidHasIt) {
  const auto p = display_modeset::plan(hdmi(), {2340, 1080, 120, 0});
  ASSERT_TRUE(p);
  EXPECT_EQ(p->mode_width, 2340);
  EXPECT_DOUBLE_EQ(p->rate, 120.0);
  EXPECT_FALSE(p->scaled);
  EXPECT_EQ(display_modeset::xrandr_args(*p), (std::vector<std::string> {"xrandr", "--output", "HDMI-0", "--mode", "2340x1080", "--rate", "120.00", "--rotate", "normal", "--scale", "1x1"}));
}

TEST(DisplayModesetPlan, ViewPortInOnTheSmallestCoveringMode) {
  const auto p = display_modeset::plan(hdmi(), {1920, 824, 60, 0});
  ASSERT_TRUE(p);
  EXPECT_EQ(p->mode_width, 1920);
  EXPECT_EQ(p->mode_height, 1080);
  EXPECT_TRUE(p->scaled);
  EXPECT_EQ(display_modeset::metamode_entry(*p, "DPY-0", 0, 0), "DPY-0: 1920x1080_60 +0+0 {ViewPortIn=1920x824, ViewPortOut=1920x1080+0+0}");

  const auto big = display_modeset::plan(hdmi(), {2400, 1080, 120, 0});
  EXPECT_EQ(big->mode_width, 2560);
  EXPECT_DOUBLE_EQ(big->rate, 119.88);
  EXPECT_EQ(display_modeset::nvidia_mode_name(*big), "2560x1440_120");
}

TEST(DisplayModesetPlan, PortraitRotatesTheLandscapeMode) {
  const auto p = display_modeset::plan(hdmi(), {1080, 2340, 120, 90});
  ASSERT_TRUE(p);
  EXPECT_EQ(p->mode_width, 2340);
  EXPECT_EQ(p->mode_height, 1080);
  EXPECT_EQ(p->screen_width, 1080);
  EXPECT_EQ(p->screen_height, 2340);
  EXPECT_FALSE(p->scaled);
  EXPECT_EQ(display_modeset::metamode_entry(*p, "DPY-0", 0, 0), "DPY-0: 2340x1080_120 +0+0 {ViewPortIn=1080x2340, ViewPortOut=2340x1080+0+0, Rotation=left}");

  // No 2400x1080 mode: scale onto 2560x1440, rotated.
  const auto scaled = display_modeset::plan(hdmi(), {1080, 2400, 120, 270});
  EXPECT_EQ(scaled->mode_width, 2560);
  EXPECT_TRUE(scaled->scaled);
  const auto args = display_modeset::xrandr_args(*scaled);
  EXPECT_EQ(args.back(), "1080x2400");
  EXPECT_NE(std::ranges::find(args, "right"), args.end());
}

TEST_F(DisplayModesetTest, ExactSwitchUsesXrandrAndRestoresTheMetaMode) {
  auto switcher = make();
  const auto original = x.metamode;
  ASSERT_TRUE(switcher.set(request(2340, 1080, 120)));
  EXPECT_EQ(x.hdmi_w, 2340);
  EXPECT_TRUE(called("xrandr --output HDMI-0 --mode 2340x1080 --rate 120.00 --rotate normal"));
  EXPECT_FALSE(called("nvidia-settings -a"));
  EXPECT_TRUE(fs::exists(state));
  ASSERT_TRUE(switcher.current());

  ASSERT_TRUE(switcher.restore());
  EXPECT_TRUE(called("nvidia-settings -a CurrentMetaMode=" + original));
  EXPECT_FALSE(fs::exists(state));
  EXPECT_FALSE(switcher.current());
}

TEST_F(DisplayModesetTest, PortraitGoesThroughTheMetaModeAndKeepsTheDpMonitor) {
  auto switcher = make();
  ASSERT_TRUE(switcher.set(request(1080, 2340, 120, 90)));
  EXPECT_EQ(x.hdmi_w, 1080);
  EXPECT_EQ(x.hdmi_h, 2340);
  EXPECT_EQ(x.hdmi_rotation, "left");
  EXPECT_EQ(x.metamode, "DPY-0: 2340x1080_120 +0+0 {ViewPortIn=1080x2340, ViewPortOut=2340x1080+0+0, Rotation=left}, DPY-1: 2560x1440_60 +1920+0 {ViewPortIn=2560x1440, ViewPortOut=2560x1440+0+0}");
  // No output is ever turned off or forced.
  for (const auto &c : x.calls) {
    EXPECT_EQ(c.find("--off"), std::string::npos) << c;
    EXPECT_EQ(c.find("DP-0"), std::string::npos) << c;
  }
}

TEST_F(DisplayModesetTest, MetaModeFailureFallsBackToXrandr) {
  x.metamode_fails = true;
  auto switcher = make();
  ASSERT_TRUE(switcher.set(request(1920, 824, 60)));
  EXPECT_TRUE(called("xrandr --output HDMI-0 --mode 1920x1080 --rate 59.94 --rotate normal --scale-from 1920x824"));
  EXPECT_EQ(x.hdmi_h, 824);
}

TEST_F(DisplayModesetTest, WithoutNvidiaSettingsRestoresWithXrandr) {
  x.nvidia_settings = false;
  auto switcher = make();
  ASSERT_TRUE(switcher.set(request(1080, 2340, 120, 90)));
  EXPECT_EQ(x.hdmi_rotation, "left");
  ASSERT_TRUE(switcher.restore());
  EXPECT_TRUE(called("xrandr --output HDMI-0 --mode 1920x1080 --rate 60.00 --rotate normal --scale 1x1"));
  EXPECT_EQ(x.hdmi_w, 1920);
  EXPECT_EQ(x.hdmi_rotation, "normal");
}

TEST_F(DisplayModesetTest, SecondSwitchKeepsTheFirstOriginal) {
  auto switcher = make();
  const auto original = x.metamode;
  ASSERT_TRUE(switcher.set(request(1080, 2340, 120, 90)));
  ASSERT_TRUE(switcher.set(request(2560, 1440, 120)));  // live resolution change without restore
  ASSERT_TRUE(switcher.restore());
  EXPECT_EQ(x.metamode, original);
}

TEST_F(DisplayModesetTest, NoOutputNoSwitch) {
  auto switcher = make("DP-1");  // named but not connected
  EXPECT_FALSE(switcher.set(request(1920, 1080, 60)));
  EXPECT_FALSE(fs::exists(state));
  EXPECT_FALSE(called("xrandr --output"));
}

TEST_F(DisplayModesetTest, RecoverRestoresAfterACrash) {
  const auto original = x.metamode;
  {
    auto crashed = make();
    ASSERT_TRUE(crashed.set(request(1080, 2340, 120, 90)));
  }
  auto restarted = make();
  EXPECT_TRUE(restarted.recover());
  EXPECT_EQ(x.metamode, original);
  EXPECT_FALSE(fs::exists(state));
  EXPECT_FALSE(restarted.recover());
}

TEST_F(DisplayModesetTest, TheOldScriptsSavedMetaModeIsTheOriginal) {
  const std::string script_saved = "DPY-0: nvidia-auto-select +0+0 {ViewPortIn=1920x1080, ViewPortOut=1920x1080+0+0}";
  std::ofstream {legacy} << script_saved << '\n';
  x.metamode = "DPY-0: 1920x1080_60 +0+0 {ViewPortIn=1920x824, ViewPortOut=1920x1080+0+0}";  // the script's switch
  auto switcher = make();
  ASSERT_TRUE(switcher.set(request(2340, 1080, 120)));
  ASSERT_TRUE(switcher.restore());
  EXPECT_EQ(x.metamode, script_saved);
  EXPECT_FALSE(fs::exists(legacy));
}

TEST_F(DisplayModesetTest, RotateTheCurrentModeOrTheStreamMode) {
  auto switcher = make();
  ASSERT_TRUE(switcher.rotate(90));  // not switched: turn 1920x1080 to 1080x1920
  EXPECT_EQ(x.hdmi_w, 1080);
  EXPECT_EQ(x.hdmi_h, 1920);
  EXPECT_EQ(x.hdmi_rotation, "left");
  ASSERT_TRUE(switcher.rotate(0));
  EXPECT_EQ(x.hdmi_w, 1920);
  EXPECT_EQ(x.hdmi_rotation, "normal");
  ASSERT_TRUE(switcher.restore());

  ASSERT_TRUE(switcher.set(request(2340, 1080, 120)));
  ASSERT_TRUE(switcher.rotate(270));
  EXPECT_EQ(x.hdmi_w, 1080);
  EXPECT_EQ(x.hdmi_h, 2340);
  EXPECT_EQ(x.hdmi_rotation, "right");
  EXPECT_FALSE(switcher.rotate(45));
}

TEST(DisplayModesetPlan, RotationHelpers) {
  EXPECT_EQ(display_modeset::rotation_for_size(1080, 2340), 90);
  EXPECT_EQ(display_modeset::rotation_for_size(2340, 1080), 0);
  EXPECT_EQ(display_modeset::rotation_for_size(1080, 1080), 0);
  EXPECT_EQ(display_modeset::size_for_rotation(2340, 1080, 90), std::make_pair(1080, 2340));
  EXPECT_EQ(display_modeset::size_for_rotation(1080, 2340, 270), std::make_pair(1080, 2340));
  EXPECT_EQ(display_modeset::size_for_rotation(1080, 2340, 0), std::make_pair(2340, 1080));
  EXPECT_EQ(display_modeset::size_for_rotation(2340, 1080, 180), std::make_pair(2340, 1080));
  EXPECT_FALSE(display_modeset::valid_rotation(45));
}

TEST(DisplayModesetJson, FoundationReplies) {
  auto outputs = display_modeset::parse_xrandr(fake_x_t {}.query(false));
  outputs.pop_back();
  outputs[0].monitor_name = "Nova VDD";
  const auto j = display_modeset::displays_json(outputs, "HDMI-0", virtual_display::target_t {":20", "/x", 1080, 2340, 120});
  EXPECT_EQ(j["status_code"], 200);
  ASSERT_EQ(j["displays"].size(), 3u);
  EXPECT_EQ(j["displays"][0]["display_name"], "HDMI-0");
  EXPECT_EQ(j["displays"][0]["friendly_name"], "Nova VDD (HDMI-0)");
  EXPECT_EQ(j["displays"][0]["mirror"], true);
  EXPECT_EQ(j["displays"][1]["friendly_name"], "DP-0");
  EXPECT_EQ(j["displays"][2]["kind"], "virtual");
  EXPECT_EQ(j["displays"][2]["height"], 2340);
  EXPECT_FALSE(j.contains("vdd"));

  const auto ok = display_modeset::rotate_json(200, "OK");
  EXPECT_EQ(ok["success"], true);
  EXPECT_EQ(ok["status_code"], 200);
  EXPECT_EQ(display_modeset::rotate_json(409, "no")["success"], false);
}
