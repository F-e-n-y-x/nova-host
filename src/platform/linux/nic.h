/**
 * @file src/platform/linux/nic.h
 * @brief Physical network interface lookup for Wake-on-LAN (MAC behind bridges, ethtool state).
 */
#pragma once

// standard includes
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace platf::nic {
  /**
   * @brief The physical NIC whose MAC a Wake-on-LAN packet must target, for traffic on @p ifname.
   *
   * - A physical interface is its own answer.
   * - A bridge (`br0`) answers with its wired physical member (`enp7s0`); the bridge's own MAC is
   *   usually random and the NIC ignores magic packets sent to it.
   * - VLANs, macvlans and bonds answer with their physical lower device.
   * - Interfaces without a MAC (`tailscale0`, WireGuard) and purely virtual ones (docker, veth,
   *   virbr) fall back to the host's primary physical NIC.
   *
   * @param sysfs_net The `/sys/class/net` directory (a fake tree in tests).
   * @param ifname Interface that holds the address the client connected to; may be empty.
   * @return Interface name, or nullopt when the host has no usable physical NIC.
   */
  std::optional<std::string> wol_interface_for(const std::filesystem::path &sysfs_net, std::string_view ifname);

  /**
   * @brief The host's primary physical NIC: wired before wireless, bridged or up before down, then by name.
   * @param sysfs_net The `/sys/class/net` directory.
   * @return Interface name, or nullopt.
   */
  std::optional<std::string> primary_physical_interface(const std::filesystem::path &sysfs_net);

  /**
   * @brief The MAC address of an interface, lower-case `aa:bb:cc:dd:ee:ff`.
   * @param sysfs_net The `/sys/class/net` directory.
   * @param ifname Interface name.
   * @return The MAC, or nullopt when it has none (or it is all zeros).
   */
  std::optional<std::string> mac_of(const std::filesystem::path &sysfs_net, std::string_view ifname);

  /**
   * @brief Wake-on-LAN state as reported by `ethtool <iface>`.
   */
  struct wol_state_t {
    std::string supported;  ///< "Supports Wake-on" letters, e.g. `pumbg`.
    std::string enabled;  ///< "Wake-on" letters, e.g. `g` or `d`.

    /**
     * @brief Whether magic-packet wake is supported.
     * @return True when `g` is among the supported modes.
     */
    [[nodiscard]] bool magic_supported() const {
      return supported.find('g') != std::string::npos;
    }

    /**
     * @brief Whether magic-packet wake is enabled.
     * @return True when `g` is among the enabled modes.
     */
    [[nodiscard]] bool magic_enabled() const {
      return enabled.find('g') != std::string::npos;
    }
  };

  /**
   * @brief Parse the Wake-on lines of `ethtool <iface>` output.
   * @param output Full ethtool output.
   * @return The state, or nullopt when the output has no "Wake-on:" line (driver doesn't report it).
   */
  std::optional<wol_state_t> parse_ethtool_wol(std::string_view output);
}  // namespace platf::nic
