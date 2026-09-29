/**
 * @file tests/unit/test_nova_perf.cpp
 * @brief Tests for per-app performance profiles: storage, the profile API and launch variables.
 */
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "src/nova_client_api.h"
#include "src/nova_perf.h"

#include "../tests_common.h"

using namespace nova_perf;
namespace fs = std::filesystem;

namespace {
  /**
   * @brief Value of one variable in an environment list.
   *
   * @param env Environment pairs.
   * @param key Variable name.
   * @return Its value, or nothing when it is not set.
   */
  std::optional<std::string> value_of(const env_t &env, const std::string &key) {
    const auto it = std::ranges::find(env, key, &env_t::value_type::first);
    if (it == env.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  /**
   * @brief A Proton game as the Library importer writes it.
   *
   * @return App object.
   */
  nlohmann::json proton_game() {
    return {
      {"name", "Grand Theft Auto V"},
      {"cmd", "\"/usr/libexec/nova-host/nova-proton-run\" \"/g/GTA5.exe\""},
      {"nova-exe", "/g/GTA5.exe"},
      {"nova-source", "exe"},
      {"nova-source-id", "gta5"},
    };
  }
}  // namespace

TEST(NovaPerf, EmptyAppsHaveTheDefaultProfile) {
  EXPECT_TRUE(is_default(for_app(nlohmann::json::object())));
  EXPECT_TRUE(is_default(for_app(proton_game())));
  EXPECT_TRUE(is_default(for_app(nullptr)));
  EXPECT_EQ(for_app(proton_game()).vkbasalt_cas, DEFAULT_CAS);
}

TEST(NovaPerf, StoredProfilesAreReadLenientlyAndClamped) {
  auto app = proton_game();
  app["nova-perf"] = {{"fps_cap", 5000}, {"fsr", "3"}, {"vkbasalt", "yes"}, {"vkbasalt_cas", -4}, {"mangohud", true}};
  const auto p = for_app(app);
  EXPECT_EQ(p.fps_cap, MAX_FPS_CAP);
  EXPECT_EQ(p.fsr, 3);
  EXPECT_FALSE(p.vkbasalt);  // not a boolean: ignored
  EXPECT_EQ(p.vkbasalt_cas, 0);
  EXPECT_TRUE(p.mangohud);
}

TEST(NovaPerf, LegacyCompatOptionsStillApplyUntilAProfileIsSaved) {
  auto app = proton_game();
  app["nova-compat"] = {{"prefix", "/p"}, {"fsr", 2}, {"fps_cap", 60}, {"mangohud", true}};
  auto p = for_app(app);
  EXPECT_EQ(p.fsr, 2);
  EXPECT_EQ(p.fps_cap, 60);
  EXPECT_TRUE(p.mangohud);

  // Once "nova-perf" exists it is the only source.
  app["nova-perf"] = {{"fps_cap", 90}};
  p = for_app(app);
  EXPECT_EQ(p.fps_cap, 90);
  EXPECT_EQ(p.fsr, 0);
  EXPECT_FALSE(p.mangohud);
}

TEST(NovaPerf, StoreKeepsOnlyNonDefaultsAndMigratesLegacyKeys) {
  auto app = proton_game();
  app["nova-compat"] = {{"prefix", "/p"}, {"fsr", 2}, {"fps_cap", 60}, {"mangohud", true}};
  store(app, {.fps_cap = 60, .fsr = 2, .vkbasalt = false, .vkbasalt_cas = DEFAULT_CAS, .mangohud = true});
  EXPECT_EQ(app["nova-perf"], (nlohmann::json {{"fps_cap", 60}, {"fsr", 2}, {"mangohud", true}}));
  EXPECT_EQ(app["nova-compat"], (nlohmann::json {{"prefix", "/p"}}));

  // A sharpness is remembered while vkBasalt is off.
  store(app, {.fps_cap = 0, .fsr = 0, .vkbasalt = true, .vkbasalt_cas = 80, .mangohud = false});
  EXPECT_EQ(app["nova-perf"], (nlohmann::json {{"vkbasalt", true}, {"vkbasalt_cas", 80}}));

  // Everything off: the object goes away, and an emptied nova-compat too.
  app["nova-compat"] = {{"fsr", 1}};
  store(app, {});
  EXPECT_FALSE(app.contains("nova-perf"));
  EXPECT_FALSE(app.contains("nova-compat"));
  EXPECT_EQ(app["name"], "Grand Theft Auto V");
}

TEST(NovaPerf, UpdatesArePartialAndStrict) {
  const profile_t start {.fps_cap = 60, .fsr = 0, .vkbasalt = false, .vkbasalt_cas = DEFAULT_CAS, .mangohud = false};
  const auto p = apply_update(start, {{"fsr", 1}, {"mangohud", true}});
  EXPECT_EQ(p.fps_cap, 60);  // untouched
  EXPECT_EQ(p.fsr, 1);
  EXPECT_TRUE(p.mangohud);
  EXPECT_EQ(apply_update(start, nlohmann::json::object()), start);

  EXPECT_THROW(apply_update(start, nlohmann::json::array()), std::invalid_argument);
  EXPECT_THROW(apply_update(start, {{"gamescope", true}}), std::invalid_argument);
  EXPECT_THROW(apply_update(start, {{"fps_cap", -1}}), std::invalid_argument);
  EXPECT_THROW(apply_update(start, {{"fps_cap", 1001}}), std::invalid_argument);
  EXPECT_THROW(apply_update(start, {{"fps_cap", "60"}}), std::invalid_argument);
  EXPECT_THROW(apply_update(start, {{"fps_cap", 59.5}}), std::invalid_argument);
  EXPECT_THROW(apply_update(start, {{"fsr", 6}}), std::invalid_argument);
  EXPECT_THROW(apply_update(start, {{"vkbasalt", 1}}), std::invalid_argument);
  EXPECT_THROW(apply_update(start, {{"vkbasalt_cas", 101}}), std::invalid_argument);
  EXPECT_EQ(apply_update(start, {{"fps_cap", 120.0}}).fps_cap, 120);
}

TEST(NovaPerf, ProfileApiUpdatesTheAppInPlace) {
  auto app = proton_game();
  app["nova-compat"] = {{"prefix", "/p"}, {"fps_cap", 60}};

  const auto p = update_app(app, R"({"fsr": 3, "vkbasalt": true})");
  EXPECT_EQ(p.fsr, 3);
  EXPECT_EQ(p.fps_cap, 60);  // carried over from the legacy options
  EXPECT_TRUE(p.vkbasalt);
  EXPECT_EQ(app["nova-perf"], (nlohmann::json {{"fps_cap", 60}, {"fsr", 3}, {"vkbasalt", true}}));
  EXPECT_EQ(app["nova-compat"], (nlohmann::json {{"prefix", "/p"}}));

  // A rejected body leaves the app alone.
  const auto before = app;
  EXPECT_THROW(update_app(app, "not json"), std::invalid_argument);
  EXPECT_THROW(update_app(app, R"({"fsr": 9})"), std::invalid_argument);
  EXPECT_THROW(update_app(app, R"([1, 2])"), std::invalid_argument);
  EXPECT_EQ(app, before);

  // Resetting every field removes the profile.
  update_app(app, R"({"fps_cap": 0, "fsr": 0, "vkbasalt": false})");
  EXPECT_FALSE(app.contains("nova-perf"));
}

TEST(NovaPerf, ProfileApiReplyNamesTheLauncher) {
  auto reply = api_reply(proton_game(), true);
  EXPECT_EQ(reply["launcher"], "proton");
  EXPECT_TRUE(reply["applies"].get<bool>());
  EXPECT_TRUE(reply["can_edit"].get<bool>());
  EXPECT_EQ(reply["profile"], to_json({}));

  reply = api_reply({{"name", "Cyberpunk 2077"}, {"cmd", "steam steam://rungameid/1091500"}}, false);
  EXPECT_EQ(reply["launcher"], "steam");
  EXPECT_FALSE(reply["applies"].get<bool>());
  EXPECT_FALSE(reply["can_edit"].get<bool>());

  EXPECT_EQ(launcher_of({{"cmd", "env LUTRIS_SKIP_INIT=1 lutris lutris:rungameid/3"}}), launcher_e::lutris);
  EXPECT_EQ(launcher_of({{"cmd", "/usr/games/supertuxkart"}}), launcher_e::command);
  EXPECT_EQ(launcher_of({{"name", "Desktop"}}), launcher_e::none);
  EXPECT_FALSE(api_reply({{"name", "Desktop"}, {"cmd", "  "}}, true)["applies"].get<bool>());
}

TEST(NovaPerf, AppListCarriesTheProfile) {
  auto tuned = proton_game();
  tuned["nova-perf"] = {{"fps_cap", 72}};
  const nlohmann::json apps = nlohmann::json::array({tuned, {{"name", "Desktop"}, {"cmd", ""}}});
  const auto list = nova_api::apps_list(apps, {"1", "2"}, std::nullopt, true, {}, fs::temp_directory_path());
  ASSERT_EQ(list.size(), 2);
  EXPECT_EQ(list[0]["profile"]["fps_cap"], 72);
  EXPECT_TRUE(list[1]["profile"].is_null());
}

TEST(NovaPerf, DefaultProfileSetsNothing) {
  EXPECT_TRUE(build_env({}, launcher_e::proton, std::nullopt).empty());
  EXPECT_TRUE(build_env({}, launcher_e::command, fs::path("/x.conf")).empty());
}

TEST(NovaPerf, ProtonGamesAreCappedByDxvkOnly) {
  const auto env = build_env({.fps_cap = 60, .fsr = 1}, launcher_e::proton, std::nullopt);
  EXPECT_EQ(value_of(env, "DXVK_FRAME_RATE"), "60");
  EXPECT_EQ(value_of(env, "VKD3D_FRAME_RATE"), "60");
  EXPECT_EQ(value_of(env, "WINE_FULLSCREEN_FSR"), "1");
  EXPECT_EQ(value_of(env, "WINE_FULLSCREEN_FSR_STRENGTH"), "0");
  EXPECT_FALSE(value_of(env, "MANGOHUD"));
  EXPECT_FALSE(value_of(env, "MANGOHUD_CONFIG"));
}

TEST(NovaPerf, NativeGamesAreCappedThroughAHiddenMangoHud) {
  auto env = build_env({.fps_cap = 90}, launcher_e::command, std::nullopt);
  EXPECT_EQ(value_of(env, "MANGOHUD"), "1");
  EXPECT_EQ(value_of(env, "MANGOHUD_CONFIG"), "no_display,fps_limit=90");

  // With the overlay on, the user's MangoHud.conf is still read.
  env = build_env({.fps_cap = 90, .mangohud = true}, launcher_e::command, std::nullopt);
  EXPECT_EQ(value_of(env, "MANGOHUD_CONFIG"), "read_cfg,fps_limit=90");

  env = build_env({.mangohud = true}, launcher_e::proton, std::nullopt);
  EXPECT_EQ(value_of(env, "MANGOHUD"), "1");
  EXPECT_FALSE(value_of(env, "MANGOHUD_CONFIG"));
}

TEST(NovaPerf, FsrStrengthFollowsTheCompatScale) {
  EXPECT_EQ(value_of(build_env({.fsr = 5}, launcher_e::proton, std::nullopt), "WINE_FULLSCREEN_FSR_STRENGTH"), "4");
  EXPECT_EQ(value_of(build_env({.fsr = 9}, launcher_e::proton, std::nullopt), "WINE_FULLSCREEN_FSR_STRENGTH"), "4");
}

TEST(NovaPerf, VkBasaltNeedsItsConfigFile) {
  const profile_t p {.vkbasalt = true, .vkbasalt_cas = 75};
  EXPECT_FALSE(value_of(build_env(p, launcher_e::command, std::nullopt), "ENABLE_VKBASALT"));

  const auto dir = fs::temp_directory_path() / "nova-perf-test-vkbasalt";
  fs::remove_all(dir);
  const auto file = write_vkbasalt_config(dir, "Marvel's Spider-Man 2", p);
  ASSERT_TRUE(file);
  EXPECT_EQ(file->filename(), "marvel-s-spider-man-2.conf");
  std::ifstream in(*file);
  std::stringstream text;
  text << in.rdbuf();
  EXPECT_NE(text.str().find("effects = cas\n"), std::string::npos);
  EXPECT_NE(text.str().find("casSharpness = 0.75\n"), std::string::npos);

  const auto env = build_env(p, launcher_e::command, file);
  EXPECT_EQ(value_of(env, "ENABLE_VKBASALT"), "1");
  EXPECT_EQ(value_of(env, "VKBASALT_CONFIG_FILE"), file->string());
  fs::remove_all(dir);
}

TEST(NovaPerf, VkBasaltConfigFormatsTheSharpness) {
  EXPECT_NE(vkbasalt_config({.vkbasalt_cas = 100}).find("casSharpness = 1.00"), std::string::npos);
  EXPECT_NE(vkbasalt_config({.vkbasalt_cas = 5}).find("casSharpness = 0.05"), std::string::npos);
  EXPECT_NE(vkbasalt_config({.vkbasalt_cas = 0}).find("casSharpness = 0.00"), std::string::npos);
}

TEST(NovaPerf, StreamSettingsAreStoredAndRead) {
  auto app = proton_game();
  store(app, {.bitrate_kbps = 40000, .power = power_e::performance});
  EXPECT_EQ(app["nova-perf"], (nlohmann::json {{"bitrate_kbps", 40000}, {"power", "performance"}}));
  const auto p = for_app(app);
  EXPECT_EQ(p.bitrate_kbps, 40000);
  EXPECT_EQ(p.power, power_e::performance);
  EXPECT_FALSE(is_default(p));
  EXPECT_EQ(to_json(p)["power"], "performance");
  EXPECT_EQ(to_json({})["power"], "default");

  // Hand edits: an unknown power word follows the host setting, bitrates are clamped.
  app["nova-perf"] = {{"bitrate_kbps", 100}, {"power", "turbo"}};
  EXPECT_EQ(for_app(app).bitrate_kbps, MIN_BITRATE_KBPS);
  EXPECT_EQ(for_app(app).power, power_e::follow);
  app["nova-perf"] = {{"bitrate_kbps", 5000000}};
  EXPECT_EQ(for_app(app).bitrate_kbps, MAX_BITRATE_KBPS);
  app["nova-perf"] = {{"bitrate_kbps", -3}};
  EXPECT_TRUE(is_default(for_app(app)));
}

TEST(NovaPerf, StreamSettingUpdatesAreStrict) {
  const profile_t start {};
  EXPECT_EQ(apply_update(start, {{"bitrate_kbps", 25000}}).bitrate_kbps, 25000);
  EXPECT_EQ(apply_update(start, {{"bitrate_kbps", 0}}).bitrate_kbps, 0);
  EXPECT_EQ(apply_update(start, {{"power", "balanced"}}).power, power_e::balanced);
  EXPECT_EQ(apply_update({.power = power_e::balanced}, {{"power", "default"}}).power, power_e::follow);
  EXPECT_THROW(apply_update(start, {{"bitrate_kbps", 100}}), std::invalid_argument);
  EXPECT_THROW(apply_update(start, {{"bitrate_kbps", MAX_BITRATE_KBPS + 1}}), std::invalid_argument);
  EXPECT_THROW(apply_update(start, {{"bitrate_kbps", "40000"}}), std::invalid_argument);
  EXPECT_THROW(apply_update(start, {{"power", "turbo"}}), std::invalid_argument);
  EXPECT_THROW(apply_update(start, {{"power", true}}), std::invalid_argument);
}

TEST(NovaPerf, PowerOverrideAndBitrateCap) {
  EXPECT_EQ(power_override({}), std::nullopt);
  EXPECT_EQ(power_override({.power = power_e::performance}), true);
  EXPECT_EQ(power_override({.power = power_e::balanced}), false);
  EXPECT_EQ(parse_power("default"), power_e::follow);
  EXPECT_EQ(parse_power("Performance"), std::nullopt);

  EXPECT_EQ(cap_bitrate(80000, 0), 80000);
  EXPECT_EQ(cap_bitrate(80000, 30000), 30000);
  EXPECT_EQ(cap_bitrate(20000, 30000), 20000);
}

TEST(NovaPerf, ProfileApiReplyListsTheLimits) {
  const auto reply = api_reply(proton_game(), true);
  EXPECT_EQ(reply["limits"]["bitrate_kbps"], (nlohmann::json {MIN_BITRATE_KBPS, MAX_BITRATE_KBPS}));
  EXPECT_EQ(reply["limits"]["fps_cap"], (nlohmann::json {0, MAX_FPS_CAP}));
  EXPECT_EQ(reply["profile"]["bitrate_kbps"], 0);
}

TEST(NovaPerf, AVirtualDisplayCapIsCombinedWithTheGameCap) {
  EXPECT_EQ(combined_cap(0, 0), 0);
  EXPECT_EQ(combined_cap(60, 0), 60);
  EXPECT_EQ(combined_cap(0, 120), 120);
  EXPECT_EQ(combined_cap(60, 120), 60);
  EXPECT_EQ(combined_cap(144, 120), 120);

  // The lower cap wins.
  auto env = build_env({.fps_cap = 60}, launcher_e::proton, std::nullopt, 120);
  EXPECT_EQ(value_of(env, "DXVK_FRAME_RATE"), "60");
  env = build_env({.fps_cap = 144}, launcher_e::proton, std::nullopt, 120);
  EXPECT_EQ(value_of(env, "DXVK_FRAME_RATE"), "120");

  // No game cap: the virtual display's variables are left alone.
  env = build_env({.fsr = 2}, launcher_e::proton, std::nullopt, 120);
  EXPECT_FALSE(value_of(env, "DXVK_FRAME_RATE"));
  EXPECT_FALSE(value_of(env, "MANGOHUD_CONFIG"));

  // The overlay stays visible and keeps the virtual display's limit.
  env = build_env({.mangohud = true}, launcher_e::command, std::nullopt, 120);
  EXPECT_EQ(value_of(env, "MANGOHUD_CONFIG"), "read_cfg,fps_limit=120");

  // Stream-only settings set no launch variables.
  EXPECT_TRUE(build_env({.bitrate_kbps = 20000, .power = power_e::performance}, launcher_e::command, std::nullopt, 120).empty());
}

TEST(NovaPerf, ProfileApiStoresStreamSettings) {
  auto app = proton_game();
  const auto p = update_app(app, R"({"bitrate_kbps": 35000, "power": "balanced"})");
  EXPECT_EQ(p.bitrate_kbps, 35000);
  EXPECT_EQ(app["nova-perf"], (nlohmann::json {{"bitrate_kbps", 35000}, {"power", "balanced"}}));
  update_app(app, R"({"bitrate_kbps": 0, "power": "default"})");
  EXPECT_FALSE(app.contains("nova-perf"));
}
