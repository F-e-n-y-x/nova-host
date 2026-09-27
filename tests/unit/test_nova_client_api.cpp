/**
 * @file tests/unit/test_nova_client_api.cpp
 * @brief Tests for the Nova client API helpers (app ids, app list, playtime, store details, bitrate).
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
#include <src/nova_client_api.h>

namespace fs = std::filesystem;

namespace {
  /**
   * @brief A temporary directory removed at the end of a test.
   */
  struct temp_dir_t {
    fs::path path;  ///< Directory path.

    temp_dir_t():
        path {fs::temp_directory_path() / ("nova-api-test-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" + ::testing::UnitTest::GetInstance()->current_test_info()->name())} {
      fs::create_directories(path);
    }

    ~temp_dir_t() {
      std::error_code ec;
      fs::remove_all(path, ec);
    }
  };

  /**
   * @brief Write a small file.
   *
   * @param p Path.
   * @param text Content.
   */
  void write(const fs::path &p, const std::string &text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
  }
}  // namespace

TEST(NovaClientApiTest, AppIdIsStableAndPrefersSourceIds) {
  const nlohmann::json a = {{"name", "Far Cry 5"}, {"cmd", "run.sh"}, {"nova-source", "steam"}, {"nova-source-id", "552520"}};
  auto renamed = a;
  renamed["name"] = "Far Cry 5 (renamed)";
  renamed["cmd"] = "other.sh";
  EXPECT_EQ(nova_api::app_id(a), nova_api::app_id(renamed));
  EXPECT_EQ(nova_api::app_id(a).size(), 16u);
  EXPECT_EQ(nova_api::app_id(a).find_first_not_of("0123456789abcdef"), std::string::npos);

  const nlohmann::json manual = {{"name", "Desktop (Mirror)"}, {"cmd", ""}};
  EXPECT_EQ(nova_api::app_id(manual), nova_api::app_id(manual));
  EXPECT_NE(nova_api::app_id(manual), nova_api::app_id(nlohmann::json {{"name", "Desktop (Virtual display)"}, {"cmd", ""}}));
  EXPECT_NE(nova_api::app_id(manual), nova_api::app_id(a));
}

TEST(NovaClientApiTest, DisplayModeParsing) {
  EXPECT_EQ(nova_api::parse_display_mode("virtual"), "virtual");
  EXPECT_EQ(nova_api::parse_display_mode("mirror"), "mirror");
  EXPECT_FALSE(nova_api::parse_display_mode("Virtual"));
  EXPECT_FALSE(nova_api::parse_display_mode(""));
  EXPECT_FALSE(nova_api::parse_display_mode("fit"));
}

TEST(NovaClientApiTest, BitrateIsClampedToSafeBounds) {
  EXPECT_FALSE(nova_api::clamp_bitrate(0, 0));
  EXPECT_FALSE(nova_api::clamp_bitrate(-5, 0));
  EXPECT_EQ(nova_api::clamp_bitrate(100, 0), nova_api::MIN_BITRATE_KBPS);
  EXPECT_EQ(nova_api::clamp_bitrate(30000, 0), 30000);
  EXPECT_EQ(nova_api::clamp_bitrate(10'000'000, 0), nova_api::MAX_BITRATE_KBPS);
  EXPECT_EQ(nova_api::clamp_bitrate(80000, 50000), 50000);  // host cap wins
  EXPECT_EQ(nova_api::clamp_bitrate(80000, 100), nova_api::MIN_BITRATE_KBPS);  // a cap below the floor never goes lower
}

TEST(NovaClientApiTest, AppsListShowsArtworkPlaytimeAndDefaults) {
  temp_dir_t tmp;
  const auto covers = tmp.path / "covers";
  write(covers / "a" / "hero.jpg", "x");
  const nlohmann::json apps = nlohmann::json::array({
    {{"name", "Desktop (Mirror)"}, {"cmd", ""}},
    {{"name", "Far Cry 5"}, {"cmd", "fc5"}, {"nova-source", "steam"}, {"nova-source-id", "552520"}, {"nova-hero", (covers / "a" / "hero.jpg").string()}, {"nova-display-mode", "virtual"}},
    {{"name", "GTA V"}, {"cmd", "gta"}, {"nova-display-mode", "bogus"}},
  });
  std::map<std::string, nova_api::app_stats_t> stats;
  stats["Far Cry 5"] = {1'800'000'000, 3600, std::nullopt};

  const auto list = nova_api::apps_list(apps, {"1", "2", "3"}, 1, true, stats, covers);
  ASSERT_EQ(list.size(), 3u);
  EXPECT_EQ(list[0]["source"], "app");
  EXPECT_EQ(list[0]["last_played"], nullptr);
  EXPECT_EQ(list[1]["appid"], "2");
  EXPECT_TRUE(list[1]["running"].get<bool>());
  EXPECT_TRUE(list[1]["has"]["hero"].get<bool>());
  EXPECT_FALSE(list[1]["has"]["poster"].get<bool>());
  EXPECT_EQ(list[1]["mode_default"], "virtual");
  EXPECT_EQ(list[1]["playtime_s"], 3600);
  EXPECT_EQ(list[1]["last_played"], 1'800'000'000);
  EXPECT_EQ(list[2]["mode_default"], nullptr);
  EXPECT_EQ(list[1]["id"], nova_api::app_id(apps[1]));
}

TEST(NovaClientApiTest, AppsListHidesEverythingButTheRunningAppWithoutLaunchPermission) {
  const nlohmann::json apps = nlohmann::json::array({{{"name", "A"}, {"cmd", "a"}}, {{"name", "B"}, {"cmd", "b"}}});
  const auto running = nova_api::apps_list(apps, {"1", "2"}, 1, false, {}, fs::temp_directory_path());
  ASSERT_EQ(running.size(), 1u);
  EXPECT_EQ(running[0]["name"], "B");
  EXPECT_TRUE(nova_api::apps_list(apps, {"1", "2"}, std::nullopt, false, {}, fs::temp_directory_path()).empty());
}

TEST(NovaClientApiTest, SessionsAccumulatePlaytimeAndKeepTheLatestSession) {
  std::map<std::string, nova_api::app_stats_t> stats;
  stream_stats::history_entry_t first;
  first.app_name = "GTA V";
  first.client_name = "Pixel";
  first.started_at = 1000;
  first.duration_s = 600;
  first.width = 1920;
  first.height = 1080;
  first.avg_fps = 59.9;
  first.video_format = 1;
  auto second = first;
  second.client_name = "S25 Ultra";
  second.started_at = 5000;
  second.duration_s = 120;
  second.width = 3120;
  second.height = 1440;
  nova_api::add_session(stats, second);
  nova_api::add_session(stats, first);  // older session arriving later must not replace last_session

  const auto &s = stats.at("GTA V");
  EXPECT_EQ(s.playtime_s, 720);
  EXPECT_EQ(s.last_played, 5120);
  ASSERT_TRUE(s.last_session);
  EXPECT_EQ(s.last_session->device, "S25 Ultra");
  EXPECT_EQ(s.last_session->codec, "HEVC");

  temp_dir_t tmp;
  const auto file = tmp.path / "app_stats.json";
  nova_api::save_stats(file, stats);
  const auto loaded = nova_api::load_stats(file);
  ASSERT_TRUE(loaded.contains("GTA V"));
  EXPECT_EQ(loaded.at("GTA V").playtime_s, 720);
  EXPECT_EQ(loaded.at("GTA V").last_session->width, 3120);
  write(tmp.path / "bad.json", "{not json");
  EXPECT_TRUE(nova_api::load_stats(tmp.path / "bad.json").empty());
  EXPECT_TRUE(nova_api::load_stats(tmp.path / "missing.json").empty());
}

TEST(NovaClientApiTest, StripHtmlProducesPlainText) {
  EXPECT_EQ(nova_api::strip_html("<p>Hello&nbsp;<b>world</b> &amp; friends</p><br/>Line&#39;s 2", 200), "Hello world & friends\nLine's 2");
  EXPECT_EQ(nova_api::strip_html("<script>alert(1)</script>ok", 200), "alert(1)ok");
  EXPECT_EQ(nova_api::strip_html("a &#x263A; b", 200), "a ☺ b");
  EXPECT_EQ(nova_api::strip_html("abcdefghij", 4), "abcd");
  EXPECT_EQ(nova_api::strip_html("<li>one</li><li>two</li>", 200), "one\ntwo");
}

TEST(NovaClientApiTest, SteamAppDetailsAreSanitized) {
  const std::string body = R"({"552520":{"success":true,"data":{
    "short_description":"Welcome to <b>Hope County</b> &amp; more.",
    "genres":[{"id":"1","description":"Action"},{"id":"25","description":"Adventure"}],
    "developers":["Ubisoft Montreal"],"publishers":["Ubisoft"],
    "release_date":{"coming_soon":false,"date":"26 Mar, 2018"},
    "metacritic":{"score":81,"url":"x"},
    "screenshots":[{"id":0,"path_full":"https://shared.akamai.steamstatic.com/a.jpg"},
                   {"id":1,"path_full":"https://evil.example.com/b.jpg"},
                   {"id":2,"path_full":"http://shared.akamai.steamstatic.com/c.jpg"}]}}})";
  const auto d = nova_api::parse_steam_appdetails(body, 552520);
  ASSERT_TRUE(d);
  EXPECT_EQ((*d)["description"], "Welcome to Hope County & more.");
  EXPECT_EQ((*d)["genres"], nlohmann::json({"Action", "Adventure"}));
  EXPECT_EQ((*d)["developer"], "Ubisoft Montreal");
  EXPECT_EQ((*d)["publisher"], "Ubisoft");
  EXPECT_EQ((*d)["release_date"], "26 Mar, 2018");
  EXPECT_EQ((*d)["metacritic"], 81);
  ASSERT_EQ((*d)["screenshot_urls"].size(), 1u);  // only HTTPS URLs on allowed hosts survive

  EXPECT_FALSE(nova_api::parse_steam_appdetails(R"({"1":{"success":false}})", 1));
  EXPECT_FALSE(nova_api::parse_steam_appdetails("garbage", 1));
  EXPECT_FALSE(nova_api::parse_steam_appdetails(body, 42));
}

TEST(NovaClientApiTest, DetailsReplyProxiesScreenshotsAndAddsPlaytime) {
  const nlohmann::json store = {{"appid", 552520}, {"description", "d"}, {"genres", {"Action"}}, {"developer", "dev"}, {"publisher", "pub"}, {"release_date", "2018"}, {"metacritic", 81}, {"screenshot_urls", {"https://shared.akamai.steamstatic.com/a.jpg", "https://shared.akamai.steamstatic.com/b.jpg"}}};
  nova_api::app_stats_t stats {5120, 720, nova_api::last_session_t {"S25 Ultra", 3120, 1440, 119.9, "HEVC"}};
  const auto r = nova_api::details_reply("0123456789abcdef", store, stats);
  EXPECT_EQ(r["screenshots"], nlohmann::json({"/nova/v1/apps/0123456789abcdef/screenshot/0", "/nova/v1/apps/0123456789abcdef/screenshot/1"}));
  EXPECT_FALSE(r.contains("screenshot_urls"));
  EXPECT_EQ(r["steam_appid"], 552520);
  EXPECT_EQ(r["last_session"]["resolution"], "3120x1440");
  EXPECT_EQ(r["playtime_s"], 720);

  const auto empty = nova_api::details_reply("0123456789abcdef", std::nullopt, std::nullopt);
  EXPECT_EQ(empty["description"], "");
  EXPECT_TRUE(empty["screenshots"].empty());
  EXPECT_EQ(empty["last_session"], nullptr);
}

TEST(NovaClientApiTest, SteamAppIdComesFromMatchOrSteamSource) {
  EXPECT_EQ(nova_api::steam_appid_for({{"nova-steam-appid", 271590}}), 271590u);
  EXPECT_EQ(nova_api::steam_appid_for({{"nova-steam-appid", "271590"}}), 271590u);
  EXPECT_EQ(nova_api::steam_appid_for({{"nova-source", "steam"}, {"nova-source-id", "552520"}}), 552520u);
  EXPECT_EQ(nova_api::steam_appid_for({{"nova-source", "lutris"}, {"nova-source-id", "7"}}), 0u);
  EXPECT_EQ(nova_api::steam_appid_for({{"nova-source", "steam"}, {"nova-source-id", "abc"}}), 0u);
}

TEST(NovaClientApiTest, StoreDetailsWithoutNetworkUseOnlyTheCache) {
  // Desktops and command-less entries never trigger a store lookup.
  EXPECT_FALSE(nova_api::store_details({{"name", "Desktop (Mirror)"}, {"cmd", ""}}, false));
  // A cold cache without network access yields nothing rather than blocking.
  EXPECT_FALSE(nova_api::store_details({{"name", "Some Game"}, {"cmd", "x"}, {"nova-steam-appid", 999999999}}, false));
}
