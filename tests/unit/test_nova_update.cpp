/**
 * @file tests/unit/test_nova_update.cpp
 * @brief Tests for Nova's update check: semver, release selection, checksums and the install gate.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

// local includes
#include <src/nova_update.h>

namespace fs = std::filesystem;
using nova_update::compare_versions;
using nova_update::parse_version;

namespace {
  int cmp(const std::string &a, const std::string &b) {
    const auto va = parse_version(a);
    const auto vb = parse_version(b);
    EXPECT_TRUE(va) << a;
    EXPECT_TRUE(vb) << b;
    return va && vb ? compare_versions(*va, *vb) : 99;
  }

  const std::string REPO = "F-e-n-y-x/nova-host";
  const std::string API = "https://api.github.com/repos/F-e-n-y-x/nova-host/releases/assets/";

  nlohmann::json asset(const std::string &name, int id, const std::string &digest = {}) {
    nlohmann::json a = {{"name", name}, {"url", API + std::to_string(id)}, {"size", 11}};
    if (!digest.empty()) {
      a["digest"] = digest;
    }
    return a;
  }

  nlohmann::json release(const std::string &tag, nlohmann::json assets, bool pre = false, bool draft = false) {
    return {{"tag_name", tag}, {"name", tag}, {"prerelease", pre}, {"draft", draft}, {"html_url", "https://github.com/" + REPO + "/releases/tag/" + tag}, {"body", "notes"}, {"assets", std::move(assets)}};
  }
}  // namespace

TEST(NovaUpdateSemver, ParsesTagsAndBuilds) {
  const auto t = parse_version("nova-v0.3.1", true);
  ASSERT_TRUE(t);
  EXPECT_EQ(t->major, 0);
  EXPECT_EQ(t->minor, 3);
  EXPECT_EQ(t->patch, 1);
  EXPECT_TRUE(t->pre.empty());
  EXPECT_FALSE(parse_version("v0.3.1", true));  // tags need the nova- prefix
  EXPECT_FALSE(parse_version("v2026.730.2631", true));  // upstream date tags are ignored
  EXPECT_FALSE(parse_version("nova-v0.3", true));
  EXPECT_FALSE(parse_version("nova-v0.3.1-", true));
  EXPECT_FALSE(parse_version("nova-v0.3.1-rc..1", true));
  EXPECT_FALSE(parse_version(""));

  const auto local = parse_version("0.1.0-e0371fee-dirty");
  ASSERT_TRUE(local);
  EXPECT_TRUE(local->local);
  EXPECT_TRUE(local->pre.empty());
  EXPECT_TRUE(parse_version("0.1.0-dirty")->local);
  EXPECT_TRUE(parse_version("0.1.0-1d43d4a3")->local);
  // In a tag the same suffix is a pre-release, not a local build.
  EXPECT_EQ(parse_version("nova-v0.1.0-1d43d4a3", true)->pre, std::vector<std::string> {"1d43d4a3"});
  EXPECT_EQ(parse_version("0.2.0-rc.1+build.5")->pre, (std::vector<std::string> {"rc", "1"}));
  EXPECT_EQ(parse_version("v0.2.0")->str(), "0.2.0");
  EXPECT_EQ(parse_version("nova-v0.2.0-rc.1")->str(), "0.2.0-rc.1");
}

TEST(NovaUpdateSemver, Precedence) {
  EXPECT_LT(cmp("0.1.0", "0.2.0"), 0);
  EXPECT_LT(cmp("0.9.0", "0.10.0"), 0);  // numeric, not lexical
  EXPECT_LT(cmp("1.0.0-rc.1", "1.0.0"), 0);
  EXPECT_LT(cmp("1.0.0-alpha", "1.0.0-alpha.1"), 0);
  EXPECT_LT(cmp("1.0.0-alpha.1", "1.0.0-alpha.beta"), 0);
  EXPECT_LT(cmp("1.0.0-beta.2", "1.0.0-beta.11"), 0);
  EXPECT_LT(cmp("1.0.0-rc.1", "1.0.0-rc.2"), 0);
  EXPECT_EQ(cmp("0.2.0", "v0.2.0"), 0);
  EXPECT_GT(cmp("0.2.1", "0.2.0"), 0);
}

TEST(NovaUpdateSemver, LocalBuildsDoNotShowFalseUpdates) {
  const auto same = *parse_version("nova-v0.1.0", true);
  const auto next = *parse_version("nova-v0.1.1", true);
  const auto pre = *parse_version("nova-v0.2.0-rc.1", true);
  // A hash-suffixed build of 0.1.0 is 0.1.0 plus local commits, not a pre-release below it.
  EXPECT_FALSE(nova_update::is_newer("0.1.0-e0371fee", same));
  EXPECT_FALSE(nova_update::is_newer("0.1.0-1d43d4a3-dirty", same));
  EXPECT_TRUE(nova_update::is_newer("0.1.0-e0371fee", next));
  EXPECT_TRUE(nova_update::is_newer("0.1.0", pre));
  EXPECT_FALSE(nova_update::is_newer("0.2.0", pre));
  EXPECT_TRUE(nova_update::is_newer("0.2.0-rc.1", *parse_version("nova-v0.2.0", true)));
  // Unparseable running versions never produce a banner.
  EXPECT_FALSE(nova_update::is_newer("", next));
  EXPECT_FALSE(nova_update::is_newer("weird", next));
}

TEST(NovaUpdateReleases, PicksHighestInstallableRelease) {
  const auto releases = nlohmann::json::array({
    release("nova-v0.2.0", {asset("nova-host_0.2.0_amd64.deb", 1, "sha256:" + std::string(64, 'a'))}),
    release("nova-v0.10.0", {asset("nova-host_0.10.0_amd64.deb", 2), asset("nova-host_0.10.0_amd64.deb.sha256", 3)}),
    release("nova-v0.11.0", {asset("nova-host_0.11.0_amd64.deb", 4)}, false, true),  // draft
    release("nova-v0.12.0-rc.1", {asset("nova-host_0.12.0_amd64.deb", 5)}, true),  // pre-release
    release("v2026.730.2631", {asset("sunshine_amd64.deb", 6)}),  // upstream tag
    release("nova-v0.13.0", {asset("nova-host_0.13.0_arm64.deb", 7)}),  // wrong arch
    release("nova-v0.14.0", nlohmann::json::array()),  // no package
  });
  auto r = nova_update::pick_release(releases, REPO, false, "amd64");
  ASSERT_TRUE(r);
  EXPECT_EQ(r->tag, "nova-v0.10.0");
  EXPECT_EQ(r->deb.name, "nova-host_0.10.0_amd64.deb");
  ASSERT_TRUE(r->checksum);
  EXPECT_EQ(r->checksum->name, "nova-host_0.10.0_amd64.deb.sha256");
  EXPECT_EQ(r->notes, "notes");

  r = nova_update::pick_release(releases, REPO, true, "amd64");
  ASSERT_TRUE(r);
  EXPECT_EQ(r->tag, "nova-v0.12.0-rc.1");
  EXPECT_TRUE(r->prerelease);

  const auto first = nova_update::pick_release(nlohmann::json::array({releases[0]}), REPO, false, "amd64");
  ASSERT_TRUE(first);
  EXPECT_EQ(first->deb.sha256, std::string(64, 'a'));
  EXPECT_FALSE(first->checksum);
}

TEST(NovaUpdateReleases, RejectsForeignAssetUrlsAndBadInput) {
  auto evil = release("nova-v1.0.0", nlohmann::json::array({{{"name", "nova-host_1.0.0_amd64.deb"}, {"url", "https://evil.example/assets/1"}, {"size", 1}}}));
  EXPECT_FALSE(nova_update::pick_release(nlohmann::json::array({evil}), REPO, false, "amd64"));
  auto other_repo = release("nova-v1.0.0", nlohmann::json::array({{{"name", "nova-host_1.0.0_amd64.deb"}, {"url", "https://api.github.com/repos/someone/else/releases/assets/1"}}}));
  EXPECT_FALSE(nova_update::pick_release(nlohmann::json::array({other_repo}), REPO, false, "amd64"));
  auto traversal = release("nova-v1.0.0", nlohmann::json::array({asset("../../x_amd64.deb", 1)}));
  EXPECT_FALSE(nova_update::pick_release(nlohmann::json::array({traversal}), REPO, false, "amd64"));
  EXPECT_FALSE(nova_update::pick_release(nlohmann::json::object(), REPO, false, "amd64"));
  EXPECT_FALSE(nova_update::pick_release(nlohmann::json::array({1, "x", nullptr}), REPO, false, "amd64"));
  // A malformed digest is ignored rather than trusted.
  auto bad_digest = release("nova-v1.0.0", {asset("nova-host_1.0.0_amd64.deb", 1, "sha256:xyz")});
  EXPECT_TRUE(nova_update::pick_release(nlohmann::json::array({bad_digest}), REPO, false, "amd64")->deb.sha256.empty());
}

TEST(NovaUpdateReleases, ParsesChecksumFiles) {
  const std::string h1(64, 'a');
  const std::string h2(64, 'B');
  EXPECT_EQ(nova_update::parse_checksum(h1 + "  nova-host_0.2.0_amd64.deb\n" + h2 + " *other.deb\n", "other.deb"), std::string(64, 'b'));
  EXPECT_EQ(nova_update::parse_checksum(h1 + "  dist/nova-host_0.2.0_amd64.deb\r\n", "nova-host_0.2.0_amd64.deb"), h1);
  EXPECT_EQ(nova_update::parse_checksum(h1 + "\n", "anything.deb"), h1);  // bare .sha256 file
  EXPECT_FALSE(nova_update::parse_checksum(h1 + "  a.deb\n", "b.deb"));
  EXPECT_FALSE(nova_update::parse_checksum("# comment\nnot-a-hash  a.deb\n", "a.deb"));
  EXPECT_FALSE(nova_update::parse_checksum("", "a.deb"));
}

namespace {
  /**
   * @brief Service fixture: fake HTTP, fake programs, temp cache dir.
   */
  class NovaUpdateServiceTest: public ::testing::Test {
  protected:
    void SetUp() override {
      dir = fs::temp_directory_path() / ("nova-update-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
      fs::create_directories(dir);
      helper = dir / "nova-install-update";
      std::ofstream {helper} << "#!/bin/sh\n";
      options.current = "0.1.0-e0371fee";
      // sha256("hello world") of the 11-byte fake package body
      sha = "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9";
      set_releases({release("nova-v0.2.0", {asset("nova-host_0.2.0_amd64.deb", 1, "sha256:" + sha)})});
    }

    void TearDown() override {
      std::error_code ec;
      fs::remove_all(dir, ec);
    }

    void set_releases(const nlohmann::json &releases) {
      pages["https://api.github.com/repos/F-e-n-y-x/nova-host/releases?per_page=30"] = {200, releases.dump(), {}};
    }

    nova_update::service_t make() {
      nova_update::deps_t d;
      d.get = [this](const std::string &url, const std::vector<std::string> &headers, std::size_t) {
        last_headers = headers;
        const auto it = pages.find(url);
        return it == pages.end() ? nova_update::http_result_t {404, {}, {}} : it->second;
      };
      d.download = [this](const std::string &, const std::vector<std::string> &headers, const fs::path &to, std::uint64_t, const std::function<void(std::uint64_t, std::uint64_t)> &progress) {
        last_headers = headers;
        std::ofstream {to, std::ios::binary} << package_body;
        progress(11, 11);
        return nova_update::http_result_t {200, {}, {}};
      };
      d.run = [this](const std::vector<std::string> &argv) {
        runs.push_back(argv);
        if (argv.front() == "dpkg-deb") {
          return run_program::result_t {0, "Package: " + deb_package + "\nVersion: " + deb_version + "\n", {}};
        }
        return sudo_result;
      };
      d.streaming = [this] {
        return streaming;
      };
      d.restart = [this] {
        ++restarts;
      };
      d.now = [this] {
        return now;
      };
      d.cache_dir = dir / "cache";
      d.helper = helper.string();
      return nova_update::service_t {std::move(d)};
    }

    std::string install_state(const nova_update::service_t &s) const {
      return s.status_json(options)["install"]["state"].get<std::string>();
    }

    fs::path dir;  ///< Temp directory.
    fs::path helper;  ///< Fake helper path (exists).
    nova_update::options_t options;  ///< Settings.
    std::map<std::string, nova_update::http_result_t> pages;  ///< Fake GET responses.
    std::vector<std::string> last_headers;  ///< Headers of the last request.
    std::vector<std::vector<std::string>> runs;  ///< Programs run.
    std::string package_body = "hello world";  ///< Downloaded bytes.
    std::string sha;  ///< Expected SHA-256.
    std::string deb_package = "nova-host";  ///< dpkg-deb Package field.
    std::string deb_version = "0.2.0";  ///< dpkg-deb Version field.
    run_program::result_t sudo_result {0, {}, {}};  ///< sudo -n helper result.
    bool streaming = false;  ///< Fake stream state.
    int restarts = 0;  ///< Restart calls.
    std::int64_t now = 1'000'000;  ///< Fake clock.
  };
}  // namespace

TEST_F(NovaUpdateServiceTest, CheckReportsRealUpdateOnly) {
  auto s = make();
  EXPECT_TRUE(s.stale(60));
  s.check(options);
  auto st = s.status_json(options);
  EXPECT_TRUE(st["update_available"].get<bool>());
  EXPECT_EQ(st["latest"]["tag"], "nova-v0.2.0");
  EXPECT_EQ(st["latest"]["checksum"], "digest");
  EXPECT_EQ(st["error"], "");
  EXPECT_FALSE(s.stale(60));
  now += 61;
  EXPECT_TRUE(s.stale(60));

  options.current = "0.2.0-abcdef12";
  EXPECT_FALSE(s.status_json(options)["update_available"].get<bool>());
}

TEST_F(NovaUpdateServiceTest, TokenIsSentOnlyWhenSetAndValid) {
  auto s = make();
  s.check(options);
  EXPECT_EQ(std::ranges::count_if(last_headers, [](const auto &h) {
              return h.starts_with("Authorization");
            }),
            0);
  options.token = "github_pat_ABC123_def";
  s.check(options);
  EXPECT_NE(std::ranges::find(last_headers, "Authorization: Bearer github_pat_ABC123_def"), last_headers.end());
  options.token = "abc\r\nX-Evil: 1";
  s.check(options);
  EXPECT_EQ(s.status_json(options)["error"], "bad_token");
  EXPECT_TRUE(s.status_json(options)["token_set"].get<bool>());
}

TEST_F(NovaUpdateServiceTest, PrivateRepoWithoutTokenIsAnErrorNotAnUpdate) {
  pages.clear();  // every GET answers 404, as GitHub does for a private repository
  auto s = make();
  s.check(options);
  const auto st = s.status_json(options);
  EXPECT_EQ(st["error"], "private");
  EXPECT_FALSE(st["update_available"].get<bool>());
  EXPECT_TRUE(st["latest"].is_null());
  options.token = "ghp_x";
  s.check(options);
  EXPECT_EQ(s.status_json(options)["error"], "not_found");
  pages["https://api.github.com/repos/F-e-n-y-x/nova-host/releases?per_page=30"] = {401, "{}", {}};
  s.check(options);
  EXPECT_EQ(s.status_json(options)["error"], "unauthorized");
  pages["https://api.github.com/repos/F-e-n-y-x/nova-host/releases?per_page=30"] = {200, "not json", {}};
  s.check(options);
  EXPECT_EQ(s.status_json(options)["error"], "bad_response");
}

TEST_F(NovaUpdateServiceTest, InstallRefusedWhileStreamingOrWithoutUpdate) {
  auto s = make();
  EXPECT_EQ(s.begin_install(options, "nova-v0.2.0"), nova_update::start_result_e::no_update);  // not checked yet
  s.check(options);
  EXPECT_EQ(s.begin_install(options, "nova-v0.3.0"), nova_update::start_result_e::no_update);  // not what was offered
  streaming = true;
  EXPECT_EQ(s.begin_install(options, "nova-v0.2.0"), nova_update::start_result_e::streaming);
  EXPECT_EQ(install_state(s), "idle");
  streaming = false;
  EXPECT_EQ(s.begin_install(options, "nova-v0.2.0"), nova_update::start_result_e::started);
  EXPECT_EQ(s.begin_install(options, "nova-v0.2.0"), nova_update::start_result_e::busy);
}

TEST_F(NovaUpdateServiceTest, InstallVerifiesAndRunsHelper) {
  auto s = make();
  s.check(options);
  ASSERT_EQ(s.begin_install(options, "nova-v0.2.0"), nova_update::start_result_e::started);
  s.run_install(options);
  EXPECT_EQ(install_state(s), "restarting");
  EXPECT_EQ(restarts, 1);
  ASSERT_EQ(runs.size(), 2u);
  EXPECT_EQ(runs[0].front(), "dpkg-deb");
  const auto deb = (dir / "cache" / "nova-host_0.2.0_amd64.deb").string();
  EXPECT_EQ(runs[1], (std::vector<std::string> {"sudo", "-n", helper.string(), deb, sha}));
  EXPECT_TRUE(fs::exists(deb));
  EXPECT_FALSE(fs::exists(deb + ".part"));
  EXPECT_NE(std::ranges::find(last_headers, "Accept: application/octet-stream"), last_headers.end());
}

TEST_F(NovaUpdateServiceTest, ChecksumMismatchDeletesDownload) {
  package_body = "tampered!!!";
  auto s = make();
  s.check(options);
  s.begin_install(options, "nova-v0.2.0");
  s.run_install(options);
  EXPECT_EQ(install_state(s), "failed");
  EXPECT_NE(s.status_json(options)["install"]["message"].get<std::string>().find("SHA-256 mismatch"), std::string::npos);
  EXPECT_TRUE(runs.empty());
  EXPECT_FALSE(fs::exists(dir / "cache" / "nova-host_0.2.0_amd64.deb"));
  EXPECT_EQ(restarts, 0);
}

TEST_F(NovaUpdateServiceTest, NoChecksumMeansNoInstall) {
  set_releases({release("nova-v0.2.0", {asset("nova-host_0.2.0_amd64.deb", 1)})});
  auto s = make();
  s.check(options);
  s.begin_install(options, "nova-v0.2.0");
  s.run_install(options);
  EXPECT_EQ(install_state(s), "failed");
  EXPECT_TRUE(runs.empty());
}

TEST_F(NovaUpdateServiceTest, ChecksumFileIsUsedAndMustAgreeWithDigest) {
  set_releases({release("nova-v0.2.0", {asset("nova-host_0.2.0_amd64.deb", 1), asset("SHA256SUMS", 2)})});
  pages[API + "2"] = {200, sha + "  nova-host_0.2.0_amd64.deb\n", {}};
  auto s = make();
  s.check(options);
  s.begin_install(options, "nova-v0.2.0");
  s.run_install(options);
  EXPECT_EQ(install_state(s), "restarting");

  set_releases({release("nova-v0.2.0", {asset("nova-host_0.2.0_amd64.deb", 1, "sha256:" + std::string(64, 'c')), asset("SHA256SUMS", 2)})});
  auto t = make();
  t.check(options);
  t.begin_install(options, "nova-v0.2.0");
  t.run_install(options);
  EXPECT_EQ(install_state(t), "failed");
}

TEST_F(NovaUpdateServiceTest, WrongPackageIsRejected) {
  deb_package = "sunshine";
  auto s = make();
  s.check(options);
  s.begin_install(options, "nova-v0.2.0");
  s.run_install(options);
  EXPECT_EQ(install_state(s), "failed");
  ASSERT_EQ(runs.size(), 1u);  // dpkg-deb only, never sudo

  deb_package = "nova-host";
  deb_version = "0.1.9";
  auto t = make();
  t.check(options);
  t.begin_install(options, "nova-v0.2.0");
  t.run_install(options);
  EXPECT_EQ(install_state(t), "failed");
}

TEST_F(NovaUpdateServiceTest, WithoutSudoRuleTheOwnerGetsACommand) {
  sudo_result = {1, {}, "sudo: a password is required\n"};
  auto s = make();
  s.check(options);
  s.begin_install(options, "nova-v0.2.0");
  s.run_install(options);
  const auto st = s.status_json(options);
  EXPECT_EQ(st["install"]["state"], "manual");
  EXPECT_NE(st["install"]["command"].get<std::string>().find("sudo apt-get install --allow-downgrades "), std::string::npos);
  EXPECT_EQ(restarts, 0);
}

TEST_F(NovaUpdateServiceTest, StreamStartingDuringDownloadDefersInstall) {
  auto s = make();
  s.check(options);
  s.begin_install(options, "nova-v0.2.0");
  streaming = true;  // a client connected while the package downloaded
  s.run_install(options);
  EXPECT_EQ(install_state(s), "manual");
  EXPECT_EQ(runs.size(), 1u);  // verified, but sudo never ran
  EXPECT_EQ(restarts, 0);
}

TEST(NovaUpdateSha, HashesFiles) {
  const auto path = fs::temp_directory_path() / ("nova-update-sha-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
  std::ofstream {path, std::ios::binary} << "hello world";
  EXPECT_EQ(nova_update::sha256_file(path), "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9");
  fs::remove(path);
  EXPECT_EQ(nova_update::sha256_file(path), "");
}
