/**
 * @file tests/unit/test_nova_compat.cpp
 * @brief Tests for Nova's Windows game compatibility layer (GE-Proton launch settings).
 */
#include <algorithm>
#include <filesystem>
#include <fstream>

#include "src/nova_compat.h"

#include "../tests_common.h"

using namespace nova_compat;

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
}  // namespace

TEST(NovaCompat, SlugifiesAppNames) {
  EXPECT_EQ(slugify("Grand Theft Auto V"), "grand-theft-auto-v");
  EXPECT_EQ(slugify("  Marvel's Spider-Man 2!! "), "marvel-s-spider-man-2");
  EXPECT_EQ(slugify("???"), "game");
  EXPECT_EQ(slugify(""), "game");
}

TEST(NovaCompat, DefaultEnvironment) {
  const auto env = build_env("Grand Theft Auto V", 271590, {}, true);
  EXPECT_EQ(value_of(env, "NOVA_GAME_SLUG"), "grand-theft-auto-v");
  EXPECT_EQ(value_of(env, "NOVA_STEAM_APPID"), "271590");
  EXPECT_EQ(value_of(env, "NOVA_PROTON_AUTO_UPDATE"), "1");
  EXPECT_FALSE(value_of(env, "NOVA_COMPAT_FSR"));
  EXPECT_FALSE(value_of(env, "NOVA_COMPAT_FPS"));
  EXPECT_FALSE(value_of(env, "NOVA_COMPAT_MANGOHUD"));
  EXPECT_FALSE(value_of(env, "NOVA_COMPAT_PROTON"));

  const auto offline = build_env("Game", 0, {}, false);
  EXPECT_FALSE(value_of(offline, "NOVA_STEAM_APPID"));
  EXPECT_EQ(value_of(offline, "NOVA_PROTON_AUTO_UPDATE"), "0");
}

TEST(NovaCompat, EveryKeyIsClearable) {
  options_t o;
  o.prefix = "/p";
  o.fsr = 2;
  o.fps_cap = 60;
  o.mangohud = true;
  o.proton_version = "GE-Proton11-7";
  o.extra_env = {"A=1"};
  const auto &keys = env_keys();
  for (const auto &[key, _] : build_env("x", 1, o, true)) {
    EXPECT_NE(std::ranges::find(keys, key), keys.end()) << key;
  }
}

TEST(NovaCompat, ClampsAndFiltersOptions) {
  options_t o;
  o.prefix = "~/Games/nova/gta-v";
  o.fsr = 9;
  o.fps_cap = 5000;
  o.mangohud = true;
  o.proton_version = "latest";
  o.extra_env = {"DXVK_HUD=fps", "=bad", "1BAD=x", "BAD KEY=x", "NOEQUALS", "MULTI=a\nb", "OK_2=a=b"};
  const auto env = build_env("GTA V", 0, o, true);
  EXPECT_EQ(value_of(env, "NOVA_COMPAT_PREFIX"), "~/Games/nova/gta-v");
  EXPECT_EQ(value_of(env, "NOVA_COMPAT_FSR"), "5");
  EXPECT_EQ(value_of(env, "NOVA_COMPAT_FPS"), "1000");
  EXPECT_EQ(value_of(env, "NOVA_COMPAT_MANGOHUD"), "1");
  EXPECT_FALSE(value_of(env, "NOVA_COMPAT_PROTON"));
  EXPECT_EQ(value_of(env, "NOVA_COMPAT_ENV"), "DXVK_HUD=fps\nOK_2=a=b");

  o.fsr = 1;
  o.proton_version = "GE-Proton11-7";
  const auto pinned = build_env("GTA V", 0, o, true);
  EXPECT_EQ(value_of(pinned, "NOVA_COMPAT_FSR"), "1");
  EXPECT_EQ(value_of(pinned, "NOVA_COMPAT_PROTON"), "GE-Proton11-7");
}

TEST(NovaCompat, ChecksLaunchTarget) {
  EXPECT_FALSE(check_launch_target(""));
  const auto dir = std::filesystem::temp_directory_path() / "nova-compat-test";
  std::filesystem::create_directories(dir);
  const auto exe = dir / "Game.exe";
  std::ofstream(exe) << "MZ";
  EXPECT_FALSE(check_launch_target(exe.string()));

  const auto missing = check_launch_target((dir / "Missing.exe").string());
  ASSERT_TRUE(missing);
  EXPECT_NE(missing->find("Missing.exe"), std::string::npos);
  EXPECT_NE(missing->find("not found"), std::string::npos);
  // A folder is not a game file.
  EXPECT_TRUE(check_launch_target(dir.string()));
  std::filesystem::remove_all(dir);
}

TEST(NovaCompat, BuildsWrapperCommand) {
  EXPECT_EQ(wrapper_command("/usr/lib/nova-host/nova-proton-run", "/DATA/Games/Grand Theft Auto V/GTA5.exe"),
            "\"/usr/lib/nova-host/nova-proton-run\" \"/DATA/Games/Grand Theft Auto V/GTA5.exe\"");
  EXPECT_TRUE(wrapper_command("/w", "/g/bad\"name.exe").empty());
  EXPECT_FALSE(wrapper_path().empty());
}
