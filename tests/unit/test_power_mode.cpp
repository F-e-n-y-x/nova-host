/**
 * @file tests/unit/test_power_mode.cpp
 * @brief Tests for Nova's streaming power mode (fake runner and inhibitor, temp marker).
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// local includes
#include <src/power_mode.h>
#include <src/run_program.h>

namespace fs = std::filesystem;

namespace {
  /**
   * @brief Fake inhibitor that records acquire/release.
   */
  class fake_inhibitor_t: public power_mode::inhibitor_t {
  public:
    explicit fake_inhibitor_t(int *acquired, int *released):
        acquired_ {acquired},
        released_ {released} {
    }

    std::vector<std::string> acquire(const std::string &) override {
      ++*acquired_;
      return {"sleep", "screensaver"};
    }

    void release() override {
      ++*released_;
    }

  private:
    int *acquired_;
    int *released_;
  };

  /**
   * @brief Fixture: a scripted fake runner (argv joined with spaces -> result) and a temp marker.
   */
  class PowerModeTest: public ::testing::Test {
  protected:
    void SetUp() override {
      dir = fs::temp_directory_path() / ("nova-power-mode-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
      fs::create_directories(dir);
      marker = dir / "state" / "power_mode.active";
      // Default machine: NVIDIA at PowerMizer 0 (adaptive), power-profiles-daemon on "balanced".
      responses["nvidia-settings -c :0 -t -q [gpu:0]/GpuPowerMizerMode"] = {0, "\n0\n"};
      responses["nvidia-settings -c :0 -a [gpu:0]/GpuPowerMizerMode=1"] = {0, "  Attribute 'GPUPowerMizerMode' (atom:0[gpu:0]) assigned value 1.\n"};
      responses["nvidia-settings -c :0 -a [gpu:0]/GpuPowerMizerMode=0"] = {0, ""};
      responses["powerprofilesctl get"] = {0, "balanced\n"};
      responses["powerprofilesctl set performance"] = {0, ""};
      responses["powerprofilesctl set balanced"] = {0, ""};
    }

    void TearDown() override {
      std::error_code ec;
      fs::remove_all(dir, ec);
    }

    power_mode::controller_t make(bool with_inhibitor = true) {
      return power_mode::controller_t {
        [this](const std::vector<std::string> &argv) {
          std::string line;
          for (const auto &a : argv) {
            line += (line.empty() ? "" : " ") + a;
          }
          calls.push_back(line);
          if (const auto it = responses.find(line); it != responses.end()) {
            return it->second;
          }
          return power_mode::run_result_t {127, {}};
        },
        with_inhibitor ? std::make_unique<fake_inhibitor_t>(&acquired, &released) : nullptr,
        [this] {
          return governors;
        },
        marker
      };
    }

    static power_mode::settings_t on() {
      power_mode::settings_t s;
      s.enabled = true;
      return s;
    }

    std::string marker_text() const {
      std::ostringstream out;
      out << std::ifstream {marker}.rdbuf();
      return out.str();
    }

    bool called(const std::string &line) const {
      return std::ranges::find(calls, line) != calls.end();
    }

    fs::path dir;  ///< Temp directory.
    fs::path marker;  ///< Marker path.
    std::map<std::string, power_mode::run_result_t> responses;  ///< Scripted command results.
    std::vector<std::string> calls;  ///< Commands run, in order.
    std::vector<std::string> governors;  ///< Fake sysfs governors.
    int acquired = 0;  ///< Inhibitor acquire count.
    int released = 0;  ///< Inhibitor release count.
  };
}  // namespace

TEST_F(PowerModeTest, DisabledDoesNothing) {
  auto c = make();
  const auto s = c.apply(power_mode::settings_t {}, "Streaming");
  EXPECT_FALSE(s.active);
  EXPECT_TRUE(calls.empty());
  EXPECT_EQ(acquired, 0);
  EXPECT_FALSE(fs::exists(marker));
}

TEST_F(PowerModeTest, AppliesAndRestoresEverything) {
  auto c = make();
  const auto s = c.apply(on(), "Streaming to Pixel");
  EXPECT_TRUE(s.active);
  EXPECT_EQ(s.gpu, "performance");
  EXPECT_EQ(s.cpu, "performance");
  EXPECT_EQ(s.inhibited, (std::vector<std::string> {"sleep", "screensaver"}));
  EXPECT_TRUE(called("nvidia-settings -c :0 -a [gpu:0]/GpuPowerMizerMode=1"));
  EXPECT_TRUE(called("powerprofilesctl set performance"));
  ASSERT_TRUE(fs::exists(marker));
  const auto saved = power_mode::saved_state_t::parse(marker_text());
  ASSERT_TRUE(saved);
  EXPECT_EQ(saved->gpu_mode, "0");
  EXPECT_EQ(saved->cpu_tool, "powerprofilesctl");
  EXPECT_EQ(saved->cpu_value, "balanced");

  calls.clear();
  EXPECT_TRUE(c.restore());
  EXPECT_EQ(calls, (std::vector<std::string> {"nvidia-settings -c :0 -a [gpu:0]/GpuPowerMizerMode=0", "powerprofilesctl set balanced"}));
  EXPECT_EQ(released, 1);
  EXPECT_FALSE(fs::exists(marker));
  EXPECT_FALSE(c.status().active);
}

TEST_F(PowerModeTest, SecondApplyWhileActiveIsIgnored) {
  auto c = make();
  c.apply(on(), "a");
  const auto n = calls.size();
  c.apply(on(), "b");
  EXPECT_EQ(calls.size(), n);
  EXPECT_EQ(acquired, 1);
}

TEST_F(PowerModeTest, RestoreWithoutApplyIsNoop) {
  auto c = make();
  EXPECT_TRUE(c.restore());
  EXPECT_TRUE(calls.empty());
  EXPECT_EQ(released, 0);
}

TEST_F(PowerModeTest, AlreadyAtMaximumLeavesItAlone) {
  responses["nvidia-settings -c :0 -t -q [gpu:0]/GpuPowerMizerMode"] = {0, "1\n"};
  responses["powerprofilesctl get"] = {0, "performance\n"};
  auto c = make();
  const auto s = c.apply(on(), "x");
  EXPECT_EQ(s.gpu, "already");
  EXPECT_EQ(s.cpu, "already");
  EXPECT_FALSE(called("nvidia-settings -c :0 -a [gpu:0]/GpuPowerMizerMode=1"));
  EXPECT_FALSE(fs::exists(marker));  // nothing to restore
  calls.clear();
  c.restore();
  EXPECT_TRUE(calls.empty());
  EXPECT_EQ(released, 1);  // inhibitors still dropped
}

TEST_F(PowerModeTest, NoNvidiaAndGovernorFallbackIsReadOnly) {
  responses.erase("nvidia-settings -c :0 -t -q [gpu:0]/GpuPowerMizerMode");
  responses.erase("powerprofilesctl get");
  governors = {"schedutil", "schedutil"};
  auto c = make();
  const auto s = c.apply(on(), "x");
  EXPECT_EQ(s.gpu, "unavailable");
  EXPECT_EQ(s.cpu, "governor:schedutil");
  for (const auto &line : calls) {
    EXPECT_EQ(line.find("cpupower"), std::string::npos);
    EXPECT_EQ(line.find("scaling_governor"), std::string::npos);
  }
  EXPECT_FALSE(fs::exists(marker));
}

TEST_F(PowerModeTest, GovernorAlreadyPerformance) {
  responses.erase("powerprofilesctl get");
  governors = {"performance", "performance"};
  auto c = make();
  EXPECT_EQ(c.apply(on(), "x").cpu, "already");
}

TEST_F(PowerModeTest, TunedIsUsedWithoutPowerProfilesDaemon) {
  responses.erase("powerprofilesctl get");
  responses["tuned-adm active"] = {0, "Current active profile: balanced\n"};
  responses["tuned-adm profile latency-performance"] = {0, ""};
  responses["tuned-adm profile balanced"] = {0, ""};
  auto c = make();
  EXPECT_EQ(c.apply(on(), "x").cpu, "performance");
  calls.clear();
  c.restore();
  EXPECT_TRUE(called("tuned-adm profile balanced"));
}

TEST_F(PowerModeTest, RefusedChangesAreReportedAndNotRestored) {
  responses["nvidia-settings -c :0 -a [gpu:0]/GpuPowerMizerMode=1"] = {1, ""};
  responses["powerprofilesctl set performance"] = {1, ""};
  auto c = make();
  const auto s = c.apply(on(), "x");
  EXPECT_EQ(s.gpu, "failed");
  EXPECT_EQ(s.cpu, "failed");
  calls.clear();
  c.restore();
  EXPECT_TRUE(calls.empty());
}

TEST_F(PowerModeTest, SubOptionsCanBeTurnedOff) {
  auto settings = on();
  settings.gpu = false;
  settings.cpu = false;
  settings.inhibit = false;
  auto c = make();
  const auto s = c.apply(settings, "x");
  EXPECT_EQ(s.gpu, "off");
  EXPECT_EQ(s.cpu, "off");
  EXPECT_TRUE(calls.empty());
  EXPECT_EQ(acquired, 0);
}

TEST_F(PowerModeTest, RecoverRestoresAfterCrash) {
  {
    auto crashed = make();
    crashed.apply(on(), "x");  // never restored: simulates a crash mid-stream
  }
  ASSERT_TRUE(fs::exists(marker));
  calls.clear();
  auto next = make();
  EXPECT_TRUE(next.recover());
  EXPECT_TRUE(called("nvidia-settings -c :0 -a [gpu:0]/GpuPowerMizerMode=0"));
  EXPECT_TRUE(called("powerprofilesctl set balanced"));
  EXPECT_FALSE(fs::exists(marker));
  EXPECT_FALSE(next.recover());
}

TEST_F(PowerModeTest, RecoverIgnoresTamperedMarker) {
  fs::create_directories(marker.parent_path());
  std::ofstream {marker} << "gpu_mode=0; rm -rf /\nx_display=:0\ncpu_tool=sh\ncpu_value=-c\n";
  auto c = make();
  EXPECT_FALSE(c.recover());
  EXPECT_TRUE(calls.empty());
  EXPECT_FALSE(fs::exists(marker));
}

TEST(PowerModeParse, SavedStateRoundTrip) {
  power_mode::saved_state_t s;
  s.gpu_mode = "2";
  s.x_display = ":1";
  s.cpu_tool = "tuned-adm";
  s.cpu_value = "virtual-guest";
  const auto back = power_mode::saved_state_t::parse(s.serialize());
  ASSERT_TRUE(back);
  EXPECT_EQ(back->gpu_mode, "2");
  EXPECT_EQ(back->x_display, ":1");
  EXPECT_EQ(back->cpu_tool, "tuned-adm");
  EXPECT_EQ(back->cpu_value, "virtual-guest");
  EXPECT_TRUE(power_mode::saved_state_t::parse("")->empty());
  EXPECT_FALSE(power_mode::saved_state_t::parse("cpu_tool=powerprofilesctl\ncpu_value=a b\n"));
  EXPECT_FALSE(power_mode::saved_state_t::parse("gpu_mode=1\nx_display=:0;id\n"));
}

TEST(PowerModeParse, CommandOutput) {
  EXPECT_EQ(power_mode::parse_powermizer("\n(nvidia-settings:1): dbind-WARNING **: x\n0\n"), "0");
  EXPECT_EQ(power_mode::parse_powermizer("2"), "2");
  EXPECT_FALSE(power_mode::parse_powermizer("ERROR: not found"));
  EXPECT_EQ(power_mode::parse_profile("balanced\n"), "balanced");
  EXPECT_EQ(power_mode::parse_profile("Current active profile: throughput-performance\n"), "throughput-performance");
  EXPECT_FALSE(power_mode::parse_profile("No current active profile.\n"));
}

TEST(RunProgram, CapturesOutputAndExitCode) {
  const auto r = run_program::run({"sh", "-c", "echo out; echo err >&2; exit 3"}, std::chrono::seconds(5));
  EXPECT_EQ(r.exit_code, 3);
  EXPECT_EQ(r.output, "out\n");
  EXPECT_EQ(r.error, "err\n");
  EXPECT_EQ(run_program::run({"nova-no-such-program-xyz"}, std::chrono::seconds(5)).exit_code, 127);
  EXPECT_EQ(run_program::run({"sleep", "5"}, std::chrono::milliseconds(200)).exit_code, -1);
}

TEST(PowerModeWanted, AGameOverrideBeatsTheHostSetting) {
  EXPECT_FALSE(power_mode::wanted(false, std::nullopt));
  EXPECT_TRUE(power_mode::wanted(true, std::nullopt));
  EXPECT_TRUE(power_mode::wanted(false, true));
  EXPECT_FALSE(power_mode::wanted(true, false));
}
