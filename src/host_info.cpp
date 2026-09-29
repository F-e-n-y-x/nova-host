/**
 * @file src/host_info.cpp
 * @brief Host facts for the web UI: hardware/encoder summary, displays, audio sinks,
 *        desktop preview and the setup-doctor health checks.
 */
// class header include
#include "host_info.h"

// standard includes
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>

// platform includes
#ifndef _WIN32
  #include <sys/utsname.h>
  #include <unistd.h>
#endif

// lib includes
#include "boost_process_compat.h"

#include <boost/asio/ip/host_name.hpp>

// local includes
#include "config.h"
#include "host_commands.h"
#include "host_power.h"
#include "logging.h"
#include "process.h"
#include "secure_files.h"
#include "video.h"
#ifdef __linux__
  #include "platform/linux/nic.h"
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#define STBIW_ASSERT(x)
#ifdef __GNUC__
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
  #pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "third-party/stb/stb_image_write.h"
#ifdef __GNUC__
  #pragma GCC diagnostic pop
#endif

using namespace std::literals;

namespace host_info {
  namespace {
    const auto process_start = std::chrono::steady_clock::now();  ///< Approximate process start for `uptime_s`.

    /**
     * @brief Small time-based cache for values that are cheap to keep but not free to compute.
     */
    template<class T>
    class ttl_cache_t {
    public:
      /**
       * @brief Create a cache.
       *
       * @param ttl How long a value stays fresh.
       */
      explicit ttl_cache_t(std::chrono::steady_clock::duration ttl):
          ttl {ttl} {
      }

      /**
       * @brief Return the cached value, recomputing it when stale.
       *
       * @param make Callable that computes a fresh value.
       * @return Current value.
       */
      template<class F>
      T get(F &&make) {
        std::lock_guard lock {mutex};
        const auto now = std::chrono::steady_clock::now();
        if (!value || now - stamp >= ttl) {
          value = make();
          stamp = now;
        }
        return *value;
      }

    private:
      std::mutex mutex;  ///< Guards the cached value.
      std::chrono::steady_clock::duration ttl;  ///< Freshness window.
      std::chrono::steady_clock::time_point stamp {};  ///< When the value was computed.
      std::optional<T> value;  ///< Cached value.
    };

    /**
     * @brief Read the first line of a file.
     *
     * @param path File to read.
     * @return First line, or empty when unreadable.
     */
    std::string read_first_line(const std::filesystem::path &path) {
      std::ifstream file {path};
      std::string line;
      std::getline(file, line);
      return line;
    }

    /**
     * @brief Whether an executable is on PATH.
     *
     * @param name Executable name.
     * @return True when found.
     */
    bool on_path(const char *name) {
      return !boost::process::v1::search_path(name).empty();
    }

    /**
     * @brief Status name for JSON.
     *
     * @param status Status to name.
     * @return `ok`, `warn` or `error`.
     */
    const char *status_name(health_status_e status) {
      switch (status) {
        case health_status_e::warn:
          return "warn";
        case health_status_e::error:
          return "error";
        default:
          return "ok";
      }
    }

    /**
     * @brief Fix-kind name for JSON.
     *
     * @param kind Kind to name.
     * @return `command`, `setting`, `doc` or `none`.
     */
    const char *fix_name(fix_kind_e kind) {
      switch (kind) {
        case fix_kind_e::command:
          return "command";
        case fix_kind_e::setting:
          return "setting";
        case fix_kind_e::doc:
          return "doc";
        default:
          return "none";
      }
    }

    /**
     * @brief Name of a memory type for JSON.
     *
     * @param type Memory type.
     * @return Lower-case name.
     */
    const char *mem_type_name(platf::mem_type_e type) {
      switch (type) {
        case platf::mem_type_e::system:
          return "system";
        case platf::mem_type_e::vaapi:
          return "vaapi";
        case platf::mem_type_e::dxgi:
          return "dxgi";
        case platf::mem_type_e::cuda:
          return "cuda";
        case platf::mem_type_e::videotoolbox:
          return "videotoolbox";
        case platf::mem_type_e::vulkan:
          return "vulkan";
        default:
          return "unknown";
      }
    }

    /**
     * @brief Whether a capture backend hands frames to the encoder without a CPU copy.
     *
     * @param capture Capture backend name.
     * @param mem_type Memory type the encoder consumes.
     * @return True/false when known, empty otherwise.
     */
    std::optional<bool> zero_copy_for(const std::string &capture, platf::mem_type_e mem_type) {
      if (capture.empty()) {
        return std::nullopt;
      }
      if (mem_type == platf::mem_type_e::system) {
        return false;
      }
      if (capture == "nvfbc" || capture == "kms" || capture == "wgc" || capture == "ddx" || capture == "avfoundation") {
        return true;
      }
      if (capture == "x11") {
        return false;
      }
      return std::nullopt;
    }

    /**
     * @brief Parse `/etc/os-release` for the pretty name.
     *
     * @return Pretty OS name, or empty.
     */
    std::string os_pretty_name() {
#ifdef _WIN32
      return "Windows";
#else
      std::ifstream file {"/etc/os-release"};
      std::string line;
      while (std::getline(file, line)) {
        if (line.starts_with("PRETTY_NAME=")) {
          auto value = line.substr(12);
          if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
          }
          return value;
        }
      }
      return {};
#endif
    }

    /**
     * @brief Running kernel release.
     *
     * @return Kernel release, or empty.
     */
    std::string kernel_release() {
#ifdef _WIN32
      return {};
#else
      utsname info {};
      if (uname(&info)) {
        return {};
      }
      return info.release;
#endif
    }

    /**
     * @brief Look up a PCI device name in the system `pci.ids` database.
     *
     * @param vendor Vendor ID, lower-case hex without prefix.
     * @param device Device ID, lower-case hex without prefix.
     * @return Device name, or empty when unknown.
     */
    std::string pci_device_name(const std::string &vendor, const std::string &device) {
      for (const auto *path : {"/usr/share/misc/pci.ids", "/usr/share/hwdata/pci.ids"}) {
        std::ifstream file {path};
        if (!file) {
          continue;
        }
        std::string line;
        bool in_vendor = false;
        while (std::getline(file, line)) {
          if (line.empty() || line[0] == '#') {
            continue;
          }
          if (line[0] != '\t') {
            in_vendor = line.starts_with(vendor + "  ");
            continue;
          }
          if (in_vendor && line.size() > 1 && line[1] != '\t' && line.substr(1).starts_with(device + "  ")) {
            return line.substr(1 + device.size() + 2);
          }
        }
      }
      return {};
    }

    /**
     * @brief Describe the GPUs the host can see.
     *
     * @return JSON array of `{vendor, name, driver}`.
     */
    nlohmann::json gpus_json() {
      nlohmann::json gpus = nlohmann::json::array();
#ifdef __linux__
      std::error_code ec;
      std::vector<std::filesystem::path> cards;
      for (const auto &entry : std::filesystem::directory_iterator("/sys/class/drm", ec)) {
        const auto name = entry.path().filename().string();
        if (name.starts_with("card") && name.find('-') == std::string::npos) {
          cards.push_back(entry.path());
        }
      }
      std::ranges::sort(cards);

      const auto nvidia_version = read_first_line("/sys/module/nvidia/version");
      for (const auto &card : cards) {
        auto vendor = read_first_line(card / "device" / "vendor");
        auto device = read_first_line(card / "device" / "device");
        if (vendor.starts_with("0x")) {
          vendor = vendor.substr(2);
        }
        if (device.starts_with("0x")) {
          device = device.substr(2);
        }
        if (vendor.empty() || device.empty()) {
          continue;  // firmware framebuffers (simple-framebuffer, efifb) aren't GPUs
        }
        std::string driver;
        std::ifstream uevent {card / "device" / "uevent"};
        for (std::string line; std::getline(uevent, line);) {
          if (line.starts_with("DRIVER=")) {
            driver = line.substr(7);
          }
        }

        std::string vendor_name = vendor == "10de" ? "NVIDIA" : vendor == "1002" ? "AMD" :
                                                              vendor == "8086"   ? "Intel" :
                                                                                   vendor;
        std::string name = pci_device_name(vendor, device);
        if (const auto open = name.find('['); open != std::string::npos) {
          if (const auto close = name.find(']', open); close != std::string::npos) {
            name = name.substr(open + 1, close - open - 1);
          }
        }

        nlohmann::json gpu;
        gpu["vendor"] = vendor_name;
        gpu["name"] = name.empty() ? std::format("{}:{}", vendor, device) : name;
        gpu["driver"] = driver;
        gpu["driver_version"] = driver == "nvidia" ? nvidia_version : std::string {};
        gpus.push_back(std::move(gpu));
      }
#endif
      return gpus;
    }

    ttl_cache_t<std::vector<health_check_t>> health_cache {10s};  ///< Cache for `/api/health`.
    ttl_cache_t<std::vector<platf::capture_output_t>> outputs_cache {2s};  ///< Cache for `/api/displays`.
    rate_limiter_t preview_limiter {2, 1s};  ///< At most two preview captures per second.

    std::mutex preview_mutex;  ///< Guards the preview cache.
    std::string preview_key;  ///< Display and width of the cached preview.
    std::string preview_data;  ///< Cached JPEG bytes.
    std::chrono::steady_clock::time_point preview_stamp {};  ///< When the preview was captured.
  }  // namespace

  rate_limiter_t::rate_limiter_t(int max_events, std::chrono::steady_clock::duration window):
      max_events {max_events},
      window {window} {
  }

  bool rate_limiter_t::try_acquire(std::chrono::steady_clock::time_point now) {
    std::lock_guard lock {mutex};
    if (count == 0 || now - window_start >= window) {
      window_start = now;
      count = 0;
    }
    if (count >= max_events) {
      return false;
    }
    ++count;
    return true;
  }

  std::vector<health_check_t> evaluate_health(const health_probes_t &probes) {
    std::vector<health_check_t> checks;

    {
      health_check_t check {.id = "encoder"};
      if (!probes.encoder_probed) {
        check.status = health_status_e::error;
        check.title = "No working encoder";
        check.detail = "Nova couldn't open a video encoder, so devices can't stream yet.";
        check.fix_kind = fix_kind_e::setting;
        check.fix_value = "encoder";
      } else if (probes.encoder_name == "software") {
        check.status = health_status_e::warn;
        check.title = "Encoding on the CPU";
        check.detail = "No GPU encoder passed the startup test, so streams use more CPU and add latency.";
        check.fix_kind = fix_kind_e::setting;
        check.fix_value = "encoder";
      } else {
        check.title = "GPU encoding active";
        check.detail = std::format("The {} encoder is in use.", probes.encoder_name);
      }
      checks.push_back(std::move(check));
    }

    {
      health_check_t check {.id = "capture"};
      if (probes.capture_method.empty()) {
        check.status = health_status_e::error;
        check.title = "No capture method";
        check.detail = "Nova couldn't find a way to capture this desktop.";
        check.fix_kind = fix_kind_e::setting;
        check.fix_value = "capture";
      } else if (probes.zero_copy.has_value() && !*probes.zero_copy) {
        check.status = health_status_e::warn;
        check.title = "Capture copies frames through the CPU";
        check.detail = std::format("{} capture sends every frame through system memory, which adds latency.", probes.capture_method);
        check.fix_kind = fix_kind_e::setting;
        check.fix_value = "capture";
      } else {
        check.title = "Capture ready";
        check.detail = probes.zero_copy.value_or(false) ?
                         std::format("{} capture keeps frames on the GPU.", probes.capture_method) :
                         std::format("Capturing with {}.", probes.capture_method);
      }
      checks.push_back(std::move(check));
    }

    if (probes.clipboard_enabled && (probes.window_system == "x11" || probes.window_system == "wayland")) {
      health_check_t check {.id = "clipboard"};
      const bool wayland = probes.window_system == "wayland";
      const bool found = wayland ? probes.wl_clipboard_found : probes.xclip_found;
      if (found) {
        check.title = "Clipboard sync ready";
        check.detail = wayland ? "wl-clipboard is installed." : "xclip is installed.";
      } else {
        check.status = health_status_e::warn;
        check.title = "Clipboard sync can't run";
        check.detail = wayland ? "Install wl-clipboard to sync the clipboard on Wayland." : "Install xclip to sync the clipboard on X11.";
        check.fix_kind = fix_kind_e::command;
        check.fix_value = wayland ? "sudo apt install wl-clipboard" : "sudo apt install xclip";
      }
      checks.push_back(std::move(check));
    }

    if (probes.check_input_devices) {
      health_check_t check {.id = "input"};
      if (probes.uinput_writable && probes.uhid_writable) {
        check.title = "Virtual input ready";
        check.detail = "Keyboards, mice, controllers and pens from devices will work.";
      } else {
        check.status = health_status_e::error;
        check.title = "Virtual input is blocked";
        check.detail = !probes.udev_rules_installed ?
                         "Nova's udev rules aren't installed, so it can't create virtual keyboards, mice or controllers." :
                         std::format("Nova can't open {}; log out and back in, or add your user to the input group.", !probes.uinput_writable ? "/dev/uinput" : "/dev/uhid");
        check.fix_kind = fix_kind_e::command;
        check.fix_value = !probes.udev_rules_installed ? "sudo udevadm control --reload-rules && sudo udevadm trigger" : "sudo usermod -aG input $USER";
      }
      checks.push_back(std::move(check));
    }

    if (probes.check_audio_server) {
      health_check_t check {.id = "audio"};
      if (probes.audio_server_reachable) {
        check.title = "Audio ready";
        check.detail = "Nova creates its own virtual speakers for each stream.";
      } else {
        check.status = health_status_e::error;
        check.title = "No sound server";
        check.detail = "Nova couldn't reach PipeWire or PulseAudio, so streams will have no sound.";
        check.fix_kind = fix_kind_e::command;
        check.fix_value = "systemctl --user restart pipewire pipewire-pulse";
      }
      checks.push_back(std::move(check));
    }

    for (const auto &output : probes.outputs) {
      if (probes.app_running || !output.connected || !output.width || !output.mode_width) {
        continue;
      }
      if (output.width == output.mode_width && output.height == output.mode_height) {
        continue;
      }
      checks.push_back({
        .id = "display-" + output.name,
        .status = health_status_e::warn,
        .title = std::format("{} isn't at its full resolution", output.name),
        .detail = std::format("It shows {}×{} while its mode is {}×{}, often left behind by a stream's prep command.", output.width, output.height, output.mode_width, output.mode_height),
        .fix_kind = fix_kind_e::command,
        .fix_value = std::format("xrandr --output {} --auto", output.name),
      });
    }

    if (probes.nvidia_drm_modeset.has_value()) {
      checks.push_back({
        .id = "nvidia-drm-modeset",
        .title = *probes.nvidia_drm_modeset ? "NVIDIA kernel modesetting on" : "NVIDIA kernel modesetting off",
        .detail = *probes.nvidia_drm_modeset ? "KMS capture and display-synced pacing are available." : "KMS capture and display-synced pacing aren't available; NvFBC or X11 capture is used instead.",
      });
    }

    if (probes.check_wake_on_lan) {
      health_check_t check {.id = "wake-on-lan"};
      if (probes.wol_interface.empty()) {
        check.status = health_status_e::warn;
        check.title = "No wired network card for Wake-on-LAN";
        check.detail = "Devices can't wake this host from sleep: Nova found no physical network interface.";
      } else if (!probes.ethtool_found) {
        check.status = health_status_e::warn;
        check.title = "Wake-on-LAN state unknown";
        check.detail = std::format("Install ethtool so Nova can check whether {} ({}) wakes on a magic packet.", probes.wol_interface, probes.wol_mac);
        check.fix_kind = fix_kind_e::command;
        check.fix_value = "sudo apt install ethtool";
      } else if (!probes.wol_supported.has_value() || !probes.wol_enabled.has_value()) {
        check.status = health_status_e::warn;
        check.title = "Wake-on-LAN state unknown";
        check.detail = std::format("The driver of {} doesn't report Wake-on-LAN.", probes.wol_interface);
      } else if (!*probes.wol_supported) {
        check.status = health_status_e::warn;
        check.title = "Network card can't wake the host";
        check.detail = std::format("{} doesn't support waking on a magic packet.", probes.wol_interface);
      } else if (!*probes.wol_enabled) {
        check.status = health_status_e::warn;
        check.title = "Wake-on-LAN is off";
        check.detail = std::format("{} ({}) won't wake the host. Turn it on, and make it persistent in NetworkManager or a systemd .link file.", probes.wol_interface, probes.wol_mac);
        check.fix_kind = fix_kind_e::command;
        check.fix_value = std::format("sudo ethtool -s {} wol g", probes.wol_interface);
      } else {
        check.title = "Wake-on-LAN ready";
        check.detail = std::format("{} ({}) wakes the host on a magic packet from the local network.", probes.wol_interface, probes.wol_mac);
      }
      checks.push_back(std::move(check));
    }

    if (probes.pcsleep_enabled && !probes.can_suspend.empty()) {
      health_check_t check {.id = "sleep"};
      if (probes.can_suspend == "yes") {
        check.title = "Devices can put the host to sleep";
        check.detail = "Devices with the Sleep permission can suspend this host.";
      } else if (probes.can_suspend == "challenge") {
        check.status = health_status_e::warn;
        check.title = "Sleep needs a password";
        check.detail = "The system asks for a password before suspending, so devices can't put the host to sleep. Allow it once with the script shipped with Nova.";
        check.fix_kind = fix_kind_e::command;
        check.fix_value = "sudo /usr/share/nova-host/nova-allow-suspend";
      } else {
        check.status = health_status_e::warn;
        check.title = "Sleep isn't available";
        check.detail = "systemd-logind says this host can't suspend (" + probes.can_suspend + ").";
      }
      checks.push_back(std::move(check));
    }

    if (!probes.exposed_files.empty()) {
      checks.push_back({
        .id = "private-files",
        .status = health_status_e::warn,
        .title = "Private files are readable by other users",
        .detail = std::format("{} and {} more are not owner-only; Nova tightens them at start-up.", probes.exposed_files.front(), probes.exposed_files.size() - 1),
        .fix_kind = fix_kind_e::command,
        .fix_value = "systemctl --user restart app-io.github.f_e_n_y_x.NovaHost.service",
      });
    }

    if (probes.origin_web_ui_allowed == "wan") {
      checks.push_back({
        .id = "web-ui-exposure",
        .status = health_status_e::warn,
        .title = "Web UI accepts internet connections",
        .detail = "Anyone who can reach port 47990 can see the sign-in page. Limit it to your network unless you need remote access.",
        .fix_kind = fix_kind_e::setting,
        .fix_value = "origin_web_ui_allowed",
      });
    }

    return checks;
  }

  health_probes_t collect_health_probes() {
    health_probes_t probes;

    const auto encoder = video::get_encoder_summary();
    probes.encoder_probed = encoder.probed;
    probes.encoder_name = encoder.name;
    probes.capture_method = platf::capture_backend_name(encoder.mem_type);
    probes.zero_copy = zero_copy_for(probes.capture_method, encoder.mem_type);
    probes.window_system = platf::window_system_name();
    probes.clipboard_enabled = config::input.clipboard_sync;
    probes.xclip_found = on_path("xclip");
    probes.wl_clipboard_found = on_path("wl-copy") && on_path("wl-paste");
    probes.app_running = proc::proc.running() > 0;
    probes.outputs = cached_outputs();
    probes.origin_web_ui_allowed = config::nvhttp.origin_web_ui_allowed;

#ifdef __linux__
    probes.check_input_devices = true;
    probes.uinput_writable = access("/dev/uinput", W_OK) == 0 || access("/dev/input/uinput", W_OK) == 0;
    probes.uhid_writable = access("/dev/uhid", W_OK) == 0;
    for (const auto *dir : {"/usr/lib/udev/rules.d", "/lib/udev/rules.d", "/etc/udev/rules.d"}) {
      std::error_code ec;
      if (std::filesystem::exists(std::filesystem::path {dir} / "60-sunshine.rules", ec)) {
        probes.udev_rules_installed = true;
      }
    }

    probes.check_audio_server = true;
    probes.audio_server_reachable = platf::audio_control() != nullptr;

    if (const auto modeset = read_first_line("/sys/module/nvidia_drm/parameters/modeset"); !modeset.empty()) {
      probes.nvidia_drm_modeset = modeset == "Y" || modeset == "1";
    }

    probes.check_wake_on_lan = true;
    const std::filesystem::path sysfs_net {"/sys/class/net"};
    if (const auto iface = platf::nic::primary_physical_interface(sysfs_net)) {
      probes.wol_interface = *iface;
      probes.wol_mac = platf::nic::mac_of(sysfs_net, *iface).value_or("");
      probes.ethtool_found = on_path("ethtool");
      if (probes.ethtool_found) {
        // Read-only query; `ethtool <iface>` reads Wake-on over netlink without privileges.
        const auto result = host_commands::run_sync({.id = "ethtool", .name = "ethtool", .cmd = "ethtool " + *iface, .timeout = 3s}, host_commands::build_env({{"LC_ALL", "C"}}));
        if (const auto wol = platf::nic::parse_ethtool_wol(result.output)) {
          probes.wol_supported = wol->magic_supported();
          probes.wol_enabled = wol->magic_enabled();
        }
      }
    }
    probes.pcsleep_enabled = config::sunshine.pcsleep_enabled;
    if (probes.pcsleep_enabled) {
      probes.can_suspend = host_power::can_suspend();
    }
#endif
    for (const auto &path : secure_files::private_paths()) {
      if (secure_files::is_exposed(path)) {
        probes.exposed_files.push_back(path.filename().string());
      }
    }
    return probes;
  }

  nlohmann::json health_to_json(const std::vector<health_check_t> &checks) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto &check : checks) {
      nlohmann::json item;
      item["id"] = check.id;
      item["status"] = status_name(check.status);
      item["title"] = check.title;
      item["detail"] = check.detail;
      if (check.fix_kind == fix_kind_e::none) {
        item["fix"] = nullptr;
      } else {
        item["fix"] = {{"kind", fix_name(check.fix_kind)}, {"value", check.fix_value}};
      }
      out.push_back(std::move(item));
    }
    return out;
  }

  nlohmann::json host_info_json() {
    const auto encoder = video::get_encoder_summary();
    const auto capture = platf::capture_backend_name(encoder.mem_type);

    nlohmann::json info;
    info["name"] = config::nvhttp.sunshine_name.empty() ? boost::asio::ip::host_name() : config::nvhttp.sunshine_name;
    info["version"] = PROJECT_VERSION;
    info["platform"] = SUNSHINE_PLATFORM;
    info["os_pretty"] = os_pretty_name();
    info["kernel"] = kernel_release();

    const char *desktop = std::getenv("XDG_CURRENT_DESKTOP");
    info["desktop_session"] = {
      {"type", platf::window_system_name()},
      {"desktop", desktop ? desktop : ""},
    };
    info["gpu"] = gpus_json();

    nlohmann::json codecs = nlohmann::json::array();
    if (!encoder.h264_codec.empty()) {
      codecs.push_back("H.264");
    }
    if (!encoder.hevc_codec.empty()) {
      codecs.push_back("HEVC");
    }
    if (!encoder.av1_codec.empty()) {
      codecs.push_back("AV1");
    }
    info["encoders"] = {
      {"probed", encoder.probed},
      {"active", encoder.name},
      {"codecs", codecs},
      {"av1", !encoder.av1_codec.empty()},
      {"hevc_main10", encoder.hevc_main10},
      {"av1_main10", encoder.av1_main10},
      {"per_codec", {
                      {"h264", encoder.h264_codec},
                      {"hevc", encoder.hevc_codec},
                      {"av1", encoder.av1_codec},
                    }},
      {"yuv444", {{"h264", encoder.yuv444[0]}, {"hevc", encoder.yuv444[1]}, {"av1", encoder.yuv444[2]}}},
      {"memory", mem_type_name(encoder.mem_type)},
      {"implementation", encoder.implementation},
      {"ref_frame_invalidation", encoder.ref_frame_invalidation},
    };

    const auto zero_copy = zero_copy_for(capture, encoder.mem_type);
    info["capture"] = {
      {"method", capture},
      {"zero_copy", zero_copy ? nlohmann::json(*zero_copy) : nlohmann::json(nullptr)},
    };
    info["uptime_s"] = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - process_start).count();
    return info;
  }

  nlohmann::json displays_json(const std::vector<platf::capture_output_t> &outputs) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto &output : outputs) {
      out.push_back({
        {"name", output.name},
        {"index", output.index},
        {"connected", output.connected},
        {"primary", output.primary},
        {"x", output.x},
        {"y", output.y},
        {"width", output.width},
        {"height", output.height},
        {"mode_width", output.mode_width},
        {"mode_height", output.mode_height},
        {"refresh_hz", output.refresh_hz},
        {"configured", !config::video.output_name.empty() && (config::video.output_name == output.name || config::video.output_name == std::to_string(output.index))},
      });
    }
    return out;
  }

  nlohmann::json audio_sinks_json(const std::vector<platf::sink_desc_t> &sinks, const std::string &configured) {
    nlohmann::json list = nlohmann::json::array();
    for (const auto &sink : sinks) {
      list.push_back({
        {"name", sink.name},
        {"description", sink.description},
        {"virtual", sink.is_virtual},
        {"configured", !configured.empty() && sink.name == configured},
      });
    }
    return {{"configured", configured}, {"sinks", list}};
  }

  std::vector<std::uint8_t> downscale_to_rgb(const platf::preview_frame_t &frame, int max_width, int &out_width, int &out_height) {
    out_width = 0;
    out_height = 0;
    if (frame.width <= 0 || frame.height <= 0 || frame.bgra.size() < static_cast<std::size_t>(frame.width) * frame.height * 4) {
      return {};
    }

    const int width = std::max(1, std::min(frame.width, max_width));
    const int height = std::max(1, static_cast<int>(static_cast<long long>(frame.height) * width / frame.width));
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(width) * height * 3);

    for (int y = 0; y < height; ++y) {
      const int sy0 = static_cast<int>(static_cast<long long>(y) * frame.height / height);
      const int sy1 = std::max(sy0 + 1, static_cast<int>(static_cast<long long>(y + 1) * frame.height / height));
      for (int x = 0; x < width; ++x) {
        const int sx0 = static_cast<int>(static_cast<long long>(x) * frame.width / width);
        const int sx1 = std::max(sx0 + 1, static_cast<int>(static_cast<long long>(x + 1) * frame.width / width));
        std::uint32_t b = 0;
        std::uint32_t g = 0;
        std::uint32_t r = 0;
        std::uint32_t n = 0;
        for (int sy = sy0; sy < sy1; ++sy) {
          const auto *row = frame.bgra.data() + static_cast<std::size_t>(sy) * frame.width * 4;
          for (int sx = sx0; sx < sx1; ++sx) {
            const auto *px = row + static_cast<std::size_t>(sx) * 4;
            b += px[0];
            g += px[1];
            r += px[2];
            ++n;
          }
        }
        auto *dst = rgb.data() + (static_cast<std::size_t>(y) * width + x) * 3;
        dst[0] = static_cast<std::uint8_t>(r / n);
        dst[1] = static_cast<std::uint8_t>(g / n);
        dst[2] = static_cast<std::uint8_t>(b / n);
      }
    }

    out_width = width;
    out_height = height;
    return rgb;
  }

  std::string encode_jpeg(const std::vector<std::uint8_t> &rgb, int width, int height, int quality) {
    if (width <= 0 || height <= 0 || rgb.size() < static_cast<std::size_t>(width) * height * 3) {
      return {};
    }
    std::string out;
    auto write = [](void *context, void *data, int size) {
      static_cast<std::string *>(context)->append(static_cast<const char *>(data), static_cast<std::size_t>(size));
    };
    if (!stbi_write_jpg_to_func(write, &out, width, height, 3, rgb.data(), std::clamp(quality, 1, 100))) {
      return {};
    }
    return out;
  }

  preview_result_t preview_jpeg(const std::string &display, int width) {
    width = std::clamp(width, 160, 1280);
    const auto target = display.empty() ? config::video.output_name : display;
    const auto key = std::format("{}@{}", target, width);
    const auto now = std::chrono::steady_clock::now();

    {
      std::lock_guard lock {preview_mutex};
      if (key == preview_key && !preview_data.empty() && now - preview_stamp < 1s) {
        return {.jpeg = preview_data};
      }
    }

    if (!preview_limiter.try_acquire(now)) {
      return {.error = "Too many preview requests; try again in a moment", .http_status = 429};
    }

    std::string error;
    const auto frame = platf::capture_preview_frame(target, error);
    if (!frame) {
      return {.error = error.empty() ? "Capture unavailable" : error, .http_status = 503};
    }

    int out_width = 0;
    int out_height = 0;
    const auto rgb = downscale_to_rgb(*frame, width, out_width, out_height);
    auto jpeg = encode_jpeg(rgb, out_width, out_height, 80);
    if (jpeg.empty()) {
      return {.error = "Couldn't encode the preview", .http_status = 500};
    }

    std::lock_guard lock {preview_mutex};
    preview_key = key;
    preview_data = jpeg;
    preview_stamp = std::chrono::steady_clock::now();
    return {.jpeg = std::move(jpeg)};
  }

  std::vector<health_check_t> cached_health() {
    return health_cache.get([] {
      return evaluate_health(collect_health_probes());
    });
  }

  std::vector<platf::capture_output_t> cached_outputs() {
    return outputs_cache.get([] {
      return platf::enumerate_outputs();
    });
  }
}  // namespace host_info
