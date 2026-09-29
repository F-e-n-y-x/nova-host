/**
 * @file src/nvenc/nvenc_cuda_factory_versions.h
 * @brief Declarations for SDK-specific Linux CUDA NVENC encoder constructors.
 */
#pragma once
#if defined(__linux__) && defined(SUNSHINE_BUILD_CUDA)

  // standard includes
  #include <memory>

  // local includes
  #include "nvenc_cuda_factory.h"
  #include "nvenc_cuda_interface.h"

namespace nvenc::detail {

  /**
   * @brief Create an SDK 11.0 CUDA encoder.
   *
   * @param cuda_device CUDA device ordinal.
   * @param cuda_stream Stream that writes the input surface.
   * @param driver Shared NVENC driver library.
   * @return SDK-neutral encoder instance.
   */
  std::unique_ptr<nvenc_cuda_interface> create_nvenc_cuda_1100(int cuda_device, void *cuda_stream, shared_library driver);
  /**
   * @brief Create an SDK 12.0 CUDA encoder.
   *
   * @param cuda_device CUDA device ordinal.
   * @param cuda_stream Stream that writes the input surface.
   * @param driver Shared NVENC driver library.
   * @return SDK-neutral encoder instance.
   */
  std::unique_ptr<nvenc_cuda_interface> create_nvenc_cuda_1200(int cuda_device, void *cuda_stream, shared_library driver);
  /**
   * @brief Create an SDK 13.0 CUDA encoder.
   *
   * @param cuda_device CUDA device ordinal.
   * @param cuda_stream Stream that writes the input surface.
   * @param driver Shared NVENC driver library.
   * @return SDK-neutral encoder instance.
   */
  std::unique_ptr<nvenc_cuda_interface> create_nvenc_cuda_1300(int cuda_device, void *cuda_stream, shared_library driver);
  /**
   * @brief Create an SDK 13.1 CUDA encoder.
   *
   * @param cuda_device CUDA device ordinal.
   * @param cuda_stream Stream that writes the input surface.
   * @param driver Shared NVENC driver library.
   * @return SDK-neutral encoder instance.
   */
  std::unique_ptr<nvenc_cuda_interface> create_nvenc_cuda_1301(int cuda_device, void *cuda_stream, shared_library driver);

}  // namespace nvenc::detail
#endif
