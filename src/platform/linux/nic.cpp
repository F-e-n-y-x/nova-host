/**
 * @file src/platform/linux/nic.cpp
 * @brief Definitions for physical network interface lookup used by Wake-on-LAN.
 */
// class header include
#include "nic.h"

// standard includes
#include <algorithm>
#include <cctype>
#include <fstream>
#include <tuple>
#include <vector>

namespace fs = std::filesystem;

namespace platf::nic {
  namespace {
    std::string read_line(const fs::path &file) {
      std::ifstream in {file};
      std::string line;
      std::getline(in, line);
      while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) {
        line.pop_back();
      }
      return line;
    }

    bool path_exists(const fs::path &path) {
      std::error_code ec;
      return fs::exists(path, ec);
    }

    /**
     * @brief Physical means backed by a device (PCI/USB/platform); virtual interfaces have no `device` link.
     */
    bool is_physical(const fs::path &sysfs_net, const std::string &ifname) {
      // ARPHRD_ETHER (1) only: rules out loopback, tun (65534), IPoIB and friends.
      return path_exists(sysfs_net / ifname / "device") && read_line(sysfs_net / ifname / "type") == "1";
    }

    bool is_wireless(const fs::path &sysfs_net, const std::string &ifname) {
      return path_exists(sysfs_net / ifname / "wireless") || path_exists(sysfs_net / ifname / "phy80211");
    }

    bool is_up(const fs::path &sysfs_net, const std::string &ifname) {
      return read_line(sysfs_net / ifname / "operstate") == "up" || read_line(sysfs_net / ifname / "carrier") == "1";
    }

    std::vector<std::string> list_dir(const fs::path &dir, std::string_view prefix = {}) {
      std::vector<std::string> names;
      std::error_code ec;
      for (const auto &entry : fs::directory_iterator(dir, ec)) {
        auto name = entry.path().filename().string();
        if (name.starts_with(prefix)) {
          names.push_back(name.substr(prefix.size()));
        }
      }
      std::ranges::sort(names);
      return names;
    }

    /**
     * @brief Pick the best physical interface among candidates: wired, then up, then by name.
     */
    std::optional<std::string> best_physical(const fs::path &sysfs_net, const std::vector<std::string> &candidates) {
      std::optional<std::string> best;
      std::tuple<bool, bool, bool> best_rank {};
      for (const auto &name : candidates) {
        if (!is_physical(sysfs_net, name) || !mac_of(sysfs_net, name)) {
          continue;
        }
        // Higher is better; bridged NICs are usually the LAN uplink.
        const auto rank = std::make_tuple(!is_wireless(sysfs_net, name), path_exists(sysfs_net / name / "brport"), is_up(sysfs_net, name));
        if (!best || rank > best_rank) {
          best = name;
          best_rank = rank;
        }
      }
      return best;
    }

    std::optional<std::string> resolve(const fs::path &sysfs_net, const std::string &ifname, int depth) {
      if (ifname.empty() || depth > 4 || !path_exists(sysfs_net / ifname)) {
        return std::nullopt;
      }
      if (is_physical(sysfs_net, ifname) && mac_of(sysfs_net, ifname)) {
        return ifname;
      }

      // Bridge: its members are in brif/.
      if (path_exists(sysfs_net / ifname / "bridge")) {
        if (auto member = best_physical(sysfs_net, list_dir(sysfs_net / ifname / "brif"))) {
          return member;
        }
        // A bridge of bridges or of bonds: look one level down.
        for (const auto &member : list_dir(sysfs_net / ifname / "brif")) {
          if (auto lower = resolve(sysfs_net, member, depth + 1)) {
            return lower;
          }
        }
        return std::nullopt;
      }

      // Bond: slaves are listed in bonding/slaves.
      if (path_exists(sysfs_net / ifname / "bonding" / "slaves")) {
        std::vector<std::string> slaves;
        std::string all = read_line(sysfs_net / ifname / "bonding" / "slaves");
        std::size_t start = 0;
        while (start < all.size()) {
          const auto end = all.find(' ', start);
          slaves.push_back(all.substr(start, end == std::string::npos ? std::string::npos : end - start));
          start = end == std::string::npos ? all.size() : end + 1;
        }
        return best_physical(sysfs_net, slaves);
      }

      // VLAN / macvlan: the parent is linked as lower_<name>.
      for (const auto &lower : list_dir(sysfs_net / ifname, "lower_")) {
        if (auto found = resolve(sysfs_net, lower, depth + 1)) {
          return found;
        }
      }
      return std::nullopt;
    }
  }  // namespace

  std::optional<std::string> mac_of(const fs::path &sysfs_net, const std::string_view ifname) {
    auto mac = read_line(sysfs_net / std::string {ifname} / "address");
    if (mac.size() != 17 || mac == "00:00:00:00:00:00") {
      return std::nullopt;
    }
    std::ranges::transform(mac, mac.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    return mac;
  }

  std::optional<std::string> primary_physical_interface(const fs::path &sysfs_net) {
    return best_physical(sysfs_net, list_dir(sysfs_net));
  }

  std::optional<std::string> wol_interface_for(const fs::path &sysfs_net, const std::string_view ifname) {
    if (auto found = resolve(sysfs_net, std::string {ifname}, 0)) {
      return found;
    }
    return primary_physical_interface(sysfs_net);
  }

  std::optional<wol_state_t> parse_ethtool_wol(const std::string_view output) {
    wol_state_t state;
    bool seen = false;
    std::size_t pos = 0;
    while (pos < output.size()) {
      auto end = output.find('\n', pos);
      if (end == std::string_view::npos) {
        end = output.size();
      }
      auto line = output.substr(pos, end - pos);
      pos = end + 1;
      while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) {
        line.remove_prefix(1);
      }
      const auto value = [&](std::string_view key) {
        auto rest = line.substr(key.size());
        while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.front()))) {
          rest.remove_prefix(1);
        }
        while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.back()))) {
          rest.remove_suffix(1);
        }
        return std::string {rest};
      };
      if (line.starts_with("Supports Wake-on:")) {
        state.supported = value("Supports Wake-on:");
      } else if (line.starts_with("Wake-on:")) {
        state.enabled = value("Wake-on:");
        seen = true;
      }
    }
    if (!seen) {
      return std::nullopt;
    }
    return state;
  }
}  // namespace platf::nic
