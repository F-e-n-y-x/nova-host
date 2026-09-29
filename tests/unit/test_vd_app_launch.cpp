/**
 * @file tests/unit/test_vd_app_launch.cpp
 * @brief Tests for launching single-instance apps on a virtual display (own profile, private D-Bus).
 */
// test includes
#include "../tests_common.h"

// standard includes
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

// local includes
#include <src/vd_app_launch.h>

using namespace std::chrono_literals;
using vd_app_launch::kind_e;
namespace fs = std::filesystem;

namespace {
  const fs::path config_dir {"/home/ayush/.config/nova-host"};
  const fs::path home {"/home/ayush"};

  bool no_electron(const std::string &) {
    return false;
  }

  std::string adapt(const std::string &cmd) {
    return vd_app_launch::adapt(cmd, config_dir, home, no_electron).cmd;
  }
}  // namespace

TEST(VdAppLaunch, ChromeGetsTheVirtualDesktopBrowserProfile) {
  // The owner's Library entry. With DISPLAY=:20 alone, Chrome handed the window to the copy on :0.
  const auto a = vd_app_launch::adapt("google-chrome-stable --start-maximized", config_dir, home, no_electron);
  EXPECT_EQ(a.kind, kind_e::chromium);
  EXPECT_TRUE(a.changed());
  EXPECT_EQ(a.cmd, "google-chrome-stable --user-data-dir=/home/ayush/.config/nova-host/virtual-display-browser --no-first-run --start-maximized");
  EXPECT_EQ(a.profile, config_dir / "virtual-display-browser") << "the same profile as nova-vd-session's browser: sign in once";
}

TEST(VdAppLaunch, EveryChromiumBrowserGetsAProfileOfItsFamily) {
  EXPECT_EQ(adapt("/usr/bin/google-chrome https://example.com"),
            "/usr/bin/google-chrome --user-data-dir=/home/ayush/.config/nova-host/virtual-display-browser --no-first-run https://example.com");
  EXPECT_EQ(adapt("chromium"), "chromium --user-data-dir=/home/ayush/.config/nova-host/virtual-display-profiles/chromium --no-first-run");
  EXPECT_EQ(adapt("chromium-browser"), "chromium-browser --user-data-dir=/home/ayush/.config/nova-host/virtual-display-profiles/chromium --no-first-run");
  EXPECT_EQ(adapt("brave-browser"), "brave-browser --user-data-dir=/home/ayush/.config/nova-host/virtual-display-profiles/brave --no-first-run");
  EXPECT_EQ(adapt("microsoft-edge-stable"), "microsoft-edge-stable --user-data-dir=/home/ayush/.config/nova-host/virtual-display-profiles/microsoft-edge --no-first-run");
  EXPECT_EQ(adapt("vivaldi-stable"), "vivaldi-stable --user-data-dir=/home/ayush/.config/nova-host/virtual-display-profiles/vivaldi --no-first-run");
  EXPECT_EQ(adapt("opera"), "opera --user-data-dir=/home/ayush/.config/nova-host/virtual-display-profiles/opera --no-first-run");
}

TEST(VdAppLaunch, ElectronAppsGetAProfileOfTheirOwn) {
  EXPECT_EQ(adapt("code --new-window"), "code --user-data-dir=/home/ayush/.config/nova-host/virtual-display-profiles/code --new-window");
  EXPECT_EQ(adapt("discord"), "discord --user-data-dir=/home/ayush/.config/nova-host/virtual-display-profiles/discord");
  EXPECT_EQ(adapt("/usr/bin/slack -u"), "/usr/bin/slack --user-data-dir=/home/ayush/.config/nova-host/virtual-display-profiles/slack -u");
  // An Electron app Nova doesn't know by name is found by its files.
  const auto a = vd_app_launch::adapt("/opt/SomeApp/someapp", config_dir, home, [](const std::string &p) {
    return p == "/opt/SomeApp/someapp";
  });
  EXPECT_EQ(a.kind, kind_e::electron);
  EXPECT_EQ(a.cmd, "/opt/SomeApp/someapp --user-data-dir=/home/ayush/.config/nova-host/virtual-display-profiles/someapp");
}

TEST(VdAppLaunch, FirefoxRunsAsASeparateInstanceWithItsOwnProfile) {
  const auto a = vd_app_launch::adapt("firefox https://example.com", config_dir, home, no_electron);
  EXPECT_EQ(a.kind, kind_e::firefox);
  EXPECT_EQ(a.cmd, "firefox --no-remote --profile /home/ayush/.config/nova-host/virtual-display-profiles/firefox https://example.com");
  EXPECT_EQ(adapt("firefox-esr"), "firefox-esr --no-remote --profile /home/ayush/.config/nova-host/virtual-display-profiles/firefox-esr");
}

TEST(VdAppLaunch, ACommandThatPicksItsOwnProfileIsLeftAlone) {
  for (const auto *cmd : {
         "google-chrome-stable --user-data-dir=/tmp/x",
         "google-chrome-stable --user-data-dir /tmp/x",
         "code --user-data-dir=/tmp/code",
       }) {
    const auto a = vd_app_launch::adapt(cmd, config_dir, home, no_electron);
    EXPECT_EQ(a.cmd, cmd);
    EXPECT_FALSE(a.changed());
    EXPECT_NE(a.note.find("user-data-dir"), std::string::npos) << a.note;
  }
  for (const auto *cmd : {"firefox -P work", "firefox --profile /tmp/p", "firefox --new-instance", "firefox -no-remote"}) {
    EXPECT_EQ(adapt(cmd), cmd);
  }
}

TEST(VdAppLaunch, OtherProgramsAreLeftAlone) {
  for (const auto *cmd : {
         "\"/usr/lib/x86_64-linux-gnu/nova-host/nova-proton-run\" \"/DATA/blue/Games/Grand Theft Auto V/GTA5.exe\"",
         "/opt/nuvio/bin/Nuvio",
         "steam steam://rungameid/1091500",
         "gnome-terminal",
         "sh -c \"google-chrome-stable\"",
         "",
       }) {
    const auto a = vd_app_launch::adapt(cmd, config_dir, home, no_electron);
    EXPECT_EQ(a.cmd, cmd);
    EXPECT_FALSE(a.changed());
    EXPECT_EQ(a.kind, kind_e::other) << cmd;
  }
}

TEST(VdAppLaunch, WrappersAndEnvironmentAreSkipped) {
  EXPECT_EQ(adapt("env GDK_SCALE=2 google-chrome-stable"),
            "env GDK_SCALE=2 google-chrome-stable --user-data-dir=/home/ayush/.config/nova-host/virtual-display-browser --no-first-run");
  EXPECT_EQ(adapt("gamemoderun nice -n 5 discord"), "gamemoderun nice -n 5 discord --user-data-dir=/home/ayush/.config/nova-host/virtual-display-profiles/discord");
}

TEST(VdAppLaunch, QuotedProgramsAndPathsWithSpaces) {
  EXPECT_EQ(adapt("\"/opt/google/chrome/google-chrome\" --kiosk"),
            "\"/opt/google/chrome/google-chrome\" --user-data-dir=/home/ayush/.config/nova-host/virtual-display-browser --no-first-run --kiosk");
  const auto a = vd_app_launch::adapt("google-chrome-stable", "/home/a b/.config/nova-host", "/home/a b", no_electron);
  EXPECT_EQ(a.cmd, "google-chrome-stable \"--user-data-dir=/home/a b/.config/nova-host/virtual-display-browser\" --no-first-run")
    << "Nova splits on spaces outside double quotes";
}

TEST(VdAppLaunch, FlatpakAppsKeepTheProfileInsideTheirSandbox) {
  const auto a = vd_app_launch::adapt("flatpak run --branch=stable com.google.Chrome --incognito", config_dir, home, no_electron);
  EXPECT_EQ(a.kind, kind_e::chromium);
  EXPECT_EQ(a.cmd, "flatpak run --branch=stable com.google.Chrome --user-data-dir=/home/ayush/.var/app/com.google.Chrome/nova-virtual-display --no-first-run --incognito");
  EXPECT_EQ(adapt("flatpak run org.mozilla.firefox"), "flatpak run org.mozilla.firefox --no-remote --profile /home/ayush/.var/app/org.mozilla.firefox/nova-virtual-display");
  EXPECT_EQ(adapt("flatpak run com.discordapp.Discord"), "flatpak run com.discordapp.Discord --user-data-dir=/home/ayush/.var/app/com.discordapp.Discord/nova-virtual-display");
  EXPECT_EQ(adapt("flatpak run org.gnome.Calculator"), "flatpak run org.gnome.Calculator");
}

TEST(VdAppLaunch, GamesKeepTheUsersBus) {
  // Proton and Steam aren't single-instance over D-Bus, and GameMode lives on the user's bus.
  EXPECT_FALSE(vd_app_launch::uses_session_bus("\"/usr/lib/x86_64-linux-gnu/nova-host/nova-proton-run\" \"/games/GTA5.exe\""));
  EXPECT_FALSE(vd_app_launch::uses_session_bus("umu-run /games/x.exe"));
  EXPECT_FALSE(vd_app_launch::uses_session_bus("steam steam://rungameid/1091500"));
  EXPECT_FALSE(vd_app_launch::uses_session_bus("wine /games/x.exe"));
  EXPECT_FALSE(vd_app_launch::uses_session_bus("gamemoderun umu-run /games/x.exe"));
  // Desktop programs join the virtual desktop's bus, so gnome-terminal and nemo open there.
  EXPECT_TRUE(vd_app_launch::uses_session_bus("gnome-terminal"));
  EXPECT_TRUE(vd_app_launch::uses_session_bus("nemo /home/ayush"));
  EXPECT_TRUE(vd_app_launch::uses_session_bus("lutris"));
  EXPECT_TRUE(vd_app_launch::uses_session_bus("google-chrome-stable --start-maximized"));
}

TEST(VdAppLaunch, ElectronFilesAreRecognised) {
  const auto dir = fs::temp_directory_path() / ("nova-electron-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
  fs::remove_all(dir);
  fs::create_directories(dir / "app" / "resources");
  fs::create_directories(dir / "plain");
  std::ofstream {dir / "app" / "resources.pak"} << "pak";
  std::ofstream {dir / "app" / "resources" / "app.asar"} << "asar";
  std::ofstream {dir / "app" / "myapp"} << "#!/bin/sh\n";
  std::ofstream {dir / "plain" / "tool"} << "#!/bin/sh\n";
  // VS Code style: a bin/ script one level below the Electron files.
  fs::create_directories(dir / "app" / "bin");
  std::ofstream {dir / "app" / "bin" / "myapp"} << "#!/bin/sh\n";
  EXPECT_TRUE(vd_app_launch::looks_electron((dir / "app" / "myapp").string()));
  EXPECT_TRUE(vd_app_launch::looks_electron((dir / "app" / "bin" / "myapp").string()));
  EXPECT_FALSE(vd_app_launch::looks_electron((dir / "plain" / "tool").string()));
  EXPECT_FALSE(vd_app_launch::looks_electron((dir / "missing").string()));
  fs::remove_all(dir);
}

TEST(VdAppLaunch, SessionEnvironmentComesFromTheSessionsFile) {
  const auto dir = fs::temp_directory_path() / ("nova-vd-session-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
  fs::remove_all(dir);
  fs::create_directories(dir);
  EXPECT_TRUE(vd_app_launch::session_env("", 0ms).empty()) << "no desktop session: nothing to join";
  EXPECT_TRUE(vd_app_launch::session_env(dir.string(), 100ms).empty()) << "the file never came";
  std::ofstream {dir / "app-env.json"} << R"({"DBUS_SESSION_BUS_ADDRESS": "unix:path=/tmp/dbus-x,guid=1", "DCONF_PROFILE": "/s/dconf-profile",
    "GIO_USE_VFS": "local", "PATH": "/evil", "LD_PRELOAD": "/evil.so"})";
  const auto env = vd_app_launch::session_env(dir.string(), 0ms);
  ASSERT_EQ(env.size(), 3u) << "only the session's own variables are taken";
  EXPECT_EQ(env[0], (std::pair<std::string, std::string> {"DBUS_SESSION_BUS_ADDRESS", "unix:path=/tmp/dbus-x,guid=1"}));
  EXPECT_EQ(env[1].first, "DCONF_PROFILE");
  EXPECT_EQ(env[2].first, "GIO_USE_VFS");
  // Without a bus address the rest would only half-isolate the app.
  std::ofstream {dir / "app-env.json", std::ios::trunc} << R"({"DCONF_PROFILE": "/s/dconf-profile"})";
  EXPECT_TRUE(vd_app_launch::session_env(dir.string(), 0ms).empty());
  std::ofstream {dir / "app-env.json", std::ios::trunc} << "not json";
  EXPECT_TRUE(vd_app_launch::session_env(dir.string(), 0ms).empty());
  fs::remove_all(dir);
}
