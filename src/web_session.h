/**
 * @file src/web_session.h
 * @brief Signed-in web UI sessions: random cookie tokens kept server-side (hashed), persisted
 *        across restarts, plus the cookie, CSRF and redirect helpers that go with them.
 */
#pragma once

// standard includes
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace web_session {
  /// Name of the session cookie.
  inline constexpr std::string_view cookie_name = "nova_session";

  /// Lifetime of a "Keep me signed in" session (fixed from sign-in).
  inline constexpr std::chrono::seconds remembered_lifetime {std::chrono::hours(24 * 30)};

  /// A browser-session sign-in ends after this much inactivity...
  inline constexpr std::chrono::seconds idle_timeout {std::chrono::hours(12)};

  /// ...and after this long in any case.
  inline constexpr std::chrono::seconds session_max_lifetime {std::chrono::hours(24 * 7)};

  /// Most sessions kept at once; signing in beyond this drops the least recently used.
  inline constexpr std::size_t max_sessions = 64;

  using clock = std::chrono::system_clock;  ///< Wall clock: expiry must survive restarts.

  /**
   * @brief One signed-in browser.
   */
  struct session_t {
    std::string id;  ///< Public identifier (for listing and revoking); not a credential.
    std::string token_hash;  ///< SHA-256 of the cookie token, hex. The token itself is never stored.
    std::string csrf_token;  ///< Per-session anti-CSRF token the UI echoes in X-CSRF-Token.
    std::string cred_tag;  ///< Ties the session to the password it was created with.
    std::int64_t created = 0;  ///< Sign-in time, seconds since the epoch.
    std::int64_t last_seen = 0;  ///< Last use, seconds since the epoch.
    std::int64_t expires = 0;  ///< Expiry, seconds since the epoch.
    bool remember = false;  ///< Signed in with "Keep me signed in".
    std::string user_agent;  ///< Browser, for the session list (truncated).
    std::string address;  ///< Address it signed in from.
  };

  /**
   * @brief A freshly created session and the token to put in its cookie.
   */
  struct created_t {
    std::string token;  ///< Cookie value; returned once, never stored.
    session_t session;  ///< The stored record.
  };

  /**
   * @brief Thread-safe session store backed by an owner-only JSON file.
   */
  class store_t {
  public:
    /**
     * @brief Create a store.
     * @param file Backing file; empty keeps sessions in memory only.
     */
    explicit store_t(std::filesystem::path file = {});

    /**
     * @brief Point the store at another file and load it (drops what is in memory).
     * @param file Backing file; empty keeps sessions in memory only.
     */
    void open(std::filesystem::path file);

    /**
     * @brief Sign a browser in.
     * @param remember Long-lived session (30 days) instead of a browser-session one.
     * @param cred_tag Current credential tag (see credential_tag()).
     * @param user_agent User-Agent header.
     * @param address Client address.
     * @param now Current time.
     * @return The token for the cookie and the stored record.
     */
    created_t create(bool remember, std::string_view cred_tag, std::string_view user_agent, std::string_view address, clock::time_point now);

    /**
     * @brief Look up the session for a cookie token and mark it used.
     * @param token Cookie value.
     * @param cred_tag Current credential tag; sessions from another password are rejected (and dropped).
     * @param now Current time.
     * @return The session, or nothing when the token is unknown, expired or stale.
     */
    std::optional<session_t> validate(std::string_view token, std::string_view cred_tag, clock::time_point now);

    /**
     * @brief Sign out the session a cookie token belongs to.
     * @param token Cookie value.
     * @return True when a session was removed.
     */
    bool revoke_token(std::string_view token);

    /**
     * @brief Sign out a session by its public id.
     * @param id Public id.
     * @return True when a session was removed.
     */
    bool revoke_id(std::string_view id);

    /**
     * @brief Sign out every session (password change).
     */
    void revoke_all();

    /**
     * @brief Live sessions, most recently used first.
     * @param now Current time; expired sessions are dropped.
     * @return Copies of the session records.
     */
    std::vector<session_t> list(clock::time_point now);

    /**
     * @brief Number of stored sessions (tests).
     * @return Count, expired ones included until the next sweep.
     */
    std::size_t size();

  private:
    void load_locked();
    void save_locked();
    void sweep_locked(std::int64_t now);

    std::mutex mutex_;
    std::filesystem::path file_;
    std::unordered_map<std::string, session_t> sessions_;  ///< By token hash.
  };

  /**
   * @brief Hash a cookie token for storage and lookup.
   * @param token Cookie value.
   * @return Hex SHA-256.
   */
  std::string hash_token(std::string_view token);

  /**
   * @brief Tag identifying the current credentials without revealing them.
   * @param password_hash Stored password hash.
   * @param salt Stored salt.
   * @return A short hex digest that changes whenever the password (or salt) changes.
   */
  std::string credential_tag(std::string_view password_hash, std::string_view salt);

  /**
   * @brief Constant-time string comparison.
   * @param a First value.
   * @param b Second value.
   * @return True when equal.
   */
  bool equal_ct(std::string_view a, std::string_view b);

  /**
   * @brief Extract one cookie from a Cookie header.
   * @param header Cookie header value ("a=1; b=2").
   * @param name Cookie name.
   * @return The value, or empty.
   */
  std::string cookie_value(std::string_view header, std::string_view name);

  /**
   * @brief Set-Cookie value that stores a session token.
   * @param token Cookie value.
   * @param max_age Lifetime for a persistent cookie; nothing for a browser-session cookie.
   * @return Header value with HttpOnly, Secure, SameSite=Strict and Path=/.
   */
  std::string set_cookie(std::string_view token, std::optional<std::chrono::seconds> max_age);

  /**
   * @brief Set-Cookie value that deletes the session cookie.
   * @return Header value.
   */
  std::string clear_cookie();

  /**
   * @brief Whether a post-sign-in destination is a same-origin relative path.
   *
   * Accepts "/x", "/x?y#z"; rejects absolute URLs, scheme-relative "//host" and "/\host",
   * backslashes, control characters, and /login itself.
   *
   * @param next Candidate destination (already URL-decoded).
   * @return True when it is safe to navigate to.
   */
  bool is_safe_next(std::string_view next);

  /**
   * @brief The /login URL that returns to @p target after signing in.
   * @param target Request path with query, e.g. "/settings?tab=a".
   * @return "/login?next=<encoded>", or "/login" when the target is not safe or is "/".
   */
  std::string login_redirect(std::string_view target);

  /**
   * @brief The process-wide store, backed by web_sessions.json next to the state file.
   * @return The store.
   */
  store_t &web_ui();
}  // namespace web_session
