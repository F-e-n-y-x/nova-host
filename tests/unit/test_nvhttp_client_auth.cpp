/**
 * @file tests/unit/test_nvhttp_client_auth.cpp
 * @brief Test exact paired-client certificate authorization and persistence.
 */

#include "../certificate_test_utils.h"
#include "../tests_common.h"

// standard includes
#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

// local includes
#include <nlohmann/json.hpp>
#include <src/client_permissions.h>
#include <src/config.h>
#include <src/nvhttp.h>

namespace fs = std::filesystem;

/**
 * @brief Isolate paired-client authorization tests from the user's Sunshine state.
 */
class ClientAuthorizationTest: public BaseTest {
protected:
  /**
   * @brief Redirect persisted client state to the test build directory.
   */
  void SetUp() override {
    BaseTest::SetUp();
    original_state_file = config::nvhttp.file_state;
    original_fresh_state = config::sunshine.flags[config::flag::FRESH_STATE];
    state_file = fs::path {SUNSHINE_TEST_BIN_DIR} / "client_authorization_state.json";

    config::nvhttp.file_state = state_file.string();
    config::sunshine.flags[config::flag::FRESH_STATE] = false;
    nvhttp::test_support::reset_client_state();

    std::error_code remove_error;
    fs::remove(state_file, remove_error);
  }

  /**
   * @brief Remove test state and restore the caller's configuration.
   */
  void TearDown() override {
    nvhttp::test_support::reset_client_state();
    std::error_code remove_error;
    fs::remove(state_file, remove_error);

    config::nvhttp.file_state = original_state_file;
    config::sunshine.flags[config::flag::FRESH_STATE] = original_fresh_state;
    BaseTest::TearDown();
  }

private:
  fs::path state_file;  ///< Task-specific persisted state fixture.
  std::string original_state_file;  ///< State-file setting restored after each test.
  bool original_fresh_state;  ///< Fresh-state flag restored after each test.
};

TEST_F(ClientAuthorizationTest, CanonicalIdentityFailsClosedAndTracksEnableState) {
  const auto paired_credentials = test_utils::certificates::generate_ca_credentials();
  const auto crlf_certificate = test_utils::certificates::to_crlf_pem(paired_credentials.x509);
  const auto unknown_credentials = crypto::gen_creds("Sunshine Unknown Client", 2048);
  const auto uuid = nvhttp::test_support::add_client("paired", crlf_certificate, true);

  EXPECT_TRUE(nvhttp::test_support::add_client("invalid", "not a certificate", true).empty());
  ASSERT_FALSE(uuid.empty());
  EXPECT_EQ(nvhttp::get_cert_by_uuid(uuid), paired_credentials.x509);
  EXPECT_TRUE(nvhttp::test_support::authorize_client_certificate(paired_credentials.x509));
  EXPECT_TRUE(nvhttp::test_support::authorize_client_certificate(crlf_certificate));
  EXPECT_FALSE(nvhttp::test_support::authorize_client_certificate(unknown_credentials.x509));
  EXPECT_FALSE(nvhttp::test_support::authorize_client_certificate("not a certificate"));

  ASSERT_TRUE(nvhttp::set_client_enabled(uuid, false));
  EXPECT_FALSE(nvhttp::test_support::authorize_client_certificate(paired_credentials.x509));
  ASSERT_TRUE(nvhttp::set_client_enabled(uuid, true));
  EXPECT_TRUE(nvhttp::test_support::authorize_client_certificate(paired_credentials.x509));
}

TEST_F(ClientAuthorizationTest, DerivedLeafDoesNotInheritPairedAuthorization) {
  const auto paired_credentials = test_utils::certificates::generate_ca_credentials();
  const auto derived_credentials = test_utils::certificates::generate_derived_leaf(paired_credentials);
  const auto uuid = nvhttp::test_support::add_client("paired issuer", paired_credentials.x509, true);

  ASSERT_FALSE(uuid.empty());
  EXPECT_TRUE(nvhttp::test_support::authorize_client_certificate(paired_credentials.x509));
  EXPECT_FALSE(nvhttp::test_support::authorize_client_certificate(derived_credentials.x509));

  ASSERT_TRUE(nvhttp::set_client_enabled(uuid, false));
  EXPECT_FALSE(nvhttp::test_support::authorize_client_certificate(paired_credentials.x509));
  EXPECT_FALSE(nvhttp::test_support::authorize_client_certificate(derived_credentials.x509));
}

TEST_F(ClientAuthorizationTest, MultipleClientsPersistAndUnpairIndependently) {
  const auto enabled_credentials = test_utils::certificates::generate_ca_credentials("Sunshine Enabled Client");
  const auto disabled_credentials = crypto::gen_creds("Sunshine Disabled Client", 2048);
  const auto expired_credentials = test_utils::certificates::expire_credentials(
    test_utils::certificates::generate_ca_credentials("Sunshine Expired Client")
  );

  const auto enabled_uuid = nvhttp::test_support::add_client("enabled", enabled_credentials.x509, true);
  const auto disabled_uuid = nvhttp::test_support::add_client("disabled", disabled_credentials.x509, false);
  const auto expired_uuid = nvhttp::test_support::add_client("expired", expired_credentials.x509, true);
  ASSERT_FALSE(enabled_uuid.empty());
  ASSERT_FALSE(disabled_uuid.empty());
  ASSERT_FALSE(expired_uuid.empty());
  EXPECT_EQ(nvhttp::get_all_clients().size(), 3);

  EXPECT_TRUE(nvhttp::test_support::authorize_client_certificate(enabled_credentials.x509));
  EXPECT_FALSE(nvhttp::test_support::authorize_client_certificate(disabled_credentials.x509));
  EXPECT_TRUE(nvhttp::test_support::authorize_client_certificate(expired_credentials.x509));

  nvhttp::test_support::reset_client_state();
  EXPECT_FALSE(nvhttp::test_support::authorize_client_certificate(enabled_credentials.x509));
  nvhttp::test_support::reload_client_state();

  EXPECT_TRUE(nvhttp::test_support::authorize_client_certificate(enabled_credentials.x509));
  EXPECT_FALSE(nvhttp::test_support::authorize_client_certificate(disabled_credentials.x509));
  EXPECT_TRUE(nvhttp::test_support::authorize_client_certificate(expired_credentials.x509));

  ASSERT_TRUE(nvhttp::set_client_enabled(disabled_uuid, true));
  EXPECT_TRUE(nvhttp::test_support::authorize_client_certificate(disabled_credentials.x509));
  ASSERT_TRUE(nvhttp::unpair_client(enabled_uuid));
  EXPECT_FALSE(nvhttp::test_support::authorize_client_certificate(enabled_credentials.x509));

  nvhttp::erase_all_clients();
  nvhttp::test_support::reset_client_state();
  nvhttp::test_support::reload_client_state();
  EXPECT_FALSE(nvhttp::test_support::authorize_client_certificate(disabled_credentials.x509));
  EXPECT_FALSE(nvhttp::test_support::authorize_client_certificate(expired_credentials.x509));
}

TEST_F(ClientAuthorizationTest, DuplicateCertificateIdentityFailsClosed) {
  const auto credentials = test_utils::certificates::generate_ca_credentials();
  const auto uuid = nvhttp::test_support::add_client("first", credentials.x509, true);
  ASSERT_FALSE(uuid.empty());
  EXPECT_FALSE(nvhttp::test_support::duplicate_client("missing"));
  ASSERT_TRUE(nvhttp::test_support::duplicate_client(uuid));

  EXPECT_FALSE(nvhttp::test_support::authorize_client_certificate(credentials.x509));
}

TEST_F(ClientAuthorizationTest, RePairingReplacesDuplicateCertificateIdentity) {
  const auto paired_credentials = test_utils::certificates::generate_ca_credentials();
  const auto other_credentials = test_utils::certificates::generate_ca_credentials("Sunshine Other Client");
  const auto original_uuid = nvhttp::test_support::add_client("original", paired_credentials.x509, true);
  const auto other_uuid = nvhttp::test_support::add_client("other", other_credentials.x509, true);
  ASSERT_FALSE(original_uuid.empty());
  ASSERT_FALSE(other_uuid.empty());
  ASSERT_TRUE(nvhttp::test_support::duplicate_client(original_uuid));
  ASSERT_EQ(nvhttp::get_all_clients().size(), 3);
  EXPECT_FALSE(nvhttp::test_support::authorize_client_certificate(paired_credentials.x509));

  const auto repaired_uuid = nvhttp::test_support::add_client(
    "repaired",
    test_utils::certificates::to_crlf_pem(paired_credentials.x509),
    true
  );

  ASSERT_FALSE(repaired_uuid.empty());
  EXPECT_NE(repaired_uuid, original_uuid);
  EXPECT_EQ(nvhttp::get_all_clients().size(), 2);
  EXPECT_EQ(nvhttp::get_cert_by_uuid(repaired_uuid), paired_credentials.x509);
  EXPECT_TRUE(nvhttp::test_support::authorize_client_certificate(paired_credentials.x509));
  EXPECT_TRUE(nvhttp::test_support::authorize_client_certificate(other_credentials.x509));

  nvhttp::test_support::reset_client_state();
  nvhttp::test_support::reload_client_state();
  EXPECT_EQ(nvhttp::get_all_clients().size(), 2);
  EXPECT_TRUE(nvhttp::test_support::authorize_client_certificate(paired_credentials.x509));
  EXPECT_TRUE(nvhttp::test_support::authorize_client_certificate(other_credentials.x509));
}

TEST_F(ClientAuthorizationTest, ConcurrentStateChangesRemainConsistent) {
  const auto credentials = test_utils::certificates::generate_ca_credentials();
  const auto uuid = nvhttp::test_support::add_client("concurrent", credentials.x509, true);
  ASSERT_FALSE(uuid.empty());

  std::atomic_bool operations_succeeded {true};
  std::vector<std::jthread> workers;
  for (std::size_t worker = 0; worker < 4; ++worker) {
    workers.emplace_back([&operations_succeeded, uuid, certificate = credentials.x509, worker]() {
      for (std::size_t iteration = 0; iteration < 8; ++iteration) {
        if (!nvhttp::set_client_enabled(uuid, (worker + iteration) % 2 == 0)) {
          operations_succeeded = false;
        }
        static_cast<void>(nvhttp::test_support::authorize_client_certificate(certificate));
      }
    });
  }
  workers.clear();

  EXPECT_TRUE(operations_succeeded);
  ASSERT_TRUE(nvhttp::set_client_enabled(uuid, false));
  EXPECT_FALSE(nvhttp::test_support::authorize_client_certificate(credentials.x509));
}

namespace {
  /**
   * @brief Find one client in the web API listing.
   *
   * @param uuid Persistent UUID of the client.
   * @return Its JSON record, or null when it is not listed.
   */
  nlohmann::json listed_client(const std::string &uuid) {
    for (const auto &client : nvhttp::get_all_clients()) {
      if (client.at("uuid") == uuid) {
        return client;
      }
    }
    return nullptr;
  }
}  // namespace

TEST_F(ClientAuthorizationTest, NewClientsGetStandardAccessAndAPairingTime) {
  const auto credentials = crypto::gen_creds("Sunshine Permissions Client", 2048);
  const auto uuid = nvhttp::test_support::add_client("phone", credentials.x509, true);
  ASSERT_FALSE(uuid.empty());

  const auto client = listed_client(uuid);
  ASSERT_FALSE(client.is_null());
  EXPECT_EQ(client.at("permissions").at("preset"), "standard");
  EXPECT_FALSE(client.at("permissions").at("power").get<bool>());
  EXPECT_FALSE(client.at("permissions").at("host_commands").get<bool>());
  EXPECT_TRUE(client.at("paired_at").is_number_integer());
  EXPECT_GT(client.at("paired_at").get<std::int64_t>(), 0);
  EXPECT_TRUE(client.at("last_connected_at").is_null());
  EXPECT_FALSE(client.at("connected").get<bool>());
  EXPECT_EQ(nvhttp::get_client_permissions(credentials.x509), client_permissions::standard);
}

TEST_F(ClientAuthorizationTest, PermissionsNamesAndConnectionTimesPersist) {
  const auto credentials = crypto::gen_creds("Sunshine Persisted Client", 2048);
  const auto uuid = nvhttp::test_support::add_client("tablet", credentials.x509, true);
  ASSERT_FALSE(uuid.empty());

  ASSERT_TRUE(nvhttp::set_client_permissions(uuid, client_permissions::play));
  ASSERT_TRUE(nvhttp::set_client_name(uuid, "Living room tablet"));
  nvhttp::record_client_connected(credentials.x509);

  nvhttp::test_support::reset_client_state();
  nvhttp::test_support::reload_client_state();

  const auto client = listed_client(uuid);
  ASSERT_FALSE(client.is_null());
  EXPECT_EQ(client.at("name"), "Living room tablet");
  EXPECT_EQ(client.at("permissions").at("preset"), "play");
  EXPECT_FALSE(client.at("permissions").at("clipboard").get<bool>());
  EXPECT_GT(client.at("last_connected_at").get<std::int64_t>(), 0);
  EXPECT_EQ(nvhttp::get_client_permissions_by_uuid(uuid), client_permissions::play);
  EXPECT_EQ(nvhttp::get_client_permissions(credentials.x509), client_permissions::play);
}

TEST_F(ClientAuthorizationTest, UnknownClientsCannotBeUpdatedAndGetNoPermissions) {
  const auto unknown = crypto::gen_creds("Sunshine Unknown Client", 2048);
  EXPECT_FALSE(nvhttp::set_client_permissions("no-such-uuid", client_permissions::view_only));
  EXPECT_FALSE(nvhttp::set_client_name("no-such-uuid", "Phone"));
  EXPECT_FALSE(nvhttp::get_client_permissions_by_uuid("no-such-uuid").has_value());
  EXPECT_EQ(nvhttp::get_client_permissions(unknown.x509), client_permissions::view_only);
  EXPECT_EQ(nvhttp::get_client_permissions(""), client_permissions::paired_default);
}

TEST_F(ClientAuthorizationTest, PermissionsFollowTheConnectionNotTheLatestHandshake) {
  using boost::asio::ip::make_address;
  using boost::asio::ip::tcp;

  const auto restricted = crypto::gen_creds("Sunshine Restricted Client", 2048);
  const auto trusted = crypto::gen_creds("Sunshine Trusted Client", 2048);
  const auto restricted_uuid = nvhttp::test_support::add_client("restricted", restricted.x509, true);
  const auto trusted_uuid = nvhttp::test_support::add_client("trusted", trusted.x509, true);
  ASSERT_TRUE(nvhttp::set_client_permissions(restricted_uuid, client_permissions::view_only));

  const tcp::endpoint restricted_conn {make_address("192.168.1.20"), 50001};
  const tcp::endpoint trusted_conn {make_address("192.168.1.30"), 50002};

  // The restricted client connects first and keeps its connection open; the trusted
  // client's later handshake must not change what the restricted connection may do.
  nvhttp::remember_verified_peer(restricted_conn, restricted.x509, "restricted");
  nvhttp::remember_verified_peer(trusted_conn, trusted.x509, "trusted");

  EXPECT_EQ(nvhttp::verified_cert_for(restricted_conn), restricted.x509);
  EXPECT_EQ(nvhttp::permissions_for_peer(restricted_conn), client_permissions::view_only);
  EXPECT_EQ(nvhttp::permissions_for_peer(trusted_conn), client_permissions::paired_default);
}

TEST_F(ClientAuthorizationTest, UnknownConnectionsGetNoPermissions) {
  const boost::asio::ip::tcp::endpoint never_verified {boost::asio::ip::make_address("10.0.0.9"), 40000};

  EXPECT_TRUE(nvhttp::verified_cert_for(never_verified).empty());
  EXPECT_EQ(nvhttp::permissions_for_peer(never_verified), client_permissions::view_only);
}

TEST_F(ClientAuthorizationTest, DeviceNamesAreValidated) {
  EXPECT_TRUE(nvhttp::is_valid_client_name("Pixel 9 Pro"));
  EXPECT_TRUE(nvhttp::is_valid_client_name("Ноутбук"));
  EXPECT_TRUE(nvhttp::is_valid_client_name(std::string(64, 'a')));
  EXPECT_FALSE(nvhttp::is_valid_client_name(std::string(65, 'a')));
  EXPECT_FALSE(nvhttp::is_valid_client_name(""));
  EXPECT_FALSE(nvhttp::is_valid_client_name(" padded"));
  EXPECT_FALSE(nvhttp::is_valid_client_name("padded "));
  EXPECT_FALSE(nvhttp::is_valid_client_name("tab\tname"));
  EXPECT_FALSE(nvhttp::is_valid_client_name(std::string {"del\x7f"}));

  const auto credentials = crypto::gen_creds("Sunshine Rename Client", 2048);
  const auto uuid = nvhttp::test_support::add_client("before", credentials.x509, true);
  EXPECT_FALSE(nvhttp::set_client_name(uuid, " bad"));
  EXPECT_EQ(listed_client(uuid).at("name"), "before");
}

TEST_F(ClientAuthorizationTest, StateFilesWithoutNewFieldsKeepStreamingAccess) {
  const auto credentials = crypto::gen_creds("Sunshine Legacy Client", 2048);
  const nlohmann::json legacy {
    {"root", {
               {"uniqueid", "0123456789ABCDEF"},
               {"named_devices", nlohmann::json::array({
                                   {{"name", "old laptop"}, {"cert", credentials.x509}, {"uuid", "LEGACY-UUID"}, {"enabled", "true"}},
                                 })},
             }},
  };
  {
    std::ofstream file {config::nvhttp.file_state};
    file << legacy.dump(2);
  }

  nvhttp::test_support::reload_client_state();

  const auto client = listed_client("LEGACY-UUID");
  ASSERT_FALSE(client.is_null());
  EXPECT_EQ(client.at("name"), "old laptop");
  EXPECT_EQ(client.at("permissions").at("preset"), "standard");
  EXPECT_TRUE(client.at("paired_at").is_null());
  EXPECT_TRUE(client.at("last_connected_at").is_null());
  EXPECT_TRUE(nvhttp::test_support::authorize_client_certificate(credentials.x509));
}

TEST_F(ClientAuthorizationTest, UpgradedPermissionNodesNeverGainHostControl) {
  // A device saved by 0.2 with every flag of that version on: the new host-control flags are
  // absent from its node and must stay off, while the old flags keep their stored values.
  const auto credentials = crypto::gen_creds("Sunshine Upgraded Client", 2048);
  const nlohmann::json state {
    {"root", {
               {"uniqueid", "0123456789ABCDEF"},
               {"named_devices", nlohmann::json::array({
                                   {{"name", "phone"}, {"cert", credentials.x509}, {"uuid", "UPGRADED-UUID"}, {"enabled", "true"}, {"permissions", {{"input_keyboard", "true"}, {"input_mouse", "true"}, {"input_controller", "true"}, {"input_touch_pen", "true"}, {"clipboard", "false"}, {"launch_apps", "true"}}}},
                                 })},
             }},
  };
  {
    std::ofstream file {config::nvhttp.file_state};
    file << state.dump(2);
  }

  nvhttp::test_support::reload_client_state();

  EXPECT_EQ(nvhttp::get_client_permissions(credentials.x509), client_permissions::play);
  const auto client = listed_client("UPGRADED-UUID");
  ASSERT_FALSE(client.is_null());
  EXPECT_FALSE(client.at("permissions").at("power").get<bool>());
  EXPECT_FALSE(client.at("permissions").at("host_commands").get<bool>());

  // Granting them explicitly persists.
  ASSERT_TRUE(nvhttp::set_client_permissions("UPGRADED-UUID", client_permissions::full));
  nvhttp::test_support::reset_client_state();
  nvhttp::test_support::reload_client_state();
  EXPECT_EQ(nvhttp::get_client_permissions(credentials.x509), client_permissions::full);
}
