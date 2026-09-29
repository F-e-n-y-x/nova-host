/**
 * @file src/web_client.h
 * @brief Nova's browser client: a sidecar that streams Nova games to a web browser over WebRTC.
 *
 * The sidecar is a pinned, patched build of moonlight-web-stream
 * (https://github.com/MrCreativ3001/moonlight-web-stream, GPL-3.0) behind a small gateway
 * (tools/nova-web-client/gateway/nova_web_gateway.py). Both run as separate processes, never
 * linked into Nova. Nova starts the gateway when `web_client` is on (off by default), restarts it
 * if it crashes, and stops it when the setting is switched off or Nova exits.
 *
 * Ports, all derived from Nova's `port` (47989 by default):
 * - gateway HTTPS: port + 6 (47995), the only listener reachable from the network;
 * - moonlight-web-stream: port + 7 (47996) on 127.0.0.1 only;
 * - WebRTC media: UDP port + 1011 .. port + 1030 (49000-49019).
 *
 * The gateway accepts browsers only from the addresses `origin_web_ui_allowed` allows, capped at
 * LAN (Tailscale counts as LAN), signs them in with Nova's own web UI session cookie, and pairs
 * the sidecar with Nova as the "Browser client" device the first time it is used.
 */
#pragma once

// standard includes
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace web_client {
  /// Offsets from Nova's base port.
  inline constexpr int gateway_port_offset = 6;
  inline constexpr int upstream_port_offset = 7;
  inline constexpr int udp_first_offset = 1011;
  inline constexpr int udp_count = 20;

  /**
   * @brief What the web UI shows about the browser client.
   */
  struct status_t {
    bool installed = false;  ///< The sidecar ships in this build (and python3 is there).
    bool enabled = false;  ///< The `web_client` setting.
    std::string state;  ///< "off", "not_installed", "starting", "running" or "failed".
    std::uint16_t port = 0;  ///< Gateway HTTPS port.
    std::uint16_t udp_min = 0;  ///< First WebRTC UDP port.
    std::uint16_t udp_max = 0;  ///< Last WebRTC UDP port.
    std::string allowed;  ///< "pc" (this computer only) or "lan" (LAN and Tailscale).
    std::string version;  ///< First line of the sidecar's VERSION file.
    int restarts = 0;  ///< Crashes since it was switched on.
    std::string last_error;  ///< Why it last stopped, when it wasn't asked to.
  };

  /**
   * @brief Everything needed to start the sidecar (derived from Nova's config; pure data).
   */
  struct launch_t {
    std::filesystem::path lib_dir;  ///< web-server, streamer, static/, gateway/.
    std::filesystem::path state_dir;  ///< Config and pairing (0700).
    std::string listen_host;  ///< "0.0.0.0", "::" or the configured bind_address.
    std::uint16_t gateway_port = 0;
    std::uint16_t upstream_port = 0;
    std::uint16_t udp_min = 0;
    std::uint16_t udp_max = 0;
    std::string nova_address;  ///< Where the sidecar reaches Nova (loopback unless bind_address is set).
    std::uint16_t nova_web_port = 0;
    std::uint16_t nova_http_port = 0;
    std::string tls_cert;  ///< Nova's certificate (PEM file).
    std::string tls_key;  ///< Nova's private key (PEM file).
    std::string allow;  ///< origin_web_ui_allowed.
  };

  /**
   * @brief The launch parameters for a base port and web UI policy.
   * @param base_port Nova's `port`.
   * @param bind_address Nova's `bind_address` (may be empty).
   * @param ipv6 Whether Nova listens on IPv6 too (`address_family = both`).
   * @param allow origin_web_ui_allowed.
   */
  launch_t make_launch(int base_port, std::string_view bind_address, bool ipv6, std::string_view allow);

  /**
   * @brief The gateway command line (python3 and its arguments).
   */
  std::vector<std::string> command_line(const launch_t &launch);

  /**
   * @brief Where the sidecar is installed: $NOVA_WEB_CLIENT_DIR, else the packaged location.
   */
  std::filesystem::path lib_dir();

  /**
   * @brief Whether a sidecar is installed in a directory.
   */
  bool installed_in(const std::filesystem::path &dir);

  /**
   * @brief Replace `key = value` in config file text, or append it; keeps every other line.
   */
  std::string set_config_line(std::string_view text, std::string_view key, std::string_view value);

  /**
   * @brief Start the sidecar at boot when `web_client` is on.
   */
  void start();

  /**
   * @brief Switch the sidecar on or off now (does not touch the config file).
   */
  void apply(bool enabled);

  /**
   * @brief Switch it on or off and save `web_client` in the config file.
   * @return False when the config file couldn't be written.
   */
  bool set_enabled(bool enabled);

  /**
   * @brief Stop the sidecar (Nova is exiting).
   */
  void shutdown();

  /**
   * @brief Current status.
   */
  status_t status();
}  // namespace web_client
