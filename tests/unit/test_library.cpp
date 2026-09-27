/**
 * @file tests/unit/test_library.cpp
 * @brief Tests for the game library scanners, title matching, artwork handling and import (src/library/).
 */
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include "src/config.h"
#include "src/library/artwork.h"
#include "src/library/folder_scan.h"
#include "src/library/heroic.h"
#include "src/library/library.h"
#include "src/library/lutris.h"
#include "src/library/steam.h"
#include "src/library/title.h"

#include "../tests_common.h"

namespace fs = std::filesystem;
using namespace library;

namespace {
  /**
   * @brief Temporary directory removed when the test ends.
   */
  struct temp_dir_t {
    fs::path path;  ///< Root of the temporary tree.

    temp_dir_t() {
      path = fs::temp_directory_path() / ("nova-library-test-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
                                          ::testing::UnitTest::GetInstance()->current_test_info()->name());
      fs::remove_all(path);
      fs::create_directories(path);
    }

    ~temp_dir_t() {
      std::error_code ec;
      fs::remove_all(path, ec);
    }
  };

  /**
   * @brief Write text to a file, creating parent folders.
   *
   * @param file File.
   * @param text Contents.
   */
  void write(const fs::path &file, const std::string &text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
  }

  /**
   * @brief Write a fake Windows executable of a given (sparse) size.
   *
   * @param file File.
   * @param size_mb Logical size in MiB.
   */
  void write_exe(const fs::path &file, std::uintmax_t size_mb) {
    write(file, "MZ\x90");
    fs::resize_file(file, size_mb * 1024 * 1024);
  }

  /**
   * @brief Write a fake native Linux executable.
   *
   * @param file File.
   */
  void write_elf(const fs::path &file) {
    write(file, std::string("\x7F" "ELF") + std::string(200 * 1024, '\0'));
    fs::permissions(file, fs::perms::owner_all, fs::perm_options::add);
  }

  /**
   * @brief Encode a solid-colour PNG.
   *
   * @param w Width.
   * @param h Height.
   * @return PNG bytes.
   */
  std::string png(int w, int h) {
    artwork::image_t img;
    img.width = w;
    img.height = h;
    img.rgba.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 200);
    return artwork::encode_png(img);
  }

  /**
   * @brief Find a detected game by title.
   *
   * @param result Scan result.
   * @param title Title.
   * @return The game, or nullptr.
   */
  const detected_game_t *find(const scan_result_t &result, const std::string &title) {
    for (const auto &g : result.games) {
      if (g.title == title) {
        return &g;
      }
    }
    return nullptr;
  }
}  // namespace

// ---- titles ----------------------------------------------------------------

TEST(LibraryTitle, CleansRepackAndSceneNames) {
  EXPECT_EQ(title::clean("Grand Theft Auto V [DODI Repack]"), "Grand Theft Auto V");
  EXPECT_EQ(title::clean("Far.Cry.5-CODEX"), "Far Cry 5");
  EXPECT_EQ(title::clean("Cyberpunk 2077 v2.1 (GOG)"), "Cyberpunk 2077");
  EXPECT_EQ(title::clean("Hades_v1.38290 - FitGirl"), "Hades");
  EXPECT_EQ(title::clean("Black Myth - Wukong"), "Black Myth - Wukong");
  EXPECT_EQ(title::clean("Grand Theft Auto V Enhanced"), "Grand Theft Auto V Enhanced");
  EXPECT_EQ(title::clean("Game (Enhanced)"), "Game (Enhanced)");
  EXPECT_EQ(title::clean("[DODI]"), "[DODI]") << "an all-tag name falls back to the raw text";
}

TEST(LibraryTitle, MatchKeyFoldsPunctuationEditionsAndNumerals) {
  EXPECT_EQ(title::match_key("Marvel's Spider-Man 2"), title::match_key("Marvels Spider Man 2"));
  EXPECT_EQ(title::match_key("Grand Theft Auto V"), "grand theft auto 5");
  EXPECT_EQ(title::match_key("The Witcher 3: Wild Hunt - Game of the Year Edition"), "witcher 3 wild hunt");
  EXPECT_EQ(title::match_key("Ratchet & Clank\xE2\x84\xA2"), "ratchet and clank");
}

TEST(LibraryTitle, SimilarityRanksRealMatchesAboveNearMisses) {
  EXPECT_DOUBLE_EQ(title::similarity("Marvel's Spider-Man 2", "Marvel’s Spider-Man 2"), 1.0);
  EXPECT_GE(title::similarity("Grand Theft Auto V", "Grand Theft Auto V Enhanced"), 0.72);
  EXPECT_LT(title::similarity("Far Cry 5", "Far Cry 3"), 0.72);
  EXPECT_LT(title::similarity("Hades", "Hades II"), title::similarity("Hades", "Hades"));
  EXPECT_DOUBLE_EQ(title::similarity("", "x"), 0.0);
}

// ---- folder scan -----------------------------------------------------------

TEST(LibraryFolder, ExcludesHelpersAndRedistFolders) {
  for (const auto *name : {"unins000.exe", "UnityCrashHandler64.exe", "vc_redist.x64.exe", "Setup.exe", "EasyAntiCheat_Setup.exe", "upc.exe", "dxwebsetup.exe"}) {
    EXPECT_TRUE(folder::is_excluded_exe(name)) << name;
  }
  for (const auto *name : {"b1.exe", "Spider-Man2.exe", "FarCry5.exe", "PlayGTAV.exe"}) {
    EXPECT_FALSE(folder::is_excluded_exe(name)) << name;
  }
  EXPECT_TRUE(folder::is_excluded_dir("_CommonRedist"));
  EXPECT_TRUE(folder::is_excluded_dir("EasyAntiCheat"));
  EXPECT_FALSE(folder::is_excluded_dir("Binaries"));
}

TEST(LibraryFolder, QuotesPathsAndBuildsLaunchers) {
  EXPECT_EQ(folder::quote("/games/Price $100/x.exe"), "\"/games/Price $$100/x.exe\"");
  EXPECT_EQ(folder::quote("/games/bad\"name/x.exe"), "");
  EXPECT_EQ(folder::windows_command("/usr/local/bin/run-windows-exe {exe}", "/g/a.exe"), "/usr/local/bin/run-windows-exe \"/g/a.exe\"");
  EXPECT_EQ(folder::windows_command("wine", "/g/a.exe"), "wine \"/g/a.exe\"");
  EXPECT_EQ(folder::windows_command("", "/g/a.exe"), "\"/g/a.exe\"");
  EXPECT_EQ(folder::windows_command("wine {exe}", "/g/\"a.exe"), "");
}

TEST(LibraryFolder, DetectsGamesAndSkipsInstallers) {
  temp_dir_t tmp;
  const auto games = tmp.path / "Games";
  // Unreal layout: the root bootstrapper is the right launcher.
  write_exe(games / "Black Myth - Wukong/b1.exe", 1);
  write_exe(games / "Black Myth - Wukong/b1/Binaries/Win64/b1-Win64-Shipping.exe", 90);
  write_exe(games / "Black Myth - Wukong/unins000.exe", 3);
  // Main exe in bin/ next to a repack launcher.
  write_exe(games / "Far Cry 5/FitGirl-Launcher.exe", 4);
  write_exe(games / "Far Cry 5/bin/FarCry5.exe", 80);
  // Redistributables must not win.
  write_exe(games / "Spider-man Remastered/Spider-Man.exe", 60);
  write_exe(games / "Spider-man Remastered/_CommonRedist/vcredist/vc_redist.x64.exe", 25);
  // Repack installer, not a game.
  write_exe(games / "Grand Theft Auto V [DODI Repack]/Setup.exe", 5);
  write(games / "Grand Theft Auto V [DODI Repack]/data1.doi", "x");
  // Empty and container folders.
  fs::create_directories(games / "Empty");
  write_exe(games / "Epic Games/Hades/Hades.exe", 20);
  // Native game.
  write(games / "Linux Game/start.sh", "#!/bin/sh\n");
  fs::permissions(games / "Linux Game/start.sh", fs::perms::owner_all, fs::perm_options::add);

  folder::options_t options;
  options.windows_launcher = "/usr/local/bin/run-windows-exe {exe}";
  const auto result = folder::scan(games, options);

  const auto *wukong = find(result, "Black Myth - Wukong");
  ASSERT_NE(wukong, nullptr);
  EXPECT_EQ(wukong->executable.filename(), "b1.exe");
  EXPECT_EQ(wukong->launch_cmd, "/usr/local/bin/run-windows-exe \"" + (games / "Black Myth - Wukong/b1.exe").string() + "\"");
  EXPECT_EQ(wukong->working_dir, (games / "Black Myth - Wukong").string());

  const auto *farcry = find(result, "Far Cry 5");
  ASSERT_NE(farcry, nullptr);
  EXPECT_EQ(farcry->executable.filename(), "FarCry5.exe");

  const auto *spiderman = find(result, "Spider-man Remastered");
  ASSERT_NE(spiderman, nullptr);
  EXPECT_EQ(spiderman->executable.filename(), "Spider-Man.exe");

  const auto *hades = find(result, "Hades");
  ASSERT_NE(hades, nullptr) << "games inside container folders are found";

  const auto *native = find(result, "Linux Game");
  ASSERT_NE(native, nullptr);
  EXPECT_EQ(native->launch_cmd, "\"" + (games / "Linux Game/start.sh").string() + "\"");

  EXPECT_EQ(find(result, "Grand Theft Auto V"), nullptr);
  const auto installer = std::ranges::find_if(result.skipped, [](const skipped_t &s) {
    return s.path.ends_with("[DODI Repack]");
  });
  ASSERT_NE(installer, result.skipped.end());
  EXPECT_NE(installer->reason.find("installer"), std::string::npos);
  EXPECT_TRUE(std::ranges::any_of(result.skipped, [](const skipped_t &s) {
    return s.path.ends_with("Empty");
  }));
}

TEST(LibraryFolder, PickedInstallFolderIsOneGame) {
  temp_dir_t tmp;
  write_exe(tmp.path / "Spider-Man2.exe", 70);
  const auto result = folder::scan(tmp.path, {});
  ASSERT_EQ(result.games.size(), 1u);
  EXPECT_EQ(result.games[0].executable.filename(), "Spider-Man2.exe");
}

TEST(LibraryFolder, DetectsNativeElfBinaries) {
  temp_dir_t tmp;
  write_elf(tmp.path / "Celeste/Celeste");
  write(tmp.path / "Celeste/Celeste.dll", "MZ");  // Not an executable for Linux.
  const auto result = folder::scan(tmp.path, {});
  ASSERT_EQ(result.games.size(), 1u);
  EXPECT_EQ(result.games[0].executable.filename(), "Celeste");
  EXPECT_EQ(result.games[0].launch_cmd, "\"" + (tmp.path / "Celeste/Celeste").string() + "\"");
}

TEST(LibraryFolder, MissingFolderIsReported) {
  const auto result = folder::scan("/nonexistent/nova-test", {});
  EXPECT_TRUE(result.games.empty());
  ASSERT_EQ(result.skipped.size(), 1u);
}

// ---- Steam -----------------------------------------------------------------

TEST(LibrarySteam, ParsesVdf) {
  const auto root = steam::parse_vdf(R"(
    // comment
    "Root" {
      "Key"  "value with \"quotes\""
      Bare   word [$WIN32]
      "Section" { "Inner" "1" }
    })");
  ASSERT_TRUE(root);
  const auto *r = root->find("root");
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->get("key"), "value with \"quotes\"");
  EXPECT_EQ(r->get("BARE"), "word");
  EXPECT_EQ(r->find("section")->get("inner"), "1");
  EXPECT_FALSE(steam::parse_vdf("\"a\" { \"b\" \"c\""));
  EXPECT_FALSE(steam::parse_vdf("\"unterminated"));
}

TEST(LibrarySteam, ParsesLibraryFoldersBothFormats) {
  const auto modern = steam::parse_library_folders(R"("libraryfolders" { "0" { "path" "/home/u/.local/share/Steam" "apps" { "271590" "1" } } "1" { "path" "/mnt/games/SteamLibrary" } })");
  ASSERT_EQ(modern.size(), 2u);
  EXPECT_EQ(modern[1], fs::path("/mnt/games/SteamLibrary"));
  const auto legacy = steam::parse_library_folders(R"("LibraryFolders" { "TimeNextStatsReport" "123" "1" "/old/lib" })");
  ASSERT_EQ(legacy.size(), 1u);
  EXPECT_EQ(legacy[0], fs::path("/old/lib"));
}

TEST(LibrarySteam, ParsesManifestsAndFiltersTools) {
  const auto app = steam::parse_app_manifest(R"("AppState" { "appid" "271590" "name" "Grand Theft Auto V Legacy" "StateFlags" "4" "installdir" "Grand Theft Auto V" })", "/lib");
  ASSERT_TRUE(app);
  EXPECT_EQ(app->appid, 271590u);
  EXPECT_TRUE(app->fully_installed);
  EXPECT_EQ(app->install_dir, fs::path("/lib/steamapps/common/Grand Theft Auto V"));
  EXPECT_FALSE(steam::parse_app_manifest(R"("AppState" { "appid" "1" "name" "x" "installdir" "../../etc" })", "/lib"));

  EXPECT_TRUE(steam::is_tool({1493710, "Proton Experimental", {}, true}));
  EXPECT_TRUE(steam::is_tool({1628350, "Steam Linux Runtime 3.0 (sniper)", {}, true}));
  EXPECT_FALSE(steam::is_tool({271590, "Grand Theft Auto V", {}, true}));
}

TEST(LibrarySteam, ScansFixtureLibraryWithCachedArt) {
  temp_dir_t tmp;
  const auto root = tmp.path / ".local/share/Steam";
  write(root / "steamapps/libraryfolders.vdf", R"("libraryfolders" { "0" { "path" ")" + root.string() + R"(" } })");
  write(root / "steamapps/appmanifest_271590.acf", R"("AppState" { "appid" "271590" "name" "Grand Theft Auto V" "StateFlags" "4" "installdir" "GTAV" })");
  write(root / "steamapps/appmanifest_1493710.acf", R"("AppState" { "appid" "1493710" "name" "Proton Experimental" "StateFlags" "4" "installdir" "Proton" })");
  write(root / "steamapps/appmanifest_10.acf", R"("AppState" { "appid" "10" "name" "Half Installed" "StateFlags" "1026" "installdir" "HI" })");
  write(root / "appcache/librarycache/271590/library_600x900.jpg", "x");

  const auto result = steam::scan(tmp.path);
  ASSERT_EQ(result.games.size(), 1u);
  const auto &gta = result.games[0];
  EXPECT_EQ(gta.steam_appid, 271590u);
  EXPECT_EQ(gta.launch_cmd, "steam steam://rungameid/271590");
  ASSERT_FALSE(gta.artwork.empty());
  EXPECT_EQ(gta.artwork[0].label, "Steam (local)") << "local cache comes before the CDN";
  EXPECT_TRUE(std::ranges::any_of(gta.artwork, [](const art_ref_t &r) {
    return r.url == "https://cdn.cloudflare.steamstatic.com/steam/apps/271590/library_hero.jpg";
  }));
  EXPECT_TRUE(std::ranges::any_of(result.skipped, [](const skipped_t &s) {
    return s.reason.find("Half Installed") != std::string::npos;
  }));
}

TEST(LibrarySteam, NoSteamIsReported) {
  temp_dir_t tmp;
  const auto result = steam::scan(tmp.path);
  EXPECT_TRUE(result.games.empty());
  EXPECT_EQ(result.skipped.size(), 1u);
}

// ---- Lutris and Heroic -------------------------------------------------------

TEST(LibraryLutris, ParsesCliOutputWithArtAndSkipsInstallers) {
  temp_dir_t tmp;
  const auto data = tmp.path / "lutris";
  const auto icons = tmp.path / "icons";
  write(data / "coverart/hades.jpg", "x");
  write(data / "banners/hades.jpg", "x");
  write(icons / "lutris_hades.png", "x");
  write_exe(tmp.path / "Repack/Setup.exe", 1);
  write(tmp.path / "Repack/data1.bin", "x");
  const auto output = "2026-09-27 INFO Startup\n[" R"({"id": 3, "slug": "hades", "name": "Hades", "runner": "wine", "directory": "/g/Hades", "coverPath": null},)"
                      R"({"id": 1, "slug": "gta-v", "name": "GTA V", "runner": "wine", "directory": ")" +
                      (tmp.path / "Repack").string() + R"("},{"id": 0, "name": "bad"}])";
  const auto result = lutris::parse_list(output, "/usr/games/lutris", data, icons);
  ASSERT_EQ(result.games.size(), 1u);
  const auto &hades = result.games[0];
  EXPECT_EQ(hades.launch_cmd, "/usr/games/lutris lutris:rungameid/3");
  EXPECT_EQ(hades.source_id, "3");
  EXPECT_EQ(hades.artwork.size(), 3u);
  ASSERT_EQ(result.skipped.size(), 1u);
  EXPECT_NE(result.skipped[0].reason.find("installer"), std::string::npos);
  EXPECT_TRUE(lutris::parse_list("no json here", "lutris", data, icons).games.empty());
}

TEST(LibraryHeroic, ParsesInstalledGamesOnly) {
  const std::vector<std::string> docs {
    R"({"library": [
      {"runner": "legendary", "app_name": "Quail", "title": "Hades", "is_installed": true, "art_square": "https://cdn1.epicgames.com/p.jpg", "art_logo": "https://cdn1.epicgames.com/l.png", "install": {"install_path": "/g/Hades"}},
      {"runner": "legendary", "app_name": "Other", "title": "Not Installed", "is_installed": false},
      {"runner": "legendary", "app_name": "Dlc", "title": "A DLC", "is_installed": true, "is_dlc": true}
    ]})",
    R"({"games": [{"runner": "gog", "app_name": "1207658924", "title": "Witcher", "is_installed": true, "art_cover": "https://images.gog-statics.com/c.jpg"}]})",
    "not json",
  };
  const auto result = heroic::parse_libraries(docs, "heroic");
  ASSERT_EQ(result.games.size(), 2u);
  EXPECT_EQ(result.games[0].launch_cmd, "heroic \"heroic://launch?appName=Quail&runner=legendary\"");
  EXPECT_EQ(result.games[0].working_dir, "/g/Hades");
  EXPECT_EQ(result.games[0].artwork.size(), 2u);
  EXPECT_EQ(result.games[1].source_id, "gog/1207658924");
  EXPECT_EQ(heroic::launch_url("a b&c", "gog"), "heroic://launch?appName=a%20b%26c&runner=gog");
}

// ---- artwork ---------------------------------------------------------------

TEST(LibraryArtwork, DecodesEncodesAndRejectsGarbage) {
  const auto bytes = png(30, 45);
  const auto img = artwork::decode(bytes);
  ASSERT_TRUE(img);
  EXPECT_EQ(img->width, 30);
  EXPECT_EQ(img->height, 45);
  EXPECT_FALSE(artwork::decode("GIF89a........"));
  EXPECT_FALSE(artwork::decode("\x89PNG\r\n\x1a\n garbage"));
  EXPECT_FALSE(artwork::decode(""));
}

TEST(LibraryArtwork, FitAndSquareKeepAspect) {
  const auto img = *artwork::decode(png(1200, 1800));
  const auto fitted = artwork::fit(img, 600, 900);
  EXPECT_EQ(fitted.width, 600);
  EXPECT_EQ(fitted.height, 900);
  const auto small = artwork::fit(*artwork::decode(png(100, 100)), 600, 900);
  EXPECT_EQ(small.width, 100) << "never upscales";
  const auto sq = artwork::square(img, 256);
  EXPECT_EQ(sq.width, 256);
  EXPECT_EQ(sq.height, 256);
}

TEST(LibraryArtwork, AllowsOnlyKnownImageHosts) {
  EXPECT_TRUE(artwork::allowed_host("cdn.cloudflare.steamstatic.com"));
  EXPECT_TRUE(artwork::allowed_host("cdn2.steamgriddb.com"));
  EXPECT_TRUE(artwork::allowed_host("store.steampowered.com"));
  EXPECT_TRUE(artwork::allowed_host("images.gog-statics.com"));
  EXPECT_FALSE(artwork::allowed_host("steamstatic.com.evil.example"));
  EXPECT_FALSE(artwork::allowed_host("evilsteamstatic.com"));
  EXPECT_FALSE(artwork::allowed_host("localhost"));
  EXPECT_FALSE(artwork::allowed_host("169.254.169.254"));
  EXPECT_FALSE(artwork::http_get("http://cdn.cloudflare.steamstatic.com/x.jpg", 10)) << "plain HTTP is refused";
  EXPECT_FALSE(artwork::http_get("https://127.0.0.1/x.jpg", 10));
}

TEST(LibraryArtwork, ParsesStoreAndSteamGridDbResponses) {
  const auto hits = artwork::parse_store_search(R"({"total":2,"items":[{"type":"app","name":"Grand Theft Auto V Enhanced","id":3240220},{"type":"sub","name":"x","id":1}]})");
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].appid, 3240220u);
  EXPECT_TRUE(artwork::parse_store_search("oops").empty());

  const auto games = artwork::parse_sgdb_search(R"({"success":true,"data":[{"id":5247,"name":"Hades"}]})");
  ASSERT_EQ(games.size(), 1u);
  EXPECT_EQ(games[0].id, 5247u);
  EXPECT_TRUE(artwork::parse_sgdb_search(R"({"success":false})").empty());

  const auto images = artwork::parse_sgdb_images(
    R"({"success":true,"data":[{"url":"https://cdn2.steamgriddb.com/grid/a.png","mime":"image/png"},{"url":"https://cdn2.steamgriddb.com/grid/b.png","nsfw":true},{"url":"https://cdn2.steamgriddb.com/grid/c.webp","mime":"image/webp"},{"url":"https://cdn2.steamgriddb.com/grid/d.jpg","mime":"image/jpeg"}]})",
    art_kind_e::poster,
    6
  );
  ASSERT_EQ(images.size(), 2u);
  EXPECT_EQ(images[0].label, "SteamGridDB");
  EXPECT_EQ(images[1].url, "https://cdn2.steamgriddb.com/grid/d.jpg");
}

TEST(LibraryArtwork, StoresLocalImagesAsValidatedFiles) {
  temp_dir_t tmp;
  write(tmp.path / "src/cover.png", png(1200, 1800));
  write(tmp.path / "src/fake.jpg", "not an image");
  const auto out = tmp.path / "out";

  const auto poster = artwork::store({art_kind_e::poster, {}, tmp.path / "src/cover.png", "Lutris"}, out);
  ASSERT_TRUE(poster);
  EXPECT_EQ(poster->filename(), "poster.png");
  const auto stored = artwork::decode([&] {
    std::ifstream in(*poster, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
  }());
  ASSERT_TRUE(stored);
  EXPECT_EQ(stored->width, 600);

  EXPECT_FALSE(artwork::store({art_kind_e::hero, {}, tmp.path / "src/fake.jpg", "x"}, out));
  EXPECT_FALSE(artwork::store({art_kind_e::hero, {}, tmp.path / "src/missing.jpg", "x"}, out));
  const auto hero = artwork::store({art_kind_e::hero, {}, tmp.path / "src/cover.png", "x"}, out);
  ASSERT_TRUE(hero);
  EXPECT_EQ(hero->filename(), "hero.jpg");

  const auto icon = artwork::icon_from_poster(*poster, out);
  ASSERT_TRUE(icon);
  EXPECT_EQ(icon->filename(), "icon.png");
}

// ---- library / import ------------------------------------------------------

TEST(LibraryCore, ParsesNamesAndScanRoots) {
  EXPECT_EQ(parse_source("steam"), source_e::steam);
  EXPECT_FALSE(parse_source("epic"));
  EXPECT_EQ(parse_kind("icon"), art_kind_e::icon);
  EXPECT_FALSE(parse_kind("banner"));

  temp_dir_t tmp;
  EXPECT_TRUE(safe_scan_root(tmp.path));
  EXPECT_FALSE(safe_scan_root("relative/path"));
  EXPECT_FALSE(safe_scan_root("/"));
  EXPECT_FALSE(safe_scan_root("/proc"));
  EXPECT_FALSE(safe_scan_root("/proc/self"));
  EXPECT_FALSE(safe_scan_root(tmp.path / "missing"));
}

TEST(LibraryCore, ResolvesWindowsLauncher) {
  EXPECT_EQ(resolve_windows_launcher("custom {exe}"), "custom {exe}");
#ifndef _WIN32
  EXPECT_FALSE(resolve_windows_launcher("").empty());
#endif
}

TEST(LibraryCore, ResolvesWindowsLauncherMode) {
  temp_dir_t tmp;
  const auto wrapper = (tmp.path / "nova-proton-run").string();
  std::ofstream(wrapper) << "#!/bin/sh\n";
  EXPECT_EQ(resolve_windows_launcher("proton", "", wrapper), "\"" + wrapper + "\" {exe}");
  EXPECT_EQ(resolve_windows_launcher("wine", "custom {exe}", wrapper), "wine {exe}");
  EXPECT_EQ(resolve_windows_launcher("custom", "custom {exe}", wrapper), "custom {exe}");
  EXPECT_EQ(resolve_windows_launcher("custom", "", wrapper), "\"" + wrapper + "\" {exe}");
  // Wrapper not installed: fall back to the umu/wine chain instead of a dead path.
  EXPECT_EQ(resolve_windows_launcher("proton", "", (tmp.path / "missing").string()), resolve_windows_launcher(std::string {}));
}

TEST(LibraryCore, MergesIntoAppsWithoutDuplicates) {
  nlohmann::json tree = R"({"env": {}, "apps": [{"name": "Zeta", "cmd": "zeta"}, {"name": "Old", "cmd": "steam steam://rungameid/1", "nova-source": "steam", "nova-source-id": "1"}]})"_json;
  std::vector<app_entry_t> entries(3);
  entries[0] = {"Alpha $5", "\"/g/a.exe\"", "/g", "folder", "/g/a.exe", "/c/p.png", {}, {}, "/c/i.png"};
  entries[1] = {"Same id", "different", {}, "steam", "1"};
  entries[2] = {"Same cmd", "zeta", {}, "folder", "/z"};
  const auto added = merge_into_apps(tree, entries);
  EXPECT_EQ(added, (std::vector<bool> {true, false, false}));
  const auto &apps = tree["apps"];
  ASSERT_EQ(apps.size(), 3u);
  EXPECT_EQ(apps[0]["name"], "Alpha $$5") << "sorted by name, $ escaped";
  EXPECT_EQ(apps[0]["image-path"], "/c/p.png");
  EXPECT_EQ(apps[0]["nova-icon"], "/c/i.png");
  EXPECT_EQ(apps[0]["nova-source"], "folder");
  EXPECT_FALSE(apps[0].contains("nova-hero"));
  EXPECT_TRUE(tree.contains("env")) << "other keys are kept";

  nlohmann::json empty;
  EXPECT_EQ(merge_into_apps(empty, {entries[0]}), std::vector<bool> {true});
  EXPECT_EQ(empty["apps"].size(), 1u);
}

TEST(LibraryCore, ArtDirIsStableAndDistinct) {
  EXPECT_EQ(art_dir("/c", "steam", "271590"), art_dir("/c", "steam", "271590"));
  EXPECT_NE(art_dir("/c", "steam", "271590"), art_dir("/c", "steam", "271591"));
  EXPECT_EQ(art_dir("/c", "steam", "1").parent_path(), fs::path("/c"));
}

TEST(LibraryCore, ServesOnlySafeArtFiles) {
  temp_dir_t tmp;
  const auto covers = tmp.path / "covers";
  write(covers / "steam-x/hero.jpg", "x");
  write(tmp.path / "outside/hero.jpg", "x");
  write(tmp.path / "outside/poster.png", png(2, 3));
  write(tmp.path / "outside/poster-not-png.png", "text");

  nlohmann::json app = {
    {"image-path", (tmp.path / "outside/poster.png").string()},
    {"nova-hero", (covers / "steam-x/hero.jpg").string()},
    {"nova-logo", (tmp.path / "outside/hero.jpg").string()},
    {"nova-icon", (covers / "steam-x/../../outside/hero.jpg").string()},
  };
  EXPECT_EQ(app_art(app, art_kind_e::hero, covers), covers / "steam-x/hero.jpg");
  EXPECT_EQ(app_art(app, art_kind_e::poster, covers), tmp.path / "outside/poster.png");
  EXPECT_FALSE(app_art(app, art_kind_e::logo, covers)) << "files outside the covers folder are refused";
  EXPECT_FALSE(app_art(app, art_kind_e::icon, covers)) << ".. can't escape the covers folder";
  app["image-path"] = (tmp.path / "outside/poster-not-png.png").string();
  EXPECT_FALSE(app_art(app, art_kind_e::poster, covers));
  EXPECT_FALSE(app_art(nlohmann::json::array(), art_kind_e::poster, covers));
}

TEST(LibraryCore, DescribesGamesWithRegisteredCandidates) {
  detected_game_t game;
  game.title = "Hades";
  game.source = source_e::lutris;
  game.source_id = "3";
  game.steam_appid = 1145360;
  game.matched_name = "Hades";
  game.match_confidence = 1.0;
  game.artwork = {{art_kind_e::poster, {}, "/local/p.jpg", "Lutris"}, {art_kind_e::hero, "https://cdn.cloudflare.steamstatic.com/h.jpg", {}, "Steam"}};
  const auto json = game_to_json(game, "job:0");
  EXPECT_EQ(json["source"], "lutris");
  EXPECT_EQ(json["matched"]["appid"], 1145360);
  ASSERT_EQ(json["artwork"]["poster"].size(), 1u);
  EXPECT_FALSE(json["artwork"]["poster"][0].contains("url")) << "local paths are never exposed";
  EXPECT_EQ(json["artwork"]["hero"][0]["url"], "https://cdn.cloudflare.steamstatic.com/h.jpg");
  const auto ref = candidate(json["artwork"]["poster"][0]["id"].get<std::string>());
  ASSERT_TRUE(ref);
  EXPECT_EQ(ref->path, fs::path("/local/p.jpg"));
  EXPECT_FALSE(candidate("c0nope"));
}

TEST(LibraryCore, ScanAndImportJobsEndToEndOffline) {
  temp_dir_t tmp;
  settings_t settings;
  settings.home = tmp.path;
  settings.apps_file = tmp.path / "apps.json";
  settings.covers_dir = tmp.path / "covers";
  settings.windows_launcher = "wine {exe}";
  settings.online = false;
  write(settings.apps_file, R"({"apps": []})");
  write_exe(tmp.path / "Games/Hades/Hades.exe", 20);
  write(tmp.path / "Games/Hades/cover.png", png(600, 900));

  const auto wait = [](const std::string &id) {
    for (int i = 0; i < 200; ++i) {
      const auto status = job_status(id);
      if (status && (*status)["state"] != "running") {
        return *status;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return nlohmann::json {};
  };

  const auto scan_id = start_scan(source_e::folder, tmp.path / "Games", settings);
  ASSERT_TRUE(scan_id);
  const auto scan = wait(*scan_id);
  ASSERT_EQ(scan["state"], "done") << scan.dump();
  ASSERT_EQ(scan["result"]["items"].size(), 1u);
  const auto item = scan["result"]["items"][0];
  EXPECT_EQ(item["title"], "Hades");
  EXPECT_FALSE(item["already_in_library"].get<bool>());

  // Artwork chosen by candidate id; a local candidate registered like a scanner would.
  const auto poster_id = register_candidate({art_kind_e::poster, {}, tmp.path / "Games/Hades/cover.png", "Local"});
  const auto import_id = start_import(nlohmann::json::array({{{"temp_id", item["temp_id"]}, {"poster", poster_id}, {"title", "Hades (test)"}}, {{"temp_id", "nope:0"}}}), settings);
  ASSERT_TRUE(import_id);
  const auto imported = wait(*import_id);
  ASSERT_EQ(imported["state"], "done") << imported.dump();
  EXPECT_EQ(imported["result"]["imported"].size(), 1u);
  EXPECT_EQ(imported["result"]["failed"].size(), 1u);

  std::ifstream in(settings.apps_file);
  const auto tree = nlohmann::json::parse(in);
  ASSERT_EQ(tree["apps"].size(), 1u);
  const auto &app = tree["apps"][0];
  EXPECT_EQ(app["name"], "Hades (test)");
  EXPECT_EQ(app["cmd"], "wine \"" + (tmp.path / "Games/Hades/Hades.exe").string() + "\"");
  ASSERT_TRUE(app.contains("image-path"));
  EXPECT_TRUE(fs::exists(app["image-path"].get<std::string>()));
  EXPECT_TRUE(app.contains("nova-icon")) << "an icon is made from the poster";

  // Importing again is a duplicate.
  const auto again = wait(*start_import(nlohmann::json::array({{{"temp_id", item["temp_id"]}}}), settings));
  EXPECT_EQ(again["result"]["duplicates"].size(), 1u);
  EXPECT_FALSE(job_status("0000000000000000"));
}

TEST(LibraryConfig, DefaultsAreEmpty) {
  EXPECT_TRUE(config::library.steamgriddb_api_key.empty());
  EXPECT_TRUE(config::library.windows_exe_launcher.empty());
}

TEST(LibraryCore, ParsesArtworkChoicesStrictly) {
  temp_dir_t tmp;
  write(tmp.path / "cover.png", png(600, 900));
  const auto poster = register_candidate({art_kind_e::poster, {}, tmp.path / "cover.png", "Local"});
  const auto hero = register_candidate({art_kind_e::hero, {}, tmp.path / "cover.png", "Local"});

  const auto refs = parse_art_choices({{"hero", hero}, {"poster", poster}});
  ASSERT_EQ(refs.size(), 2u);
  EXPECT_EQ(refs[0].kind, art_kind_e::poster) << "returned in poster/hero/logo/icon order";
  EXPECT_EQ(refs[1].kind, art_kind_e::hero);

  EXPECT_THROW(parse_art_choices(nlohmann::json::object()), std::invalid_argument);
  EXPECT_THROW(parse_art_choices(nlohmann::json::array()), std::invalid_argument);
  EXPECT_THROW(parse_art_choices({{"poster", 7}}), std::invalid_argument);
  EXPECT_THROW(parse_art_choices({{"poster", "c0000000000"}}), std::invalid_argument);
  EXPECT_THROW(parse_art_choices({{"logo", poster}}), std::invalid_argument) << "a poster candidate can't be used as a logo";
}

TEST(LibraryCore, SetsArtworkFieldsPerKind) {
  nlohmann::json app = {{"name", "Game"}};
  set_app_art(app, art_kind_e::poster, "/c/poster.png");
  set_app_art(app, art_kind_e::hero, "/c/hero.jpg");
  set_app_art(app, art_kind_e::logo, "/c/logo.png");
  set_app_art(app, art_kind_e::icon, "/c/icon.png");
  EXPECT_EQ(app["image-path"], "/c/poster.png");
  EXPECT_EQ(app["nova-hero"], "/c/hero.jpg");
  EXPECT_EQ(app["nova-logo"], "/c/logo.png");
  EXPECT_EQ(app["nova-icon"], "/c/icon.png");
}

TEST(LibraryCore, AppliesArtworkToAnExistingApp) {
  temp_dir_t tmp;
  write(tmp.path / "art/cover.png", png(1200, 1800));
  settings_t settings;
  settings.home = tmp.path;
  settings.apps_file = tmp.path / "apps.json";
  settings.covers_dir = tmp.path / "covers";
  settings.online = false;
  write(settings.apps_file, R"({"apps":[{"name":"Alpha","cmd":"a"},{"name":"Beta","cmd":"b","image-path":"/old.png"}]})");

  const auto wait = [](const std::string &id) {
    for (int i = 0; i < 200; ++i) {
      const auto status = job_status(id);
      if (status && (*status)["state"] != "running") {
        return *status;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return nlohmann::json {};
  };

  const auto poster = register_candidate({art_kind_e::poster, {}, tmp.path / "art/cover.png", "Local"});
  const auto job = start_apply_artwork(1, {{"app_index", 1}, {"poster", poster}}, settings);
  ASSERT_TRUE(job);
  const auto status = wait(*job);
  ASSERT_EQ(status["state"], "done") << status.dump();
  EXPECT_EQ(status["kind"], "artwork");
  EXPECT_EQ(status["result"]["applied"], nlohmann::json::array({"poster", "icon"})) << "icon derived from the new poster";

  std::ifstream in(settings.apps_file);
  const auto tree = nlohmann::json::parse(in);
  const auto &beta = tree["apps"][1];
  EXPECT_EQ(beta["name"], "Beta");
  EXPECT_NE(beta["image-path"], "/old.png");
  EXPECT_TRUE(fs::exists(beta["image-path"].get<std::string>()));
  EXPECT_TRUE(fs::exists(beta["nova-icon"].get<std::string>()));
  EXPECT_FALSE(tree["apps"][0].contains("image-path")) << "other apps are untouched";

  const auto missing = wait(*start_apply_artwork(9, {{"poster", poster}}, settings));
  EXPECT_EQ(missing["state"], "failed");
  EXPECT_THROW(start_apply_artwork(0, nlohmann::json::object(), settings), std::invalid_argument);
}

TEST(LibraryCore, CancelsJobsCooperatively) {
  EXPECT_FALSE(cancel_job("0000000000000000")) << "unknown ids can't be cancelled";

  temp_dir_t tmp;
  for (int i = 0; i < 5; ++i) {
    write_exe(tmp.path / "Games" / ("Game" + std::to_string(i)) / ("Game" + std::to_string(i) + ".exe"), 2);
  }
  folder::options_t options;
  options.windows_launcher = "wine {exe}";
  options.cancelled = [] {
    return true;
  };
  const auto stopped = folder::scan(tmp.path / "Games", options);
  EXPECT_TRUE(stopped.games.empty()) << "a cancelled folder walk stops before evaluating games";

  options.cancelled = nullptr;
  EXPECT_EQ(folder::scan(tmp.path / "Games", options).games.size(), 5u);

  settings_t settings;
  settings.home = tmp.path;
  settings.apps_file = tmp.path / "apps.json";
  settings.covers_dir = tmp.path / "covers";
  settings.windows_launcher = "wine {exe}";
  settings.online = false;
  const auto id = start_scan(source_e::folder, tmp.path / "Games", settings);
  ASSERT_TRUE(id);
  cancel_job(*id);
  nlohmann::json status;
  for (int i = 0; i < 200; ++i) {
    status = *job_status(*id);
    if (status["state"] != "running") {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  // The scan may pass its last checkpoint before the request lands; either way it ends.
  EXPECT_TRUE(status["state"] == "cancelled" || status["state"] == "done") << status.dump();
  EXPECT_FALSE(cancel_job(*id)) << "finished jobs can't be cancelled";
}
