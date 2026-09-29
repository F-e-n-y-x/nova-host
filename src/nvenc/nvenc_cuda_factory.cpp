/**
 * @file src/nvenc/nvenc_cuda_factory.cpp
 * @brief Definitions for runtime NVENC SDK selection of the Linux CUDA encoder.
 */
#if defined(__linux__) && defined(SUNSHINE_BUILD_CUDA)

  // this include
  #include "nvenc_cuda_factory.h"

  // standard includes
  #include <cstdint>
  #include <utility>

  // platform includes
  #include <dlfcn.h>

  // local includes
  #include "nvenc_cuda_factory_versions.h"
  #include "src/logging.h"

namespace {

  constexpr auto nvenc_library_name = "libnvidia-encode.so.1";

  using get_max_supported_version_fn = std::uint32_t (*)(std::uint32_t *);

}  // namespace

namespace nvenc {

  shared_library make_shared_library(void *handle) {
    if (!handle) {
      return {};
    }
    return shared_library(handle, [](void *library) {
      dlclose(library);
    });
  }

  nvenc_cuda_factory::nvenc_cuda_factory(shared_library driver, nvenc_sdk_version sdk_version, create_encoder_fn create):
      driver(std::move(driver)),
      selected_sdk_version(sdk_version),
      create(std::move(create)) {
  }

  std::shared_ptr<nvenc_cuda_factory> nvenc_cuda_factory::get() {
    return get({
      []() {
        return make_shared_library(dlopen(nvenc_library_name, RTLD_LAZY | RTLD_LOCAL));
      },
      [](void *library, const char *symbol) {
        return dlsym(library, symbol);
      },
    });
  }

  std::shared_ptr<nvenc_cuda_factory> nvenc_cuda_factory::get(const nvenc_cuda_runtime_api &runtime_api) {
    auto driver = runtime_api.load_driver();
    if (!driver) {
      BOOST_LOG(warning) << "NvEnc: couldn't load " << nvenc_library_name;
      return {};
    }

    const auto get_max_version = reinterpret_cast<get_max_supported_version_fn>(runtime_api.get_symbol(driver.get(), "NvEncodeAPIGetMaxSupportedVersion"));
    if (!get_max_version) {
      BOOST_LOG(error) << "NvEnc: no NvEncodeAPIGetMaxSupportedVersion() in " << nvenc_library_name;
      return {};
    }

    std::uint32_t packed_max_version = 0;
    if (get_max_version(&packed_max_version) != 0U) {
      BOOST_LOG(error) << "NvEnc: NvEncodeAPIGetMaxSupportedVersion() failed";
      return {};
    }

    const auto max_version = decode_nvenc_driver_version(packed_max_version);
    const auto sdk_version = select_nvenc_sdk_version(max_version);
    BOOST_LOG(debug) << "NvEnc: driver supports NVENC API " << max_version / 100 << '.' << max_version % 100;
    switch (sdk_version) {
      case nvenc_sdk_version::sdk_13_1:
        return std::make_shared<nvenc_cuda_factory>(std::move(driver), sdk_version, detail::create_nvenc_cuda_1301);
      case nvenc_sdk_version::sdk_13_0:
        return std::make_shared<nvenc_cuda_factory>(std::move(driver), sdk_version, detail::create_nvenc_cuda_1300);
      case nvenc_sdk_version::sdk_12_0:
        return std::make_shared<nvenc_cuda_factory>(std::move(driver), sdk_version, detail::create_nvenc_cuda_1200);
      case nvenc_sdk_version::sdk_11_0:
        return std::make_shared<nvenc_cuda_factory>(std::move(driver), sdk_version, detail::create_nvenc_cuda_1100);
      case nvenc_sdk_version::unsupported:
      default:
        BOOST_LOG(error) << "NvEnc: the driver's NVENC API " << max_version / 100 << '.' << max_version % 100
                         << " is older than 11.0; native NVENC needs driver 470 or newer";
        return {};
    }
  }

  std::unique_ptr<nvenc_cuda_interface> nvenc_cuda_factory::create_nvenc_cuda(int cuda_device, void *cuda_stream) const {
    return create(cuda_device, cuda_stream, driver);
  }

  nvenc_sdk_version nvenc_cuda_factory::sdk_version() const {
    return selected_sdk_version;
  }

}  // namespace nvenc
#endif
