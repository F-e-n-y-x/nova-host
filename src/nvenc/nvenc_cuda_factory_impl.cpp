/**
 * @file src/nvenc/nvenc_cuda_factory_impl.cpp
 * @brief SDK-specific constructors used by the Linux CUDA NVENC factory.
 */
#if defined(__linux__) && defined(SUNSHINE_BUILD_CUDA)

  #ifndef NVENC_NAMESPACE
    #error NVENC_NAMESPACE must identify the version-specific implementation namespace
  #endif

  #ifndef NVENC_FACTORY_SUFFIX
    #error NVENC_FACTORY_SUFFIX must identify the version-specific constructor suffix
  #endif

  // standard includes
  #include <utility>

  // local includes
  #include "nvenc_cuda.h"
  #include "nvenc_cuda_factory_versions.h"

  #ifndef DOXYGEN
    #define NVENC_CONCAT_IMPL(left, right) left##right
    #define NVENC_CONCAT(left, right) NVENC_CONCAT_IMPL(left, right)

namespace nvenc::detail {

  std::unique_ptr<nvenc_cuda_interface> NVENC_CONCAT(create_nvenc_cuda_, NVENC_FACTORY_SUFFIX)(
    int cuda_device,
    void *cuda_stream,
    shared_library driver
  ) {
    return std::make_unique<NVENC_NAMESPACE::nvenc_cuda>(cuda_device, cuda_stream, std::move(driver));
  }

}  // namespace nvenc::detail

    #undef NVENC_CONCAT
    #undef NVENC_CONCAT_IMPL
  #endif
#endif
