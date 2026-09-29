/**
 * @file src/nvenc/nvenc_cuda_interface.h
 * @brief Declarations for the SDK-neutral NVENC interface with CUDA device-memory input.
 */
#pragma once

// standard includes
#include <cstddef>
#include <cstdint>

// local includes
#include "nvenc_encoder.h"

namespace nvenc {

  /**
   * @brief Pitched CUDA device buffer that NVENC reads each frame from.
   *
   * Planes are stacked vertically in one allocation with the same pitch:
   * NV12 has `height` luma rows followed by `height / 2` interleaved chroma rows,
   * YUV 4:4:4 has three planes of `height` rows each.
   */
  struct nvenc_cuda_surface {
    std::uintptr_t device_ptr = 0;  ///< `CUdeviceptr` of the first luma row.
    std::size_t pitch = 0;  ///< Bytes between the starts of two rows.
    std::uint32_t width = 0;  ///< Encoded width in pixels.
    std::uint32_t height = 0;  ///< Encoded height in pixels.
    bool yuv444 = false;  ///< Planar YUV 4:4:4 instead of NV12.
  };

  /**
   * @brief SDK-neutral standalone NVENC encoder that consumes CUDA device memory.
   */
  class nvenc_cuda_interface: public virtual nvenc_encoder {
  public:
    /**
     * @brief Get the input surface of the created encoder.
     *
     * @return Input surface, with a null `device_ptr` before `create_encoder()` succeeds.
     */
    virtual nvenc_cuda_surface get_input_surface() const = 0;
  };

}  // namespace nvenc
