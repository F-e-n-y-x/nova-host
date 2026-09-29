/**
 * @file src/nvenc/nvenc_backend.cpp
 * @brief Definitions for choosing between the native and the FFmpeg NVENC encoder.
 */
// this include
#include "nvenc_backend.h"

namespace nvenc {

  std::optional<nvenc_backend> nvenc_backend_from_view(std::string_view value) {
    using enum nvenc_backend;
    if (value == "auto") {
      return automatic;
    }
    if (value == "native") {
      return native;
    }
    if (value == "ffmpeg") {
      return ffmpeg;
    }
    return std::nullopt;
  }

  std::string_view to_string(nvenc_backend backend) {
    using enum nvenc_backend;
    switch (backend) {
      case native:
        return "native";
      case ffmpeg:
        return "ffmpeg";
      case automatic:
      default:
        return "auto";
    }
  }

}  // namespace nvenc
