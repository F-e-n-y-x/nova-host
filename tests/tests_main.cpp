/**
 * @file tests/tests_main.cpp
 * @brief Entry point definition.
 */

// standard includes
#include <cstdlib>
#include <filesystem>
#include <string>

#ifdef _WIN32
  #include <process.h>
#else
  #include <unistd.h>
#endif

// test includes
#include "tests_common.h"
#include "tests_environment.h"
#include "tests_events.h"

namespace {
  /**
   * @brief Set an environment variable on every supported platform.
   *
   * @param name Variable name.
   * @param value Variable value.
   */
  void set_env(const char *name, const std::string &value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
  }

  /**
   * @brief Redirect the user's home and config directories to a throwaway directory.
   *
   * Runs before C++ static initializers (priority 101), because several globals resolve
   * platf::appdata() during static init. Without this the suite reads and rewrites the
   * developer's real config, state file and paired devices. Set NOVA_TESTS_USE_REAL_HOME=1
   * to opt out.
   */
  __attribute__((constructor(101))) void isolate_user_directories() {
    if (std::getenv("NOVA_TESTS_USE_REAL_HOME") != nullptr) {
      return;
    }
#ifdef _WIN32
    const auto pid = std::to_string(_getpid());
#else
    const auto pid = std::to_string(getpid());
#endif
    std::error_code ec;
    const auto root = std::filesystem::temp_directory_path(ec) / ("nova-tests-" + pid);
    std::filesystem::create_directories(root / ".config", ec);
    const auto home = root.string();
    set_env("HOME", home);
    set_env("XDG_CONFIG_HOME", (root / ".config").string());
    set_env("XDG_DATA_HOME", (root / ".local" / "share").string());
    set_env("XDG_STATE_HOME", (root / ".local" / "state").string());
    set_env("XDG_CACHE_HOME", (root / ".cache").string());
    set_env("CONFIGURATION_DIRECTORY", "");
#ifdef _WIN32
    set_env("APPDATA", (root / "AppData" / "Roaming").string());
    set_env("LOCALAPPDATA", (root / "AppData" / "Local").string());
    set_env("USERPROFILE", home);
#endif
  }
}  // namespace

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  testing::AddGlobalTestEnvironment(new SunshineEnvironment);
  testing::UnitTest::GetInstance()->listeners().Append(new SunshineEventListener);
  return RUN_ALL_TESTS();
}
