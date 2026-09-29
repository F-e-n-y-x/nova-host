/**
 * @file src/platform/linux/cuda.h
 * @brief Definitions for CUDA implementation.
 */
#pragma once

#if defined(SUNSHINE_BUILD_CUDA)
  // standard includes
  #include <cstdint>
  #include <memory>
  #include <optional>
  #include <string>
  #include <vector>

  // local includes
  #include "src/video_colorspace.h"

namespace platf {
  struct avcodec_encode_device_t;
  struct nvenc_encode_device_t;
  struct img_t;
  enum class pix_fmt_e;
}  // namespace platf

namespace cuda {

  namespace nvfbc {
    std::vector<std::string> display_names();
  }

  std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device(int width, int height, bool vram);

  /**
   * @brief Create a GL->CUDA encoding device for consuming captured dmabufs.
   * @param in_width Width of captured frames.
   * @param in_height Height of captured frames.
   * @param offset_x Offset of content in captured frame.
   * @param offset_y Offset of content in captured frame.
   * @return FFmpeg encoding device context.
   */
  std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_gl_encode_device(int width, int height, int offset_x, int offset_y);

  /**
   * @brief Create a native NVENC encoding device for captured CUDA frames (NvFBC).
   * @param width Width of captured frames.
   * @param height Height of captured frames.
   * @param pix_fmt Encoder input format; only NV12 and 8-bit YUV 4:4:4 are supported.
   * @return Native NVENC encoding device, or nullptr when native NVENC isn't available.
   */
  std::unique_ptr<platf::nvenc_encode_device_t> make_nvenc_encode_device(int width, int height, platf::pix_fmt_e pix_fmt);

  int init();
}  // namespace cuda

typedef struct cudaArray *cudaArray_t;

  #if !defined(__CUDACC__)
typedef struct CUstream_st *cudaStream_t;
typedef unsigned long long cudaTextureObject_t;
  #else /* defined(__CUDACC__) */
typedef __location__(device_builtin) struct CUstream_st *cudaStream_t;
typedef __location__(device_builtin) unsigned long long cudaTextureObject_t;
  #endif /* !defined(__CUDACC__) */

namespace cuda {

  class freeCudaPtr_t {
  public:
    void operator()(void *ptr);
  };

  class freeCudaStream_t {
  public:
    void operator()(cudaStream_t ptr);
  };

  using ptr_t = std::unique_ptr<void, freeCudaPtr_t>;
  using stream_t = std::unique_ptr<CUstream_st, freeCudaStream_t>;

  stream_t make_stream(int flags = 0);

  /**
   * @brief Get the CUDA runtime's current device ordinal.
   * @return Device ordinal, or -1 on error.
   */
  int current_device();

  /**
   * @brief Get the texture of a captured CUDA image for the colour converters.
   * @param img Image allocated by a CUDA capture backend.
   * @param linear Use linear filtering (scaling) instead of point sampling.
   * @return CUDA texture object of the image.
   */
  cudaTextureObject_t img_texture(platf::img_t &img, bool linear);

  /**
   * @brief Create a CUDA capture image from a BGRA frame in system memory (tests and benchmarks).
   * @param width Width in pixels.
   * @param height Height in pixels.
   * @param bgra Pixels, `width * 4` bytes per row.
   * @return Image the CUDA encode devices can convert, or nullptr on error.
   */
  std::shared_ptr<platf::img_t> make_img_from_ram(int width, int height, const std::uint8_t *bgra);

  /**
   * @brief Read a captured CUDA image back into system memory (tests and diagnostics).
   * @param texture Texture of the image (img_texture(img, false)).
   * @param height Image height in rows.
   * @param pitch Bytes per row (`width * 4`).
   * @param dst Destination, at least `height * pitch` bytes.
   * @return 0 on success, -1 on a CUDA error.
   */
  int download_texture(cudaTextureObject_t texture, int height, int pitch, std::uint8_t *dst);

  struct viewport_t {
    int width;
    int height;
    int offsetX;
    int offsetY;
  };

  class tex_t {
  public:
    static std::optional<tex_t> make(int height, int pitch);

    tex_t();
    tex_t(tex_t &&);

    tex_t &operator=(tex_t &&other);

    ~tex_t();

    int copy(std::uint8_t *src, int height, int pitch);

    cudaArray_t array;

    struct texture {
      cudaTextureObject_t point;
      cudaTextureObject_t linear;
    } texture;
  };

  class sws_t {
  public:
    sws_t() = default;
    sws_t(int in_width, int in_height, int out_width, int out_height, int pitch, int threadsPerBlock, ptr_t &&color_matrix);

    /**
     * in_width, in_height -- The width and height of the captured image in pixels
     * out_width, out_height -- the width and height of the NV12 image in pixels
     *
     * pitch -- The size of a single row of pixels in bytes
     */
    static std::optional<sws_t> make(int in_width, int in_height, int out_width, int out_height, int pitch);

    // Converts loaded image into a CUDevicePtr
    int convert_nv12(std::uint8_t *Y, std::uint8_t *UV, std::uint32_t pitchY, std::uint32_t pitchUV, cudaTextureObject_t texture, stream_t::pointer stream);
    int convert_nv12(std::uint8_t *Y, std::uint8_t *UV, std::uint32_t pitchY, std::uint32_t pitchUV, cudaTextureObject_t texture, stream_t::pointer stream, const viewport_t &viewport);
    int convert_yuv444(std::uint8_t *Y, std::uint8_t *U, std::uint8_t *V, std::uint32_t pitch, cudaTextureObject_t texture, stream_t::pointer stream);
    int convert_yuv444(std::uint8_t *Y, std::uint8_t *U, std::uint8_t *V, std::uint32_t pitch, cudaTextureObject_t texture, stream_t::pointer stream, const viewport_t &viewport);

    void apply_colorspace(const video::sunshine_colorspace_t &colorspace);

    int load_ram(platf::img_t &img, cudaArray_t array);

    ptr_t color_matrix;

    int threadsPerBlock;

    viewport_t viewport;

    float scale;
  };
}  // namespace cuda

#endif
