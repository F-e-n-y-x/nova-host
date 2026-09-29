/**
 * @file tests/unit/test_phase1_host.cpp
 * @brief Phase 1 host features: login lockout, private files, host commands, sleep, Wake-on-LAN
 *        MAC lookup, capabilities.
 */
// test includes
#include "../tests_common.h"

// standard includes
#include <algorithm>
#include <chrono>
#include <format>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef _WIN32
  #include <sys/stat.h>
  #include <unistd.h>
#endif

// local includes
#include <src/client_permissions.h>
#include <src/host_commands.h>
#include <src/host_info.h>
#include <src/login_guard.h>
#include <src/network.h>
#include <src/nova_client_api.h>
#include <src/secure_files.h>
#ifdef __linux__
  #include <src/platform/linux/nic.h>
#endif

namespace fs = std::filesystem;
using namespace std::literals;

namespace {
  struct temp_dir_t {
    fs::path path;

    temp_dir_t():
        path {fs::temp_directory_path() / ("nova-phase1-" + std::to_string(::getpid()) + "-" + ::testing::UnitTest::GetInstance()->current_test_info()->name())} {
      fs::remove_all(path);
      fs::create_directories(path);
    }

    ~temp_dir_t() {
      std::error_code ec;
      fs::remove_all(path, ec);
    }
  };

  void write(const fs::path &file, const std::string &text) {
    fs::create_directories(file.parent_path());
    std::ofstream {file} << text << '\n';
  }

  host_commands::command_t command(std::string cmd, std::chrono::seconds timeout = 5s) {
    return {.id = "t", .name = "test", .cmd = std::move(cmd), .timeout = timeout};
  }
}  // namespace

// ---------------------------------------------------------------------------------------------
// Login lockout
// ---------------------------------------------------------------------------------------------

TEST(Phase1LoginGuard, LocksAfterFiveDistinctFailuresAndDoubles) {
  login_guard::limiter_t limiter;
  const auto t0 = login_guard::limiter_t::clock::now();
  for (int i = 0; i < 4; ++i) {
    EXPECT_EQ(limiter.record_failure("1.2.3.4", std::format("pw{}", i), t0), 0s);
  }
  EXPECT_EQ(limiter.locked_for("1.2.3.4", t0), 0s);
  EXPECT_EQ(limiter.record_failure("1.2.3.4", "pw4", t0), 30s);
  EXPECT_EQ(limiter.locked_for("1.2.3.4", t0 + 10s), 20s);
  EXPECT_EQ(limiter.locked_for("1.2.3.4", t0 + 31s), 0s);
  EXPECT_EQ(limiter.record_failure("1.2.3.4", "pw5", t0 + 31s), 60s);
  EXPECT_EQ(limiter.record_failure("1.2.3.4", "pw6", t0 + 100s), 120s);
  // Other sources are unaffected.
  EXPECT_EQ(limiter.locked_for("1.2.3.5", t0 + 100s), 0s);
}

TEST(Phase1LoginGuard, CapsAtMaximumAndSuccessResets) {
  login_guard::limiter_t limiter;
  const auto t0 = login_guard::limiter_t::clock::now();
  std::chrono::seconds last {};
  for (int i = 0; i < 40; ++i) {
    last = limiter.record_failure("src", std::format("pw{}", i), t0 + std::chrono::hours {i});
  }
  EXPECT_EQ(last, 15min);
  limiter.record_success("src");
  EXPECT_EQ(limiter.failures("src"), 0);
  EXPECT_EQ(limiter.locked_for("src", t0 + 40h), 0s);
}

TEST(Phase1LoginGuard, RepeatedSameCredentialsCountOnce) {
  login_guard::limiter_t limiter;
  const auto t0 = login_guard::limiter_t::clock::now();
  for (int i = 0; i < 50; ++i) {
    EXPECT_EQ(limiter.record_failure("src", "same", t0), 0s);
  }
  EXPECT_EQ(limiter.failures("src"), 1);
}

TEST(Phase1LoginGuard, ForgetsIdleSources) {
  login_guard::policy_t policy;
  policy.forget_after = 60s;
  login_guard::limiter_t limiter {policy};
  const auto t0 = login_guard::limiter_t::clock::now();
  limiter.record_failure("old", "a", t0);
  limiter.record_failure("new", "b", t0 + 120s);  // prunes "old"
  EXPECT_EQ(limiter.failures("old"), 0);
  EXPECT_EQ(limiter.failures("new"), 1);
}

TEST(Phase1LoginGuard, Ipv6IsGroupedBySlash64) {
  EXPECT_EQ(login_guard::source_key("192.168.1.9"), "192.168.1.9");
  EXPECT_EQ(login_guard::source_key("2001:db8:1:2:aaaa::1"), login_guard::source_key("2001:db8:1:2:bbbb::2"));
  EXPECT_NE(login_guard::source_key("2001:db8:1:2::1"), login_guard::source_key("2001:db8:1:3::1"));
  EXPECT_EQ(login_guard::source_key("not an address"), "not an address");
}

// ---------------------------------------------------------------------------------------------
// Tailscale counts as LAN (so origin_web_ui_allowed = lan keeps it)
// ---------------------------------------------------------------------------------------------

TEST(Phase1Network, TailscaleAddressesAreLan) {
  EXPECT_EQ(net::from_address("100.64.0.1"), net::LAN);
  EXPECT_EQ(net::from_address("100.101.102.103"), net::LAN);
  EXPECT_EQ(net::from_address("100.127.255.254"), net::LAN);
  EXPECT_EQ(net::from_address("fd7a:115c:a1e0::1234"), net::LAN);
  EXPECT_EQ(net::from_address("100.128.0.1"), net::WAN);
  EXPECT_EQ(net::from_address("8.8.8.8"), net::WAN);
  EXPECT_EQ(net::from_enum_string("lan"), net::LAN);
}

// ---------------------------------------------------------------------------------------------
// Private files
// ---------------------------------------------------------------------------------------------

#ifndef _WIN32
TEST(Phase1SecureFiles, WritePrivateCreatesOwnerOnlyFileAtomically) {
  temp_dir_t dir;
  const auto file = dir.path / "state.json";
  ASSERT_TRUE(secure_files::write_private(file, "{\"a\":1}"));
  struct stat st {};
  ASSERT_EQ(::stat(file.c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 0777, 0600u);
  EXPECT_FALSE(secure_files::is_exposed(file));
  ASSERT_TRUE(secure_files::write_private(file, "second"));
  std::ifstream in {file};
  std::string text;
  std::getline(in, text);
  EXPECT_EQ(text, "second");
  // No temp files left behind.
  EXPECT_EQ(std::distance(fs::directory_iterator {dir.path}, fs::directory_iterator {}), 1);
}

TEST(Phase1SecureFiles, RestrictTightensFilesAndDirectories) {
  temp_dir_t dir;
  const auto sub = dir.path / "credentials";
  fs::create_directories(sub);
  const auto key = sub / "cakey.pem";
  write(key, "secret");
  ::chmod(sub.c_str(), 0775);
  ::chmod(key.c_str(), 0664);
  EXPECT_TRUE(secure_files::is_exposed(key));
  EXPECT_TRUE(secure_files::restrict(sub));
  EXPECT_TRUE(secure_files::restrict(key));
  struct stat st {};
  ::stat(sub.c_str(), &st);
  EXPECT_EQ(st.st_mode & 0777, 0700u);
  ::stat(key.c_str(), &st);
  EXPECT_EQ(st.st_mode & 0777, 0600u);
  EXPECT_TRUE(secure_files::restrict(dir.path / "missing"));
}

TEST(Phase1SecureFiles, RestrictDoesNotFollowSymlinks) {
  temp_dir_t dir;
  const auto target = dir.path / "target";
  write(target, "x");
  ::chmod(target.c_str(), 0644);
  fs::create_symlink(target, dir.path / "link");
  secure_files::restrict(dir.path / "link");
  struct stat st {};
  ::stat(target.c_str(), &st);
  EXPECT_EQ(st.st_mode & 0777, 0644u);
}
#endif

// ---------------------------------------------------------------------------------------------
// Host commands
// ---------------------------------------------------------------------------------------------

TEST(Phase1HostCommands, SplitsLikeAShellWithoutRunningOne) {
  using host_commands::split_command_line;
  EXPECT_EQ(split_command_line("steam -shutdown"), (std::vector<std::string> {"steam", "-shutdown"}));
  EXPECT_EQ(split_command_line("sh -c 'echo $HOME; ls | wc -l'"), (std::vector<std::string> {"sh", "-c", "echo $HOME; ls | wc -l"}));
  EXPECT_EQ(split_command_line(R"(a "b c" d\ e "f\"g")"), (std::vector<std::string> {"a", "b c", "d e", "f\"g"}));
  EXPECT_EQ(split_command_line(R"(x '' y)"), (std::vector<std::string> {"x", "", "y"}));
  EXPECT_FALSE(split_command_line("echo 'open"));
  EXPECT_FALSE(split_command_line("   "));
}

TEST(Phase1HostCommands, ParseValidatesAndDerivesIds) {
  std::vector<std::string> errors;
  const auto list = host_commands::parse_list(nlohmann::json::parse(R"([
    {"name": "Restart Steam", "cmd": "steam -shutdown", "icon": "refresh", "confirm": true, "timeout": 5000},
    {"name": "Restart Steam", "cmd": "true"},
    {"id": "lock", "name": "Lock", "cmd": "loginctl lock-session", "icon": "not-an-icon"},
    {"id": "lock", "name": "Dup", "cmd": "true"},
    {"id": "bad id!", "name": "Bad", "cmd": "true"},
    {"name": "", "cmd": "true"},
    {"name": "Quote", "cmd": "echo 'x"},
    42
  ])"), &errors);
  ASSERT_EQ(list.size(), 3u);
  EXPECT_EQ(list[0].id, "restart-steam");
  EXPECT_EQ(list[0].icon, "refresh");
  EXPECT_TRUE(list[0].confirm);
  EXPECT_EQ(list[0].timeout, host_commands::max_timeout);
  EXPECT_EQ(list[1].id, "restart-steam-2");
  EXPECT_EQ(list[2].id, "lock");
  EXPECT_EQ(list[2].icon, "");
  EXPECT_EQ(errors.size(), 5u);
  EXPECT_TRUE(host_commands::parse_text("not json").empty());
  EXPECT_TRUE(host_commands::parse_text("").empty());
}

TEST(Phase1HostCommands, SuperCmdsListsAppThenGlobalWithoutDuplicates) {
  const std::vector<host_commands::command_t> app {{.id = "a", .name = "App one", .cmd = "true"}, {.id = "g", .name = "App shadow", .cmd = "true"}};
  const std::vector<host_commands::command_t> global {{.id = "g", .name = "Global", .cmd = "true"}, {.id = "h", .name = "Other", .cmd = "true"}};
  const auto json = nlohmann::json::parse(host_commands::super_cmds_json(app, global));
  ASSERT_EQ(json.size(), 3u);
  EXPECT_EQ(json[0], (nlohmann::json {{"id", "a"}, {"name", "App one"}}));
  EXPECT_EQ(json[1]["name"], "App shadow");
  EXPECT_EQ(json[2]["id"], "h");
  EXPECT_EQ(host_commands::super_cmds_json({}, {}), "[]");

  const auto found = host_commands::resolve("g", app, global);
  ASSERT_TRUE(found);
  EXPECT_EQ(found->first.name, "App shadow");
  EXPECT_TRUE(found->second);
  EXPECT_FALSE(host_commands::resolve("missing", app, global));
  EXPECT_FALSE(host_commands::valid_id("../etc"));
  EXPECT_FALSE(host_commands::valid_id(std::string(65, 'a')));
  EXPECT_TRUE(host_commands::valid_id("Restart_Steam-2"));
}

#ifndef _WIN32
TEST(Phase1HostCommands, RunCapturesOutputAndExitCode) {
  const auto env = host_commands::build_env({{"NOVA_TEST_VALUE", "hello world"}});
  auto result = host_commands::run_sync(command("sh -c 'echo \"$NOVA_TEST_VALUE\"; echo oops >&2; exit 3'"), env);
  EXPECT_TRUE(result.started);
  EXPECT_EQ(result.exit_code, 3);
  EXPECT_FALSE(result.timed_out);
  EXPECT_NE(result.output.find("hello world"), std::string::npos);
  EXPECT_NE(result.output.find("oops"), std::string::npos);
  EXPECT_FALSE(result.ok());

  result = host_commands::run_sync(command("true"), env);
  EXPECT_TRUE(result.ok());
}

TEST(Phase1HostCommands, ArgumentsAreNeverInterpretedByAShell) {
  temp_dir_t dir;
  const auto marker = dir.path / "pwned";
  // Without a shell, ";" and "touch" are just arguments to echo.
  const auto result = host_commands::run_sync(command("echo hi ; touch " + marker.string()), host_commands::build_env({}));
  EXPECT_TRUE(result.ok());
  EXPECT_FALSE(fs::exists(marker));
  EXPECT_NE(result.output.find("; touch"), std::string::npos);
}

TEST(Phase1HostCommands, TimeoutKillsTheWholeGroup) {
  const auto start = std::chrono::steady_clock::now();
  const auto result = host_commands::run_sync(command("sh -c 'sleep 30 & sleep 30'", 1s), host_commands::build_env({}));
  const auto took = std::chrono::steady_clock::now() - start;
  EXPECT_TRUE(result.timed_out);
  EXPECT_FALSE(result.ok());
  EXPECT_LT(took, 8s);
}

TEST(Phase1HostCommands, MissingProgramAndHugeOutput) {
  auto result = host_commands::run_sync(command("/nonexistent/nova-binary"), host_commands::build_env({}));
  EXPECT_FALSE(result.started);
  EXPECT_FALSE(result.error.empty());

  result = host_commands::run_sync(command("sh -c 'head -c 100000 /dev/zero | tr \"\\\\0\" x'"), host_commands::build_env({}));
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.output.size(), host_commands::max_output_bytes);
  EXPECT_TRUE(result.truncated);
}

TEST(Phase1HostCommands, AsyncRunsAreTrackedAndNotStartedTwice) {
  host_commands::command_t slow {.id = "phase1-async", .name = "Async", .cmd = "sleep 1", .timeout = 10s};
  EXPECT_EQ(host_commands::start_async(slow, host_commands::build_env({}), "test"), host_commands::start_e::started);
  EXPECT_EQ(host_commands::start_async(slow, host_commands::build_env({}), "test"), host_commands::start_e::already_running);
  auto run = host_commands::last_run("phase1-async");
  ASSERT_TRUE(run);
  EXPECT_TRUE(run->running);
  EXPECT_EQ(run->device, "test");
  ASSERT_TRUE(host_commands::wait_idle(10s));
  run = host_commands::last_run("phase1-async");
  ASSERT_TRUE(run);
  EXPECT_FALSE(run->running);
  EXPECT_TRUE(run->result.ok());
  EXPECT_EQ(host_commands::run_to_json(*run)["ok"], true);
}
#endif

// ---------------------------------------------------------------------------------------------
// Sleep, capabilities, command listing
// ---------------------------------------------------------------------------------------------

TEST(Phase1Sleep, RefusalsFollowPermissionConfigAndOtherStreams) {
  namespace perm = client_permissions;
  EXPECT_EQ(nova_api::pcsleep_refusal(true, perm::standard, 0)->status, 403);
  EXPECT_EQ(nova_api::pcsleep_refusal(false, perm::full, 0)->status, 503);
  EXPECT_EQ(nova_api::pcsleep_refusal(true, perm::full, 1)->status, 409);
  EXPECT_FALSE(nova_api::pcsleep_refusal(true, perm::standard | perm::power, 0));
}

TEST(Phase1Capabilities, FeaturesAndCallerPermissions) {
  nova_api::host_features_t features {.mic = true, .clipboard = true, .motion = true, .pcsleep = true, .commands = true, .abr = true, .network_probe = true};
  auto caps = nova_api::capabilities("0.3.0", features, client_permissions::standard);
  EXPECT_TRUE(caps["nova"].get<bool>());
  const auto has = [](const nlohmann::json &list, const char *name) {
    return std::find(list.begin(), list.end(), nlohmann::json(name)) != list.end();
  };
  for (const auto *name : {"apps", "bitrate", "pcsleep", "commands", "supercmd", "mic", "clipboard", "motion", "rumble", "trigger_rumble", "wol", "running", "abr", "network_probe"}) {
    EXPECT_TRUE(has(caps["features"], name)) << name;
  }
  EXPECT_TRUE(has(caps["permissions"], "clipboard"));
  EXPECT_FALSE(has(caps["permissions"], "power"));
  EXPECT_FALSE(has(caps["permissions"], "host_commands"));

  caps = nova_api::capabilities("0.3.0", {}, client_permissions::full);
  EXPECT_FALSE(has(caps["features"], "pcsleep"));
  EXPECT_FALSE(has(caps["features"], "mic"));
  EXPECT_FALSE(has(caps["features"], "commands"));
  EXPECT_FALSE(has(caps["features"], "abr"));
  EXPECT_FALSE(has(caps["features"], "network_probe"));
  EXPECT_TRUE(has(caps["permissions"], "power"));
  EXPECT_TRUE(has(caps["permissions"], "host_commands"));
}

TEST(Phase1Commands, ListRespectsPermissionAndRunningApp) {
  const std::vector<host_commands::command_t> global {{.id = "g-lock", .name = "Lock", .icon = "lock", .cmd = "true"}};
  const auto apps = nlohmann::json::parse(R"([
    {"name": "Steam", "uuid": "A", "menu-cmd": [{"id": "restart", "name": "Restart Steam", "cmd": "true", "confirm": true}]},
    {"name": "Game", "uuid": "B", "menu-cmd": [{"id": "kill", "name": "Kill", "cmd": "true"}]}
  ])");

  auto list = nova_api::commands_list(false, global, apps, true, std::nullopt);
  EXPECT_FALSE(list["allowed"].get<bool>());
  EXPECT_TRUE(list["commands"].empty());

  list = nova_api::commands_list(true, global, apps, true, 1);
  ASSERT_EQ(list["commands"].size(), 3u);
  EXPECT_EQ(list["commands"][0]["id"], "restart");
  EXPECT_EQ(list["commands"][0]["scope"], "app");
  EXPECT_EQ(list["commands"][0]["app"], nova_api::app_id(apps[0]));
  EXPECT_FALSE(list["commands"][0]["runnable"].get<bool>());
  EXPECT_TRUE(list["commands"][0]["confirm"].get<bool>());
  EXPECT_TRUE(list["commands"][1]["runnable"].get<bool>());
  EXPECT_EQ(list["commands"][2]["scope"], "global");
  EXPECT_TRUE(list["commands"][2]["app"].is_null());
  EXPECT_EQ(list["commands"][2]["icon"], "lock");

  // Without launch permission only the running app's commands are visible.
  list = nova_api::commands_list(true, global, apps, false, 1);
  ASSERT_EQ(list["commands"].size(), 2u);
  EXPECT_EQ(list["commands"][0]["id"], "kill");
}

// ---------------------------------------------------------------------------------------------
// Wake-on-LAN: MAC of the physical NIC behind a bridge
// ---------------------------------------------------------------------------------------------

#ifdef __linux__
namespace {
  /**
   * @brief Build a fake /sys/class/net shaped like atom: enp7s0 in br0, tailscale0, docker0, veth.
   */
  fs::path fake_sysfs(const temp_dir_t &dir) {
    const auto net = dir.path / "net";
    const auto nic = [&net](const std::string &name, const std::string &mac, bool physical, const std::string &type = "1", const std::string &state = "up") {
      write(net / name / "address", mac);
      write(net / name / "type", type);
      write(net / name / "operstate", state);
      if (physical) {
        fs::create_directories(net / name / "device");
      }
    };
    nic("lo", "00:00:00:00:00:00", false, "772");
    nic("enp7s0", "9c:6b:00:a2:f9:49", true);
    fs::create_directories(net / "enp7s0" / "brport");
    nic("br0", "76:0d:83:b6:35:7a", false);
    fs::create_directories(net / "br0" / "bridge");
    fs::create_directories(net / "br0" / "brif" / "enp7s0");
    nic("tailscale0", "", false, "65534", "unknown");
    nic("docker0", "2e:6e:ba:b7:1f:55", false);
    fs::create_directories(net / "docker0" / "bridge");
    fs::create_directories(net / "docker0" / "brif" / "veth3b8b97d");
    nic("veth3b8b97d", "e6:bb:1a:57:71:b9", false);
    nic("wlp4s0", "aa:bb:cc:dd:ee:ff", true, "1", "down");
    fs::create_directories(net / "wlp4s0" / "wireless");
    return net;
  }
}  // namespace

TEST(Phase1WakeOnLan, BridgeReportsItsPhysicalMember) {
  temp_dir_t dir;
  const auto net = fake_sysfs(dir);
  EXPECT_EQ(platf::nic::wol_interface_for(net, "br0"), "enp7s0");
  EXPECT_EQ(platf::nic::mac_of(net, "enp7s0"), "9c:6b:00:a2:f9:49");
  EXPECT_EQ(platf::nic::wol_interface_for(net, "enp7s0"), "enp7s0");
}

TEST(Phase1WakeOnLan, TailscaleAndVirtualInterfacesFallBackToThePrimaryNic) {
  temp_dir_t dir;
  const auto net = fake_sysfs(dir);
  EXPECT_EQ(platf::nic::wol_interface_for(net, "tailscale0"), "enp7s0");
  EXPECT_EQ(platf::nic::wol_interface_for(net, "docker0"), "enp7s0");
  EXPECT_EQ(platf::nic::wol_interface_for(net, "veth3b8b97d"), "enp7s0");
  EXPECT_EQ(platf::nic::wol_interface_for(net, ""), "enp7s0");
  EXPECT_EQ(platf::nic::primary_physical_interface(net), "enp7s0");
  EXPECT_FALSE(platf::nic::mac_of(net, "tailscale0"));
  EXPECT_FALSE(platf::nic::mac_of(net, "lo"));
}

TEST(Phase1WakeOnLan, VlanUsesItsLowerDevice) {
  temp_dir_t dir;
  const auto net = fake_sysfs(dir);
  write(net / "enp7s0.10" / "address", "9c:6b:00:a2:f9:49");
  write(net / "enp7s0.10" / "type", "1");
  fs::create_directories(net / "enp7s0.10" / "lower_enp7s0");
  EXPECT_EQ(platf::nic::wol_interface_for(net, "enp7s0.10"), "enp7s0");
}

TEST(Phase1WakeOnLan, NoPhysicalNicMeansNoAnswer) {
  temp_dir_t dir;
  const auto net = dir.path / "net";
  write(net / "tailscale0" / "type", "65534");
  EXPECT_FALSE(platf::nic::wol_interface_for(net, "tailscale0"));
}

TEST(Phase1WakeOnLan, ParsesEthtoolOutput) {
  const auto state = platf::nic::parse_ethtool_wol(
    "Settings for enp7s0:\n"
    "\tSupported ports: [ TP ]\n"
    "\tSupports Wake-on: pumbg\n"
    "\tWake-on: d\n"
    "\tLink detected: yes\n"
  );
  ASSERT_TRUE(state);
  EXPECT_TRUE(state->magic_supported());
  EXPECT_FALSE(state->magic_enabled());
  EXPECT_TRUE(platf::nic::parse_ethtool_wol("\tSupports Wake-on: g\n\tWake-on: g\n")->magic_enabled());
  EXPECT_FALSE(platf::nic::parse_ethtool_wol("Settings for lo:\n\tLink detected: yes\n"));
}
#endif

// ---------------------------------------------------------------------------------------------
// Health checks
// ---------------------------------------------------------------------------------------------

TEST(Phase1Health, WakeOnLanSleepAndPrivateFileChecks) {
  host_info::health_probes_t probes;
  probes.check_wake_on_lan = true;
  probes.wol_interface = "enp7s0";
  probes.wol_mac = "9c:6b:00:a2:f9:49";
  probes.ethtool_found = true;
  probes.wol_supported = true;
  probes.wol_enabled = false;
  probes.pcsleep_enabled = true;
  probes.can_suspend = "challenge";
  probes.exposed_files = {"nova-host.conf", "sunshine_state.json"};

  const auto checks = host_info::evaluate_health(probes);
  const auto find = [&checks](const std::string &id) {
    const auto it = std::ranges::find_if(checks, [&id](const auto &c) {
      return c.id == id;
    });
    return it == checks.end() ? nullptr : &*it;
  };
  const auto *wol = find("wake-on-lan");
  ASSERT_NE(wol, nullptr);
  EXPECT_EQ(wol->status, host_info::health_status_e::warn);
  EXPECT_EQ(wol->fix_value, "sudo ethtool -s enp7s0 wol g");
  const auto *sleep = find("sleep");
  ASSERT_NE(sleep, nullptr);
  EXPECT_EQ(sleep->status, host_info::health_status_e::warn);
  EXPECT_NE(sleep->fix_value.find("nova-allow-suspend"), std::string::npos);
  ASSERT_NE(find("private-files"), nullptr);

  probes.wol_enabled = true;
  probes.can_suspend = "yes";
  probes.exposed_files.clear();
  const auto good = host_info::evaluate_health(probes);
  EXPECT_TRUE(std::ranges::all_of(good, [](const auto &c) {
    return c.id != "wake-on-lan" || c.status == host_info::health_status_e::ok;
  }));
  EXPECT_TRUE(std::ranges::none_of(good, [](const auto &c) {
    return c.id == "private-files";
  }));
}
