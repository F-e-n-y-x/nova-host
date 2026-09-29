/**
 * @file tests/unit/test_process.cpp
 * @brief Test src/process.* functions.
 */
// test includes
#include "../tests_common.h"

// standard includes
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#ifdef __linux__
  #include <unistd.h>
#endif

// local includes
#include <src/app_processes.h>
#include <src/process.h>
#include <src/utility.h>

namespace fs = std::filesystem;

TEST(ProcessTest, PrepareCommand) {
#ifdef SUNSHINE_BUILD_FLATPAK
  EXPECT_EQ(proc::prepare_command("steam"), "flatpak-spawn --host steam");
  EXPECT_EQ(proc::prepare_command("flatpak-spawn --host steam"), "flatpak-spawn --host steam");
  EXPECT_EQ(proc::prepare_command("  flatpak-spawn --host steam  "), "flatpak-spawn --host steam");
  EXPECT_EQ(proc::prepare_command("  steam  "), "flatpak-spawn --host steam");
  EXPECT_EQ(proc::prepare_command(""), "");
  EXPECT_EQ(proc::prepare_command("  \t"), "");
#else
  EXPECT_EQ(proc::prepare_command("steam"), "steam");
  EXPECT_EQ(proc::prepare_command("  steam  "), "  steam  ");
  EXPECT_EQ(proc::prepare_command("flatpak-spawn --host steam"), "flatpak-spawn --host steam");
  EXPECT_EQ(proc::prepare_command(""), "");
#endif
}

class ProcessPNGTest: public BaseTest {
protected:
  void SetUp() override {
    BaseTest::SetUp();
    // Create test directory
    test_dir = fs::temp_directory_path() / "sunshine_process_png_test";  // NOSONAR(cpp:S5443): safe for tests
    fs::create_directories(test_dir);
  }

  void TearDown() override {
    // Clean up test directory
    if (fs::exists(test_dir)) {
      fs::remove_all(test_dir);
    }
    BaseTest::TearDown();
  }

  // Helper function to create a file with specific content
  void createTestFile(const fs::path &path, const std::vector<unsigned char> &content) const {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(content.data()), content.size());
    file.close();
  }

  fs::path test_dir;
};

// Tests for check_valid_png function
TEST_F(ProcessPNGTest, CheckValidPNG_ValidSignature) {
  // Valid PNG signature
  const std::vector<unsigned char> valid_png_data = {
    0x89,
    0x50,
    0x4E,
    0x47,
    0x0D,
    0x0A,
    0x1A,
    0x0A,  // PNG signature
    // Add some dummy data to make it more realistic
    0x00,
    0x00,
    0x00,
    0x0D,
    0x49,
    0x48,
    0x44,
    0x52
  };

  const fs::path test_file = test_dir / "valid.png";
  createTestFile(test_file, valid_png_data);

  EXPECT_TRUE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_WrongSignature) {
  // Invalid PNG signature (wrong magic bytes)
  const std::vector<unsigned char> invalid_png_data = {
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00
  };

  const fs::path test_file = test_dir / "invalid.png";
  createTestFile(test_file, invalid_png_data);

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_TooShort) {
  // File too short (less than 8 bytes)
  const std::vector<unsigned char> short_data = {
    0x89,
    0x50,
    0x4E,
    0x47
  };

  const fs::path test_file = test_dir / "short.png";
  createTestFile(test_file, short_data);

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_EmptyFile) {
  // Empty file
  const std::vector<unsigned char> empty_data = {};

  const fs::path test_file = test_dir / "empty.png";
  createTestFile(test_file, empty_data);

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_NonExistentFile) {
  // File doesn't exist
  const fs::path test_file = test_dir / "nonexistent.png";

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_RealFile) {
  // Test with the actual sunshine.png from the project root

  // Only run this test if the file exists
  if (const fs::path sunshine_png = fs::path(SUNSHINE_SOURCE_DIR) / "sunshine.png"; fs::exists(sunshine_png)) {
    EXPECT_TRUE(proc::check_valid_png(sunshine_png));
  } else {
    GTEST_SKIP() << "sunshine.png not found in project root";
  }
}

TEST_F(ProcessPNGTest, CheckValidPNG_JPEGFile) {
  // JPEG signature (not PNG)
  const std::vector<unsigned char> jpeg_data = {
    0xFF,
    0xD8,
    0xFF,
    0xE0,
    0x00,
    0x10,
    0x4A,
    0x46
  };

  const fs::path test_file = test_dir / "fake.png";
  createTestFile(test_file, jpeg_data);

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_PartialSignature) {
  // Partial PNG signature (first 4 bytes correct, rest wrong)
  const std::vector<unsigned char> partial_png_data = {
    0x89,
    0x50,
    0x4E,
    0x47,
    0x00,
    0x00,
    0x00,
    0x00
  };

  const fs::path test_file = test_dir / "partial.png";
  createTestFile(test_file, partial_png_data);

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

// Tests for validate_app_image_path function
TEST_F(ProcessPNGTest, ValidateAppImagePath_EmptyPath) {
  // Empty path should return default
  const std::string result = proc::validate_app_image_path("");
  EXPECT_EQ(result, DEFAULT_APP_IMAGE_PATH);
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_NonPNGExtension) {
  // Non-PNG extension should return default
  const std::string result = proc::validate_app_image_path("image.jpg");
  EXPECT_EQ(result, DEFAULT_APP_IMAGE_PATH);
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_CaseInsensitiveExtension) {
  // Test that .PNG (uppercase) is recognized
  // Create a valid PNG file
  const std::vector<unsigned char> valid_png_data = {
    0x89,
    0x50,
    0x4E,
    0x47,
    0x0D,
    0x0A,
    0x1A,
    0x0A,
    0x00,
    0x00,
    0x00,
    0x0D,
    0x49,
    0x48,
    0x44,
    0x52
  };

  const fs::path test_file = test_dir / "test.PNG";
  createTestFile(test_file, valid_png_data);

  const std::string result = proc::validate_app_image_path(test_file.string());
  // Should accept uppercase .PNG extension
  EXPECT_NE(result, DEFAULT_APP_IMAGE_PATH);
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_NonExistentFile) {
  // Non-existent PNG file should return default
  const std::string result = proc::validate_app_image_path("/nonexistent/path/image.png");
  EXPECT_EQ(result, DEFAULT_APP_IMAGE_PATH);
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_InvalidPNGSignature) {
  // File with .png extension but invalid signature should return default
  const std::vector<unsigned char> invalid_data = {
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00
  };

  const fs::path test_file = test_dir / "invalid.png";
  createTestFile(test_file, invalid_data);

  const std::string result = proc::validate_app_image_path(test_file.string());
  EXPECT_EQ(result, DEFAULT_APP_IMAGE_PATH);
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_ValidPNG) {
  // Valid PNG file should return the path
  const std::vector<unsigned char> valid_png_data = {
    0x89,
    0x50,
    0x4E,
    0x47,
    0x0D,
    0x0A,
    0x1A,
    0x0A,
    0x00,
    0x00,
    0x00,
    0x0D,
    0x49,
    0x48,
    0x44,
    0x52
  };

  const fs::path test_file = test_dir / "valid.png";
  createTestFile(test_file, valid_png_data);

  const std::string result = proc::validate_app_image_path(test_file.string());
  EXPECT_EQ(result, test_file.string());
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_OldSteamDefault) {
  // Test the special case for old steam image path
  const std::string result = proc::validate_app_image_path("./assets/steam.png");
  EXPECT_EQ(result, SUNSHINE_ASSETS_DIR "/steam.png");
}

TEST(ProcessTest, ReplacingTheAppListKeepsTheRunningApp) {
  // A library scan or a save in the web UI reloads apps.json while a game runs (Nova keeps it
  // running after a disconnect, so this happens often): the running app must not be forgotten.
  proc::ctx_t desktop {};
  desktop.id = "1";
  desktop.name = "Desktop";  // no command: runs until terminated
  auto make = [](std::vector<proc::ctx_t> apps) {
    return proc::proc_t {boost::process::v1::environment {}, std::move(apps)};
  };
  auto p = make({desktop});
  EXPECT_EQ(p.started_at(), 0);
  auto session = std::make_shared<rtsp_stream::launch_session_t>();
  auto stop = util::fail_guard([&p]() {
    p.terminate();  // ~proc_t asserts nothing runs
  });
  ASSERT_EQ(p.execute(1, session), 0);
  EXPECT_FALSE(p.executing());
  EXPECT_EQ(p.running(), 1);
  EXPECT_GT(p.started_at(), 0);

  auto other = desktop;
  other.id = "2";
  other.name = "Other";
  p.replace_apps(make({desktop, other}));
  EXPECT_EQ(p.get_apps().size(), 2u);
  EXPECT_EQ(p.running(), 1) << "still running after the reload";
  EXPECT_EQ(p.get_last_run_app_name(), "Desktop");

  p.terminate();
  EXPECT_EQ(p.running(), 0);
  EXPECT_EQ(p.started_at(), 0);
}

#ifdef __linux__
namespace {
  proc::proc_t make_proc(proc::ctx_t app) {
    app.id = "1";
    std::vector<proc::ctx_t> apps {std::move(app)};
    return proc::proc_t {boost::this_process::environment(), std::move(apps)};
  }

  proc::ctx_t command_app(const std::string &name, const std::string &cmd) {
    proc::ctx_t app {};
    app.name = name;
    app.cmd = cmd;
    app.auto_detach = true;
    app.wait_all = true;
    app.exit_timeout = std::chrono::seconds {1};
    return app;
  }

  /**
   * @brief Call running() until it says `want` or the time is up.
   * @return How long it took, or nullopt.
   */
  std::optional<std::chrono::milliseconds> wait_running(proc::proc_t &p, int want, std::chrono::milliseconds limit) {
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < limit) {
      if (p.running() == want) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds {100});
    }
    return std::nullopt;
  }

  struct temp_dir_t {
    fs::path path;

    temp_dir_t() {
      path = fs::temp_directory_path() / ("nova-proc-" + std::to_string(::getpid()) + "-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
      fs::remove_all(path);
      fs::create_directories(path);
    }

    ~temp_dir_t() {
      std::error_code ec;
      fs::remove_all(path, ec);
    }
  };
}  // namespace

TEST(ProcessTest, AGameLeftInTheGroupKeepsTheAppRunningAfterItsWrapperExits) {
  // A launcher that starts the game and exits: the game keeps Nova's process group but is no
  // longer Nova's child. It used to count as ended (and, with auto-detach, as "detached").
  auto p = make_proc(command_app("Wrapper", "/bin/sh -c \"sleep 6 & exit 0\""));
  auto stop = util::fail_guard([&p]() {
    p.terminate();
  });
  auto session = std::make_shared<rtsp_stream::launch_session_t>();
  ASSERT_EQ(p.execute(1, session), 0);
  std::this_thread::sleep_for(std::chrono::milliseconds {700});  // the wrapper is gone
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(p.running(), 1) << "sleep still runs in the app's group";
    std::this_thread::sleep_for(std::chrono::milliseconds {100});
  }
  EXPECT_TRUE(p.tracked());
  EXPECT_TRUE(wait_running(p, 0, std::chrono::seconds {9})) << "ends once the game does (after the auto-detach window)";
}

TEST(ProcessTest, ACommandThatExitsAtOnceIsDetachedAndNotTracked) {
  // Like Chrome handing its window to the copy already running on the desktop.
  auto p = make_proc(command_app("Hand-off", "/bin/true"));
  auto stop = util::fail_guard([&p]() {
    p.terminate();
  });
  auto session = std::make_shared<rtsp_stream::launch_session_t>();
  ASSERT_EQ(p.execute(1, session), 0);
  std::this_thread::sleep_for(std::chrono::milliseconds {500});
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(p.running(), 1);
  }
  EXPECT_FALSE(p.tracked()) << "Nova can't see when it closes";
  p.terminate();
  EXPECT_EQ(p.running(), 0);
  EXPECT_TRUE(p.tracked());
}

TEST(ProcessTest, EndingCountsAsNotRunningWhileTheAppIsStillClosing) {
  // /cancel waits up to exit-timeout for the app; meanwhile /nova/v1/running must not say it runs.
  auto app = command_app("Stubborn", "/bin/sh -c \"trap '' TERM; sleep 30\"");
  app.exit_timeout = std::chrono::seconds {2};
  auto p = make_proc(app);
  auto session = std::make_shared<rtsp_stream::launch_session_t>();
  ASSERT_EQ(p.execute(1, session), 0);
  std::this_thread::sleep_for(std::chrono::milliseconds {300});
  EXPECT_EQ(p.running(), 1);
  EXPECT_FALSE(p.ending());
  std::thread closer {[&p]() {
    p.terminate();
  }};
  std::this_thread::sleep_for(std::chrono::milliseconds {500});
  EXPECT_TRUE(p.ending()) << "SIGTERM is ignored, so terminate() is still waiting";
  closer.join();
  EXPECT_FALSE(p.ending());
  EXPECT_EQ(p.running(), 0);
}

TEST(ProcessTest, AGameWhoseWineHelpersLingerEndsWithinSeconds) {
  // A Proton game quits but wineserver stays: umu-run keeps waiting for it, so the app's own
  // process never exits. Nova ends the app once only helpers remain.
  temp_dir_t tmp;
  fs::create_symlink("/bin/sleep", tmp.path / "wineserver");
  fs::create_symlink("/bin/sleep", tmp.path / "GTA5.exe");
  const auto launcher = tmp.path / "umu-run";
  {
    std::ofstream out {launcher};
    out << "#!/bin/sh\n"
        << '"' << (tmp.path / "wineserver").string() << "\" 60 &\n"
        << '"' << (tmp.path / "GTA5.exe").string() << "\" 2\n"
        << "wait\n";
  }
  fs::permissions(launcher, fs::perms::owner_all);
  auto p = make_proc(command_app("Grand Theft Auto V", launcher.string()));
  auto stop = util::fail_guard([&p]() {
    p.terminate();
  });
  auto session = std::make_shared<rtsp_stream::launch_session_t>();
  ASSERT_EQ(p.execute(1, session), 0);
  const auto start = std::chrono::steady_clock::now();
  std::this_thread::sleep_for(std::chrono::milliseconds {1200});
  EXPECT_EQ(p.running(), 1) << "the game runs";
  const auto ended = wait_running(p, 0, std::chrono::seconds {12});
  ASSERT_TRUE(ended) << "wineserver kept the app alive";
  const auto after = std::chrono::steady_clock::now() - start;
  EXPECT_GE(after, std::chrono::seconds {2} + app_processes::linger_t::grace) << "not before the game exited and the grace period passed";
  EXPECT_LE(after, std::chrono::seconds {9});
  // The lingering helper was ended with the app.
  std::this_thread::sleep_for(std::chrono::milliseconds {300});
  for (const auto &proc : app_processes::snapshot()) {
    if (proc.state == 'Z' || proc.argv.empty()) {
      continue;
    }
    EXPECT_EQ(proc.argv.front().find(tmp.path.string()), std::string::npos) << "left running: " << proc.argv.front();
  }
}
#endif
