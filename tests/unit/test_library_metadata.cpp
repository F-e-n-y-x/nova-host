/**
 * @file tests/unit/test_library_metadata.cpp
 * @brief Tests for game details/artwork source settings and the IGDB and RAWG parsers.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

// local includes
#include <src/config.h>
#include <src/confighttp.h>
#include <src/library/artwork.h>
#include <src/library/library.h>
#include <src/library/metadata.h>

using namespace library;
using namespace library::metadata;

namespace {
  /**
   * @brief Restores every setting changed by applying a test config.
   */
  struct library_config_guard_t {
    config::video_t video = config::video;  ///< Video settings before the test.
    config::audio_t audio = config::audio;  ///< Audio settings before the test.
    config::stream_t stream = config::stream;  ///< Stream settings before the test.
    config::nvhttp_t nvhttp = config::nvhttp;  ///< HTTP settings before the test.
    config::input_t input = config::input;  ///< Input settings before the test.
    config::sunshine_t sunshine = config::sunshine;  ///< Core settings before the test.
    config::library_t library = config::library;  ///< Library settings before the test.
    decltype(config::modified_config_settings) modified = config::modified_config_settings;  ///< Changed keys before the test.

    /**
     * @brief Put the saved settings back.
     */
    ~library_config_guard_t() {
      config::video = video;
      config::audio = audio;
      config::stream = stream;
      config::nvhttp = nvhttp;
      config::input = input;
      config::sunshine = sunshine;
      config::library = library;
      config::modified_config_settings = modified;
    }
  };

  /**
   * @brief Apply a config text, with the apps file pointed at a scratch file so nothing is copied into HOME.
   *
   * @param text Config lines.
   */
  void apply_test_config(const std::string &text) {
    const auto apps = std::filesystem::temp_directory_path() / "nova-metadata-test-apps.json";
    if (!std::filesystem::exists(apps)) {
      std::ofstream(apps) << R"({"apps":[]})";
    }
    config::apply_config_for_test("file_apps = " + apps.generic_string() + "\n" + text);
  }

  /**
   * @brief Artwork reference with a label.
   *
   * @param kind Kind.
   * @param label Source label.
   * @param url URL, to tell references apart.
   * @return Reference.
   */
  art_ref_t ref(art_kind_e kind, const std::string &label, const std::string &url) {
    return {kind, url, {}, label};
  }
}  // namespace

TEST(LibraryMetadata, ConfigDefaultsAndParsing) {
  library_config_guard_t guard;
  apply_test_config("");
  auto s = from_config();
  EXPECT_TRUE(s.steam);
  EXPECT_EQ(s.language, "english");
  EXPECT_TRUE(s.auto_fetch);
  EXPECT_EQ(s.ttl_days, 30);
  EXPECT_EQ(s.art_priority, (std::vector<std::string> {"steam", "steamgriddb", "lutris", "igdb"}));
  EXPECT_TRUE(s.prefer_official);
  EXPECT_FALSE(s.igdb_enabled());

  apply_test_config(
    "metadata_steam = disabled\n"
    "metadata_language = german\n"
    "metadata_auto_fetch = disabled\n"
    "metadata_ttl_days = 7\n"
    "art_source_priority = SteamGridDB, igdb,nonsense,steam\n"
    "art_prefer_official = disabled\n"
    "art_steamgriddb_poster_style = material\n"
    "art_steamgriddb_hero_style = blurred\n"
    "art_allow_animated = enabled\n"
    "art_nsfw = enabled\n"
    "art_humor = enabled\n"
    "igdb_client_id = abc\n"
    "igdb_client_secret = def\n"
    "rawg_api_key = r\n"
  );
  s = from_config();
  EXPECT_FALSE(s.steam);
  EXPECT_EQ(s.language, "german");
  EXPECT_FALSE(s.auto_fetch);
  EXPECT_EQ(s.ttl_days, 7);
  EXPECT_EQ(s.art_priority, (std::vector<std::string> {"steamgriddb", "igdb", "steam"}));
  EXPECT_FALSE(s.prefer_official);
  EXPECT_EQ(s.sgdb.poster_style, "material");
  EXPECT_EQ(s.sgdb.hero_style, "blurred");
  EXPECT_TRUE(s.sgdb.animated);
  EXPECT_TRUE(s.sgdb.nsfw);
  EXPECT_TRUE(s.sgdb.humor);
  EXPECT_TRUE(s.igdb_enabled());
  EXPECT_EQ(s.rawg_api_key, "r");
}

TEST(LibraryMetadata, RejectsOutOfRangeValues) {
  library_config_guard_t guard;
  apply_test_config(
    "metadata_ttl_days = 9999\n"
    "metadata_language = en;rm -rf\n"
    "art_steamgriddb_poster_style = sparkly\n"
  );
  const auto s = from_config();
  EXPECT_EQ(s.ttl_days, 30);
  EXPECT_EQ(s.language, "english");
  EXPECT_EQ(s.sgdb.poster_style, "");
}

TEST(LibraryMetadata, ParsesPriorityLists) {
  EXPECT_EQ(parse_priority(""), std::vector<std::string> {});
  EXPECT_EQ(parse_priority(" Steam ,steam,IGDB,lutris "), (std::vector<std::string> {"steam", "igdb", "lutris"}));
  EXPECT_EQ(parse_priority("epic,,gog"), std::vector<std::string> {});
}

TEST(LibraryMetadata, RanksArtworkByPriority) {
  metadata::settings_t s;
  s.prefer_official = false;
  s.art_priority = {"steamgriddb", "igdb", "steam"};
  std::vector<art_ref_t> refs {
    ref(art_kind_e::poster, "Steam", "s1"),
    ref(art_kind_e::poster, "Lutris", "l1"),
    ref(art_kind_e::poster, "SteamGridDB", "g1"),
    ref(art_kind_e::poster, "Upload", "u1"),
    ref(art_kind_e::poster, "IGDB", "i1"),
    ref(art_kind_e::poster, "SteamGridDB", "g2"),
    ref(art_kind_e::poster, "Steam (local)", "s2"),
  };
  rank_artwork(refs, s);
  std::vector<std::string> urls;
  for (const auto &r : refs) {
    urls.push_back(r.url);
  }
  // Lutris is not listed, so it's dropped; uploads are kept last; order within a source is kept.
  EXPECT_EQ(urls, (std::vector<std::string> {"g1", "g2", "i1", "s1", "s2", "u1"}));

  s.prefer_official = true;
  rank_artwork(refs, s);
  EXPECT_EQ(refs.front().url, "s1");
}

TEST(LibraryMetadata, BuildsSteamGridDbQueries) {
  artwork::sgdb_options_t opts;
  EXPECT_EQ(artwork::sgdb_query(opts, art_kind_e::poster), "types=static&nsfw=false&humor=false&dimensions=600x900");
  EXPECT_EQ(artwork::sgdb_query(opts, art_kind_e::logo), "types=static&nsfw=false&humor=false");
  opts.animated = true;
  opts.nsfw = true;
  opts.humor = true;
  opts.poster_style = "material";
  opts.hero_style = "blurred";
  EXPECT_EQ(artwork::sgdb_query(opts, art_kind_e::poster), "types=static,animated&nsfw=any&humor=any&dimensions=600x900&styles=material");
  EXPECT_EQ(artwork::sgdb_query(opts, art_kind_e::hero), "types=static,animated&nsfw=any&humor=any&styles=blurred");
}

TEST(LibraryMetadata, AllowsMetadataApiHosts) {
  EXPECT_TRUE(artwork::allowed_host("api.igdb.com"));
  EXPECT_TRUE(artwork::allowed_host("images.igdb.com"));
  EXPECT_TRUE(artwork::allowed_host("id.twitch.tv"));
  EXPECT_TRUE(artwork::allowed_host("api.rawg.io"));
  EXPECT_TRUE(artwork::allowed_host("media.rawg.io"));
  EXPECT_FALSE(artwork::allowed_host("evil.igdb.com.example"));
  EXPECT_FALSE(artwork::allowed_host("rawg.io"));
}

TEST(LibraryMetadata, ParsesTwitchTokens) {
  std::int64_t expires = 0;
  EXPECT_EQ(parse_twitch_token(R"({"access_token":"tok123","expires_in":5000,"token_type":"bearer"})", expires).value_or(""), "tok123");
  EXPECT_EQ(expires, 5000);
  EXPECT_FALSE(parse_twitch_token(R"({"message":"invalid client"})", expires));
  EXPECT_FALSE(parse_twitch_token("not json", expires));
}

TEST(LibraryMetadata, ParsesIgdbGames) {
  const auto games = parse_igdb_games(R"([
    {"id": 1234, "name": "Hollow Knight", "summary": "A challenging\r\n\n\n\nadventure.\u0001",
     "first_release_date": 1487894400,
     "genres": [{"id": 1, "name": "Platform"}, {"id": 2, "name": "Adventure"}],
     "involved_companies": [
       {"developer": false, "publisher": true, "company": {"name": "Team Cherry Pub"}},
       {"developer": true, "publisher": false, "company": {"name": "Team Cherry"}}
     ],
     "cover": {"image_id": "co1rgi"},
     "artworks": [{"image_id": "ar5l8"}, {"image_id": "bad/../id"}],
     "screenshots": [{"image_id": "sc6c1a"}]},
    {"name": "no id"},
    "garbage"
  ])");
  ASSERT_EQ(games.size(), 1U);
  const auto &g = games.front();
  EXPECT_EQ(g.id, 1234U);
  EXPECT_EQ(g.name, "Hollow Knight");
  EXPECT_EQ(g.summary, "A challenging\n\nadventure.");
  EXPECT_EQ(g.genres, (std::vector<std::string> {"Platform", "Adventure"}));
  EXPECT_EQ(g.developer, "Team Cherry");
  EXPECT_EQ(g.publisher, "Team Cherry Pub");
  EXPECT_EQ(g.cover_image_id, "co1rgi");
  EXPECT_EQ(g.artwork_image_ids, std::vector<std::string> {"ar5l8"});

  const auto details = igdb_details(g);
  EXPECT_EQ(details["igdb_id"], 1234);
  EXPECT_EQ(details["release_date"], "24 Feb, 2017");
  EXPECT_EQ(details["screenshot_urls"][0], "https://images.igdb.com/igdb/image/upload/t_1080p/sc6c1a.jpg");

  const auto art = igdb_artwork(g);
  ASSERT_EQ(art.size(), 2U);
  EXPECT_EQ(art[0].kind, art_kind_e::poster);
  EXPECT_EQ(art[0].label, "IGDB");
  EXPECT_EQ(art[1].kind, art_kind_e::hero);
  EXPECT_EQ(art_source_of(art[0]), "igdb");
}

TEST(LibraryMetadata, EscapesIgdbSearchTerms) {
  EXPECT_EQ(igdb_escape(R"(Half "Life"\ 2)"), "Half Life 2");
  EXPECT_EQ(igdb_escape("a\nb"), "ab");
}

TEST(LibraryMetadata, ParsesRawgReplies) {
  const auto hits = parse_rawg_search(R"({"count": 2, "results": [{"id": 3498, "slug": "grand-theft-auto-v", "name": "Grand Theft Auto V"}, {"slug": "x"}]})");
  ASSERT_EQ(hits.size(), 1U);
  EXPECT_EQ(hits[0].id, 3498U);
  EXPECT_EQ(hits[0].slug, "grand-theft-auto-v");

  const auto details = parse_rawg_game(
    R"({"id": 3498, "name": "Grand Theft Auto V", "description_raw": "Rockstar's open world.", "released": "2013-09-17",
        "metacritic": 250, "genres": [{"name": "Action"}], "developers": [{"name": "Rockstar North"}], "publishers": [{"name": "Rockstar Games"}]})",
    R"({"results": [{"image": "https://media.rawg.io/media/games/a.jpg"}, {"image": "https://evil.example/b.jpg"}, {"image": "http://media.rawg.io/c.jpg"}]})"
  );
  ASSERT_TRUE(details);
  EXPECT_EQ((*details)["rawg_id"], 3498);
  EXPECT_EQ((*details)["description"], "Rockstar's open world.");
  EXPECT_EQ((*details)["release_date"], "17 Sep, 2013");
  EXPECT_EQ((*details)["metacritic"], 100);
  EXPECT_EQ((*details)["developer"], "Rockstar North");
  EXPECT_EQ((*details)["publisher"], "Rockstar Games");
  ASSERT_EQ((*details)["screenshot_urls"].size(), 1U);
  EXPECT_FALSE(parse_rawg_game("{}", ""));
  EXPECT_TRUE(parse_rawg_game(R"({"id": 1, "released": "20xx-yy-zz"})", ""));
}

TEST(LibraryMetadata, FormatsDatesAndValidatesLanguages) {
  EXPECT_EQ(format_date(0), "");
  EXPECT_EQ(format_date(1724025600), "19 Aug, 2024");
  EXPECT_TRUE(valid_steam_language("schinese"));
  EXPECT_FALSE(valid_steam_language(""));
  EXPECT_FALSE(valid_steam_language("en-us"));
  EXPECT_FALSE(valid_steam_language("aaaaaaaaaaaaaaaaaaaaaaaaa"));
}

TEST(LibraryMetadata, MasksAndRestoresSecrets) {
  nlohmann::json out = {{"steamgriddb_api_key", "k1"}, {"igdb_client_secret", "s1"}, {"rawg_api_key", ""}, {"igdb_client_id", "id1"}};
  confighttp::mask_secret_config(out);
  EXPECT_EQ(out["steamgriddb_api_key"], "********");
  EXPECT_EQ(out["igdb_client_secret"], "********");
  EXPECT_EQ(out["rawg_api_key"], "");  // not set stays visibly empty
  EXPECT_EQ(out["igdb_client_id"], "id1");  // the client id is not a secret

  nlohmann::json in = {{"steamgriddb_api_key", "********"}, {"igdb_client_secret", "new-secret"}, {"rawg_api_key", "********"}};
  confighttp::restore_secret_config(in, {{"steamgriddb_api_key", "k1"}, {"igdb_client_secret", "s1"}});
  EXPECT_EQ(in["steamgriddb_api_key"], "k1");
  EXPECT_EQ(in["igdb_client_secret"], "new-secret");
  EXPECT_EQ(in["rawg_api_key"], "");
}

namespace {
  /**
   * @brief Wait until a library job leaves the "running" state.
   *
   * @param id Job id.
   * @return Final status.
   */
  nlohmann::json wait_for_job(const std::string &id) {
    for (int i = 0; i < 400; ++i) {
      const auto status = library::job_status(id);
      if (status && (*status)["state"] != "running") {
        return *status;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return {};
  }
}  // namespace

TEST(LibraryMetadata, RefreshTasksReportProgressResultAndCancel) {
  const auto id = library::start_task("metadata", [](const library::task_progress_t &progress, const std::function<bool()> &) {
    progress(1, 2, "Fetching game details");
    progress(2, 2, "Fetching game details");
    return nlohmann::json {{"refreshed", 2}, {"matched", 1}};
  });
  ASSERT_TRUE(id);
  const auto done = wait_for_job(*id);
  EXPECT_EQ(done["state"], "done");
  EXPECT_EQ(done["kind"], "metadata");
  EXPECT_EQ(done["result"]["matched"], 1);
  EXPECT_EQ(done["progress"]["done"], 2);

  std::atomic<bool> started {false};
  const auto slow = library::start_task("metadata", [&started](const library::task_progress_t &, const std::function<bool()> &cancelled) {
    started = true;
    for (int i = 0; i < 500 && !cancelled(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return nlohmann::json::object();
  });
  ASSERT_TRUE(slow);
  while (!started) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  EXPECT_TRUE(library::cancel_job(*slow));
  EXPECT_EQ(wait_for_job(*slow)["state"], "cancelled");

  const auto failing = library::start_task("metadata", [](const library::task_progress_t &, const std::function<bool()> &) -> nlohmann::json {
    throw std::runtime_error("boom");
  });
  ASSERT_TRUE(failing);
  const auto failed = wait_for_job(*failing);
  EXPECT_EQ(failed["state"], "failed");
  EXPECT_EQ(failed["error"], "boom");
}
