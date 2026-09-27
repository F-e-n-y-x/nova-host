/**
 * @file tests/unit/test_library_match.cpp
 * @brief Tests for title matching (query expansion, scoring, merged multi-source search), the local
 * Steam catalogue and the SSRF guard for user image URLs. No network access.
 */
#include "../tests_common.h"

// standard includes
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <string>
#include <vector>

// local includes
#include <src/library/artwork.h>
#include <src/library/match.h>
#include <src/library/metadata.h>
#include <src/library/steam_catalog.h>
#include <src/library/url_fetch.h>

using namespace library;

namespace {
  /**
   * @brief Whether a candidate list has a Steam app id.
   *
   * @param list Candidates.
   * @param appid App id.
   * @return True when present.
   */
  bool has_appid(const std::vector<match::candidate_t> &list, std::uint32_t appid) {
    return std::ranges::any_of(list, [appid](const match::candidate_t &c) {
      return c.steam_appid == appid;
    });
  }

  /**
   * @brief Position of an app id in a candidate list.
   *
   * @param list Candidates.
   * @param appid App id.
   * @return Index, or the list size when missing.
   */
  std::size_t rank_of(const std::vector<match::candidate_t> &list, std::uint32_t appid) {
    const auto it = std::ranges::find_if(list, [appid](const match::candidate_t &c) {
      return c.steam_appid == appid;
    });
    return static_cast<std::size_t>(it - list.begin());
  }

  /**
   * @brief Catalogue like the real Steam list around GTA V, including the delisted Legacy app.
   *
   * @return Entries.
   */
  std::vector<steam_catalog::entry_t> gta_catalog() {
    return {
      {271590, "Grand Theft Auto V Legacy"},
      {3240220, "Grand Theft Auto V Enhanced"},
      {12210, "Grand Theft Auto IV: The Complete Edition"},
      {1546990, "Grand Theft Auto: Vice City – The Definitive Edition"},
      {12220, "Grand Theft Auto: Episodes from Liberty City"},
      {3482260, "GTA+"},
      {552520, "Far Cry 5"},
      {1174180, "Red Dead Redemption 2"},
    };
  }

  /**
   * @brief Sources that answer like Steam does today: the store search only knows the Enhanced edition.
   *
   * @param index Catalogue.
   * @return Sources.
   */
  match::sources_t fake_sources(const steam_catalog::index_t &index) {
    match::sources_t s;
    s.steam_search = [](const std::string &term) {
      std::vector<match::candidate_t> out;
      if (match::canonical(term).find("grand theft auto") != std::string::npos) {
        out.push_back({"steam", 3240220, 0, 0, "Grand Theft Auto V Enhanced"});
        out.push_back({"steam", 1546990, 0, 0, "Grand Theft Auto: Vice City – The Definitive Edition"});
      }
      return out;
    };
    s.catalog_search = [&index](const std::string &query, std::size_t limit) {
      return index.search(query, limit);
    };
    s.steam_items = [](const std::vector<std::uint32_t> &appids) {
      std::vector<match::candidate_t> out;
      for (const auto id : appids) {
        match::candidate_t c;
        c.steam_appid = id;
        c.type = match::steam_type_e::game;
        c.unlisted = id == 271590;
        c.year = id == 271590 ? "2015" : "";
        out.push_back(c);
      }
      return out;
    };
    return s;
  }
}  // namespace

TEST(LibraryMatch, ExpandsAbbreviationsAndNumerals) {
  EXPECT_EQ(match::canonical("GTA V"), "grand theft auto 5");
  EXPECT_EQ(match::canonical("Grand Theft Auto V"), "grand theft auto 5");
  EXPECT_EQ(match::canonical("RDR2"), "rdr2");  // only whole words are expanded
  EXPECT_EQ(match::canonical("RDR 2"), "red dead redemption 2");
  EXPECT_EQ(match::canonical("Final Fantasy XVI"), "final fantasy 16");

  const auto variants = match::query_variants("GTA V [FitGirl Repack]");
  ASSERT_FALSE(variants.empty());
  EXPECT_EQ(variants.front(), "GTA V");
  EXPECT_NE(std::ranges::find(variants, "Grand Theft Auto V"), variants.end());
  EXPECT_NE(std::ranges::find(variants, "Grand Theft Auto 5"), variants.end());
  const auto plain = match::query_variants("Grand Theft Auto V Enhanced");
  EXPECT_NE(std::ranges::find(plain, "Grand Theft Auto V"), plain.end());
  EXPECT_LE(variants.size(), 4U);
}

TEST(LibraryMatch, ScoresEditionsHighAndOtherSequelsLow) {
  EXPECT_GE(match::score("gta v", "Grand Theft Auto V Legacy"), 0.72);
  EXPECT_GE(match::score("gta v", "Grand Theft Auto V Enhanced"), 0.72);
  EXPECT_LT(match::score("gta v", "Grand Theft Auto IV: The Complete Edition"), 0.5);
  EXPECT_DOUBLE_EQ(match::score("Far Cry 5", "Far Cry® 5"), 1.0);
  EXPECT_GT(match::score("Grand Theft Auto V Legacy", "Grand Theft Auto V Legacy"), match::score("Grand Theft Auto V Legacy", "Grand Theft Auto V Enhanced"));
  EXPECT_EQ(match::edition_of("Grand Theft Auto V Enhanced"), "Enhanced");
  EXPECT_EQ(match::edition_of("Grand Theft Auto IV: The Complete Edition"), "Complete Edition");
  EXPECT_EQ(match::edition_of("Hades"), "");
}

TEST(LibraryMatch, GtaVFindsEnhancedAndLegacy) {
  steam_catalog::index_t index;
  index.assign(gta_catalog());
  const auto sources = fake_sources(index);
  for (const std::string query : {"gta v", "GTA V", "Grand Theft Auto V Legacy", "grand theft auto 5"}) {
    const auto list = match::search(query, sources);
    EXPECT_TRUE(has_appid(list, 271590)) << query;
    EXPECT_TRUE(has_appid(list, 3240220)) << query;
    EXPECT_LT(rank_of(list, 271590), rank_of(list, 12210)) << query;  // GTA IV below both
    EXPECT_LT(rank_of(list, 3240220), rank_of(list, 12210)) << query;
  }
  const auto legacy = match::search("Grand Theft Auto V Legacy", sources);
  ASSERT_FALSE(legacy.empty());
  EXPECT_EQ(legacy.front().steam_appid, 271590U);
  EXPECT_TRUE(legacy.front().unlisted);
  EXPECT_EQ(legacy.front().year, "2015");
  // Results are deduplicated by app id although several sources returned them.
  const auto list = match::search("gta v", sources);
  EXPECT_EQ(std::ranges::count_if(list, [](const auto &c) {
              return c.steam_appid == 3240220;
            }),
            1);
}

TEST(LibraryMatch, RanksGamesAboveDlcAndMergesSources) {
  match::sources_t s;
  s.steam_search = [](const std::string &) {
    return std::vector<match::candidate_t> {{"steam", 10, 0, 0, "Hades Soundtrack"}, {"steam", 11, 0, 0, "Hades"}};
  };
  s.igdb_search = [](const std::string &) {
    match::candidate_t c {"igdb", 11, 1234, 0, "Hades"};
    c.year = "2020";
    return std::vector<match::candidate_t> {c, {"igdb", 0, 99, 0, "Hades II"}};
  };
  s.steam_items = [](const std::vector<std::uint32_t> &ids) {
    std::vector<match::candidate_t> out;
    for (const auto id : ids) {
      match::candidate_t c;
      c.steam_appid = id;
      c.type = id == 10 ? match::steam_type_e::other : match::steam_type_e::game;
      out.push_back(c);
    }
    return out;
  };
  const auto list = match::search("Hades", s);
  ASSERT_GE(list.size(), 3U);
  EXPECT_EQ(list[0].steam_appid, 11U);
  EXPECT_EQ(list[0].igdb_id, 1234U);  // IGDB hit merged into the Steam one
  EXPECT_EQ(list[0].year, "2020");
  EXPECT_EQ(list.back().steam_appid, 10U);  // the soundtrack last
  EXPECT_EQ(match::steam_appid_in("https://store.steampowered.com/app/271590/Grand_Theft_Auto_V_Legacy/").value_or(0), 271590U);
  EXPECT_EQ(match::steam_appid_in(" 271590 ").value_or(0), 271590U);
  EXPECT_FALSE(match::steam_appid_in("GTA 5"));
}

TEST(LibraryMatch, ParsesSteamStoreAndSteamGridDbReplies) {
  const auto items = match::parse_steam_store_items(R"({"response":{"store_items":[
    {"item_type":0,"id":271590,"success":1,"name":"Grand Theft Auto V Legacy","appid":271590,"type":0,"unlisted":true,
     "assets":{"asset_url_format":"steam/apps/271590/${FILENAME}?t=1765387725","library_capsule":"library_600x900.jpg"},
     "release":{"steam_release_date":1428966000,"original_release_date":1428994800}},
    {"item_type":0,"id":5,"success":2},
    {"item_type":0,"id":6,"success":1,"name":"Bad","appid":6,"type":4,"assets":{"asset_url_format":"steam/apps/6/${FILENAME}","library_capsule":"../../x.jpg?"}}
  ]}})");
  ASSERT_EQ(items.size(), 2U);
  EXPECT_EQ(items[0].steam_appid, 271590U);
  EXPECT_EQ(items[0].type, match::steam_type_e::game);
  EXPECT_TRUE(items[0].unlisted);
  EXPECT_EQ(items[0].year, "2015");
  EXPECT_EQ(items[0].poster_url, "https://shared.steamstatic.com/store_item_assets/steam/apps/271590/library_600x900.jpg?t=1765387725");
  EXPECT_EQ(items[1].type, match::steam_type_e::dlc);
  EXPECT_TRUE(items[1].poster_url.empty());  // odd file names are ignored
  EXPECT_TRUE(match::parse_steam_store_items("not json").empty());

  EXPECT_EQ(match::parse_sgdb_steam_appid(R"({"success":true,"data":{"id":1,"external_platform_data":{"steam":[{"id":"271590"}]}}})"), 271590U);
  EXPECT_EQ(match::parse_sgdb_steam_appid(R"({"success":true,"data":{"id":1}})"), 0U);
}

TEST(LibraryMatch, CatalogueParsesSavesAndSearches) {
  bool more = false;
  std::uint32_t last = 0;
  const auto page = steam_catalog::parse_store_app_list(R"({"response":{"apps":[{"appid":271590,"name":"Grand Theft Auto V Legacy","last_modified":1},{"appid":7,"name":"Tab\tName"}],"have_more_results":true,"last_appid":7}})", more, last);
  ASSERT_EQ(page.size(), 2U);
  EXPECT_TRUE(more);
  EXPECT_EQ(last, 7U);
  EXPECT_EQ(page[1].name, "Tab Name");

  const auto spy = steam_catalog::parse_steamspy_page(R"({"271590":{"appid":271590,"name":"Grand Theft Auto V Legacy"},"x":{"name":"no id"},"10":{"name":"Counter-Strike"}})");
  EXPECT_EQ(spy.size(), 2U);

  const auto path = std::filesystem::temp_directory_path() / "nova-test-steam-catalog.tsv";
  ASSERT_TRUE(steam_catalog::save(path, gta_catalog()));
  const auto loaded = steam_catalog::load(path);
  std::filesystem::remove(path);
  ASSERT_EQ(loaded.size(), gta_catalog().size());
  EXPECT_EQ(loaded[0].appid, 271590U);
  EXPECT_EQ(loaded[0].name, "Grand Theft Auto V Legacy");

  steam_catalog::index_t index;
  index.assign(loaded);
  EXPECT_EQ(index.size(), loaded.size());
  const auto hits = index.search("gta 5", 10);
  EXPECT_TRUE(has_appid(hits, 271590));
  EXPECT_TRUE(has_appid(hits, 3240220));
  EXPECT_FALSE(has_appid(hits, 552520));
  EXPECT_TRUE(has_appid(index.search("RDR 2", 5), 1174180));
}

TEST(LibraryUrlFetch, ChecksUrlSyntax) {
  std::string error;
  EXPECT_TRUE(url_fetch::check_url("https://example.com/a.png", error));
  EXPECT_FALSE(url_fetch::check_url("http://example.com/a.png", error));
  EXPECT_FALSE(url_fetch::check_url("file:///etc/passwd", error));
  EXPECT_FALSE(url_fetch::check_url("https://user:pw@example.com/a.png", error));
  EXPECT_FALSE(url_fetch::check_url("https://example.com:8443/a.png", error));
  EXPECT_FALSE(url_fetch::check_url("not a url", error));
  EXPECT_FALSE(url_fetch::check_url("https://example.com/" + std::string(3000, 'a'), error));
  const auto v6 = url_fetch::check_url("https://[2606:4700::1111]/a.png", error);
  ASSERT_TRUE(v6);
  EXPECT_EQ(v6->host, "2606:4700::1111");
}

TEST(LibraryUrlFetch, BlocksPrivateAddresses) {
  for (const char *ip : {"127.0.0.1", "10.1.2.3", "172.16.0.1", "192.168.10.10", "169.254.169.254", "100.64.0.1", "0.0.0.0", "224.0.0.1", "255.255.255.255", "::1", "::", "fe80::1", "fc00::1", "fd12::1", "::ffff:127.0.0.1", "::ffff:192.168.1.1", "64:ff9b::a00:1", "2002:c0a8:0101::1", "ff02::1", "2001:db8::1", "bogus"}) {
    EXPECT_FALSE(url_fetch::is_public_ip(ip)) << ip;
  }
  for (const char *ip : {"8.8.8.8", "1.1.1.1", "151.101.1.1", "2606:4700::1111", "::ffff:8.8.8.8"}) {
    EXPECT_TRUE(url_fetch::is_public_ip(ip)) << ip;
  }
  std::string error;
  EXPECT_FALSE(url_fetch::resolve_public("localhost", error));
  EXPECT_FALSE(error.empty());
}

TEST(LibraryUrlFetch, RefusesLocalTargetsAndNonImages) {
  // Refused before any connection is made.
  const auto loopback = url_fetch::fetch_image("https://127.0.0.1/poster.png");
  EXPECT_FALSE(loopback.body);
  EXPECT_NE(loopback.error.find("private"), std::string::npos);
  EXPECT_FALSE(url_fetch::fetch_image("https://localhost/poster.png").body);
  EXPECT_FALSE(url_fetch::fetch_image("https://[::1]/poster.png").body);
  EXPECT_FALSE(url_fetch::fetch_image("http://example.com/poster.png").body);

  EXPECT_TRUE(url_fetch::is_image_type("image/png"));
  EXPECT_TRUE(url_fetch::is_image_type("Image/JPEG; charset=binary"));
  EXPECT_FALSE(url_fetch::is_image_type("image/svg+xml"));
  EXPECT_FALSE(url_fetch::is_image_type("text/html"));
  EXPECT_FALSE(url_fetch::is_image_type("image/"));
  EXPECT_EQ(url_fetch::max_image_bytes, 20U * 1024 * 1024);
}

TEST(LibraryMatchLive, GtaVAgainstRealSteam) {
  // Talks to Steam and SteamSpy; run with NOVA_LIVE_TESTS=1.
  if (!std::getenv("NOVA_LIVE_TESTS")) {
    GTEST_SKIP() << "set NOVA_LIVE_TESTS=1 to run";
  }
  const auto page = artwork::http_get("https://steamspy.com/api.php?request=all&page=0", 4 * 1024 * 1024);
  ASSERT_TRUE(page);
  steam_catalog::index_t index;
  index.assign(steam_catalog::parse_steamspy_page(*page));
  auto sources = match::live_sources(metadata::settings_t {});
  sources.catalog_search = [&index](const std::string &query, std::size_t limit) {
    return index.search(query, limit);
  };
  for (const std::string query : {"gta v", "Grand Theft Auto V Legacy"}) {
    const auto list = match::search(query, sources);
    EXPECT_TRUE(has_appid(list, 271590)) << query;
    EXPECT_TRUE(has_appid(list, 3240220)) << query;
    for (std::size_t i = 0; i < std::min<std::size_t>(list.size(), 6); ++i) {
      std::cout << query << " -> " << list[i].steam_appid << " " << list[i].name << " " << list[i].year << " " << list[i].confidence << " poster=" << !list[i].poster_url.empty() << std::endl;
    }
  }
}
