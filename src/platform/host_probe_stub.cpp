/**
 * @file src/platform/host_probe_stub.cpp
 * @brief Host-probe fallbacks for platforms without a native implementation yet.
 */
// local includes
#include "src/config.h"
#include "src/platform/common.h"

namespace platf {
  std::vector<capture_output_t> enumerate_outputs() {
    return {};
  }

  std::string capture_backend_name([[maybe_unused]] mem_type_e hwdevice_type) {
    if (!config::video.capture.empty()) {
      return config::video.capture;
    }
#ifdef _WIN32
    return "ddx";
#elif defined(__APPLE__)
    return "avfoundation";
#else
    return {};
#endif
  }

  std::string window_system_name() {
#ifdef _WIN32
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "none";
#endif
  }

  std::optional<preview_frame_t> capture_preview_frame([[maybe_unused]] const std::string &display_name, std::string &error) {
    error = "Preview isn't available on this platform yet";
    return std::nullopt;
  }
}  // namespace platf
