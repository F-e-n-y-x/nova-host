/**
 * @file tests/unit/test_app_processes.cpp
 * @brief Tests for Nova's view of the running app's processes (a Proton game whose helpers linger).
 */
// test includes
#include "../tests_common.h"

// standard includes
#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

// local includes
#include <src/app_processes.h>

using namespace std::chrono_literals;
using app_processes::linger_t;
using app_processes::process_t;

namespace {
  /**
   * @brief The tree of a real `nova-proton-run notepad.exe` (GE-Proton 11-7 through umu-run),
   * recorded with ps on atom. Only umu-run stays in Nova's process group: the container gets a
   * session of its own and so does every Wine process.
   */
  std::vector<process_t> proton_tree(bool with_game) {
    std::vector<process_t> t {
      {100, 99, 100, 'S', "umu-run", {"/usr/bin/python3", "/usr/bin/umu-run", "/games/notepad.exe"}},
      {120, 100, 120, 'S', "srt-bwrap", {"/home/u/.local/share/umu/steamrt4/pressure-vessel/libexec/steam-runtime-tools-0/srt-bwrap", "--args", "12"}},
      {188, 100, 120, 'S', "dbus-launch", {"dbus-launch", "--autolaunch=aa8c3eb9"}},
      {189, 100, 189, 'S', "dbus-daemon", {"/usr/bin/dbus-daemon", "--syslog-only"}},
      {192, 100, 189, 'S', "at-spi-bus-laun", {"/usr/libexec/at-spi-bus-launcher"}},
      {197, 192, 189, 'S', "dbus-daemon", {"/usr/bin/dbus-daemon", "--config-file=/usr/share/defaults/at-spi2/accessibility.conf"}},
      {198, 120, 198, 'S', "pv-adverb", {"/usr/lib/pressure-vessel/from-host/libexec/steam-runtime-tools-0/pv-adverb", "--prefix=/usr/lib/pressure-vessel/from-host"}},
      {221, 198, 198, 'S', "python3", {"python3", "/home/u/.local/share/lutris/runners/wine/ge-proton/proton", "waitforexitandrun", "/games/notepad.exe"}},
      {279, 221, 198, 'S', "umu.exe", {"c:\\windows\\system32\\umu.exe", "/games/notepad.exe"}},
      {281, 198, 281, 'S', "wineserver", {"/home/u/.local/share/lutris/runners/wine/ge-proton/files/lib/wine/x86_64-unix/wineserver"}},
      {285, 198, 285, 'S', "services.exe", {"C:\\windows\\system32\\services.exe"}},
      {288, 198, 288, 'S', "winedevice.exe", {"C:\\windows\\system32\\winedevice.exe"}},
      {298, 198, 298, 'S', "plugplay.exe", {"C:\\windows\\system32\\plugplay.exe"}},
      {324, 198, 324, 'S', "svchost.exe", {"C:\\windows\\system32\\svchost.exe", "-k", "LocalServiceNetworkRestricted"}},
      {353, 198, 353, 'S', "explorer.exe", {"C:\\windows\\system32\\explorer.exe", "/desktop"}},
      {384, 198, 384, 'S', "rpcss.exe", {"C:\\windows\\system32\\rpcss.exe"}},
      {394, 198, 394, 'S', "tabtip.exe", {"C:\\windows\\system32\\tabtip.exe"}},
      {396, 198, 396, 'S', "xalia.exe", {"\\\\?\\X:\\.local\\share\\lutris\\runners\\wine\\ge-proton\\files\\share\\wine/../xalia/xalia.exe"}},
      // someone else's processes
      {1, 0, 1, 'S', "systemd", {"/sbin/init"}},
      {500, 1, 500, 'S', "chrome", {"/opt/google/chrome/chrome"}},
    };
    if (with_game) {
      t.push_back({401, 198, 401, 'S', "GTA5.exe", {"Z:\\DATA\\blue\\Games\\Grand Theft Auto V\\GTA5.exe"}});
    }
    return t;
  }
}  // namespace

TEST(AppProcesses, NamesComeFromUnixAndWindowsPaths) {
  EXPECT_EQ(app_processes::name_of({1, 0, 1, 'S', "services.exe", {"C:\\windows\\system32\\services.exe"}}), "services.exe");
  EXPECT_EQ(app_processes::name_of({1, 0, 1, 'S', "GTA5.exe", {"Z:\\DATA\\Games\\Grand Theft Auto V\\GTA5.exe"}}), "gta5.exe");
  EXPECT_EQ(app_processes::name_of({1, 0, 1, 'S', "umu-run", {"/usr/bin/python3", "/usr/bin/umu-run"}}), "python3");
  EXPECT_EQ(app_processes::name_of({1, 0, 1, 'S', "kworker", {}}), "kworker") << "no command line: the kernel name";
}

TEST(AppProcesses, WineAndProtonPlumbingAreHelpers) {
  for (const auto &p : proton_tree(false)) {
    if (p.pid == 1 || p.pid == 500) {
      continue;
    }
    EXPECT_TRUE(app_processes::is_helper(p)) << p.comm;
  }
  EXPECT_FALSE(app_processes::is_helper({1, 0, 1, 'S', "GTA5.exe", {"Z:\\Games\\GTA5.exe"}}));
  EXPECT_FALSE(app_processes::is_helper({1, 0, 1, 'S', "Launcher.exe", {"C:\\Program Files\\Rockstar Games\\Launcher\\Launcher.exe"}}))
    << "a game's own launcher is part of the game";
  EXPECT_FALSE(app_processes::is_helper({1, 0, 1, 'S', "python3", {"python3", "mygame.py"}})) << "a Python game is a game";
  EXPECT_FALSE(app_processes::is_helper({1, 0, 1, 'S', "chrome", {"/opt/google/chrome/chrome"}}));
  EXPECT_FALSE(app_processes::is_helper({1, 0, 1, 'S', "wine64-preloa", {"/usr/bin/wine64-preloader", "/usr/bin/wine64", "game.exe"}}))
    << "a Wine loader that hasn't become the .exe yet is not known to be a helper";
}

TEST(AppProcesses, MembersFollowTheTreeOutOfTheProcessGroup) {
  const auto seen = app_processes::members(proton_tree(true), 100, 100);
  // umu-run is the only one in the group; the rest are its descendants in other sessions.
  EXPECT_EQ(seen.processes.size(), 19u);
  EXPECT_EQ(seen.games, 1);
  for (const auto &p : seen.processes) {
    EXPECT_NE(p.pid, 1);
    EXPECT_NE(p.pid, 500) << "processes outside the app are not members";
  }
}

TEST(AppProcesses, AReparentedGroupMemberStaysAMember) {
  // The wrapper exited; the game it started kept Nova's process group and was reparented to init.
  const std::vector<process_t> all {
    {1, 0, 1, 'S', "systemd", {"/sbin/init"}},
    {700, 1, 650, 'S', "game", {"/opt/game/game"}},
    {701, 700, 650, 'S', "game-helper", {"/opt/game/helper"}},
    {702, 1, 650, 'Z', "zombie", {}},
  };
  const auto seen = app_processes::members(all, 0, 650);
  EXPECT_EQ(seen.processes.size(), 2u) << "zombies don't count";
  EXPECT_EQ(seen.games, 2);
}

TEST(AppProcesses, NothingKnownMeansNoMembers) {
  EXPECT_TRUE(app_processes::members(proton_tree(true), 0, 0).processes.empty());
}

TEST(AppProcesses, HelperNamesForTheLog) {
  const auto seen = app_processes::members(proton_tree(false), 100, 100);
  const auto names = seen.helper_names();
  EXPECT_NE(names.find("wineserver"), std::string::npos) << names;
  EXPECT_NE(names.find("services.exe"), std::string::npos) << names;
}

TEST(AppProcessesLinger, TheGameEndsWhenOnlyHelpersRemainForTheGracePeriod) {
  linger_t linger;
  const auto t0 = linger_t::clock::time_point {} + 1h;
  const auto playing = app_processes::members(proton_tree(true), 100, 100);
  const auto lingering = app_processes::members(proton_tree(false), 100, 100);
  EXPECT_FALSE(linger.update(playing, t0));
  EXPECT_TRUE(linger.game_seen());
  // The game quit; wineserver, services.exe and friends are still there.
  EXPECT_FALSE(linger.update(lingering, t0 + 1s));
  EXPECT_FALSE(linger.update(lingering, t0 + 3s));
  EXPECT_TRUE(linger.update(lingering, t0 + 1s + linger_t::grace));
}

TEST(AppProcessesLinger, StartingUpIsNotTheEnd) {
  // umu-run can spend minutes creating a prefix or fetching Proton before the game appears.
  linger_t linger;
  const auto t0 = linger_t::clock::time_point {} + 1h;
  const auto starting = app_processes::members(proton_tree(false), 100, 100);
  for (auto t = t0; t < t0 + 10min; t += 1s) {
    EXPECT_FALSE(linger.update(starting, t));
  }
  EXPECT_FALSE(linger.game_seen());
}

TEST(AppProcessesLinger, AGameComingBackResetsTheGracePeriod) {
  // Launchers hand over to the game: for a moment only helpers remain.
  linger_t linger;
  const auto t0 = linger_t::clock::time_point {} + 1h;
  const auto playing = app_processes::members(proton_tree(true), 100, 100);
  const auto between = app_processes::members(proton_tree(false), 100, 100);
  EXPECT_FALSE(linger.update(playing, t0));
  EXPECT_FALSE(linger.update(between, t0 + 1s));
  EXPECT_FALSE(linger.update(playing, t0 + 3s));
  EXPECT_FALSE(linger.update(between, t0 + 4s));
  EXPECT_FALSE(linger.update(between, t0 + 6s));
  EXPECT_TRUE(linger.update(between, t0 + 7s));
}

TEST(AppProcessesLinger, NothingLeftIsLeftToTheNormalExitPath) {
  linger_t linger;
  const auto t0 = linger_t::clock::time_point {} + 1h;
  EXPECT_FALSE(linger.update(app_processes::members(proton_tree(true), 100, 100), t0));
  EXPECT_FALSE(linger.update({}, t0 + 1min));
  linger.reset();
  EXPECT_FALSE(linger.game_seen());
}

#ifdef __linux__
  #include <unistd.h>

TEST(AppProcesses, SnapshotSeesThisProcess) {
  const auto all = app_processes::snapshot();
  const auto self = std::ranges::find_if(all, [](const process_t &p) {
    return p.pid == static_cast<int>(::getpid());
  });
  ASSERT_NE(self, all.end());
  EXPECT_EQ(self->ppid, static_cast<int>(::getppid()));
  EXPECT_EQ(self->pgid, static_cast<int>(::getpgrp()));
  EXPECT_FALSE(self->argv.empty());
  EXPECT_FALSE(app_processes::is_helper(*self));
}
#endif
