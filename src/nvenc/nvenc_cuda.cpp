/**
 * @file src/nvenc/nvenc_cuda.cpp
 * @brief Definitions for the Linux NVENC encoder with CUDA device-memory input.
 */
#if defined(__linux__) && defined(SUNSHINE_BUILD_CUDA)
  // this include
  #include "nvenc_cuda.h"

  // standard includes
  #include <memory>

  // platform includes
  #include <dlfcn.h>

  // local includes
  #include "nvenc_utils.h"

namespace NVENC_NAMESPACE {

  nvenc_cuda::context_guard_t::context_guard_t(nvenc_cuda &parent):
      parent(parent) {
    pushed = parent.cuda_context && !parent.cuda_failed(parent.cuda_functions.cuCtxPushCurrent(parent.cuda_context), "cuCtxPushCurrent()");
  }

  nvenc_cuda::context_guard_t::~context_guard_t() {
    if (pushed) {
      CUcontext popped = nullptr;
      parent.cuda_failed(parent.cuda_functions.cuCtxPopCurrent(&popped), "cuCtxPopCurrent()");
    }
  }

  nvenc_cuda::nvenc_cuda(int cuda_device, void *cuda_stream, ::nvenc::shared_library driver):
      nvenc_base(NV_ENC_DEVICE_TYPE_CUDA),
      cuda_device_ordinal(cuda_device),
      cuda_stream(static_cast<CUstream>(cuda_stream)),
      driver(std::move(driver)) {
  }

  nvenc_cuda::~nvenc_cuda() {
    if (encoder) {
      destroy_encoder();
    }
    free_input_buffer();

    if (cuda_context) {
      cuda_failed(cuda_functions.cuDevicePrimaryCtxRelease(cuda_device), "cuDevicePrimaryCtxRelease()");
      cuda_context = nullptr;
      device = nullptr;
    }
    if (cuda_functions.library) {
      dlclose(cuda_functions.library);
    }
    cuda_functions = {};
  }

  bool nvenc_cuda::create_encoder(
    const ::nvenc::nvenc_config &config,
    const video::config_t &client_config,
    const video::sunshine_colorspace_t &colorspace,
    platf::pix_fmt_e buffer_format
  ) {
    if (!nvenc && !init_library()) {
      return false;
    }
    context_guard_t guard {*this};
    if (!guard) {
      return false;
    }
    if (!nvenc_base::create_encoder(config, client_config, colorspace, buffer_format)) {
      return false;
    }

    // Let NVENC wait on the conversion stream instead of blocking the CPU on it each frame.
    io_streams = false;
    if (cuda_stream && nvenc->nvEncSetIOCudaStreams) {
      // NVENC keeps these pointers, so they must point at a member that outlives the session.
      if (nvenc_failed(nvenc->nvEncSetIOCudaStreams(encoder, &cuda_stream, &cuda_stream))) {
        BOOST_LOG(warning) << "NvEnc: NvEncSetIOCudaStreams() failed, synchronizing on the CPU instead: " << last_nvenc_error_string;
      } else {
        io_streams = true;
      }
    }
    return true;
  }

  void nvenc_cuda::destroy_encoder() {
    context_guard_t guard {*this};
    if (cuda_stream && cuda_context) {
      // Conversion work may still be queued against the input surface.
      cuda_failed(cuda_functions.cuStreamSynchronize(cuda_stream), "cuStreamSynchronize()");
    }
    nvenc_base::destroy_encoder();
    free_input_buffer();
    io_streams = false;
  }

  ::nvenc::nvenc_encoded_frame nvenc_cuda::encode_frame(uint64_t frame_index, bool force_idr) {
    context_guard_t guard {*this};
    if (!guard) {
      return {};
    }
    return nvenc_base::encode_frame(frame_index, force_idr);
  }

  bool nvenc_cuda::invalidate_ref_frames(uint64_t first_frame, uint64_t last_frame) {
    context_guard_t guard {*this};
    if (!guard) {
      return false;
    }
    return nvenc_base::invalidate_ref_frames(first_frame, last_frame);
  }

  bool nvenc_cuda::set_bitrate(int bitrate_kbps) {
    context_guard_t guard {*this};
    if (!guard) {
      return false;
    }
    return nvenc_base::set_bitrate(bitrate_kbps);
  }

  ::nvenc::nvenc_cuda_surface nvenc_cuda::get_input_surface() const {
    if (!registered_input_buffer) {
      return {};
    }
    return {
      .device_ptr = static_cast<std::uintptr_t>(input_buffer),
      .pitch = input_pitch,
      .width = encoder_params.width,
      .height = encoder_params.height,
      .yuv444 = encoder_params.buffer_format == NV_ENC_BUFFER_FORMAT_YUV444,
    };
  }

  bool nvenc_cuda::load_cuda() {
    if (cuda_functions.library) {
      return true;
    }

    auto library = dlopen("libcuda.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (!library) {
      BOOST_LOG(error) << "NvEnc: couldn't load libcuda.so.1: " << dlerror();
      return false;
    }

    const auto load = [&]<typename T>(T *&location, const char *symbol) {
      location = reinterpret_cast<T *>(dlsym(library, symbol));
      return location != nullptr;
    };
    if (!load(cuda_functions.cuInit, "cuInit") ||
        !load(cuda_functions.cuDeviceGet, "cuDeviceGet") ||
        !load(cuda_functions.cuDevicePrimaryCtxRetain, "cuDevicePrimaryCtxRetain") ||
        !load(cuda_functions.cuDevicePrimaryCtxRelease, "cuDevicePrimaryCtxRelease_v2") ||
        !load(cuda_functions.cuCtxPushCurrent, "cuCtxPushCurrent_v2") ||
        !load(cuda_functions.cuCtxPopCurrent, "cuCtxPopCurrent_v2") ||
        !load(cuda_functions.cuMemAllocPitch, "cuMemAllocPitch_v2") ||
        !load(cuda_functions.cuMemFree, "cuMemFree_v2") ||
        !load(cuda_functions.cuStreamSynchronize, "cuStreamSynchronize")) {
      BOOST_LOG(error) << "NvEnc: missing CUDA functions in libcuda.so.1";
      dlclose(library);
      cuda_functions = {};
      return false;
    }
    cuda_functions.library = library;
    return true;
  }

  bool nvenc_cuda::init_library() {
    if (nvenc) {
      return true;
    }
    if (!driver || !load_cuda()) {
      return false;
    }

    if (cuda_failed(cuda_functions.cuInit(0), "cuInit()") ||
        cuda_failed(cuda_functions.cuDeviceGet(&cuda_device, cuda_device_ordinal), "cuDeviceGet()") ||
        cuda_failed(cuda_functions.cuDevicePrimaryCtxRetain(&cuda_context, cuda_device), "cuDevicePrimaryCtxRetain()")) {
      cuda_context = nullptr;
      return false;
    }
    device = cuda_context;

    auto create_instance = reinterpret_cast<decltype(NvEncodeAPICreateInstance) *>(dlsym(driver.get(), "NvEncodeAPICreateInstance"));
    if (!create_instance) {
      BOOST_LOG(error) << "NvEnc: no NvEncodeAPICreateInstance() in libnvidia-encode.so.1";
      return false;
    }

    auto new_nvenc = std::make_unique<NV_ENCODE_API_FUNCTION_LIST>();
    new_nvenc->version = NV_ENCODE_API_FUNCTION_LIST_VER;
    if (nvenc_failed(create_instance(new_nvenc.get()))) {
      BOOST_LOG(error) << "NvEnc: NvEncodeAPICreateInstance() failed: " << last_nvenc_error_string;
      return false;
    }

    nvenc = std::move(new_nvenc);
    return true;
  }

  bool nvenc_cuda::create_and_register_input_buffer() {
    size_t rows;
    switch (encoder_params.buffer_format) {
      case NV_ENC_BUFFER_FORMAT_NV12:
        rows = encoder_params.height + (encoder_params.height + 1) / 2;
        break;
      case NV_ENC_BUFFER_FORMAT_YUV444:
        rows = static_cast<size_t>(encoder_params.height) * 3;
        break;
      default:
        // The CUDA converters only write 8-bit NV12 and planar 4:4:4.
        BOOST_LOG(error) << "NvEnc: CUDA input supports only NV12 and 8-bit YUV 4:4:4";
        return false;
    }

    if (!input_buffer && cuda_failed(cuda_functions.cuMemAllocPitch(&input_buffer, &input_pitch, encoder_params.width, rows, 16), "cuMemAllocPitch()")) {
      input_buffer = 0;
      return false;
    }

    NV_ENC_REGISTER_RESOURCE register_resource = {.version = NV_ENC_REGISTER_RESOURCE_VER};
    register_resource.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR;
    register_resource.width = encoder_params.width;
    register_resource.height = encoder_params.height;
    register_resource.pitch = static_cast<uint32_t>(input_pitch);
    register_resource.resourceToRegister = reinterpret_cast<void *>(input_buffer);
    register_resource.bufferFormat = encoder_params.buffer_format;
    register_resource.bufferUsage = NV_ENC_INPUT_IMAGE;

    if (nvenc_failed(nvenc->nvEncRegisterResource(encoder, &register_resource))) {
      BOOST_LOG(error) << "NvEnc: NvEncRegisterResource() failed: " << last_nvenc_error_string;
      return false;
    }
    registered_input_buffer = register_resource.registeredResource;
    return true;
  }

  bool nvenc_cuda::synchronize_input_buffer() {
    if (io_streams) {
      // NVENC orders its reads after the conversion stream.
      return true;
    }
    return !cuda_failed(cuda_functions.cuStreamSynchronize(cuda_stream), "cuStreamSynchronize()");
  }

  void nvenc_cuda::free_input_buffer() {
    if (!input_buffer) {
      return;
    }
    context_guard_t guard {*this};
    if (guard) {
      cuda_failed(cuda_functions.cuMemFree(input_buffer), "cuMemFree()");
    }
    input_buffer = 0;
    input_pitch = 0;
  }

  bool nvenc_cuda::cuda_failed(CUresult result, const char *what) {
    if (result == CUDA_SUCCESS) {
      return false;
    }
    BOOST_LOG(error) << "NvEnc: " << what << " failed: CUDA error " << static_cast<int>(result);
    return true;
  }

}  // namespace NVENC_NAMESPACE
#endif
