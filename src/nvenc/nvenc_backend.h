/**
 * @file src/nvenc/nvenc_backend.h
 * @brief Declarations for choosing between the native and the FFmpeg NVENC encoder.
 */
#pragma once

// standard includes
#include <algorithm>
#include <optional>
#include <string_view>
#include <vector>

namespace nvenc {

  /**
   * @brief Which NVENC implementation the host may use (`nvenc_backend`).
   *
   * Only Linux has two implementations. Windows always uses its native D3D11 encoder.
   */
  enum class nvenc_backend {
    automatic,  ///< Native CUDA encoder, falling back to FFmpeg when it doesn't work.
    native,  ///< Native CUDA encoder only; never FFmpeg NVENC.
    ffmpeg,  ///< FFmpeg `h264_nvenc`/`hevc_nvenc` only (no reference frame invalidation).
  };

  /**
   * @brief Parse an `nvenc_backend` configuration value.
   *
   * @param value Configuration value: `auto`, `native` or `ffmpeg`.
   * @return Parsed backend, or empty for an unknown value.
   */
  std::optional<nvenc_backend> nvenc_backend_from_view(std::string_view value);

  /**
   * @brief Get the configuration spelling of a backend.
   *
   * @param backend Backend to name.
   * @return `auto`, `native` or `ffmpeg`.
   */
  std::string_view to_string(nvenc_backend backend);

  /**
   * @brief Whether the native encoder may be probed and used.
   *
   * @param backend Configured backend.
   * @return `false` only for `ffmpeg`.
   */
  constexpr bool native_allowed(nvenc_backend backend) {
    return backend != nvenc_backend::ffmpeg;
  }

  /**
   * @brief Whether FFmpeg NVENC may be probed and used.
   *
   * @param backend Configured backend.
   * @return `false` only for `native`.
   */
  constexpr bool ffmpeg_allowed(nvenc_backend backend) {
    return backend != nvenc_backend::native;
  }

  /**
   * @brief Whether a stream whose native session fails to start may switch to FFmpeg NVENC.
   *
   * @param backend Configured backend.
   * @return `true` only for `auto`.
   */
  constexpr bool session_fallback_allowed(nvenc_backend backend) {
    return backend == nvenc_backend::automatic;
  }

  /**
   * @brief Drop the NVENC implementations the configured backend doesn't allow from a probe list.
   *
   * The native encoder is kept ahead of the FFmpeg one, so probing tries it first.
   *
   * @tparam T Encoder type.
   * @param encoders Probe list, in priority order; modified in place.
   * @param backend Configured backend.
   * @param native Native NVENC encoder (may be null when it isn't built).
   * @param ffmpeg FFmpeg NVENC encoder.
   */
  template<typename T>
  void filter_nvenc_encoders(std::vector<T *> &encoders, nvenc_backend backend, const T *native, const T *ffmpeg) {
    std::erase_if(encoders, [&](const T *encoder) {
      return (encoder == native && !native_allowed(backend)) || (encoder == ffmpeg && native && !ffmpeg_allowed(backend));
    });
  }

}  // namespace nvenc
