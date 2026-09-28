/**
 * @file src/nova_update_http.cpp
 * @brief Web UI routes for the Nova update check and installer.
 */
// standard includes
#include <sstream>
#include <thread>

// lib includes
#include <nlohmann/json.hpp>

// local includes
#include "logging.h"
#include "nova_update.h"
#include "nova_update_http.h"
#include "platform/common.h"

using namespace std::literals;

namespace nova_update {
  namespace {
    constexpr std::int64_t AUTO_CHECK_AGE_S = 6 * 60 * 60;  ///< Background check at most every 6 hours.
    constexpr std::int64_t MANUAL_CHECK_AGE_S = 60;  ///< "Check now" at most once a minute (60 unauthenticated calls/hour).

    void write_json(const confighttp::resp_https_t &response, SimpleWeb::StatusCode code, const nlohmann::json &tree) {
      SimpleWeb::CaseInsensitiveMultimap headers;
      headers.emplace("Content-Type", "application/json");
      headers.emplace("Cache-Control", "no-store");
      headers.emplace("X-Frame-Options", "DENY");
      headers.emplace("Content-Security-Policy", "frame-ancestors 'none';");
      response->write(code, tree.dump(), headers);
    }

    void start_check(const options_t &options) {
      if (!instance().claim_check()) {
        return;
      }
      std::thread([options] {
        platf::set_thread_name("nova::upcheck");
        instance().check(options);
      }).detach();
    }

    /**
     * @brief GET /api/update/status[?refresh=1]
     *
     * Returns the cached result and starts a background check when it's stale; poll while
     * `checking` is true or `install.state` is in progress.
     */
    void get_status(const confighttp::resp_https_t &response, const confighttp::req_https_t &request) {
      if (!confighttp::authenticate(response, request)) {
        return;
      }
      const auto options = current_options();
      if (options.enabled) {
        const auto query = request->parse_query_string();
        const bool refresh = query.contains("refresh") && query.find("refresh")->second == "1";
        if (instance().stale(refresh ? MANUAL_CHECK_AGE_S : AUTO_CHECK_AGE_S)) {
          start_check(options);
        }
      }
      write_json(response, SimpleWeb::StatusCode::success_ok, instance().status_json(options));
    }

    /**
     * @brief POST /api/update/install {"tag": "nova-vX.Y.Z"}
     *
     * The tag must be the newer release from the last check (what the owner confirmed). Refused
     * with 409 while a client is streaming or another install runs.
     */
    void post_install(const confighttp::resp_https_t &response, const confighttp::req_https_t &request) {
      if (!confighttp::check_content_type(response, request, "application/json")) {
        return;
      }
      if (!confighttp::authenticate(response, request)) {
        return;
      }
      if (!confighttp::validate_csrf_token(response, request, confighttp::get_client_id(request))) {
        return;
      }
      confighttp::print_req(request);
      std::stringstream ss;
      ss << request->content.rdbuf();
      const auto body = nlohmann::json::parse(ss.str(), nullptr, false);
      if (body.is_discarded() || !body.is_object() || !body.contains("tag") || !body["tag"].is_string()) {
        confighttp::bad_request(response, request, "Expected {\"tag\": \"nova-vX.Y.Z\"}");
        return;
      }
      const auto options = current_options();
      const auto tag = body["tag"].get<std::string>();
      switch (instance().begin_install(options, tag)) {
        case start_result_e::streaming:
          write_json(response, SimpleWeb::StatusCode::client_error_conflict, {{"status", false}, {"error", "streaming"}});
          return;
        case start_result_e::busy:
          write_json(response, SimpleWeb::StatusCode::client_error_conflict, {{"status", false}, {"error", "busy"}});
          return;
        case start_result_e::no_update:
          write_json(response, SimpleWeb::StatusCode::client_error_conflict, {{"status", false}, {"error", "no_update"}});
          return;
        case start_result_e::started:
          break;
      }
      BOOST_LOG(info) << "Update install: owner confirmed "sv << tag;
      std::thread([options] {
        platf::set_thread_name("nova::upinst");
        instance().run_install(options);
      }).detach();
      write_json(response, SimpleWeb::StatusCode::success_accepted, {{"status", true}});
    }
  }  // namespace

  void register_routes(confighttp::https_server_t &server) {
    server.resource["^/api/update/status$"]["GET"] = get_status;
    server.resource["^/api/update/install$"]["POST"] = post_install;
  }
}  // namespace nova_update
