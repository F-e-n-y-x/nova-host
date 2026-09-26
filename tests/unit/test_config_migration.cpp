/**
 * @file tests/unit/test_config_migration.cpp
 * @brief Tests for adopting pre-rename (Zenith / Sunshine) settings (src/config_migration.h).
 */
#include <filesystem>
#include <fstream>
#include <sstream>

#include "src/config_migration.h"

#include "../tests_common.h"

namespace fs = std::filesystem;

namespace {
  /**
   * @brief Temporary directory removed when the test ends.
   */
  struct temp_dir_t {
    fs::path path;  ///< Root of the temporary tree.

    temp_dir_t() {
      path = fs::temp_directory_path() / ("nova-migration-test-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
      fs::remove_all(path);
      fs::create_directories(path);
    }

    ~temp_dir_t() {
      std::error_code ec;
      fs::remove_all(path, ec);
    }
  };

  /**
   * @brief Write @p text to @p file, creating parent directories.
   *
   * @param file File to write.
   * @param text Contents.
   */
  void write(const fs::path &file, const std::string &text) {
    fs::create_directories(file.parent_path());
    std::ofstream {file} << text;
  }

  /**
   * @brief Read @p file into a string.
   *
   * @param file File to read.
   * @return Contents.
   */
  std::string read(const fs::path &file) {
    std::ifstream in {file};
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
  }
}  // namespace

TEST(ConfigMigration, CopiesZenithDirectoryAndRenamesConfig) {
  temp_dir_t tmp;
  const auto old_dir = tmp.path / "sunshine";
  const auto new_dir = tmp.path / "nova-host";
  write(old_dir / "zenith.conf", "sunshine_name = atom\n");
  write(old_dir / "sunshine_state.json", "{}");
  write(old_dir / "covers" / "1.png", "png");
  write(old_dir / "zenith.log", "old log");

  const auto result = config_migration::migrate_directory(new_dir, {old_dir}, "nova-host.conf");

  ASSERT_TRUE(result.migrated) << result.error;
  EXPECT_EQ(result.source, old_dir);
  EXPECT_EQ(read(new_dir / "nova-host.conf"), "sunshine_name = atom\n");
  EXPECT_FALSE(fs::exists(new_dir / "zenith.conf"));
  EXPECT_TRUE(fs::exists(new_dir / "sunshine_state.json"));
  EXPECT_TRUE(fs::exists(new_dir / "covers" / "1.png"));
  EXPECT_FALSE(fs::exists(new_dir / "zenith.log"));
  EXPECT_FALSE(fs::exists(tmp.path / "nova-host.migrating"));
  // Copied, not moved: the old install still has everything.
  EXPECT_TRUE(fs::exists(old_dir / "zenith.conf"));
  EXPECT_TRUE(fs::exists(old_dir / "zenith.log"));
}

TEST(ConfigMigration, PrefersZenithConfigOverSunshineConfig) {
  temp_dir_t tmp;
  const auto old_dir = tmp.path / "sunshine";
  const auto new_dir = tmp.path / "nova-host";
  write(old_dir / "sunshine.conf", "from sunshine\n");
  write(old_dir / "zenith.conf", "from zenith\n");

  ASSERT_TRUE(config_migration::migrate_directory(new_dir, {old_dir}, "nova-host.conf").migrated);
  EXPECT_EQ(read(new_dir / "nova-host.conf"), "from zenith\n");
  EXPECT_EQ(read(new_dir / "sunshine.conf"), "from sunshine\n");
}

TEST(ConfigMigration, AdoptsUpstreamSunshineConfig) {
  temp_dir_t tmp;
  const auto old_dir = tmp.path / "sunshine";
  const auto new_dir = tmp.path / "nova-host";
  write(old_dir / "sunshine.conf", "port = 47989\n");

  ASSERT_TRUE(config_migration::migrate_directory(new_dir, {old_dir}, "nova-host.conf").migrated);
  EXPECT_EQ(read(new_dir / "nova-host.conf"), "port = 47989\n");
}

TEST(ConfigMigration, LeavesExistingNewDirectoryAlone) {
  temp_dir_t tmp;
  const auto old_dir = tmp.path / "sunshine";
  const auto new_dir = tmp.path / "nova-host";
  write(old_dir / "zenith.conf", "old\n");
  write(new_dir / "nova-host.conf", "current\n");

  const auto result = config_migration::migrate_directory(new_dir, {old_dir}, "nova-host.conf");

  EXPECT_FALSE(result.migrated);
  EXPECT_TRUE(result.error.empty());
  EXPECT_EQ(read(new_dir / "nova-host.conf"), "current\n");
}

TEST(ConfigMigration, SkipsCandidatesWithoutConfigAndUsesTheNextOne) {
  temp_dir_t tmp;
  const auto empty_dir = tmp.path / "empty";
  const auto old_dir = tmp.path / "sunshine";
  const auto new_dir = tmp.path / "nova-host";
  write(empty_dir / "unrelated.txt", "x");
  write(old_dir / "zenith.conf", "found\n");

  const auto result = config_migration::migrate_directory(new_dir, {tmp.path / "missing", empty_dir, old_dir}, "nova-host.conf");

  ASSERT_TRUE(result.migrated);
  EXPECT_EQ(result.source, old_dir);
}

TEST(ConfigMigration, NoLegacyInstallMeansNoNewDirectory) {
  temp_dir_t tmp;
  const auto new_dir = tmp.path / "nova-host";

  const auto result = config_migration::migrate_directory(new_dir, {tmp.path / "sunshine"}, "nova-host.conf");

  EXPECT_FALSE(result.migrated);
  EXPECT_TRUE(result.error.empty());
  EXPECT_FALSE(fs::exists(new_dir));
}

TEST(ConfigMigration, AdoptsLegacyFileInSameDirectory) {
  temp_dir_t tmp;
  write(tmp.path / "zenith.conf", "windows settings\n");

  const auto result = config_migration::adopt_legacy_file(tmp.path / "nova-host.conf");

  ASSERT_TRUE(result.migrated) << result.error;
  EXPECT_EQ(result.source, tmp.path / "zenith.conf");
  EXPECT_EQ(read(tmp.path / "nova-host.conf"), "windows settings\n");
  EXPECT_TRUE(fs::exists(tmp.path / "zenith.conf"));
}

TEST(ConfigMigration, KeepsExistingConfigFile) {
  temp_dir_t tmp;
  write(tmp.path / "zenith.conf", "old\n");
  write(tmp.path / "nova-host.conf", "current\n");

  EXPECT_FALSE(config_migration::adopt_legacy_file(tmp.path / "nova-host.conf").migrated);
  EXPECT_EQ(read(tmp.path / "nova-host.conf"), "current\n");
}

TEST(ConfigMigration, NoLegacyFileIsANoOp) {
  temp_dir_t tmp;

  const auto result = config_migration::adopt_legacy_file(tmp.path / "nova-host.conf");

  EXPECT_FALSE(result.migrated);
  EXPECT_TRUE(result.error.empty());
  EXPECT_FALSE(fs::exists(tmp.path / "nova-host.conf"));
}
