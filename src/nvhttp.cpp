/**
 * @file src/nvhttp.cpp
 * @brief Definitions for the nvhttp (GameStream) server.
 */
// macros
#define BOOST_BIND_GLOBAL_PLACEHOLDERS

// standard includes
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// lib includes
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/context_base.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <Simple-Web-Server/server_http.hpp>

// local includes
#include "client_permissions.h"
#include "clipboard.h"
#include "config.h"
#include "display_follow.h"
#include "display_device.h"
#include "file_handler.h"
#include "globals.h"
#include "httpcommon.h"
#include "library/library.h"
#include "logging.h"
#include "network.h"
#include "library/metadata.h"
#include "nova_client_api.h"
#include "nvhttp.h"
#include "platform/common.h"
#include "process.h"
#include "rtsp.h"
#include "system_tray.h"
#include "utility.h"
#include "uuid.h"
#include "video.h"

using namespace std::literals;

namespace nvhttp {

  static constexpr std::string_view EMPTY_PROPERTY_TREE_ERROR_MSG = "Property tree is empty. Probably, control flow got interrupted by an unexpected C++ exception. This is a bug in Sunshine. Moonlight-qt will report Malformed XML (missing root element)."sv;

  namespace fs = std::filesystem;
  namespace pt = boost::property_tree;

  crypto::cert_chain_t cert_chain;  ///< Enabled paired-client certificates accepted by Sunshine's GameStream HTTPS server.

  /**
   * @brief Get the mutex protecting paired-client authorization state.
   *
   * @return Mutex serializing paired-client state and certificate authorization changes.
   */
  std::mutex &client_auth_mutex() {
    static std::mutex mutex;
    return mutex;
  }

  void remember_verified_peer(const boost::asio::ip::tcp::endpoint &peer);

  /**
   * @brief HTTPS server backend that adds Sunshine's client-certificate verification.
   */
  class SunshineHTTPSServer: public SimpleWeb::ServerBase<SunshineHTTPS> {
  public:
    /**
     * @brief Initialize the HTTPS server with Sunshine's certificate and key files.
     *
     * @param certification_file Path to the server certificate file.
     * @param private_key_file Path to the matching private key file.
     */
    SunshineHTTPSServer(const std::string &certification_file, const std::string &private_key_file):
        ServerBase<SunshineHTTPS>::ServerBase(443),
        context(boost::asio::ssl::context::tls_server) {
      // Disabling TLS 1.0 and 1.1 (see RFC 8996)
      context.set_options(boost::asio::ssl::context::no_tlsv1);
      context.set_options(boost::asio::ssl::context::no_tlsv1_1);
      context.use_certificate_chain_file(certification_file);
      context.use_private_key_file(private_key_file, boost::asio::ssl::context::pem);
    }

    std::function<int(SSL *)> verify;  ///< Callback that validates a client's TLS certificate after handshake.
    std::function<void(std::shared_ptr<Response>, std::shared_ptr<Request>)> on_verify_failed;  ///< Handler used to return the pairing challenge when client verification fails.

  protected:
    boost::asio::ssl::context context;  ///< TLS server context configured with Sunshine's certificate and protocol policy.

    /**
     * @brief Enable client-certificate verification after the listening socket is bound.
     */
    void after_bind() override {
      if (verify) {
        context.set_verify_mode(boost::asio::ssl::verify_peer | boost::asio::ssl::verify_fail_if_no_peer_cert | boost::asio::ssl::verify_client_once);
        context.set_verify_callback([](int verified, boost::asio::ssl::verify_context &ctx) {
          // To respond with an error message, a connection must be established
          return 1;
        });
      }
    }

    // This is Server<HTTPS>::accept() with SSL validation support added
    /**
     * @brief Accept a pending connection and arm the server for the next client.
     */
    void accept() override {
      auto connection = create_connection(*io_service, context);

      acceptor->async_accept(connection->socket->lowest_layer(), [this, connection](const SimpleWeb::error_code &ec) {
        auto lock = connection->handler_runner->continue_lock();
        if (!lock) {
          return;
        }

        if (ec != SimpleWeb::error::operation_aborted) {
          this->accept();
        }

        auto session = std::make_shared<Session>(config.max_request_streambuf_size, connection);

        if (!ec) {
          boost::asio::ip::tcp::no_delay option(true);
          SimpleWeb::error_code ec;
          session->connection->socket->lowest_layer().set_option(option, ec);

          session->connection->set_timeout(config.timeout_request);
          session->connection->socket->async_handshake(boost::asio::ssl::stream_base::server, [this, session](const SimpleWeb::error_code &ec) {
            session->connection->cancel_timeout();
            auto lock = session->connection->handler_runner->continue_lock();
            if (!lock) {
              return;
            }
            if (!ec) {
              if (verify && !verify(session->connection->socket->native_handle())) {
                this->write(session, on_verify_failed);
              } else {
                if (verify) {
                  SimpleWeb::error_code endpoint_ec;
                  const auto peer = session->connection->socket->lowest_layer().remote_endpoint(endpoint_ec);
                  if (!endpoint_ec) {
                    remember_verified_peer(peer);
                  }
                }
                this->read(session);
              }
            } else if (this->on_error) {
              this->on_error(session->request, ec);
            }
          });
        } else if (this->on_error) {
          this->on_error(session->request, ec);
        }
      });
    }
  };

  /**
   * @brief HTTPS server type used for GameStream endpoints requiring TLS.
   */
  using https_server_t = SunshineHTTPSServer;
  /**
   * @brief Plain HTTP server type used for GameStream endpoints without TLS.
   */
  using http_server_t = SimpleWeb::Server<SimpleWeb::HTTP>;

  /**
   * @brief Internal HTTPS credential paths for the configuration server.
   */
  struct conf_intern_t {
    std::string servercert;  ///< Server certificate PEM string.
    std::string pkey;  ///< Private key PEM string or path.
  } conf_intern;  ///< TLS credential paths loaded from Sunshine's runtime configuration.

  /**
   * @brief Certificate entry associated with a client name and UUID.
   */
  struct named_cert_t {
    std::string name;  ///< Human-readable name for this item.
    std::string uuid;  ///< Persistent Moonlight client UUID associated with the certificate.
    std::string cert;  ///< Certificate PEM string or path.
    bool enabled = true;  ///< Whether this persisted client entry may connect.
    client_permissions::mask_t permissions = client_permissions::full;  ///< What the client may do while streaming.
    std::int64_t paired_at = 0;  ///< Unix time (seconds) the client paired; 0 when unknown (paired before tracking).
    std::int64_t last_connected_at = 0;  ///< Unix time (seconds) of the client's last launch or resume; 0 when never.
  };

  /**
   * @brief Current wall-clock time as Unix seconds.
   *
   * @return Seconds since the Unix epoch.
   */
  std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  }

  /**
   * @brief Persisted pairing data for one Moonlight client.
   */
  struct client_t {
    std::vector<named_cert_t> named_devices;  ///< Persisted Moonlight clients allowed to pair or reconnect.
  };

  // uniqueID, session
  std::unordered_map<std::string, pair_session_t> map_id_sess;  ///< Pairing sessions keyed by temporary unique ID.

  /**
   * @brief Get the mutex protecting pairing-session state.
   *
   * @return Mutex serializing pairing-session storage and lifecycle transitions.
   */
  std::mutex &map_id_sess_mutex() {
    static std::mutex mutex;
    return mutex;
  }

  client_t client_root;  ///< In-memory representation of the paired-client database.
  std::atomic<uint32_t> session_id_counter;  ///< Monotonic counter used to allocate GameStream session IDs.

  // Set by TLS verify callback, read by launch/resume handler (single-threaded HTTPS server)
  std::string last_verified_client_cert;  ///< Last client certificate accepted by the TLS verify callback.  // NOSONAR(cpp:S5421): intentionally mutable global
  std::string last_verified_client_name;  ///< Friendly name of last client certificate accepted by the TLS verify callback. // NOSONAR(cpp:S5421): intentionally mutable global

  /**
   * @brief A verified TLS client: its certificate and friendly name.
   */
  struct verified_peer_t {
    std::string cert;  ///< PEM of the certificate accepted for the connection.
    std::string name;  ///< Friendly name of that client.
  };

  /**
   * @brief Verified clients keyed by the peer endpoint of their TLS connection.
   *
   * The last_verified_* globals change on every handshake, so a request on a kept-alive
   * connection could otherwise be attributed to whichever client connected most recently.
   * Guarded by client_auth_mutex().
   *
   * @return The map.
   */
  std::map<boost::asio::ip::tcp::endpoint, verified_peer_t> &verified_peers() {
    static std::map<boost::asio::ip::tcp::endpoint, verified_peer_t> peers;
    return peers;
  }

  void remember_verified_peer(const boost::asio::ip::tcp::endpoint &peer, std::string cert, std::string name) {
    constexpr std::size_t max_tracked_connections = 1024;
    std::lock_guard lock {client_auth_mutex()};
    auto &peers = verified_peers();
    if (peers.size() >= max_tracked_connections && !peers.contains(peer)) {
      // Endpoints of closed connections are overwritten on reuse; drop the rest wholesale.
      peers.clear();
    }
    peers[peer] = {std::move(cert), std::move(name)};
  }

  /**
   * @brief Record the client just verified for the connection from @p peer.
   *
   * Called right after the verify callback succeeds, on the same connection, so the
   * last_verified_* globals still describe this client.
   *
   * @param peer Remote endpoint of the verified connection.
   */
  void remember_verified_peer(const boost::asio::ip::tcp::endpoint &peer) {
    std::string cert;
    std::string name;
    {
      std::lock_guard lock {client_auth_mutex()};
      cert = last_verified_client_cert;
      name = last_verified_client_name;
    }
    remember_verified_peer(peer, std::move(cert), std::move(name));
  }

  /**
   * @brief Look up the verified client for a connection's peer endpoint.
   *
   * @param peer Remote endpoint of the connection.
   * @return Its certificate and name, or empty values when the connection is unknown.
   */
  verified_peer_t verified_peer_at(const boost::asio::ip::tcp::endpoint &peer) {
    std::lock_guard lock {client_auth_mutex()};
    const auto &peers = verified_peers();
    if (const auto it = peers.find(peer); it != peers.end()) {
      return it->second;
    }
    return {};
  }

  std::string verified_cert_for(const boost::asio::ip::tcp::endpoint &peer) {
    return verified_peer_at(peer).cert;
  }

  client_permissions::mask_t permissions_for_peer(const boost::asio::ip::tcp::endpoint &peer) {
    const auto cert = verified_cert_for(peer);
    return cert.empty() ? client_permissions::view_only : get_client_permissions(cert);
  }


  /**
   * @brief Case-insensitive map used for HTTP headers and query parameters.
   */
  using args_t = SimpleWeb::CaseInsensitiveMultimap;
  /**
   * @brief Shared HTTPS response object passed to GameStream handlers.
   */
  using resp_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SunshineHTTPS>::Response>;
  /**
   * @brief Shared HTTPS request object received by GameStream handlers.
   */
  using req_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SunshineHTTPS>::Request>;
  /**
   * @brief Shared HTTP response object passed to redirect and discovery handlers.
   */
  using resp_http_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTP>::Response>;
  /**
   * @brief Shared HTTP request object received by redirect and discovery handlers.
   */
  using req_http_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTP>::Request>;

  /**
   * @brief The verified client on the connection that sent @p request.
   *
   * @param request HTTPS request.
   * @return Certificate and name, or empty values when the connection is unknown.
   */
  verified_peer_t verified_peer_for(const req_https_t &request) {
    return verified_peer_at(request->remote_endpoint());
  }

  /**
   * @brief Permissions of the client that sent @p request; nothing when it can't be identified.
   *
   * @param request HTTPS request.
   * @return The client's permission mask, or client_permissions::view_only if unknown.
   */
  client_permissions::mask_t permissions_for_request(const req_https_t &request) {
    return permissions_for_peer(request->remote_endpoint());
  }

  /**
   * @brief Certificate operations supported by the pairing API.
   */
  enum class op_e {
    ADD,  ///< Add certificate
    REMOVE  ///< Remove certificate
  };

  /**
   * @brief Read a named query argument from the HTTP request map.
   *
   * @param args Parsed query-string argument map.
   * @param name Query parameter name to read.
   * @param default_value Value returned when the parameter is absent.
   * @return Query parameter value, default value, or an empty string.
   */
  std::string get_arg(const args_t &args, const char *name, const char *default_value = nullptr) {
    auto it = args.find(name);
    if (it == std::end(args)) {
      if (default_value != nullptr) {
        return std::string(default_value);
      }

      throw std::out_of_range(name);
    }
    return it->second;
  }

  /**
   * @brief Persist the current state to its backing store.
   */
  void save_state() {
    pt::ptree root;

    if (fs::exists(config::nvhttp.file_state)) {
      try {
        pt::read_json(config::nvhttp.file_state, root);
      } catch (std::exception &e) {
        BOOST_LOG(error) << "Couldn't read "sv << config::nvhttp.file_state << ": "sv << e.what();
        return;
      }
    }

    root.erase("root"s);

    root.put("root.uniqueid", http::unique_id);
    client_t &client = client_root;
    pt::ptree node;

    pt::ptree named_cert_nodes;
    for (auto &named_cert : client.named_devices) {
      pt::ptree named_cert_node;
      named_cert_node.put("name"s, named_cert.name);
      named_cert_node.put("cert"s, named_cert.cert);
      named_cert_node.put("uuid"s, named_cert.uuid);
      named_cert_node.put("enabled"s, named_cert.enabled);
      pt::ptree permissions_node;
      for (const auto &[flag_name, flag] : client_permissions::flag_names) {
        permissions_node.put(std::string {flag_name}, client_permissions::has(named_cert.permissions, flag));
      }
      named_cert_node.add_child("permissions"s, permissions_node);
      if (named_cert.paired_at > 0) {
        named_cert_node.put("paired_at"s, named_cert.paired_at);
      }
      if (named_cert.last_connected_at > 0) {
        named_cert_node.put("last_connected_at"s, named_cert.last_connected_at);
      }
      named_cert_nodes.push_back(std::make_pair(""s, named_cert_node));
    }
    root.add_child("root.named_devices"s, named_cert_nodes);

    try {
      pt::write_json(config::nvhttp.file_state, root);
    } catch (std::exception &e) {
      BOOST_LOG(error) << "Couldn't write "sv << config::nvhttp.file_state << ": "sv << e.what();
      return;
    }
  }

  /**
   * @brief Convert a PEM certificate to Sunshine's canonical OpenSSL serialization.
   *
   * @param cert_pem PEM-encoded certificate to canonicalize.
   * @return Canonical PEM text, or an empty string when the certificate is invalid.
   */
  std::string canonical_certificate_pem(const std::string_view cert_pem) {
    auto certificate = crypto::x509(cert_pem);
    return certificate ? crypto::pem(certificate) : std::string {};
  }

  /**
   * @brief Rebuild the GameStream trust stores from enabled paired-client records.
   *
   * @note The caller must hold `client_auth_mutex()`.
   */
  void rebuild_client_cert_chain() {
    cert_chain.clear();
    for (const auto &named_cert : client_root.named_devices) {
      if (!named_cert.enabled) {
        continue;
      }

      auto certificate = crypto::x509(named_cert.cert);
      if (!certificate) {
        BOOST_LOG(warning) << "Ignoring invalid paired-client certificate"sv;
        continue;
      }

      cert_chain.add(std::move(certificate));
    }
  }

  /**
   * @brief Check whether a certificate exactly matches an enabled paired-client record.
   *
   * @param certificate Parsed client certificate to compare by canonical X.509 identity.
   * @return `true` only when exactly one matching paired-client record is enabled.
   * @note The caller must hold `client_auth_mutex()`.
   */
  bool is_client_enabled(const X509 *certificate) {
    bool matched = false;
    for (const auto &named_cert : client_root.named_devices) {
      if (auto stored_certificate = crypto::x509(named_cert.cert); !stored_certificate || X509_cmp(stored_certificate.get(), certificate) != 0) {
        continue;
      }

      if (matched || !named_cert.enabled) {
        return false;
      }
      matched = true;
    }
    return matched;
  }

  /**
   * @brief Verify a client certificate against the exact enabled paired identity.
   *
   * @param certificate Parsed client certificate presented during the TLS handshake.
   * @return `nullptr` when authorized, otherwise a non-sensitive error string.
   * @note The caller must hold `client_auth_mutex()`.
   */
  const char *verify_client_certificate(X509 *certificate) {
    if (const auto verification_error = cert_chain.verify(certificate); verification_error) {
      return verification_error;
    }
    return is_client_enabled(certificate) ? nullptr : "Client certificate identity is not enabled";
  }

  /**
   * @brief Load state from its backing store.
   */
  void load_state() {
    if (!fs::exists(config::nvhttp.file_state)) {
      BOOST_LOG(info) << "File "sv << config::nvhttp.file_state << " doesn't exist"sv;
      http::unique_id = uuid_util::uuid_t::generate().string();
      return;
    }

    pt::ptree tree;
    try {
      pt::read_json(config::nvhttp.file_state, tree);
    } catch (std::exception &e) {
      BOOST_LOG(error) << "Couldn't read "sv << config::nvhttp.file_state << ": "sv << e.what();

      return;
    }

    auto unique_id_p = tree.get_optional<std::string>("root.uniqueid");
    if (!unique_id_p) {
      // This file doesn't contain moonlight credentials
      http::unique_id = uuid_util::uuid_t::generate().string();
      return;
    }
    http::unique_id = std::move(*unique_id_p);

    auto root = tree.get_child("root");
    client_t client;

    // Import from old format
    if (root.get_child_optional("devices")) {
      auto device_nodes = root.get_child("devices");
      for (auto &[_, device_node] : device_nodes) {
        auto uniqID = device_node.get<std::string>("uniqueid");

        if (device_node.count("certs")) {
          for (auto &[_, el] : device_node.get_child("certs")) {
            named_cert_t named_cert;
            named_cert.name = ""s;
            named_cert.cert = el.get_value<std::string>();
            named_cert.uuid = uuid_util::uuid_t::generate().string();
            client.named_devices.emplace_back(named_cert);
          }
        }
      }
    }

    if (root.count("named_devices")) {
      for (auto &[_, el] : root.get_child("named_devices")) {
        named_cert_t named_cert;
        named_cert.name = el.get_child("name").get_value<std::string>();
        named_cert.cert = el.get_child("cert").get_value<std::string>();
        named_cert.uuid = el.get_child("uuid").get_value<std::string>();
        named_cert.enabled = el.get<bool>("enabled", true);
        // State files written before permissions existed have no node: keep full access.
        if (const auto permissions_node = el.get_child_optional("permissions")) {
          client_permissions::mask_t mask = 0;
          for (const auto &[flag_name, flag] : client_permissions::flag_names) {
            if (permissions_node->get<bool>(std::string {flag_name}, true)) {
              mask |= flag;
            }
          }
          named_cert.permissions = mask;
        }
        named_cert.paired_at = std::max<std::int64_t>(0, el.get<std::int64_t>("paired_at", 0));
        named_cert.last_connected_at = std::max<std::int64_t>(0, el.get<std::int64_t>("last_connected_at", 0));
        client.named_devices.emplace_back(named_cert);
      }
    }

    for (auto &named_cert : client.named_devices) {
      auto canonical_certificate = canonical_certificate_pem(named_cert.cert);
      if (!canonical_certificate.empty()) {
        named_cert.cert = std::move(canonical_certificate);
      }
    }

    std::lock_guard lock {client_auth_mutex()};
    client_root = std::move(client);
    rebuild_client_cert_chain();
  }

  /**
   * @brief Add authorized client data.
   *
   * A completed pairing replaces all records with the same exact X.509 identity so legacy duplicate records cannot make the newly paired client fail authorization.
   *
   * @param name Human-readable name to assign.
   * @param cert Certificate data or object used by the operation.
   * @return Persistent UUID for the added client, or an empty string when the certificate is invalid.
   */
  std::string add_authorized_client(const std::string &name, std::string &&cert) {
    auto certificate = crypto::x509(cert);
    if (!certificate) {
      return {};
    }

    named_cert_t named_cert;
    named_cert.name = name;
    named_cert.cert = crypto::pem(certificate);
    named_cert.uuid = uuid_util::uuid_t::generate().string();
    named_cert.paired_at = unix_now();
    const auto uuid = named_cert.uuid;

    std::lock_guard lock {client_auth_mutex()};
    std::erase_if(client_root.named_devices, [&certificate](const named_cert_t &existing_client) {
      auto existing_certificate = crypto::x509(existing_client.cert);
      return existing_certificate && X509_cmp(existing_certificate.get(), certificate.get()) == 0;
    });
    client_root.named_devices.emplace_back(std::move(named_cert));
    rebuild_client_cert_chain();

    if (!config::sunshine.flags[config::flag::FRESH_STATE]) {
      save_state();
    }
    return uuid;
  }

  /**
   * @brief Read apps.json (under the library lock).
   *
   * @return Parsed tree, or an empty object on failure.
   */
  nlohmann::json read_apps_file() {
    try {
      std::scoped_lock lock(library::apps_file_mutex());
      return nlohmann::json::parse(file_handler::read_file(config::stream.file_apps.c_str()));
    } catch (const std::exception &) {
      return nlohmann::json::object();
    }
  }

  /**
   * @brief Position in apps.json of the app with GameStream id @p appid.
   *
   * @param appid GameStream app id.
   * @return Index, or nullopt.
   */
  std::optional<std::size_t> app_index_for_id(int appid) {
    const auto &apps = proc::proc.get_apps();
    for (std::size_t i = 0; i < apps.size(); ++i) {
      if (apps[i].id == std::to_string(appid)) {
        return i;
      }
    }
    return std::nullopt;
  }

  /**
   * @brief Run Nova's display-follow step for a launch or resume.
   *
   * @param session The launch session built from the client's request.
   * @param appid GameStream id of the app being launched or resumed.
   * @param launching True for /launch (the app's own prep commands still run), false for /resume.
   */
  void follow_client_display(const rtsp_stream::launch_session_t &session, int appid, bool launching);

  /**
   * @brief Per-app default display mode (`nova-display-mode` in apps.json).
   *
   * @param appid GameStream app id.
   * @return "virtual", "mirror" or empty.
   */
  std::string default_display_mode(int appid) {
    const auto index = app_index_for_id(appid);
    if (!index) {
      return {};
    }
    const auto tree = read_apps_file();
    if (!tree.contains("apps") || !tree["apps"].is_array() || *index >= tree["apps"].size()) {
      return {};
    }
    const auto &app = tree["apps"][*index];
    if (app.contains("nova-display-mode") && app["nova-display-mode"].is_string()) {
      return nova_api::parse_display_mode(app["nova-display-mode"].get<std::string>()).value_or(std::string {});
    }
    return {};
  }

  void follow_client_display(const rtsp_stream::launch_session_t &session, int appid, bool launching) {
    display_follow::request_t request;
    request.width = session.width;
    request.height = session.height;
    request.fps = session.fps;
    request.client_name = session.client_name;
    request.mode = session.display_mode.empty() ? default_display_mode(appid) : session.display_mode;

    bool legacy_prep = false;
    for (const auto &app : proc::proc.get_apps()) {
      if (app.id != std::to_string(appid)) {
        continue;
      }
      request.app_name = app.name;
      // On launch the app's own prep command still runs; on resume it does not.
      for (const auto &cmd : app.prep_cmds) {
        legacy_prep = legacy_prep || (launching && display_follow::is_legacy_prep(cmd.do_cmd, config::video.display_follow_cmd));
      }
      break;
    }
    display_follow::stream_requested(request, legacy_prep, rtsp_stream::session_count() > 0);
  }

  /**
   * @brief Create launch session.
   *
   * @param host_audio Host audio.
   * @param args Arguments forwarded to the callable or parser.
   * @param peer Verified client on the connection that asked for the session.
   * @return Constructed launch session object.
   */
  std::shared_ptr<rtsp_stream::launch_session_t> make_launch_session(bool host_audio, const args_t &args, const verified_peer_t &peer) {
    auto launch_session = std::make_shared<rtsp_stream::launch_session_t>();

    launch_session->id = ++session_id_counter;

    auto rikey = util::from_hex_vec(get_arg(args, "rikey"), true);
    std::copy(rikey.cbegin(), rikey.cend(), std::back_inserter(launch_session->gcm_key));

    launch_session->host_audio = host_audio;
    std::stringstream mode = std::stringstream(get_arg(args, "mode", "0x0x0"));
    // Split mode by the char "x", to populate width/height/fps
    int x = 0;
    std::string segment;
    while (std::getline(mode, segment, 'x')) {
      if (x == 0) {
        launch_session->width = atoi(segment.c_str());
      }
      if (x == 1) {
        launch_session->height = atoi(segment.c_str());
      }
      if (x == 2) {
        launch_session->fps = atoi(segment.c_str());
      }
      x++;
    }
    launch_session->unique_id = (get_arg(args, "uniqueid", "unknown"));
    launch_session->appid = (int) util::from_view(get_arg(args, "appid", "unknown"));
    launch_session->enable_sops = util::from_view(get_arg(args, "sops", "0"));
    launch_session->surround_info = (int) util::from_view(get_arg(args, "surroundAudioInfo", "196610"));
    launch_session->surround_params = (get_arg(args, "surroundParams", ""));
    launch_session->continuous_audio = util::from_view(get_arg(args, "continuousAudio", "0"));
    launch_session->gcmap = (int) util::from_view(get_arg(args, "gcmap", "0"));
    launch_session->enable_hdr = util::from_view(get_arg(args, "hdrMode", "0"));

    // Encrypted RTSP is enabled with client reported corever >= 1
    auto corever = util::from_view(get_arg(args, "corever", "0"));
    if (corever >= 1) {
      launch_session->rtsp_cipher = crypto::cipher::gcm_t {
        launch_session->gcm_key,
        false
      };
      launch_session->rtsp_iv_counter = 0;
    }
    launch_session->rtsp_url_scheme = launch_session->rtsp_cipher ? "rtspenc://"s : "rtsp://"s;
    launch_session->client_cert = peer.cert;
    launch_session->client_name = peer.name;
    launch_session->permissions = peer.cert.empty() ? client_permissions::view_only : get_client_permissions(peer.cert);
    if (const auto mode = nova_api::parse_display_mode(get_arg(args, "nova_display", ""))) {
      launch_session->display_mode = *mode;
    } else {
      launch_session->display_mode = default_display_mode(launch_session->appid);
    }

    // Generate the unique identifiers for this connection that we will send later during RTSP handshake
    unsigned char raw_payload[8];
    RAND_bytes(raw_payload, sizeof(raw_payload));
    launch_session->av_ping_payload = util::hex_vec(raw_payload);
    RAND_bytes((unsigned char *) &launch_session->control_connect_data, sizeof(launch_session->control_connect_data));

    launch_session->iv.resize(16);
    uint32_t prepend_iv = util::endian::big<uint32_t>((int) util::from_view(get_arg(args, "rikeyid")));
    auto prepend_iv_p = (uint8_t *) &prepend_iv;
    std::copy(prepend_iv_p, prepend_iv_p + sizeof(prepend_iv), std::begin(launch_session->iv));
    return launch_session;
  }

  /**
   * @brief Write a completed response to the client waiting for PIN approval.
   *
   * @param sess Pairing session that owns the waiting response.
   * @param tree XML response body to write.
   * @return `true` when a live response was available.
   */
  bool write_pairing_response(pair_session_t &sess, const pt::ptree &tree) {
    std::ostringstream data;
    pt::write_xml(data, tree);

    auto &response = sess.async_insert_pin.response;
    if (response.has_left() && response.left()) {
      response.left()->close_connection_after_response = true;
      response.left()->write(data.str());
    } else if (response.has_right() && response.right()) {
      response.right()->close_connection_after_response = true;
      response.right()->write(data.str());
    } else {
      return false;
    }

    response = std::monostate {};
    return true;
  }

  /**
   * @brief Publish the final result of a pairing handshake.
   *
   * @param sess Pairing session whose waiter should be notified.
   * @param success Whether the client completed the authenticated handshake.
   */
  void complete_pairing(pair_session_t &sess, const bool success) {
    {
      std::lock_guard lock {sess.completion->mutex};
      if (sess.completion->result.has_value()) {
        return;
      }
      sess.completion->result = success;
    }
    sess.completion->condition.notify_all();
  }

  /**
   * @brief Expire stale pairing sessions while the session mutex is held.
   *
   * @param now Monotonic time used to evaluate session deadlines.
   */
  void expire_pair_sessions_unlocked(const std::chrono::steady_clock::time_point now) {
    for (auto it = map_id_sess.begin(); it != map_id_sess.end();) {
      if (it->second.async_insert_pin.expires_at > now) {
        ++it;
        continue;
      }

      pt::ptree tree;
      tree.put("root.paired", 0);
      tree.put("root.<xmlattr>.status_code", 408);
      tree.put("root.<xmlattr>.status_message", "Pairing session expired");
      write_pairing_response(it->second, tree);
      complete_pairing(it->second, false);
      it = map_id_sess.erase(it);
    }
  }

  pair_session_insert_e insert_pair_session(pair_session_t sess, std::string &pairing_id) {
    std::scoped_lock lock {map_id_sess_mutex()};
    const auto now = std::chrono::steady_clock::now();
    expire_pair_sessions_unlocked(now);
    pairing_id.clear();

    if (map_id_sess.contains(sess.client.uniqueID)) {
      return pair_session_insert_e::ALREADY_EXISTS;
    }
    if (map_id_sess.size() >= MAX_PENDING_PAIRING_SESSIONS) {
      return pair_session_insert_e::FULL;
    }

    do {
      pairing_id = crypto::rand_alphabet(PAIRING_ID_SIZE, "0123456789abcdef"sv);
    } while (std::ranges::any_of(map_id_sess, [&](const auto &entry) {
      return entry.second.async_insert_pin.id == pairing_id;
    }));

    sess.async_insert_pin.id = pairing_id;
    sess.async_insert_pin.expires_at = now + PAIRING_SESSION_TIMEOUT;
    map_id_sess.emplace(sess.client.uniqueID, std::move(sess));
    return pair_session_insert_e::ADDED;
  }

  bool is_valid_pairing_id(const std::string_view pairing_id) {
    return pairing_id.size() == PAIRING_ID_SIZE && std::ranges::all_of(pairing_id, [](const char character) {
             return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') || (character >= 'A' && character <= 'F');
           });
  }

  bool is_valid_pairing_pin(const std::string_view pin) {
    return pin.size() == 4 && std::ranges::all_of(pin, [](const char character) {
             return character >= '0' && character <= '9';
           });
  }

  bool is_valid_pairing_name(const std::string_view name) {
    return !name.empty() && name.size() <= MAX_PAIRING_CLIENT_NAME_SIZE;
  }

  void expire_pair_sessions(const std::chrono::steady_clock::time_point now) {
    std::scoped_lock lock {map_id_sess_mutex()};
    expire_pair_sessions_unlocked(now);
  }

  std::vector<pending_pairing_t> get_pending_pairings() {
    std::scoped_lock lock {map_id_sess_mutex()};
    expire_pair_sessions_unlocked(std::chrono::steady_clock::now());

    std::vector<const pair_session_t *> pending_sessions;
    pending_sessions.reserve(map_id_sess.size());
    for (const auto &[_, sess] : map_id_sess) {
      if (sess.last_phase == PAIR_PHASE::NONE) {
        pending_sessions.push_back(&sess);
      }
    }
    std::ranges::sort(pending_sessions, {}, [](const pair_session_t *sess) {
      return sess->async_insert_pin.expires_at;
    });

    std::vector<pending_pairing_t> result;
    result.reserve(pending_sessions.size());
    for (const auto *sess : pending_sessions) {
      result.push_back({
        .id = sess->async_insert_pin.id,
        .name = sess->async_insert_pin.device_name,
        .address = sess->async_insert_pin.address,
      });
    }
    return result;
  }

  bool cancel_pairing(const std::string_view pairing_id) {
    if (!is_valid_pairing_id(pairing_id)) {
      return false;
    }

    std::scoped_lock lock {map_id_sess_mutex()};
    expire_pair_sessions_unlocked(std::chrono::steady_clock::now());

    const auto sess_it = std::ranges::find_if(map_id_sess, [&](const auto &entry) {
      return entry.second.last_phase == PAIR_PHASE::NONE && entry.second.async_insert_pin.id == pairing_id;
    });
    if (sess_it == map_id_sess.end()) {
      return false;
    }

    pt::ptree tree;
    tree.put("root.paired", 0);
    tree.put("root.<xmlattr>.status_code", 400);
    tree.put("root.<xmlattr>.status_message", "Pairing request cancelled by operator");
    write_pairing_response(sess_it->second, tree);
    complete_pairing(sess_it->second, false);
    map_id_sess.erase(sess_it);
    return true;
  }

  void remove_session(const pair_session_t &sess) {
    std::scoped_lock lock {map_id_sess_mutex()};
    if (const auto sess_it = map_id_sess.find(sess.client.uniqueID); sess_it != map_id_sess.end()) {
      complete_pairing(sess_it->second, false);
      map_id_sess.erase(sess_it);
    }
  }

  /**
   * @brief Return the GameStream pairing failure response.
   *
   * @param sess Pairing session that owns the request state.
   * @param tree XML property tree used for the response body.
   * @param status_msg Status msg.
   */
  void fail_pair(pair_session_t &sess, pt::ptree &tree, const std::string status_msg) {
    tree.put("root.paired", 0);
    tree.put("root.<xmlattr>.status_code", 400);
    tree.put("root.<xmlattr>.status_message", status_msg);
    sess.failed = true;
    complete_pairing(sess, false);
  }

  /**
   * @brief Return the server certificate text for pairing responses.
   *
   * @param sess Pairing session that owns the request state.
   * @param tree XML property tree used for the response body.
   * @param pin PIN supplied by the client during pairing.
   */
  void getservercert(pair_session_t &sess, pt::ptree &tree, const std::string &pin) {
    if (sess.last_phase != PAIR_PHASE::NONE) {
      fail_pair(sess, tree, "Out of order call to getservercert");
      return;
    }
    sess.last_phase = PAIR_PHASE::GETSERVERCERT;

    if (sess.async_insert_pin.salt.size() < 32) {
      fail_pair(sess, tree, "Salt too short");
      return;
    }

    std::string_view salt_view {sess.async_insert_pin.salt.data(), 32};

    auto salt = util::from_hex<std::array<uint8_t, 16>>(salt_view, true);

    auto key = crypto::gen_aes_key(salt, pin);
    sess.cipher_key = std::make_unique<crypto::aes_t>(key);

    tree.put("root.paired", 1);
    tree.put("root.plaincert", util::hex_vec(conf_intern.servercert, true));
    tree.put("root.<xmlattr>.status_code", 200);
  }

  /**
   * @brief Handle the client-challenge phase of GameStream pairing.
   *
   * @param sess Pairing session that owns the request state.
   * @param tree XML property tree used for the response body.
   * @param challenge Client challenge bytes from the pairing request.
   */
  void clientchallenge(pair_session_t &sess, pt::ptree &tree, const std::string &challenge) {
    if (sess.last_phase != PAIR_PHASE::GETSERVERCERT) {
      fail_pair(sess, tree, "Out of order call to clientchallenge");
      return;
    }
    sess.last_phase = PAIR_PHASE::CLIENTCHALLENGE;

    if (!sess.cipher_key) {
      fail_pair(sess, tree, "Cipher key not set");
      return;
    }
    crypto::cipher::ecb_t cipher(*sess.cipher_key, false);

    std::vector<uint8_t> decrypted;
    cipher.decrypt(challenge, decrypted);

    auto x509 = crypto::x509(conf_intern.servercert);
    auto sign = crypto::signature(x509);
    auto serversecret = crypto::rand(16);

    decrypted.insert(std::end(decrypted), std::begin(sign), std::end(sign));
    decrypted.insert(std::end(decrypted), std::begin(serversecret), std::end(serversecret));

    auto hash = crypto::hash({(char *) decrypted.data(), decrypted.size()});
    auto serverchallenge = crypto::rand(16);

    std::string plaintext;
    plaintext.reserve(hash.size() + serverchallenge.size());

    plaintext.insert(std::end(plaintext), std::begin(hash), std::end(hash));
    plaintext.insert(std::end(plaintext), std::begin(serverchallenge), std::end(serverchallenge));

    std::vector<uint8_t> encrypted;
    cipher.encrypt(plaintext, encrypted);

    sess.serversecret = std::move(serversecret);
    sess.serverchallenge = std::move(serverchallenge);

    tree.put("root.paired", 1);
    tree.put("root.challengeresponse", util::hex_vec(encrypted, true));
    tree.put("root.<xmlattr>.status_code", 200);
  }

  /**
   * @brief Handle the server-challenge response phase of GameStream pairing.
   *
   * @param sess Pairing session that owns the request state.
   * @param tree XML property tree used for the response body.
   * @param encrypted_response Encrypted response.
   */
  void serverchallengeresp(pair_session_t &sess, pt::ptree &tree, const std::string &encrypted_response) {
    if (sess.last_phase != PAIR_PHASE::CLIENTCHALLENGE) {
      fail_pair(sess, tree, "Out of order call to serverchallengeresp");
      return;
    }
    sess.last_phase = PAIR_PHASE::SERVERCHALLENGERESP;

    if (!sess.cipher_key || sess.serversecret.empty()) {
      fail_pair(sess, tree, "Cipher key or serversecret not set");
      return;
    }

    std::vector<uint8_t> decrypted;
    crypto::cipher::ecb_t cipher(*sess.cipher_key, false);

    cipher.decrypt(encrypted_response, decrypted);

    sess.clienthash = std::move(decrypted);

    auto serversecret = sess.serversecret;
    auto sign = crypto::sign256(crypto::pkey(conf_intern.pkey), serversecret);

    serversecret.insert(std::end(serversecret), std::begin(sign), std::end(sign));

    tree.put("root.pairingsecret", util::hex_vec(serversecret, true));
    tree.put("root.paired", 1);
    tree.put("root.<xmlattr>.status_code", 200);
  }

  /**
   * @brief Handle the client pairing-secret phase of GameStream pairing.
   *
   * @param sess Pairing session that owns the request state.
   * @param tree XML property tree used for the response body.
   * @param client_pairing_secret Client pairing secret.
   */
  void clientpairingsecret(pair_session_t &sess, pt::ptree &tree, const std::string &client_pairing_secret) {
    if (sess.last_phase != PAIR_PHASE::SERVERCHALLENGERESP) {
      fail_pair(sess, tree, "Out of order call to clientpairingsecret");
      return;
    }
    sess.last_phase = PAIR_PHASE::CLIENTPAIRINGSECRET;

    auto &client = sess.client;

    if (client_pairing_secret.size() <= 16) {
      fail_pair(sess, tree, "Client pairing secret too short");
      return;
    }

    std::string_view secret {client_pairing_secret.data(), 16};
    std::string_view sign {client_pairing_secret.data() + secret.size(), client_pairing_secret.size() - secret.size()};

    auto x509 = crypto::x509(client.cert);
    if (!x509) {
      fail_pair(sess, tree, "Invalid client certificate");
      return;
    }
    auto x509_sign = crypto::signature(x509);

    std::string data;
    data.reserve(sess.serverchallenge.size() + x509_sign.size() + secret.size());

    data.insert(std::end(data), std::begin(sess.serverchallenge), std::end(sess.serverchallenge));
    data.insert(std::end(data), std::begin(x509_sign), std::end(x509_sign));
    data.insert(std::end(data), std::begin(secret), std::end(secret));

    auto hash = crypto::hash(data);

    // if hash not correct, probably MITM
    bool same_hash = hash.size() == sess.clienthash.size() && std::equal(hash.begin(), hash.end(), sess.clienthash.begin());
    auto verify = crypto::verify256(crypto::x509(client.cert), secret, sign);
    bool paired = false;
    if (same_hash && verify) {
      // The client is now successfully paired and will be authorized to connect
      paired = !add_authorized_client(client.name, std::move(client.cert)).empty();
    }

    tree.put("root.paired", paired ? 1 : 0);
    tree.put("root.<xmlattr>.status_code", 200);
    complete_pairing(sess, paired);
  }

  template<class T>
  struct tunnel;

  /**
   * @brief HTTPS tunnel session used for encrypted client requests.
   */
  template<>
  struct tunnel<SunshineHTTPS> {
    static auto constexpr to_string = "HTTPS"sv;  ///< To string.
  };

  /**
   * @brief Plain HTTP server wrapper used for non-TLS endpoints.
   */
  template<>
  struct tunnel<SimpleWeb::HTTP> {
    static auto constexpr to_string = "NONE"sv;  ///< To string.
  };

  /**
   * @brief Write req details to the log.
   *
   * @param request HTTP request data from the client.
   */
  template<class T>
  void print_req(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request) {
    BOOST_LOG(debug) << "TUNNEL :: "sv << tunnel<T>::to_string;

    BOOST_LOG(debug) << "METHOD :: "sv << request->method;
    BOOST_LOG(debug) << "DESTINATION :: "sv << request->path;

    for (auto &[name, val] : request->header) {
      BOOST_LOG(debug) << name << " -- " << val;
    }

    BOOST_LOG(debug) << " [--] "sv;

    for (auto &[name, val] : request->parse_query_string()) {
      BOOST_LOG(debug) << name << " -- " << val;
    }

    BOOST_LOG(debug) << " [--] "sv;
  }

  /**
   * @brief Return a GameStream HTTP not-found response.
   *
   * @param response HTTP response object to populate.
   * @param request HTTP request data from the client.
   */
  template<class T>
  void not_found(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> response, std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request) {
    print_req<T>(request);

    pt::ptree tree;
    tree.put("root.<xmlattr>.status_code", 404);

    std::ostringstream data;

    pt::write_xml(data, tree);
    response->write(data.str());

    *response
      << "HTTP/1.1 404 NOT FOUND\r\n"
      << data.str();

    response->close_connection_after_response = true;
  }

  /**
   * @brief Start a GameStream pairing session for a `getservercert` request.
   *
   * @tparam T HTTP server transport type.
   * @param response HTTP response retained while Web UI PIN approval is pending.
   * @param request HTTP request containing the client pairing parameters.
   * @param args Parsed request query parameters.
   * @param tree XML property tree used for immediate responses.
   * @param unique_id Client-provided pairing-session identifier.
   * @return `true` when the response remains pending for Web UI PIN approval.
   */
  template<class T>
  bool start_pairing_session(
    const std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> &response,
    const std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> &request,
    const args_t &args,
    pt::ptree &tree,
    const std::string &unique_id
  ) {
    pair_session_t sess;
    sess.client.uniqueID = unique_id;
    sess.client.cert = util::from_hex_vec(get_arg(args, "clientcert"), true);
    sess.async_insert_pin.salt = get_arg(args, "salt");
    sess.async_insert_pin.device_name = get_arg(args, "devicename");
    sess.async_insert_pin.address = net::addr_to_normalized_string(request->remote_endpoint().address());

    BOOST_LOG(debug) << sess.client.cert;
    const bool pin_stdin = config::sunshine.flags[config::flag::PIN_STDIN];
    if (!pin_stdin) {
      sess.async_insert_pin.response = response;
    }

    std::string pairing_id;
    switch (insert_pair_session(std::move(sess), pairing_id)) {
      using enum pair_session_insert_e;

      case ALREADY_EXISTS:
        tree.put("root.paired", 0);
        tree.put("root.<xmlattr>.status_code", 409);
        tree.put("root.<xmlattr>.status_message", "A pairing session with this uniqueid already exists");
        return false;
      case FULL:
        tree.put("root.paired", 0);
        tree.put("root.<xmlattr>.status_code", 503);
        tree.put("root.<xmlattr>.status_message", "Too many pending pairing sessions");
        return false;
      case ADDED:
        break;
    }

    if (!pin_stdin) {
#if defined SUNSHINE_TRAY && SUNSHINE_TRAY >= 1
      system_tray::update_tray_require_pin();
#endif
      return true;
    }

    std::string pin;
    std::cout << "Please insert pin: "sv;
    std::getline(std::cin, pin);

    std::scoped_lock lock {map_id_sess_mutex()};
    expire_pair_sessions_unlocked(std::chrono::steady_clock::now());
    const auto sess_it = map_id_sess.find(unique_id);
    if (sess_it == map_id_sess.end() || sess_it->second.async_insert_pin.id != pairing_id) {
      tree.put("root.paired", 0);
      tree.put("root.<xmlattr>.status_code", 408);
      tree.put("root.<xmlattr>.status_message", "Pairing session expired");
      return false;
    }

    getservercert(sess_it->second, tree, pin);
    if (sess_it->second.failed) {
      map_id_sess.erase(sess_it);
    }
    return false;
  }

  /**
   * @brief Dispatch the top-level GameStream pairing request by phase.
   *
   * @param response HTTP response object to populate.
   * @param request HTTP request data from the client.
   */
  template<class T>
  void pair(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> response, std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request) {
    print_req<T>(request);

    pt::ptree tree;

    auto fg = util::fail_guard([&]() {
      std::ostringstream data;

      pt::write_xml(data, tree);
      response->write(data.str());
      response->close_connection_after_response = true;
    });

    auto args = request->parse_query_string();
    if (args.find("uniqueid"s) == std::end(args)) {
      tree.put("root.<xmlattr>.status_code", 400);
      tree.put("root.<xmlattr>.status_message", "Missing uniqueid parameter");

      return;
    }

    auto uniqID {get_arg(args, "uniqueid")};

    const auto phrase_it = args.find("phrase");
    if (phrase_it != std::end(args) && phrase_it->second == "getservercert"sv) {
      if (start_pairing_session<T>(response, request, args, tree, uniqID)) {
        fg.disable();
      }
      return;
    }
    if (phrase_it != std::end(args) && phrase_it->second == "pairchallenge"sv) {
      tree.put("root.paired", 1);
      tree.put("root.<xmlattr>.status_code", 200);
      return;
    }

    std::scoped_lock lock {map_id_sess_mutex()};
    expire_pair_sessions_unlocked(std::chrono::steady_clock::now());
    auto sess_it = map_id_sess.find(uniqID);
    if (sess_it == std::end(map_id_sess)) {
      tree.put("root.<xmlattr>.status_code", 400);
      tree.put("root.<xmlattr>.status_message", "Invalid uniqueid");

      return;
    }

    bool pairing_complete = false;
    args_t::const_iterator it;
    if (it = args.find("clientchallenge"); it != std::end(args)) {
      auto challenge = util::from_hex_vec(it->second, true);
      clientchallenge(sess_it->second, tree, challenge);
    } else if (it = args.find("serverchallengeresp"); it != std::end(args)) {
      auto encrypted_response = util::from_hex_vec(it->second, true);
      serverchallengeresp(sess_it->second, tree, encrypted_response);
    } else if (it = args.find("clientpairingsecret"); it != std::end(args)) {
      auto pairingsecret = util::from_hex_vec(it->second, true);
      clientpairingsecret(sess_it->second, tree, pairingsecret);
      pairing_complete = true;
    } else {
      fail_pair(sess_it->second, tree, "Invalid pairing request");
    }

    if (pairing_complete || sess_it->second.failed) {
      map_id_sess.erase(sess_it);
    }
  }

  bool pin(const std::string_view pairing_id, std::string pin, std::string name) {
    if (!is_valid_pairing_id(pairing_id) || !is_valid_pairing_pin(pin) || !is_valid_pairing_name(name)) {
      return false;
    }

    std::shared_ptr<pairing_completion_t> completion;
    auto completion_deadline = std::chrono::steady_clock::time_point::min();
    {
      std::scoped_lock lock {map_id_sess_mutex()};
      const auto now = std::chrono::steady_clock::now();
      expire_pair_sessions_unlocked(now);
      const auto sess_it = std::ranges::find_if(map_id_sess, [&](const auto &entry) {
        return entry.second.last_phase == PAIR_PHASE::NONE && entry.second.async_insert_pin.id == pairing_id;
      });
      if (sess_it == map_id_sess.end()) {
        return false;
      }

      auto &sess = sess_it->second;
      pt::ptree tree;
      getservercert(sess, tree, pin);
      if (!sess.failed) {
        sess.client.name = std::move(name);
      }

      if (!write_pairing_response(sess, tree) || sess.failed) {
        complete_pairing(sess, false);
        map_id_sess.erase(sess_it);
        return false;
      }

      completion = sess.completion;
      completion_deadline = std::min(sess.async_insert_pin.expires_at, now + config::stream.ping_timeout);
    }

    {
      std::unique_lock lock {completion->mutex};
      if (completion->condition.wait_until(lock, completion_deadline, [&completion]() {
            return completion->result.has_value();
          })) {
        return *completion->result;
      }
    }

    std::scoped_lock lock {map_id_sess_mutex()};
    if (const auto sess_it = std::ranges::find_if(map_id_sess, [&](const auto &entry) {
          return entry.second.async_insert_pin.id == pairing_id && entry.second.completion == completion;
        });
        sess_it != map_id_sess.end()) {
      complete_pairing(sess_it->second, false);
      map_id_sess.erase(sess_it);
    }
    return false;
  }

  /**
   * @brief Get codec mode flags.
   *
   * @return Moonlight codec capability bitmask for the currently probed encoders.
   */
  uint32_t get_codec_mode_flags() {
    uint32_t codec_mode_flags = SCM_H264;
    if (video::last_encoder_probe_supported_yuv444_for_codec[0]) {
      codec_mode_flags |= SCM_H264_HIGH8_444;
    }
    if (video::active_hevc_mode >= 2) {
      codec_mode_flags |= SCM_HEVC;
      if (video::last_encoder_probe_supported_yuv444_for_codec[1]) {
        codec_mode_flags |= SCM_HEVC_REXT8_444;
      }
    }
    if (video::active_hevc_mode == 3 || video::active_hevc_mode == 5) {
      codec_mode_flags |= SCM_HEVC_MAIN10;
    }
    if ((video::active_hevc_mode == 4 || video::active_hevc_mode == 5) && video::last_encoder_probe_supported_yuv444_for_codec[1]) {
      codec_mode_flags |= SCM_HEVC_REXT10_444;
    }

    if (video::active_av1_mode >= 2) {
      codec_mode_flags |= SCM_AV1_MAIN8;
      if (video::last_encoder_probe_supported_yuv444_for_codec[2]) {
        codec_mode_flags |= SCM_AV1_HIGH8_444;
      }
    }
    if (video::active_av1_mode == 3 || video::active_av1_mode == 5) {
      codec_mode_flags |= SCM_AV1_MAIN10;
    }
    if ((video::active_av1_mode == 4 || video::active_av1_mode == 5) && video::last_encoder_probe_supported_yuv444_for_codec[2]) {
      codec_mode_flags |= SCM_AV1_HIGH10_444;
    }
    return codec_mode_flags;
  }

  /**
   * @brief Build the GameStream server-info response.
   *
   * @param response HTTP response object to populate.
   * @param request HTTP request data from the client.
   */
  template<class T>
  void serverinfo(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> response, std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request) {
    print_req<T>(request);

    int pair_status = 0;
    if constexpr (std::is_same_v<SunshineHTTPS, T>) {
      auto args = request->parse_query_string();
      auto clientID = args.find("uniqueid"s);

      if (clientID != std::end(args)) {
        pair_status = 1;
      }
    }

    auto local_endpoint = request->local_endpoint();

    pt::ptree tree;

    tree.put("root.<xmlattr>.status_code", 200);
    tree.put("root.hostname", config::nvhttp.sunshine_name);

    tree.put("root.appversion", VERSION);
    tree.put("root.GfeVersion", GFE_VERSION);
    tree.put("root.uniqueid", http::unique_id);
    tree.put("root.HttpsPort", net::map_port(PORT_HTTPS));
    tree.put("root.ExternalPort", net::map_port(PORT_HTTP));
    tree.put("root.MaxLumaPixelsHEVC", video::active_hevc_mode > 1 ? "1869449984" : "0");

    // Only include the MAC address for requests sent from paired clients over HTTPS.
    // For HTTP requests, use a placeholder MAC address that Moonlight knows to ignore.
    if constexpr (std::is_same_v<SunshineHTTPS, T>) {
      tree.put("root.mac", platf::get_mac_address(net::addr_to_normalized_string(local_endpoint.address())));
    } else {
      tree.put("root.mac", "00:00:00:00:00:00");
    }

    // Moonlight clients track LAN IPv6 addresses separately from LocalIP which is expected to
    // always be an IPv4 address. If we return that same IPv6 address here, it will clobber the
    // stored LAN IPv4 address. To avoid this, we need to return an IPv4 address in this field
    // when we get a request over IPv6.
    //
    // HACK: We should return the IPv4 address of local interface here, but we don't currently
    // have that implemented. For now, we will emulate the behavior of GFE+GS-IPv6-Forwarder,
    // which returns 127.0.0.1 as LocalIP for IPv6 connections. Moonlight clients with IPv6
    // support know to ignore this bogus address.
    if (local_endpoint.address().is_v6() && !local_endpoint.address().to_v6().is_v4_mapped()) {
      tree.put("root.LocalIP", "127.0.0.1");
    } else {
      tree.put("root.LocalIP", net::addr_to_normalized_string(local_endpoint.address()));
    }

    const uint32_t codec_mode_flags = get_codec_mode_flags();
    tree.put("root.ServerCodecModeSupport", codec_mode_flags);

    if (!config::nvhttp.external_ip.empty()) {
      tree.put("root.ExternalIP", config::nvhttp.external_ip);
    }

    auto current_appid = proc::proc.running();
    tree.put("root.PairStatus", pair_status);
    tree.put("root.currentgame", current_appid);
    tree.put("root.state", current_appid > 0 ? "SUNSHINE_SERVER_BUSY" : "SUNSHINE_SERVER_FREE");

    std::ostringstream data;

    pt::write_xml(data, tree);
    response->write(data.str());
    response->close_connection_after_response = true;
  }

  nlohmann::json get_all_clients() {
    std::vector<named_cert_t> clients;
    {
      std::lock_guard lock {client_auth_mutex()};
      clients = client_root.named_devices;
    }

    // Session lookups take the RTSP session lock, so they run after the client lock is released.
    nlohmann::json named_cert_nodes = nlohmann::json::array();
    for (const auto &named_cert : clients) {
      nlohmann::json named_cert_node;
      named_cert_node["name"] = named_cert.name;
      named_cert_node["uuid"] = named_cert.uuid;
      named_cert_node["enabled"] = named_cert.enabled;
      named_cert_node["permissions"] = client_permissions::to_json(named_cert.permissions);
      named_cert_node["paired_at"] = named_cert.paired_at > 0 ? nlohmann::json(named_cert.paired_at) : nlohmann::json(nullptr);
      named_cert_node["last_connected_at"] = named_cert.last_connected_at > 0 ? nlohmann::json(named_cert.last_connected_at) : nlohmann::json(nullptr);
      named_cert_node["connected"] = rtsp_stream::has_session_for_cert(named_cert.cert);
      named_cert_nodes.push_back(named_cert_node);
    }

    return named_cert_nodes;
  }

  /**
   * @brief Build the GameStream application list response.
   *
   * @param response HTTP response object to populate.
   * @param request HTTP request data from the client.
   */
  void applist(resp_https_t response, req_https_t request) {
    print_req<SunshineHTTPS>(request);

    pt::ptree tree;

    auto g = util::fail_guard([&]() {
      std::ostringstream data;

      pt::write_xml(data, tree);
      response->write(data.str());
      response->close_connection_after_response = true;
    });

    auto &apps = tree.add_child("root", pt::ptree {});

    apps.put("<xmlattr>.status_code", 200);

    // Without launch permission a client only sees the running app, which it may resume.
    const bool can_launch = client_permissions::has(permissions_for_request(request), client_permissions::launch_apps);
    const auto running_appid = proc::proc.running();

    for (auto &proc : proc::proc.get_apps()) {
      if (!can_launch && util::from_view(proc.id) != running_appid) {
        continue;
      }
      pt::ptree app;

      app.put("IsHdrSupported"s, video::active_hevc_mode >= 3 ? 1 : 0);
      app.put("AppTitle"s, proc.name);
      app.put("ID", proc.id);

      apps.push_back(std::make_pair("App", std::move(app)));
    }
  }

  /**
   * @brief Launch the requested application for a GameStream session.
   *
   * @param host_audio Host audio.
   * @param response HTTP response object to populate.
   * @param request HTTP request data from the client.
   */
  void launch(bool &host_audio, resp_https_t response, req_https_t request) {
    print_req<SunshineHTTPS>(request);

    pt::ptree tree;
    bool revert_display_configuration {false};
    auto g = util::fail_guard([&]() {
      std::ostringstream data;

      if (tree.empty()) {
        BOOST_LOG(error) << EMPTY_PROPERTY_TREE_ERROR_MSG;
      }

      pt::write_xml(data, tree);
      response->write(data.str());
      response->close_connection_after_response = true;

      if (revert_display_configuration) {
        display_device::revert_configuration();
      }
    });

    auto args = request->parse_query_string();
    if (
      args.find("rikey"s) == std::end(args) ||
      args.find("rikeyid"s) == std::end(args) ||
      args.find("localAudioPlayMode"s) == std::end(args) ||
      args.find("appid"s) == std::end(args)
    ) {
      tree.put("root.resume", 0);
      tree.put("root.<xmlattr>.status_code", 400);
      tree.put("root.<xmlattr>.status_message", "Missing a required launch parameter");

      return;
    }

    if (!client_permissions::has(permissions_for_request(request), client_permissions::launch_apps)) {
      BOOST_LOG(info) << "Refusing launch: this client isn't allowed to launch apps"sv;
      tree.put("root.resume", 0);
      tree.put("root.<xmlattr>.status_code", 403);
      tree.put("root.<xmlattr>.status_message", "This device isn't allowed to launch apps on this host");

      return;
    }

    auto appid = util::from_view(get_arg(args, "appid"));

    auto current_appid = proc::proc.running();
    if (current_appid > 0) {
      tree.put("root.resume", 0);
      tree.put("root.<xmlattr>.status_code", 400);
      tree.put("root.<xmlattr>.status_message", "An app is already running on this host");

      return;
    }

    host_audio = util::from_view(get_arg(args, "localAudioPlayMode"));
    auto launch_session = make_launch_session(host_audio, args, verified_peer_for(request));

    // Nova: switch the display to this client's mode before the probe and prep commands see it.
    follow_client_display(*launch_session, (int) appid, true);

    bool probe_found_nothing {false};  ///< The pre-prep probe chose no encoder at all, so the re-probe is the last word.

    if (rtsp_stream::session_count() == 0) {
      // The display should be restored in case something fails as there are no other sessions.
      revert_display_configuration = true;

      // We want to prepare display only if there are no active sessions at
      // the moment. This should be done before probing encoders as it could
      // change the active displays.
      display_device::configure_display(config::video, *launch_session);

      // Probe encoders again before streaming to ensure our chosen
      // encoder matches the active GPU (which could have changed
      // due to hotplugging, driver crash, primary monitor change,
      // or any number of other factors).
      //
      // A probe that came up empty says one of two things, and only one of them
      // is fatal here. If the GPU cannot encode, nothing later in this function
      // changes that. But if it merely had nothing to capture, the display it
      // was looking for is the one this app's prep commands are about to create,
      // and answering 503 now guarantees it never will be: proc::execute lives
      // past this return. Headless can only ever fail this way — its display
      // does not exist until the app it belongs to launches — so leave the
      // verdict to the re-probe below rather than deciding it a step too early.
      if (video::probe_encoders()) {
        if (!video::probe_missed_display()) {
          tree.put("root.<xmlattr>.status_code", 503);
          tree.put("root.<xmlattr>.status_message", "Failed to initialize video capture/encoding. Is a display connected and turned on?");
          tree.put("root.gamesession", 0);

          return;
        }

        probe_found_nothing = true;
      }
    }

    auto encryption_mode = net::encryption_mode_for_address(request->remote_endpoint().address());
    if (!launch_session->rtsp_cipher && encryption_mode == config::ENCRYPTION_MODE_MANDATORY) {
      BOOST_LOG(error) << "Rejecting client that cannot comply with mandatory encryption requirement"sv;

      tree.put("root.<xmlattr>.status_code", 403);
      tree.put("root.<xmlattr>.status_message", "Encryption is mandatory for this host but unsupported by the client");
      tree.put("root.gamesession", 0);

      return;
    }

    if (appid > 0) {
      auto err = proc::proc.execute((int) appid, launch_session);
      if (err) {
        tree.put("root.<xmlattr>.status_code", err);
        const auto &reason = proc::proc.last_error();
        tree.put("root.<xmlattr>.status_message", reason.empty() ? "Failed to start the specified application" : reason);
        tree.put("root.gamesession", 0);

        return;
      }

      // The probe above ran before this app's prep commands, and on Nova those
      // are what create the virtual display. A host whose only display is that
      // VDD therefore had nothing to capture a moment ago and settled for
      // software encoding — not because the GPU cannot encode, but because it was
      // asked at the one moment the display deliberately did not exist yet.
      //
      // The prep command has now run and waited for the connector to light, so
      // ask again. Only when the first probe actually came up empty: a host with
      // a monitor attached has already answered the question, and re-probing it
      // would cost a second of capture setup for nothing.
      if (video::probe_missed_display()) {
        BOOST_LOG(info) << "No display existed when encoders were probed; re-probing now that the app's display is up"sv;
        if (video::probe_encoders()) {
          if (probe_found_nothing) {
            // The first probe had no encoder to fall back on, and the display
            // the prep command was supposed to bring up did not answer either.
            // Now there is genuinely nothing to stream, and the app we started
            // a moment ago has no session to belong to, so take it back down
            // before reporting the failure the earlier return deferred.
            BOOST_LOG(error) << "No usable encoder even after the app's display came up"sv;
            proc::proc.terminate();

            tree.put("root.<xmlattr>.status_code", 503);
            tree.put("root.<xmlattr>.status_message", "Failed to initialize video capture/encoding. Is a display connected and turned on?");
            tree.put("root.gamesession", 0);

            return;
          }

          BOOST_LOG(warning) << "Re-probe still found no usable encoder; continuing with the earlier choice"sv;
        }
      }
    }

    tree.put("root.<xmlattr>.status_code", 200);
    tree.put(
      "root.sessionUrl0",
      std::format(
        "{}{}:{}",
        launch_session->rtsp_url_scheme,
        net::addr_to_url_escaped_string(request->local_endpoint().address()),
        static_cast<int>(net::map_port(rtsp_stream::RTSP_SETUP_PORT))
      )
    );
    tree.put("root.gamesession", 1);

    record_client_connected(launch_session->client_cert);
    rtsp_stream::launch_session_raise(launch_session);

    // Stream was started successfully, we will revert the config when the app or session terminates
    revert_display_configuration = false;
  }

  /**
   * @brief Resume an existing GameStream session.
   *
   * @param host_audio Host audio.
   * @param response HTTP response object to populate.
   * @param request HTTP request data from the client.
   */
  void resume(bool &host_audio, resp_https_t response, req_https_t request) {
    print_req<SunshineHTTPS>(request);

    pt::ptree tree;
    auto g = util::fail_guard([&]() {
      std::ostringstream data;

      if (tree.empty()) {
        BOOST_LOG(error) << EMPTY_PROPERTY_TREE_ERROR_MSG;
      }

      pt::write_xml(data, tree);
      response->write(data.str());
      response->close_connection_after_response = true;
    });

    auto current_appid = proc::proc.running();
    if (current_appid == 0) {
      tree.put("root.resume", 0);
      tree.put("root.<xmlattr>.status_code", 503);
      tree.put("root.<xmlattr>.status_message", "No running app to resume");

      return;
    }

    auto args = request->parse_query_string();
    if (
      args.find("rikey"s) == std::end(args) ||
      args.find("rikeyid"s) == std::end(args)
    ) {
      tree.put("root.resume", 0);
      tree.put("root.<xmlattr>.status_code", 400);
      tree.put("root.<xmlattr>.status_message", "Missing a required resume parameter");

      return;
    }

    // Newer Moonlight clients send localAudioPlayMode on /resume too,
    // so we should use it if it's present in the args and there are
    // no active sessions we could be interfering with.
    const bool no_active_sessions {rtsp_stream::session_count() == 0};
    if (no_active_sessions && args.find("localAudioPlayMode"s) != std::end(args)) {
      host_audio = util::from_view(get_arg(args, "localAudioPlayMode"));
    }
    const auto launch_session = make_launch_session(host_audio, args, verified_peer_for(request));

    // Nova: a resumed app's prep commands don't run again, so follow this client's mode here.
    follow_client_display(*launch_session, proc::proc.running(), false);

    if (no_active_sessions) {
      // We want to prepare display only if there are no active sessions at
      // the moment. This should be done before probing encoders as it could
      // change the active displays.
      display_device::configure_display(config::video, *launch_session);

      // Probe encoders again before streaming to ensure our chosen
      // encoder matches the active GPU (which could have changed
      // due to hotplugging, driver crash, primary monitor change,
      // or any number of other factors).
      if (video::probe_encoders()) {
        tree.put("root.resume", 0);
        tree.put("root.<xmlattr>.status_code", 503);
        tree.put("root.<xmlattr>.status_message", "Failed to initialize video capture/encoding. Is a display connected and turned on?");

        return;
      }
    }

    auto encryption_mode = net::encryption_mode_for_address(request->remote_endpoint().address());
    if (!launch_session->rtsp_cipher && encryption_mode == config::ENCRYPTION_MODE_MANDATORY) {
      BOOST_LOG(error) << "Rejecting client that cannot comply with mandatory encryption requirement"sv;

      tree.put("root.<xmlattr>.status_code", 403);
      tree.put("root.<xmlattr>.status_message", "Encryption is mandatory for this host but unsupported by the client");
      tree.put("root.gamesession", 0);

      return;
    }

    tree.put("root.<xmlattr>.status_code", 200);
    tree.put(
      "root.sessionUrl0",
      std::format(
        "{}{}:{}",
        launch_session->rtsp_url_scheme,
        net::addr_to_url_escaped_string(request->local_endpoint().address()),
        static_cast<int>(net::map_port(rtsp_stream::RTSP_SETUP_PORT))
      )
    );
    tree.put("root.resume", 1);

    record_client_connected(launch_session->client_cert);
    rtsp_stream::launch_session_raise(launch_session);
  }

  /**
   * @brief Check whether cel.
   *
   * @param response HTTP response object to populate.
   * @param request HTTP request data from the client.
   */
  void cancel(resp_https_t response, req_https_t request) {
    print_req<SunshineHTTPS>(request);

    pt::ptree tree;
    auto g = util::fail_guard([&]() {
      std::ostringstream data;

      pt::write_xml(data, tree);
      response->write(data.str());
      response->close_connection_after_response = true;
    });

    tree.put("root.cancel", 1);
    tree.put("root.<xmlattr>.status_code", 200);

    rtsp_stream::terminate_sessions();

    if (proc::proc.running() > 0) {
      proc::proc.terminate();
    }

    // The config needs to be reverted regardless of whether "proc::proc.terminate()" was called or not.
    display_device::revert_configuration();
  }

  /**
   * @brief Return an application asset requested by the client.
   *
   * @param response HTTP response object to populate.
   * @param request HTTP request data from the client.
   */
  void appasset(resp_https_t response, req_https_t request) {
    print_req<SunshineHTTPS>(request);

    auto args = request->parse_query_string();
    auto app_image = proc::proc.get_app_image((int) util::from_view(get_arg(args, "appid")));

    std::ifstream in(app_image, std::ios::binary);
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "image/png");
    response->write(SimpleWeb::StatusCode::success_ok, in, headers);
    response->close_connection_after_response = true;
  }

  /**
   * @brief Serve and consume an out-of-band clipboard blob (kind=3 refs).
   * @param response HTTP response object.
   * @param request HTTP request data from the client.
   */
  /**
   * @brief Refuse a clipboard request from a client without the clipboard permission.
   *
   * @param response HTTP response object to populate when refusing.
   * @param request The request, used to identify the client's connection.
   * @return `true` when the request was refused and answered.
   */
  bool refuse_without_clipboard_permission(const resp_https_t &response, const req_https_t &request) {
    if (client_permissions::has(permissions_for_request(request), client_permissions::clipboard)) {
      return false;
    }
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "application/json");
    response->write(SimpleWeb::StatusCode::client_error_forbidden, R"({"error":"clipboard access is not allowed for this device"})", headers);
    response->close_connection_after_response = true;
    return true;
  }

  void clipboard_blob_get(resp_https_t response, req_https_t request) {
    print_req<SunshineHTTPS>(request);
    if (refuse_without_clipboard_permission(response, request)) {
      return;
    }

    auto blob = clipboard::blob::take(request->path_match[1]);
    if (!blob) {
      response->write(SimpleWeb::StatusCode::client_error_not_found);
      response->close_connection_after_response = true;
      return;
    }
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", blob->first);
    response->write(SimpleWeb::StatusCode::success_ok, std::string(blob->second.begin(), blob->second.end()), headers);
    response->close_connection_after_response = true;
  }

  /**
   * @brief Accept a client-uploaded clipboard blob; replies with its id.
   * @param response HTTP response object.
   * @param request HTTP request data from the client.
   */
  void clipboard_blob_post(resp_https_t response, req_https_t request) {
    print_req<SunshineHTTPS>(request);
    if (refuse_without_clipboard_permission(response, request)) {
      return;
    }

    auto content = request->content.string();
    auto type = request->header.find("content-type");
    auto mime = type != request->header.end() ? type->second : std::string("application/octet-stream");

    auto id = clipboard::blob::put(mime, std::vector<std::uint8_t>(content.begin(), content.end()));
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "application/json");
    if (id.empty()) {
      response->write(SimpleWeb::StatusCode::client_error_payload_too_large, R"({"error":"blob store full or payload too large"})", headers);
    } else {
      response->write(SimpleWeb::StatusCode::success_ok, "{\"id\":\"" + id + "\"}", headers);
    }
    response->close_connection_after_response = true;
  }

  // ---------------------------------------------------------------------------
  // Nova client API (/nova/v1/*, /bitrate): paired devices, authenticated by client certificate.
  // ---------------------------------------------------------------------------

  /**
   * @brief Write a JSON reply and close the connection.
   *
   * @param response HTTPS response.
   * @param code HTTP status.
   * @param body JSON body.
   */
  void nova_json(const resp_https_t &response, SimpleWeb::StatusCode code, const nlohmann::json &body) {
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "application/json");
    headers.emplace("Cache-Control", "no-store");
    response->write(code, body.dump(), headers);
    response->close_connection_after_response = true;
  }

  /**
   * @brief The verified device on this connection, or a 401 reply when there is none.
   *
   * @param response HTTPS response (written on failure).
   * @param request HTTPS request.
   * @return The device, or nullopt after replying 401.
   */
  std::optional<verified_peer_t> nova_require_device(const resp_https_t &response, const req_https_t &request) {
    auto peer = verified_peer_for(request);
    if (peer.cert.empty()) {
      nova_json(response, SimpleWeb::StatusCode::client_error_unauthorized, {{"error", "this device isn't paired"}});
      return std::nullopt;
    }
    return peer;
  }

  /**
   * @brief Index of the running app in apps.json order.
   *
   * @return Index, or nullopt when nothing runs.
   */
  std::optional<std::size_t> nova_running_index() {
    if (const auto running_id = proc::proc.running(); running_id > 0) {
      return app_index_for_id(running_id);
    }
    return std::nullopt;
  }

  /**
   * @brief An app visible to the device on this connection, looked up by its Nova id.
   *
   * Devices without launch permission only see the running app.
   *
   * @param request HTTPS request.
   * @param id Nova app id.
   * @return The app object, or nullopt.
   */
  std::optional<nlohmann::json> nova_find_app(const req_https_t &request, const std::string &id) {
    const auto tree = read_apps_file();
    if (!tree.contains("apps") || !tree["apps"].is_array()) {
      return std::nullopt;
    }
    const bool can_launch = client_permissions::has(permissions_for_request(request), client_permissions::launch_apps);
    const auto running = nova_running_index();
    for (std::size_t i = 0; i < tree["apps"].size(); ++i) {
      if (nova_api::app_id(tree["apps"][i]) == id) {
        if (!can_launch && (!running || *running != i)) {
          return std::nullopt;
        }
        return tree["apps"][i];
      }
    }
    return std::nullopt;
  }

  /**
   * @brief Send an image file with caching headers (ETag from size + mtime, 304 on match).
   *
   * @param response HTTPS response.
   * @param request HTTPS request.
   * @param file Image file.
   */
  void nova_send_image(const resp_https_t &response, const req_https_t &request, const std::filesystem::path &file) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec) {
      nova_json(response, SimpleWeb::StatusCode::client_error_not_found, {{"error", "artwork not found"}});
      return;
    }
    const auto mtime = std::filesystem::last_write_time(file, ec).time_since_epoch().count();
    const auto etag = std::format("\"{:x}-{:x}\"", size, static_cast<std::uint64_t>(mtime));
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("ETag", etag);
    headers.emplace("Cache-Control", "private, max-age=86400");
    if (const auto it = request->header.find("If-None-Match"); it != request->header.end() && it->second == etag) {
      response->write(SimpleWeb::StatusCode::redirection_not_modified, headers);
      response->close_connection_after_response = true;
      return;
    }
    auto ext = file.extension().string();
    std::ranges::transform(ext, ext.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    headers.emplace("Content-Type", ext == ".png" ? "image/png" : ext == ".webp" ? "image/webp" : "image/jpeg");
    response->write(SimpleWeb::StatusCode::success_ok, file_handler::read_file(file.string().c_str()), headers);
    response->close_connection_after_response = true;
  }

  /**
   * @brief GET /nova/v1/capabilities: tells a client this host speaks the Nova API.
   *
   * @param response HTTPS response.
   * @param request HTTPS request.
   */
  void nova_capabilities(resp_https_t response, req_https_t request) {
    print_req<SunshineHTTPS>(request);
    if (!nova_require_device(response, request)) {
      return;
    }
    nova_json(response, SimpleWeb::StatusCode::success_ok, {{"nova", true}, {"version", PROJECT_VERSION}, {"features", {"apps", "art", "details", "display_mode", "bitrate", "sessions"}}});
  }

  /**
   * @brief GET /nova/v1/apps: the device's app list with artwork flags, playtime and display-mode defaults.
   *
   * @param response HTTPS response.
   * @param request HTTPS request.
   */
  void nova_apps(resp_https_t response, req_https_t request) {
    print_req<SunshineHTTPS>(request);
    if (!nova_require_device(response, request)) {
      return;
    }
    const auto tree = read_apps_file();
    std::vector<std::string> ids;
    for (const auto &app : proc::proc.get_apps()) {
      ids.push_back(app.id);
    }
    const bool can_launch = client_permissions::has(permissions_for_request(request), client_permissions::launch_apps);
    const auto apps = nova_api::apps_list(tree.contains("apps") ? tree["apps"] : nlohmann::json::array(), ids, nova_running_index(), can_launch, nova_api::stats(), platf::appdata() / "covers" / "library");
    nova_json(response, SimpleWeb::StatusCode::success_ok, apps);
  }

  /**
   * @brief GET /nova/v1/apps/<id>/art/<poster|hero|logo|icon|background>: one piece of an app's artwork.
   *
   * A background that was never set falls back to the hero.
   *
   * @param response HTTPS response.
   * @param request HTTPS request.
   */
  void nova_app_art(resp_https_t response, req_https_t request) {
    print_req<SunshineHTTPS>(request);
    if (!nova_require_device(response, request)) {
      return;
    }
    const auto app = nova_find_app(request, request->path_match[1].str());
    const auto kind = library::parse_kind(request->path_match[2].str());
    const auto covers = platf::appdata() / "covers" / "library";
    auto file = app && kind ? library::app_art(*app, *kind, covers) : std::nullopt;
    if (!file && app && kind == library::art_kind_e::background) {
      file = library::app_art(*app, library::art_kind_e::hero, covers);
    }
    if (!file) {
      nova_json(response, SimpleWeb::StatusCode::client_error_not_found, {{"error", "artwork not found"}});
      return;
    }
    nova_send_image(response, request, *file);
  }

  /**
   * @brief GET /nova/v1/apps/<id>/details: store details (fetched once, cached) plus playtime.
   *
   * @param response HTTPS response.
   * @param request HTTPS request.
   */
  void nova_app_details(resp_https_t response, req_https_t request) {
    print_req<SunshineHTTPS>(request);
    if (!nova_require_device(response, request)) {
      return;
    }
    const auto id = request->path_match[1].str();
    const auto app = nova_find_app(request, id);
    if (!app) {
      nova_json(response, SimpleWeb::StatusCode::client_error_not_found, {{"error", "app not found"}});
      return;
    }
    const auto all = nova_api::stats();
    std::optional<nova_api::app_stats_t> stats;
    if (const auto it = all.find(app->value("name", std::string {})); it != all.end()) {
      stats = it->second;
    }
    nova_json(response, SimpleWeb::StatusCode::success_ok, nova_api::details_reply(id, nova_api::store_details(*app, library::metadata::from_config().auto_fetch), stats));
  }

  /**
   * @brief GET /nova/v1/apps/<id>/screenshot/<n>: a store screenshot, re-encoded and cached on the host.
   *
   * @param response HTTPS response.
   * @param request HTTPS request.
   */
  void nova_app_screenshot(resp_https_t response, req_https_t request) {
    print_req<SunshineHTTPS>(request);
    if (!nova_require_device(response, request)) {
      return;
    }
    const auto app = nova_find_app(request, request->path_match[1].str());
    const auto file = app ? nova_api::screenshot_file(*app, std::stoul(request->path_match[2].str())) : std::nullopt;
    if (!file) {
      nova_json(response, SimpleWeb::StatusCode::client_error_not_found, {{"error", "screenshot not found"}});
      return;
    }
    nova_send_image(response, request, *file);
  }

  /**
   * @brief GET /bitrate?bitrate=<kbps>: change the calling device's running stream bitrate.
   *
   * Compatible with the Sunshine-Foundation extension moonlight-vplus uses; replies
   * `<root status_code="200"><bitrate>1</bitrate></root>`, or `0` when the device isn't streaming.
   *
   * @param response HTTPS response.
   * @param request HTTPS request.
   */
  void bitrate(resp_https_t response, req_https_t request) {
    print_req<SunshineHTTPS>(request);
    pt::ptree tree;
    auto g = util::fail_guard([&]() {
      std::ostringstream data;
      pt::write_xml(data, tree);
      response->write(data.str());
      response->close_connection_after_response = true;
    });
    const auto peer = verified_peer_for(request);
    const auto args = request->parse_query_string();
    long long requested = 0;
    try {
      requested = std::stoll(get_arg(args, "bitrate", "0"));
    } catch (...) {
      requested = 0;
    }
    const auto kbps = nova_api::clamp_bitrate(requested, config::video.max_bitrate);
    if (peer.cert.empty() || !kbps) {
      tree.put("root.bitrate", 0);
      tree.put("root.<xmlattr>.status_code", peer.cert.empty() ? 401 : 400);
      tree.put("root.<xmlattr>.status_message", peer.cert.empty() ? "This device isn't paired" : "Invalid bitrate");
      return;
    }
    const int sessions = rtsp_stream::request_bitrate_by_cert(peer.cert, *kbps);
    BOOST_LOG(info) << "Nova: "sv << peer.name << " asked for "sv << *kbps << " kbps ("sv << sessions << " session(s))"sv;
    tree.put("root.bitrate", sessions > 0 ? 1 : 0);
    tree.put("root.applied_kbps", *kbps);
    tree.put("root.<xmlattr>.status_code", 200);
  }

  /**
   * @brief Stream a file-transfer offer's bytes from disk (kind=4 offers).
   * @param response HTTP response object.
   * @param request HTTP request data from the client.
   */
  void clipboard_file_get(resp_https_t response, req_https_t request) {
    print_req<SunshineHTTPS>(request);
    if (refuse_without_clipboard_permission(response, request)) {
      return;
    }

    auto path = clipboard::blob::file_path(request->path_match[1]);
    if (!path) {
      response->write(SimpleWeb::StatusCode::client_error_not_found);
      response->close_connection_after_response = true;
      return;
    }
    auto in = std::make_shared<std::ifstream>(*path, std::ios::binary);
    if (!in->good()) {
      response->write(SimpleWeb::StatusCode::client_error_gone);
      response->close_connection_after_response = true;
      return;
    }
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "application/octet-stream");
    headers.emplace("Content-Disposition", "attachment; filename=\"" + std::filesystem::path(*path).filename().string() + "\"");
    response->write(SimpleWeb::StatusCode::success_ok, *in, headers);
    response->close_connection_after_response = true;
  }

  void setup(const std::string &pkey, const std::string &cert) {
    conf_intern.pkey = pkey;
    conf_intern.servercert = cert;
  }

  /**
   * @brief Check whether a paired client certificate is allowed to connect and retrieve its friendly name.
   *
   * @param cert_pem PEM-encoded client certificate to look up.
   * @return Pair of (bool enabled, string name) where "enabled" is True when the client certificate belongs to an
             enabled device and "name" is the friendly client name set during pairing.
   */
  std::pair<bool, std::string> get_client_status(const std::string_view cert_pem);

  void start() {
    platf::set_thread_name("nvhttp");
    auto shutdown_event = mail::man->event<bool>(mail::shutdown);

    auto port_http = net::map_port(PORT_HTTP);
    auto port_https = net::map_port(PORT_HTTPS);
    auto address_family = net::af_from_enum_string(config::sunshine.address_family);

    bool clean_slate = config::sunshine.flags[config::flag::FRESH_STATE];

    if (!clean_slate) {
      load_state();
    }

    auto pkey = file_handler::read_file(config::nvhttp.pkey.c_str());
    auto cert = file_handler::read_file(config::nvhttp.cert.c_str());
    setup(pkey, cert);

    // resume doesn't always get the parameter "localAudioPlayMode"
    // launch will store it in host_audio
    bool host_audio {};

    https_server_t https_server {config::nvhttp.cert, config::nvhttp.pkey};
    http_server_t http_server;

    // Verify certificates after establishing connection
    https_server.verify = [](SSL *ssl) {
      crypto::x509_t x509 {
#if OPENSSL_VERSION_MAJOR >= 3
        SSL_get1_peer_certificate(ssl)
#else
        SSL_get_peer_certificate(ssl)
#endif
      };
      if (!x509) {
        BOOST_LOG(info) << "unknown -- denied"sv;
        return 0;
      }

      int verified = 0;

      auto fg = util::fail_guard([&]() {
        char subject_name[256];

        X509_NAME_oneline(X509_get_subject_name(x509.get()), subject_name, sizeof(subject_name));

        BOOST_LOG(debug) << subject_name << " -- "sv << (verified ? "verified"sv : "denied"sv);
      });

      std::lock_guard lock {client_auth_mutex()};
      auto err_str = verify_client_certificate(x509.get());
      if (err_str) {
        BOOST_LOG(warning) << "SSL Verification error :: "sv << err_str;

        return verified;
      }

      // Check if this client is enabled
      auto pem = crypto::pem(x509);
      auto [enabled, client_name] = get_client_status(pem);
      if (!enabled) {
        BOOST_LOG(info) << "Client is disabled -- denied"sv;
        return verified;
      }

      last_verified_client_cert = pem;
      last_verified_client_name = client_name;
      verified = 1;

      return verified;
    };

    https_server.on_verify_failed = [](resp_https_t resp, req_https_t req) {
      pt::ptree tree;
      auto g = util::fail_guard([&]() {
        std::ostringstream data;

        pt::write_xml(data, tree);
        resp->write(data.str());
        resp->close_connection_after_response = true;
      });

      tree.put("root.<xmlattr>.status_code"s, 401);
      tree.put("root.<xmlattr>.query"s, req->path);
      tree.put("root.<xmlattr>.status_message"s, "The client is not authorized. Certificate verification failed."s);
    };

    https_server.default_resource["GET"] = not_found<SunshineHTTPS>;
    https_server.resource["^/serverinfo$"]["GET"] = serverinfo<SunshineHTTPS>;
    https_server.resource["^/pair$"]["GET"] = [](auto resp, auto req) {
      pair<SunshineHTTPS>(resp, req);
    };
    https_server.resource["^/applist$"]["GET"] = applist;
    https_server.resource["^/appasset$"]["GET"] = appasset;
    https_server.resource["^/launch$"]["GET"] = [&host_audio](auto resp, auto req) {
      launch(host_audio, resp, req);
    };
    https_server.resource["^/resume$"]["GET"] = [&host_audio](auto resp, auto req) {
      resume(host_audio, resp, req);
    };
    https_server.resource["^/cancel$"]["GET"] = cancel;
    https_server.resource["^/bitrate$"]["GET"] = bitrate;
    https_server.resource["^/nova/v1/capabilities$"]["GET"] = nova_capabilities;
    https_server.resource["^/nova/v1/apps$"]["GET"] = nova_apps;
    https_server.resource["^/nova/v1/apps/([0-9a-f]{16})/art/(poster|hero|logo|icon|background)$"]["GET"] = nova_app_art;
    https_server.resource["^/nova/v1/apps/([0-9a-f]{16})/details$"]["GET"] = nova_app_details;
    https_server.resource["^/nova/v1/apps/([0-9a-f]{16})/screenshot/([0-9]{1,2})$"]["GET"] = nova_app_screenshot;
    https_server.resource["^/api/v1/clipboard/blob$"]["POST"] = clipboard_blob_post;
    https_server.resource["^/api/v1/clipboard/blob/([a-fA-F0-9-]+)$"]["GET"] = clipboard_blob_get;
    https_server.resource["^/clipboard/file/([a-fA-F0-9-]+)$"]["GET"] = clipboard_file_get;

    https_server.config.reuse_address = true;
    https_server.config.address = net::get_bind_address(address_family);
    https_server.config.port = port_https;

    http_server.default_resource["GET"] = not_found<SimpleWeb::HTTP>;
    http_server.resource["^/serverinfo$"]["GET"] = serverinfo<SimpleWeb::HTTP>;
    http_server.resource["^/pair$"]["GET"] = [](auto resp, auto req) {
      pair<SimpleWeb::HTTP>(resp, req);
    };

    http_server.config.reuse_address = true;
    http_server.config.address = net::get_bind_address(address_family);
    http_server.config.port = port_http;

    auto accept_and_run = [&](auto *http_server) {
      try {
        std::string name = "nvhttp::" + std::to_string(http_server->config.port);
        platf::set_thread_name(name);
        http_server->start();
      } catch (boost::system::system_error &err) {
        // It's possible the exception gets thrown after calling http_server->stop() from a different thread
        if (shutdown_event->peek()) {
          return;
        }

        BOOST_LOG(fatal) << "Couldn't start http server on ports ["sv << port_https << ", "sv << port_https << "]: "sv << err.what();
        shutdown_event->raise(true);
        return;
      }
    };
    std::jthread ssl {accept_and_run, &https_server};
    std::jthread tcp {accept_and_run, &http_server};

    // Wait for any event
    shutdown_event->view();

    https_server.stop();
    http_server.stop();

    ssl.join();
    tcp.join();
  }

  void erase_all_clients() {
    std::lock_guard lock {client_auth_mutex()};
    BOOST_LOG(info) << "Unpaired all devices ("sv << client_root.named_devices.size() << " removed)"sv;
    client_root = {};
    cert_chain.clear();
    save_state();
  }

  bool unpair_client(const std::string_view uuid) {
    std::lock_guard lock {client_auth_mutex()};
    bool removed = false;
    for (auto it = client_root.named_devices.begin(); it != client_root.named_devices.end();) {
      if ((*it).uuid == uuid) {
        BOOST_LOG(info) << "Unpaired device ["sv << (*it).name << "] ("sv << uuid << ')';
        it = client_root.named_devices.erase(it);
        removed = true;
      } else {
        ++it;
      }
    }

    rebuild_client_cert_chain();
    save_state();
    return removed;
  }

  bool set_client_enabled(const std::string_view uuid, bool enabled) {
    std::lock_guard lock {client_auth_mutex()};
    for (auto &named_cert : client_root.named_devices) {
      if (named_cert.uuid == uuid) {
        if (named_cert.enabled != enabled) {
          BOOST_LOG(info) << (enabled ? "Allowed device ["sv : "Blocked device ["sv) << named_cert.name << "] ("sv << uuid << ')';
        }
        named_cert.enabled = enabled;
        rebuild_client_cert_chain();
        save_state();
        return true;
      }
    }
    return false;
  }

  std::string get_client_name_by_uuid(const std::string_view uuid) {
    std::lock_guard lock {client_auth_mutex()};
    for (const auto &named_cert : client_root.named_devices) {
      if (named_cert.uuid == uuid) {
        return named_cert.name;
      }
    }
    return {};
  }

  /**
   * @brief Get cert by UUID.
   */
  std::string get_cert_by_uuid(const std::string_view uuid) {
    std::lock_guard lock {client_auth_mutex()};
    for (const auto &named_cert : client_root.named_devices) {
      if (named_cert.uuid == uuid) {
        return named_cert.cert;
      }
    }
    return {};
  }

  bool is_valid_client_name(const std::string_view name) {
    if (name.empty() || name.front() == ' ' || name.back() == ' ') {
      return false;
    }
    std::size_t characters = 0;
    for (const auto byte : name) {
      const auto c = static_cast<unsigned char>(byte);
      if (c < 0x20 || c == 0x7F) {
        return false;
      }
      // Count UTF-8 code points: every byte except continuation bytes starts one.
      if ((c & 0xC0) != 0x80) {
        ++characters;
      }
    }
    return characters <= 64;
  }

  bool set_client_name(const std::string_view uuid, const std::string_view name) {
    if (!is_valid_client_name(name)) {
      return false;
    }
    std::lock_guard lock {client_auth_mutex()};
    for (auto &named_cert : client_root.named_devices) {
      if (named_cert.uuid == uuid) {
        if (named_cert.name != name) {
          BOOST_LOG(info) << "Renamed device ["sv << named_cert.name << "] to ["sv << name << "] ("sv << uuid << ')';
        }
        named_cert.name = std::string {name};
        save_state();
        return true;
      }
    }
    return false;
  }

  bool set_client_permissions(const std::string_view uuid, const client_permissions::mask_t permissions) {
    const auto mask = client_permissions::sanitize(permissions);
    std::string cert;
    {
      std::lock_guard lock {client_auth_mutex()};
      for (auto &named_cert : client_root.named_devices) {
        if (named_cert.uuid == uuid) {
          if (named_cert.permissions != mask) {
            BOOST_LOG(info) << "Changed permissions of device ["sv << named_cert.name << "] ("sv << uuid << ") to "sv
                            << client_permissions::preset_name(mask) << " (mask 0x"sv << std::hex << static_cast<unsigned>(mask) << std::dec << ')';
          }
          named_cert.permissions = mask;
          cert = named_cert.cert;
          save_state();
          break;
        }
      }
    }
    if (cert.empty()) {
      return false;
    }
    // Running streams pick the change up immediately, without reconnecting.
    rtsp_stream::update_permissions_by_cert(cert, mask);
    return true;
  }

  std::optional<client_permissions::mask_t> get_client_permissions_by_uuid(const std::string_view uuid) {
    std::lock_guard lock {client_auth_mutex()};
    for (const auto &named_cert : client_root.named_devices) {
      if (named_cert.uuid == uuid) {
        return named_cert.permissions;
      }
    }
    return std::nullopt;
  }

  client_permissions::mask_t get_client_permissions(const std::string_view cert_pem) {
    if (cert_pem.empty()) {
      return client_permissions::full;
    }
    std::lock_guard lock {client_auth_mutex()};
    for (const auto &named_cert : client_root.named_devices) {
      if (named_cert.cert == cert_pem) {
        return named_cert.permissions;
      }
    }
    // Only paired certificates pass TLS verification, so an unknown one gets nothing.
    return client_permissions::view_only;
  }

  void record_client_connected(const std::string_view cert_pem) {
    if (cert_pem.empty()) {
      return;
    }
    std::lock_guard lock {client_auth_mutex()};
    for (auto &named_cert : client_root.named_devices) {
      if (named_cert.cert == cert_pem) {
        named_cert.last_connected_at = unix_now();
        if (!config::sunshine.flags[config::flag::FRESH_STATE]) {
          save_state();
        }
        return;
      }
    }
  }

  std::optional<std::pair<std::string, std::string>> get_client_identity(const std::string_view cert_pem) {
    if (cert_pem.empty()) {
      return std::nullopt;
    }
    std::lock_guard lock {client_auth_mutex()};
    for (const auto &named_cert : client_root.named_devices) {
      if (named_cert.cert == cert_pem) {
        return std::make_pair(named_cert.uuid, named_cert.name);
      }
    }
    return std::nullopt;
  }

  /**
   * @brief Check whether a paired client certificate is allowed to connect and return its friendly name.
   */
  std::pair<bool, std::string> get_client_status(const std::string_view cert_pem) {
    const client_t &client = client_root;
    for (const auto &named_cert : client.named_devices) {
      if (named_cert.cert == cert_pem) {
        return {named_cert.enabled, named_cert.name};
      }
    }
    return {true, {}};
  }

#ifdef SUNSHINE_TESTS
  namespace test_support {
    void pair_http(
      std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTP>::Response> response,
      std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTP>::Request> request
    ) {
      pair<SimpleWeb::HTTP>(std::move(response), std::move(request));
    }

    void reset_client_state() {
      std::lock_guard lock {client_auth_mutex()};
      client_root = {};
      cert_chain.clear();
    }

    std::string add_client(const std::string &name, std::string cert, bool enabled) {
      auto uuid = add_authorized_client(name, std::move(cert));
      if (!uuid.empty() && !enabled) {
        set_client_enabled(uuid, false);
      }
      return uuid;
    }

    bool duplicate_client(const std::string_view uuid) {
      std::lock_guard lock {client_auth_mutex()};
      const auto client_it = std::ranges::find(client_root.named_devices, uuid, &named_cert_t::uuid);
      if (client_it == client_root.named_devices.end()) {
        return false;
      }

      auto duplicate = *client_it;
      duplicate.uuid = uuid_util::uuid_t::generate().string();
      client_root.named_devices.emplace_back(std::move(duplicate));
      rebuild_client_cert_chain();
      save_state();
      return true;
    }

    bool authorize_client_certificate(const std::string_view cert) {
      auto certificate = crypto::x509(cert);
      if (!certificate) {
        return false;
      }

      std::lock_guard lock {client_auth_mutex()};
      return verify_client_certificate(certificate.get()) == nullptr;
    }

    bool complete_pairing(const std::string_view pairing_id, const bool success) {
      std::scoped_lock lock {map_id_sess_mutex()};
      const auto sess_it = std::ranges::find_if(map_id_sess, [&](const auto &entry) {
        return entry.second.async_insert_pin.id == pairing_id;
      });
      if (sess_it == map_id_sess.end()) {
        return false;
      }

      nvhttp::complete_pairing(sess_it->second, success);
      map_id_sess.erase(sess_it);
      return true;
    }

    void reload_client_state() {
      load_state();
    }
  }  // namespace test_support
#endif
}  // namespace nvhttp
