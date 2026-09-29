/**
 * @file src/platform/linux/cuda_nvenc.cpp
 * @brief Native NVENC encode device for CUDA capture (NvFBC) on Linux.
 */
// standard includes
#include <cstdint>
#include <memory>
#include <vector>

// local includes
#include "cuda.h"
#include "src/config.h"
#include "src/logging.h"
#include "src/nvenc/nvenc_cuda_factory.h"
#include "src/platform/common.h"
#include "src/video.h"

using namespace std::literals;

namespace cuda {

  namespace {

    /**
     * @brief Encode device that converts captured CUDA textures straight into NVENC's input surface.
     *
     * The RGB to YUV kernels are the ones the FFmpeg path uses; they write into the pitched
     * buffer the native encoder registered with NVENC, on a stream NVENC waits on. Nothing
     * crosses PCIe and there is no FFmpeg frame pool in between.
     */
    class cuda_nvenc_t: public platf::nvenc_encode_device_t {
    public:
      /**
       * @brief Load NVENC and create an encoder object (no session yet).
       *
       * @param in_width Width of captured frames.
       * @param in_height Height of captured frames.
       * @param pix_fmt Encoder input format.
       * @return `true` on success.
       */
      bool init_device(int in_width, int in_height, platf::pix_fmt_e pix_fmt) {
        if (pix_fmt != platf::pix_fmt_e::nv12 && pix_fmt != platf::pix_fmt_e::yuv444p) {
          BOOST_LOG(debug) << "NvEnc: native CUDA input doesn't support "sv << platf::from_pix_fmt(pix_fmt);
          return false;
        }
        if (init()) {
          return false;
        }

        auto factory = nvenc::nvenc_cuda_factory::get();
        if (!factory) {
          return false;
        }

        const int device = current_device();
        if (device < 0) {
          BOOST_LOG(error) << "NvEnc: couldn't get the CUDA device"sv;
          return false;
        }

        stream = make_stream();
        if (!stream) {
          return false;
        }

        encoder = factory->create_nvenc_cuda(device, stream.get());
        if (!encoder) {
          return false;
        }

        width = in_width;
        height = in_height;
        buffer_format = pix_fmt;
        nvenc = encoder.get();
        return true;
      }

      /**
       * @brief Open the NVENC session and prepare the colour converter for its input surface.
       *
       * @param client_config Client stream configuration negotiated for this session.
       * @param colorspace Colorimetry information used for conversion or encoding.
       * @return `true` on success.
       */
      bool init_encoder(const ::video::config_t &client_config, const ::video::sunshine_colorspace_t &colorspace) override {
        if (!encoder || !encoder->create_encoder(config::video.nv, client_config, colorspace, buffer_format)) {
          return false;
        }

        surface = encoder->get_input_surface();
        if (!surface.device_ptr) {
          BOOST_LOG(error) << "NvEnc: native encoder has no CUDA input surface"sv;
          return false;
        }

        auto sws_opt = sws_t::make(width, height, surface.width, surface.height, width * 4);
        if (!sws_opt) {
          return false;
        }
        sws = std::move(*sws_opt);
        sws.apply_colorspace(colorspace);
        linear_interpolation = width != (int) surface.width || height != (int) surface.height;

        const auto caps = encoder->capabilities();
        BOOST_LOG(info) << "NvEnc: native CUDA encoder "sv << surface.width << 'x' << surface.height
                        << " rfi="sv << (caps.rfi_active ? "on"sv : caps.rfi_supported ? "off (dpb)"sv : "unsupported"sv)
                        << " intra-refresh="sv << (caps.intra_refresh_active ? "on"sv : caps.intra_refresh_supported ? "off"sv : "unsupported"sv)
                        << " dpb="sv << caps.ref_frames_in_dpb;

        return clear_surface() == 0;
      }

      /**
       * @brief Convert a captured frame into the NVENC input surface.
       *
       * @param img Captured CUDA image.
       * @return 0 on success.
       */
      int convert(platf::img_t &img) override {
        return convert_texture(img_texture(img, linear_interpolation), sws.viewport);
      }

    private:
      /**
       * @brief Run the colour converter for one texture.
       *
       * @param texture Source texture.
       * @param viewport Destination rectangle inside the surface.
       * @return 0 on success.
       */
      int convert_texture(cudaTextureObject_t texture, const viewport_t &viewport) {
        auto *base = reinterpret_cast<std::uint8_t *>(surface.device_ptr);
        const auto plane = surface.pitch * surface.height;
        const auto pitch = static_cast<std::uint32_t>(surface.pitch);
        if (surface.yuv444) {
          return sws.convert_yuv444(base, base + plane, base + 2 * plane, pitch, texture, stream.get(), viewport);
        }
        return sws.convert_nv12(base, base + plane, pitch, pitch, texture, stream.get(), viewport);
      }

      /**
       * @brief Fill the whole surface with black so letterbox bars aren't green.
       *
       * @return 0 on success.
       */
      int clear_surface() {
        auto tex = tex_t::make(height, width * 4);
        if (!tex) {
          return -1;
        }

        platf::img_t img;
        img.width = width;
        img.height = height;
        img.pixel_pitch = 4;
        img.row_pitch = img.width * img.pixel_pitch;
        std::vector<std::uint8_t> image_data(img.row_pitch * img.height);
        img.data = image_data.data();

        if (sws.load_ram(img, tex->array)) {
          return -1;
        }
        return convert_texture(tex->texture.linear, {(int) surface.width, (int) surface.height, 0, 0});
      }

      stream_t stream;  ///< Conversion stream; NVENC waits on it. Destroyed after the encoder.
      std::unique_ptr<nvenc::nvenc_cuda_interface> encoder;  ///< Native encoder owning the NVENC session.
      nvenc::nvenc_cuda_surface surface;  ///< Input surface of the current session.
      sws_t sws;  ///< RGB to YUV converter.
      platf::pix_fmt_e buffer_format = platf::pix_fmt_e::nv12;  ///< Encoder input format.
      int width = 0;  ///< Captured frame width.
      int height = 0;  ///< Captured frame height.
      bool linear_interpolation = false;  ///< Scale with linear filtering.
    };

  }  // namespace

  std::unique_ptr<platf::nvenc_encode_device_t> make_nvenc_encode_device(int width, int height, platf::pix_fmt_e pix_fmt) {
    auto device = std::make_unique<cuda_nvenc_t>();
    if (!device->init_device(width, height, pix_fmt)) {
      return nullptr;
    }
    return device;
  }

}  // namespace cuda
