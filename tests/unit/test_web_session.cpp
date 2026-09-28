/**
 * @file tests/unit/test_web_session.cpp
 * @brief Web UI sign-in sessions: store, expiry, revocation, persistence, cookie and redirect helpers.
 */
// test includes
#include "../tests_common.h"

// standard includes
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

// lib includes
#include <nlohmann/json.hpp>

// local includes
#include <src/web_session.h>

using namespace std::literals;

namespace {
  namespace fs = std::filesystem;
  using web_session::clock;

  const clock::time_point t0 = clock::time_point {} + std::chrono::hours(24 * 365 * 56);  // a fixed "now"
  const std::string tag = web_session::credential_tag("hash", "salt");

  class WebSessionStoreTest: public testing::Test {
  protected:
    fs::path dir;

    void SetUp() override {
      dir = fs::temp_directory_path() / std::format("nova_web_session_{}", ::testing::UnitTest::GetInstance()->random_seed());  // NOSONAR(cpp:S5443): test temp dir
      fs::remove_all(dir);
      fs::create_directories(dir);
    }

    void TearDown() override {
      fs::remove_all(dir);
    }
  };
}  // namespace

TEST_F(WebSessionStoreTest, CreateAndValidate) {
  web_session::store_t store;
  const auto created = store.create(false, tag, "Mozilla/5.0 Firefox/140", "192.168.1.5", t0);
  EXPECT_EQ(created.token.size(), 64u);
  EXPECT_EQ(created.session.csrf_token.size(), 64u);
  EXPECT_NE(created.session.token_hash, created.token);
  EXPECT_EQ(created.session.token_hash, web_session::hash_token(created.token));

  const auto found = store.validate(created.token, tag, t0 + 1min);
  ASSERT_TRUE(found);
  EXPECT_EQ(found->id, created.session.id);
  EXPECT_EQ(found->address, "192.168.1.5");

  EXPECT_FALSE(store.validate(std::string(64, 'f'), tag, t0));
  EXPECT_FALSE(store.validate("short", tag, t0));
  EXPECT_FALSE(store.validate(created.token + "0", tag, t0));
}

TEST_F(WebSessionStoreTest, TokensAreUnique) {
  web_session::store_t store;
  const auto a = store.create(false, tag, "", "", t0);
  const auto b = store.create(false, tag, "", "", t0);
  EXPECT_NE(a.token, b.token);
  EXPECT_NE(a.session.id, b.session.id);
  EXPECT_NE(a.session.csrf_token, b.session.csrf_token);
}

TEST_F(WebSessionStoreTest, BrowserSessionExpiresWhenIdleAndAtMaximum) {
  web_session::store_t store;
  const auto s = store.create(false, tag, "", "", t0);
  // Used every few hours: stays valid...
  auto now = t0;
  for (int i = 0; i < 12; ++i) {
    now += 11h;
    ASSERT_TRUE(store.validate(s.token, tag, now)) << i;
  }
  // ...but never beyond the maximum lifetime.
  EXPECT_FALSE(store.validate(s.token, tag, t0 + web_session::session_max_lifetime + 1s));

  const auto idle = store.create(false, tag, "", "", t0);
  EXPECT_FALSE(store.validate(idle.token, tag, t0 + web_session::idle_timeout + 1s));
  EXPECT_EQ(store.size(), 0u) << "expired sessions are dropped";
}

TEST_F(WebSessionStoreTest, RememberedSessionLastsThirtyDays) {
  web_session::store_t store;
  const auto s = store.create(true, tag, "", "", t0);
  EXPECT_EQ(s.session.expires - s.session.created, web_session::remembered_lifetime.count());
  EXPECT_TRUE(store.validate(s.token, tag, t0 + std::chrono::hours(24 * 29)));
  EXPECT_FALSE(store.validate(s.token, tag, t0 + web_session::remembered_lifetime));
}

TEST_F(WebSessionStoreTest, RevokeByTokenIdAndAll) {
  web_session::store_t store;
  const auto a = store.create(false, tag, "", "", t0);
  const auto b = store.create(false, tag, "", "", t0);
  const auto c = store.create(false, tag, "", "", t0);
  EXPECT_TRUE(store.revoke_token(a.token));
  EXPECT_FALSE(store.revoke_token(a.token));
  EXPECT_FALSE(store.validate(a.token, tag, t0));
  EXPECT_TRUE(store.revoke_id(b.session.id));
  EXPECT_FALSE(store.revoke_id(b.session.id));
  EXPECT_FALSE(store.validate(b.token, tag, t0));
  EXPECT_TRUE(store.validate(c.token, tag, t0));
  store.revoke_all();
  EXPECT_FALSE(store.validate(c.token, tag, t0));
  EXPECT_EQ(store.size(), 0u);
}

TEST_F(WebSessionStoreTest, CredentialChangeInvalidates) {
  web_session::store_t store;
  const auto s = store.create(false, tag, "", "", t0);
  EXPECT_FALSE(store.validate(s.token, web_session::credential_tag("newhash", "salt"), t0));
  EXPECT_FALSE(store.validate(s.token, tag, t0)) << "a stale session is dropped, not just refused";
  EXPECT_NE(web_session::credential_tag("hash", "salt"), web_session::credential_tag("hash", "salt2"));
}

TEST_F(WebSessionStoreTest, BoundedNumberOfSessions) {
  web_session::store_t store;
  const auto oldest = store.create(false, tag, "", "", t0);
  for (std::size_t i = 1; i < web_session::max_sessions; ++i) {
    store.create(false, tag, "", "", t0 + std::chrono::seconds(i));
  }
  EXPECT_EQ(store.size(), web_session::max_sessions);
  store.create(false, tag, "", "", t0 + 1h);
  EXPECT_EQ(store.size(), web_session::max_sessions);
  EXPECT_FALSE(store.validate(oldest.token, tag, t0 + 1h)) << "the least recently used session makes room";
}

TEST_F(WebSessionStoreTest, PersistsHashedAndOwnerOnly) {
  const auto file = dir / "web_sessions.json";
  std::string token;
  {
    web_session::store_t store {file};
    token = store.create(true, tag, "Agent\x01\nX", "10.0.0.2", t0).token;
  }
  ASSERT_TRUE(fs::exists(file));
  std::ifstream in(file);
  const std::string text {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  EXPECT_EQ(text.find(token), std::string::npos);
  EXPECT_NE(text.find(web_session::hash_token(token)), std::string::npos);
#ifndef _WIN32
  EXPECT_EQ(fs::status(file).permissions() & (fs::perms::group_all | fs::perms::others_all), fs::perms::none);
#endif

  web_session::store_t reloaded {file};
  const auto found = reloaded.validate(token, tag, t0 + 1h);
  ASSERT_TRUE(found);
  EXPECT_TRUE(found->remember);
  EXPECT_EQ(found->user_agent, "Agent  X") << "control characters are not kept";
}

TEST_F(WebSessionStoreTest, CorruptFileStartsEmpty) {
  const auto file = dir / "web_sessions.json";
  std::ofstream(file) << "{not json";
  web_session::store_t store {file};
  EXPECT_EQ(store.size(), 0u);
  const auto s = store.create(false, tag, "", "", t0);
  EXPECT_TRUE(store.validate(s.token, tag, t0));
  EXPECT_TRUE(nlohmann::json::parse(std::ifstream(file)).contains("sessions"));
}

TEST_F(WebSessionStoreTest, ListIsMostRecentFirstWithoutExpired) {
  web_session::store_t store;
  const auto a = store.create(false, tag, "", "", t0);
  const auto b = store.create(true, tag, "", "", t0 + 1s);
  ASSERT_TRUE(store.validate(a.token, tag, t0 + 1h));
  auto list = store.list(t0 + 1h);
  ASSERT_EQ(list.size(), 2u);
  EXPECT_EQ(list[0].id, a.session.id);
  list = store.list(t0 + std::chrono::hours(24 * 8));
  ASSERT_EQ(list.size(), 1u);
  EXPECT_EQ(list[0].id, b.session.id);
}

TEST(WebSessionCookie, SetCookieFlags) {
  const auto session = web_session::set_cookie("abc", std::nullopt);
  EXPECT_EQ(session, "nova_session=abc; Path=/; HttpOnly; Secure; SameSite=Strict");
  const auto kept = web_session::set_cookie("abc", web_session::remembered_lifetime);
  EXPECT_EQ(kept, "nova_session=abc; Path=/; HttpOnly; Secure; SameSite=Strict; Max-Age=2592000");
  const auto cleared = web_session::clear_cookie();
  EXPECT_TRUE(cleared.starts_with("nova_session=; Path=/; HttpOnly; Secure; SameSite=Strict; Max-Age=0"));
}

TEST(WebSessionCookie, ParsesCookieHeader) {
  EXPECT_EQ(web_session::cookie_value("nova_session=abc", "nova_session"), "abc");
  EXPECT_EQ(web_session::cookie_value("a=1; nova_session=abc; b=2", "nova_session"), "abc");
  EXPECT_EQ(web_session::cookie_value("a=1;nova_session=\"abc\"", "nova_session"), "abc");
  EXPECT_EQ(web_session::cookie_value("xnova_session=abc; nova_session_old=d", "nova_session"), "");
  EXPECT_EQ(web_session::cookie_value("", "nova_session"), "");
  EXPECT_EQ(web_session::cookie_value(";;; =; nova_session", "nova_session"), "");
}

TEST(WebSessionCookie, ConstantTimeEquality) {
  EXPECT_TRUE(web_session::equal_ct("abc", "abc"));
  EXPECT_TRUE(web_session::equal_ct("", ""));
  EXPECT_FALSE(web_session::equal_ct("abc", "abd"));
  EXPECT_FALSE(web_session::equal_ct("abc", "abcd"));
}

TEST(WebSessionRedirect, SafeNextAcceptsSameOriginPaths) {
  for (const auto *ok : {"/", "/settings", "/settings?tab=a#b", "/devices/123", "/library?q=%2F%2F"}) {
    EXPECT_TRUE(web_session::is_safe_next(ok)) << ok;
  }
}

TEST(WebSessionRedirect, SafeNextRejectsOpenRedirects) {
  for (const auto *bad : {"", "https://evil.example", "//evil.example", "/\\evil.example", "\\\\evil", "evil.example", "javascript:alert(1)",
                          "/ok\\..\\x", "/a\nb", "/a\tb", " /settings", "/login", "/login?next=/x", "/logout", "http:/evil"}) {
    EXPECT_FALSE(web_session::is_safe_next(bad)) << bad;
  }
  EXPECT_FALSE(web_session::is_safe_next(std::string(3000, 'a').insert(0, "/")));
}

TEST(WebSessionRedirect, LoginRedirectEncodesTarget) {
  EXPECT_EQ(web_session::login_redirect("/"), "/login");
  EXPECT_EQ(web_session::login_redirect("/settings"), "/login?next=/settings");
  EXPECT_EQ(web_session::login_redirect("/a b?x=1&y=2"), "/login?next=/a%20b%3Fx%3D1%26y%3D2");
  EXPECT_EQ(web_session::login_redirect("//evil.example"), "/login");
  EXPECT_EQ(web_session::login_redirect("/login"), "/login");
}
