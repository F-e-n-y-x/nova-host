/**
 * @file src/nvenc/nvenc_cuda.h
 * @brief Declarations for the Linux NVENC encoder with CUDA device-memory input.
 */
#pragma once
#if defined(__linux__) && defined(SUNSHINE_BUILD_CUDA)

  // local includes
  #include "nvenc_base.h"
  #include "nvenc_cuda_factory.h"
  #include "nvenc_cuda_interface.h"

namespace NVENC_NAMESPACE {

  /**
   * @brief Native NVENC encoder on a CUDA device.
   *
   * The session runs on the device's primary CUDA context, which is the context the CUDA
   * runtime (and therefore NvFBC capture and the RGB to YUV kernels) uses. The input is a
   * pitched device allocation registered once with NVENC; the platform code converts each
   * captured frame straight into it on `cuda_stream`. When the driver accepts it, NVENC waits
   * on that stream (`NvEncSetIOCudaStreams()`), so there is no CPU synchronization per frame.
   */
  class nvenc_cuda final: public nvenc_base, public ::nvenc::nvenc_cuda_interface {
  public:
    /**
     * @param cuda_device CUDA device ordinal used by the CUDA runtime.
     * @param cuda_stream `CUstream` the input surface is written on, or null for the default stream.
     * @param driver Shared handle of `libnvidia-encode.so.1`.
     */
    nvenc_cuda(int cuda_device, void *cuda_stream, ::nvenc::shared_library driver);
    ~nvenc_cuda() override;

    bool create_encoder(
      const ::nvenc::nvenc_config &config,
      const video::config_t &client_config,
      const video::sunshine_colorspace_t &colorspace,
      platf::pix_fmt_e buffer_format
    ) override;

    void destroy_encoder() override;

    ::nvenc::nvenc_encoded_frame encode_frame(uint64_t frame_index, bool force_idr) override;

    bool invalidate_ref_frames(uint64_t first_frame, uint64_t last_frame) override;

    bool set_bitrate(int bitrate_kbps) override;

    ::nvenc::nvenc_cuda_surface get_input_surface() const override;

  protected:
    bool init_library() override;
    bool create_and_register_input_buffer() override;
    bool synchronize_input_buffer() override;

  private:
    /**
     * @brief Keep the primary context current for the lifetime of the guard.
     */
    class context_guard_t {
    public:
      /**
       * @param parent Encoder whose context is pushed.
       */
      explicit context_guard_t(nvenc_cuda &parent);
      ~context_guard_t();

      context_guard_t(const context_guard_t &) = delete;
      context_guard_t &operator=(const context_guard_t &) = delete;

      /**
       * @brief Whether the context was pushed.
       */
      explicit operator bool() const {
        return pushed;
      }

    private:
      nvenc_cuda &parent;  ///< Encoder whose context was pushed.
      bool pushed = false;  ///< Whether the push succeeded.
    };

    /**
     * @brief Load the CUDA driver functions the encoder needs from `libcuda.so.1`.
     * @return `true` on success.
     */
    bool load_cuda();

    /**
     * @brief Free the pitched input surface.
     */
    void free_input_buffer();

    /**
     * @brief Log a failed CUDA driver call.
     * @param result Result of the call.
     * @param what Name of the call.
     * @return `true` when `result` is an error.
     */
    bool cuda_failed(CUresult result, const char *what);

    const int cuda_device_ordinal;  ///< CUDA runtime device ordinal.
    CUstream cuda_stream;  ///< Stream that writes the input surface; NVENC keeps a pointer to it.
    ::nvenc::shared_library driver;  ///< Keeps libnvidia-encode.so.1 loaded.

    struct {
      tcuInit *cuInit;
      tcuDeviceGet *cuDeviceGet;
      tcuDevicePrimaryCtxRetain *cuDevicePrimaryCtxRetain;
      tcuDevicePrimaryCtxRelease *cuDevicePrimaryCtxRelease;
      tcuCtxPushCurrent_v2 *cuCtxPushCurrent;
      tcuCtxPopCurrent_v2 *cuCtxPopCurrent;
      tcuMemAllocPitch_v2 *cuMemAllocPitch;
      tcuMemFree_v2 *cuMemFree;
      tcuStreamSynchronize *cuStreamSynchronize;
      void *library;
    } cuda_functions = {};  ///< CUDA driver entry points loaded from libcuda.so.1.

    CUdevice cuda_device = 0;  ///< CUDA device of the session.
    CUcontext cuda_context = nullptr;  ///< Retained primary context of `cuda_device`.
    CUdeviceptr input_buffer = 0;  ///< Pitched input surface registered with NVENC.
    size_t input_pitch = 0;  ///< Pitch of `input_buffer` in bytes.
    bool io_streams = false;  ///< NVENC waits on `cuda_stream` itself.
  };

}  // namespace NVENC_NAMESPACE
#endif
