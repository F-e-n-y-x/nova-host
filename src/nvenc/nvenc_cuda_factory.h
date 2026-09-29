/**
 * @file src/nvenc/nvenc_cuda_factory.h
 * @brief Declarations for runtime NVENC SDK selection of the Linux CUDA encoder.
 */
#pragma once
#if defined(__linux__) && defined(SUNSHINE_BUILD_CUDA)

  // standard includes
  #include <functional>
  #include <memory>

  // local includes
  #include "nvenc_cuda_interface.h"
  #include "nvenc_version.h"

namespace nvenc {

  /**
   * @brief Shared ownership of a `dlopen()` handle, closed with the last owner.
   */
  using shared_library = std::shared_ptr<void>;

  /**
   * @brief Wrap a `dlopen()` handle in shared ownership.
   *
   * @param handle Handle returned by `dlopen()`, may be null.
   * @return Shared handle, empty when `handle` is null.
   */
  shared_library make_shared_library(void *handle);

  /**
   * @brief Runtime operations used to discover the installed NVENC API.
   */
  struct nvenc_cuda_runtime_api {
    using load_driver_fn = std::function<shared_library()>;  ///< Load `libnvidia-encode.so.1`.
    using get_symbol_fn = std::function<void *(void *, const char *)>;  ///< Resolve a driver export.

    load_driver_fn load_driver;  ///< Function that loads the NVENC driver library.
    get_symbol_fn get_symbol;  ///< Function that resolves a driver export.
  };

  /**
   * @brief Factory bound to the newest NVENC SDK supported by the installed Linux driver.
   */
  class nvenc_cuda_factory {
  public:
    /**
     * @brief SDK-specific encoder constructor.
     */
    using create_encoder_fn = std::function<std::unique_ptr<nvenc_cuda_interface>(int, void *, shared_library)>;

    /**
     * @brief Construct a factory bound to one NVENC SDK implementation.
     *
     * @param driver Shared NVENC driver library.
     * @param sdk_version Selected SDK version.
     * @param create CUDA encoder constructor for that SDK.
     */
    nvenc_cuda_factory(shared_library driver, nvenc_sdk_version sdk_version, create_encoder_fn create);

    /**
     * @brief Load the NVENC driver and select a compatible SDK implementation.
     *
     * @return Initialized factory, or an empty pointer when NVENC is unavailable.
     */
    static std::shared_ptr<nvenc_cuda_factory> get();

    /**
     * @brief Select a compatible SDK implementation using the supplied runtime operations.
     *
     * @param runtime_api Runtime operations used to load and query the NVENC driver.
     * @return Initialized factory, or an empty pointer when NVENC is unavailable.
     */
    static std::shared_ptr<nvenc_cuda_factory> get(const nvenc_cuda_runtime_api &runtime_api);

    /**
     * @brief Create a native CUDA NVENC encoder.
     *
     * @param cuda_device CUDA device ordinal whose primary context the encoder uses.
     * @param cuda_stream `CUstream` that writes the input surface.
     * @return SDK-neutral encoder instance.
     */
    std::unique_ptr<nvenc_cuda_interface> create_nvenc_cuda(int cuda_device, void *cuda_stream) const;

    /**
     * @brief Get the SDK implementation selected for this factory.
     *
     * @return Selected NVENC SDK version.
     */
    nvenc_sdk_version sdk_version() const;

  private:
    shared_library driver;  ///< Keeps `libnvidia-encode.so.1` loaded for created encoders.
    nvenc_sdk_version selected_sdk_version;  ///< SDK implementation selected at load time.
    create_encoder_fn create;  ///< Constructor for the selected SDK.
  };

}  // namespace nvenc
#endif
