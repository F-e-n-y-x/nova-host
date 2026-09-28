/**
 * @file src/web_session.cpp
 * @brief Signed-in web UI sessions.
 */
// class header include
#include "web_session.h"

// standard includes
#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <stdexcept>

// lib includes
#include <nlohmann/json.hpp>
#include <openssl/crypto.h>
#include <openssl/rand.h>

// local includes
#include "config.h"
#include "crypto.h"
#include "logging.h"
#include "secure_files.h"

using namespace std::literals;

namespace web_session {
  namespace fs = std::filesystem;

  namespace {
    /// Don't rewrite the file for every request: only when last-use moved this much.
    constexpr std::int64_t persist_last_seen_after = 5 * 60;

    constexpr std::size_t max_user_agent = 200;

    std::string to_hex(std::string_view bytes) {
      static constexpr char digits[] = "0123456789abcdef";
      std::string out;
      out.reserve(bytes.size() * 2);
      for (const unsigned char c : bytes) {
        out.push_back(digits[c >> 4]);
        out.push_back(digits[c & 0x0f]);
      }
      return out;
    }

    /// Hex of @p bytes random bytes from OpenSSL's CSPRNG; throws rather than hand out a weak token.
    std::string random_hex(const std::size_t bytes) {
      std::string raw(bytes, '\0');
      if (RAND_bytes(reinterpret_cast<unsigned char *>(raw.data()), static_cast<int>(raw.size())) != 1) {
        throw std::runtime_error("no secure random bytes available");
      }
      return to_hex(raw);
    }

    std::string sha256_hex(std::string_view text) {
      const auto digest = crypto::hash(text);
      return to_hex({reinterpret_cast<const char *>(digest.data()), digest.size()});
    }

    std::int64_t seconds_of(clock::time_point t) {
      return std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count();
    }

    std::int64_t expiry_for(const session_t &s) {
      if (s.remember) {
        return s.created + remembered_lifetime.count();
      }
      return std::min(s.last_seen + idle_timeout.count(), s.created + session_max_lifetime.count());
    }

    std::string clean_user_agent(std::string_view ua) {
      std::string out;
      for (const char c : ua.substr(0, max_user_agent)) {
        out.push_back(static_cast<unsigned char>(c) < 0x20 || c == 0x7f ? ' ' : c);
      }
      return out;
    }
  }  // namespace

  std::string hash_token(std::string_view token) {
    return sha256_hex(token);
  }

  std::string credential_tag(std::string_view password_hash, std::string_view salt) {
    return sha256_hex(std::format("nova-web-session\n{}\n{}", password_hash, salt)).substr(0, 32);
  }

  bool equal_ct(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
      return false;
    }
    return a.empty() || CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
  }

  store_t::store_t(fs::path file) {
    open(std::move(file));
  }

  void store_t::open(fs::path file) {
    std::scoped_lock lock(mutex_);
    file_ = std::move(file);
    sessions_.clear();
    load_locked();
  }

  void store_t::load_locked() {
    if (file_.empty()) {
      return;
    }
    std::error_code ec;
    if (!fs::exists(file_, ec)) {
      return;
    }
    try {
      std::ifstream in(file_, std::ios::binary);
      const auto tree = nlohmann::json::parse(in);
      for (const auto &item : tree.at("sessions")) {
        session_t s;
        s.id = item.at("id").get<std::string>();
        s.token_hash = item.at("token_hash").get<std::string>();
        s.csrf_token = item.at("csrf").get<std::string>();
        s.cred_tag = item.at("cred_tag").get<std::string>();
        s.created = item.at("created").get<std::int64_t>();
        s.last_seen = item.at("last_seen").get<std::int64_t>();
        s.expires = item.at("expires").get<std::int64_t>();
        s.remember = item.value("remember", false);
        s.user_agent = clean_user_agent(item.value("user_agent", ""s));
        s.address = item.value("address", ""s);
        if (s.token_hash.size() == 64 && !s.id.empty() && s.csrf_token.size() >= 32) {
          sessions_.emplace(s.token_hash, std::move(s));
        }
      }
    } catch (const std::exception &e) {
      BOOST_LOG(warning) << "Web UI: couldn't read "sv << file_.string() << ", everyone signs in again: "sv << e.what();
      sessions_.clear();
    }
  }

  void store_t::save_locked() {
    if (file_.empty()) {
      return;
    }
    auto items = nlohmann::json::array();
    for (const auto &[hash, s] : sessions_) {
      items.push_back({
        {"id", s.id},
        {"token_hash", s.token_hash},
        {"csrf", s.csrf_token},
        {"cred_tag", s.cred_tag},
        {"created", s.created},
        {"last_seen", s.last_seen},
        {"expires", s.expires},
        {"remember", s.remember},
        {"user_agent", s.user_agent},
        {"address", s.address},
      });
    }
    const nlohmann::json tree {{"version", 1}, {"sessions", std::move(items)}};
    if (!secure_files::write_private(file_, tree.dump(2))) {
      BOOST_LOG(warning) << "Web UI: couldn't save sign-in sessions to "sv << file_.string();
    }
  }

  void store_t::sweep_locked(const std::int64_t now) {
    std::erase_if(sessions_, [now](const auto &entry) {
      return entry.second.expires <= now;
    });
  }

  created_t store_t::create(const bool remember, const std::string_view cred_tag, const std::string_view user_agent, const std::string_view address, const clock::time_point now) {
    created_t out;
    out.token = random_hex(32);  // 256 bits
    auto &s = out.session;
    s.id = random_hex(9);
    s.token_hash = hash_token(out.token);
    s.csrf_token = random_hex(32);
    s.cred_tag = std::string {cred_tag};
    s.created = s.last_seen = seconds_of(now);
    s.remember = remember;
    s.expires = expiry_for(s);
    s.user_agent = clean_user_agent(user_agent);
    s.address = std::string {address};

    std::scoped_lock lock(mutex_);
    sweep_locked(s.created);
    while (sessions_.size() >= max_sessions) {
      const auto oldest = std::ranges::min_element(sessions_, {}, [](const auto &entry) {
        return entry.second.last_seen;
      });
      sessions_.erase(oldest);
    }
    sessions_.emplace(s.token_hash, s);
    save_locked();
    return out;
  }

  std::optional<session_t> store_t::validate(const std::string_view token, const std::string_view cred_tag, const clock::time_point now) {
    // Only well-formed tokens are looked up; anything else can't be ours.
    if (token.size() != 64 || !std::ranges::all_of(token, [](const char c) {
          return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        })) {
      return std::nullopt;
    }
    const auto hash = hash_token(token);
    const auto t = seconds_of(now);

    std::scoped_lock lock(mutex_);
    const auto it = sessions_.find(hash);
    if (it == sessions_.end()) {
      return std::nullopt;
    }
    auto &s = it->second;
    if (s.expires <= t || !equal_ct(s.cred_tag, cred_tag)) {
      sessions_.erase(it);
      save_locked();
      return std::nullopt;
    }
    const bool persist = t - s.last_seen >= persist_last_seen_after;
    s.last_seen = std::max(s.last_seen, t);
    s.expires = expiry_for(s);
    auto copy = s;
    if (persist) {
      save_locked();
    }
    return copy;
  }

  bool store_t::revoke_token(const std::string_view token) {
    const auto hash = hash_token(token);
    std::scoped_lock lock(mutex_);
    if (sessions_.erase(hash) == 0) {
      return false;
    }
    save_locked();
    return true;
  }

  bool store_t::revoke_id(const std::string_view id) {
    std::scoped_lock lock(mutex_);
    const auto removed = std::erase_if(sessions_, [id](const auto &entry) {
      return entry.second.id == id;
    });
    if (removed == 0) {
      return false;
    }
    save_locked();
    return true;
  }

  void store_t::revoke_all() {
    std::scoped_lock lock(mutex_);
    sessions_.clear();
    save_locked();
  }

  std::vector<session_t> store_t::list(const clock::time_point now) {
    std::scoped_lock lock(mutex_);
    sweep_locked(seconds_of(now));
    std::vector<session_t> out;
    out.reserve(sessions_.size());
    for (const auto &[hash, s] : sessions_) {
      out.push_back(s);
    }
    std::ranges::sort(out, std::ranges::greater {}, &session_t::last_seen);
    return out;
  }

  std::size_t store_t::size() {
    std::scoped_lock lock(mutex_);
    return sessions_.size();
  }

  std::string cookie_value(const std::string_view header, const std::string_view name) {
    std::size_t pos = 0;
    while (pos < header.size()) {
      auto end = header.find(';', pos);
      if (end == std::string_view::npos) {
        end = header.size();
      }
      auto part = header.substr(pos, end - pos);
      while (!part.empty() && (part.front() == ' ' || part.front() == '\t')) {
        part.remove_prefix(1);
      }
      while (!part.empty() && (part.back() == ' ' || part.back() == '\t')) {
        part.remove_suffix(1);
      }
      if (const auto eq = part.find('='); eq != std::string_view::npos && part.substr(0, eq) == name) {
        auto value = part.substr(eq + 1);
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
          value = value.substr(1, value.size() - 2);
        }
        return std::string {value};
      }
      pos = end + 1;
    }
    return {};
  }

  std::string set_cookie(const std::string_view token, const std::optional<std::chrono::seconds> max_age) {
    auto out = std::format("{}={}; Path=/; HttpOnly; Secure; SameSite=Strict", cookie_name, token);
    if (max_age) {
      out += std::format("; Max-Age={}", max_age->count());
    }
    return out;
  }

  std::string clear_cookie() {
    return std::format("{}=; Path=/; HttpOnly; Secure; SameSite=Strict; Max-Age=0; Expires=Thu, 01 Jan 1970 00:00:00 GMT", cookie_name);
  }

  bool is_safe_next(const std::string_view next) {
    if (next.empty() || next.size() > 2048 || next.front() != '/') {
      return false;
    }
    if (next.size() > 1 && (next[1] == '/' || next[1] == '\\')) {
      return false;  // "//evil" and "/\evil" are scheme-relative in browsers
    }
    for (const unsigned char c : next) {
      if (c < 0x20 || c == 0x7f || c == '\\') {
        return false;
      }
    }
    const auto path = next.substr(0, next.find_first_of("?#"));
    return path != "/login" && path != "/login/" && path != "/logout" && path != "/logout/";
  }

  std::string login_redirect(const std::string_view target) {
    if (target == "/" || !is_safe_next(target)) {
      return "/login";
    }
    std::string out = "/login?next=";
    for (const unsigned char c : target) {
      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
        out.push_back(static_cast<char>(c));
      } else {
        out += std::format("%{:02X}", c);
      }
    }
    return out;
  }

  store_t &web_ui() {
    static store_t store {fs::path {config::nvhttp.file_state}.parent_path() / "web_sessions.json"};
    return store;
  }
}  // namespace web_session
