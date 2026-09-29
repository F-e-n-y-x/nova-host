/**
 * @file tests/unit/test_nvenc_cuda_gpu.cpp
 * @brief Real-GPU checks of the Linux native NVENC encoder: short offline encodes of synthetic
 *        CUDA frames (no capture, no streaming, no input). Skipped unless NOVA_NVENC_GPU_TESTS=1.
 */
#if defined(__linux__) && defined(SUNSHINE_BUILD_CUDA)

  // standard includes
  #include <algorithm>
  #include <cstdint>
  #include <cstdlib>
  #include <filesystem>
  #include <format>
  #include <fstream>
  #include <iostream>
  #include <memory>
  #include <numeric>
  #include <string>
  #include <vector>

  // lib includes
  #include <gtest/gtest.h>

  // local includes
  #include "src/config.h"
  #include "src/nvenc/nvenc_encoder.h"
  #include "src/platform/common.h"
  #include "src/platform/linux/cuda.h"
  #include "src/video.h"

namespace {

  /**
   * @brief Whether the real-GPU tests were asked for.
   */
  bool gpu_tests_enabled() {
    const char *value = std::getenv("NOVA_NVENC_GPU_TESTS");
    return value && std::string_view {value} == "1";
  }

  /**
   * @brief CUDA "display" that only creates encode devices and images; it never captures.
   */
  class offline_cuda_display final: public platf::display_t {
  public:
    offline_cuda_display(int w, int h) {
      width = w;
      height = h;
      env_width = w;
      env_height = h;
    }

    platf::capture_e capture(const push_captured_image_cb_t &, const pull_free_image_cb_t &, bool *) override {
      return platf::capture_e::error;
    }

    std::shared_ptr<platf::img_t> alloc_img() override {
      std::vector<std::uint8_t> black(static_cast<std::size_t>(width) * height * 4);
      return cuda::make_img_from_ram(width, height, black.data());
    }

    int dummy_img(platf::img_t *) override {
      return 0;
    }

    std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device(platf::pix_fmt_e) override {
      return cuda::make_avcodec_encode_device(width, height, true);
    }

    std::unique_ptr<platf::nvenc_encode_device_t> make_nvenc_encode_device(platf::pix_fmt_e pix_fmt) override {
      return cuda::make_nvenc_encode_device(width, height, pix_fmt);
    }
  };

  /**
   * @brief Synthetic desktop/game-like frames: gradient, moving blocks and a noisy band.
   */
  std::vector<std::shared_ptr<platf::img_t>> synthetic_frames(int width, int height, int count) {
    std::vector<std::shared_ptr<platf::img_t>> frames;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    std::uint32_t seed = 12345;
    for (int f = 0; f < count; ++f) {
      for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
          auto *p = &pixels[(static_cast<std::size_t>(y) * width + x) * 4];
          p[0] = static_cast<std::uint8_t>((x + f * 8) & 0xFF);
          p[1] = static_cast<std::uint8_t>((y * 255) / height);
          p[2] = static_cast<std::uint8_t>(((x ^ y) + f * 3) & 0xFF);
          p[3] = 0xFF;
          const int bx = (x + f * 24) % width;
          if (bx > width / 3 && bx < width / 3 + 200 && y > height / 3 && y < height / 3 + 200) {
            p[0] = p[1] = p[2] = 0xF0;
          }
          if (y > height * 3 / 4 && y < height * 3 / 4 + 64) {
            seed = seed * 1664525U + 1013904223U;
            p[0] = p[1] = p[2] = static_cast<std::uint8_t>(seed >> 24);
          }
        }
      }
      auto img = cuda::make_img_from_ram(width, height, pixels.data());
      if (!img) {
        return {};
      }
      frames.push_back(std::move(img));
    }
    return frames;
  }

  /**
   * @brief Client configuration for an offline encode.
   */
  video::config_t stream_config(int width, int height, int video_format, int bitrate_kbps = 50000) {
    video::config_t config {};
    config.width = width;
    config.height = height;
    config.framerate = 120;
    config.bitrate = bitrate_kbps;
    config.slicesPerFrame = 1;
    config.numRefFrames = 0;
    config.encoderCscMode = 0;
    config.videoFormat = video_format;
    return config;
  }

  /**
   * @brief Latency percentile of a sorted sample.
   */
  double percentile(const std::vector<double> &sorted, double p) {
    const auto index = static_cast<std::size_t>(p * static_cast<double>(sorted.size() - 1) + 0.5);
    return sorted[std::min(index, sorted.size() - 1)];
  }

}  // namespace

class NvencCudaGpuTest: public testing::Test {
protected:
  void SetUp() override {
    if (!gpu_tests_enabled()) {
      GTEST_SKIP() << "set NOVA_NVENC_GPU_TESTS=1 to run the real-GPU NVENC tests";
    }
    ASSERT_EQ(cuda::init(), 0);
    saved_nv = config::video.nv;
    saved_legacy = config::video.nv_legacy;
    // Stream defaults, as config::apply_config() derives them for FFmpeg NVENC.
    config::video.nv = {};
    config::video.nv_legacy.preset = "p1";
    config::video.nv_legacy.multipass = 1;  // NV_ENC_TWO_PASS_QUARTER_RESOLUTION
    config::video.nv_legacy.h264_coder = 1;  // CABAC
    config::video.nv_legacy.spatial_aq = 0;
    config::video.nv_legacy.vbv_percentage_increase = 0;
  }

  void TearDown() override {
    if (gpu_tests_enabled()) {
      config::video.nv = saved_nv;
      config::video.nv_legacy = saved_legacy;
    }
  }

  nvenc::nvenc_config saved_nv;
  decltype(config::video.nv_legacy) saved_legacy;
};

TEST_F(NvencCudaGpuTest, ReportsLossRecoveryCapsPerCodec) {
  const video::sunshine_colorspace_t colorspace {video::colorspace_e::rec709, false, 8};
  for (int format : {0, 1}) {
    for (bool intra_refresh : {false, true}) {
      config::video.nv.intra_refresh = intra_refresh;
      auto device = cuda::make_nvenc_encode_device(1920, 1080, platf::pix_fmt_e::nv12);
      ASSERT_TRUE(device);
      ASSERT_TRUE(device->init_encoder(stream_config(1920, 1080, format), colorspace));
      const auto caps = device->nvenc->capabilities();
      const auto line = std::format("{} intra_refresh_requested={}: rfi_supported={} rfi_active={} ir_supported={} ir_active={} dpb={}", format ? "HEVC" : "H.264", intra_refresh, caps.rfi_supported, caps.rfi_active, caps.intra_refresh_supported, caps.intra_refresh_active, caps.ref_frames_in_dpb);
      std::cout << "[caps] " << line << std::endl;
      RecordProperty(std::format("caps_{}_ir{}", format ? "hevc" : "h264", intra_refresh), line);
      EXPECT_TRUE(caps.rfi_active);
      EXPECT_EQ(caps.intra_refresh_active, intra_refresh && caps.intra_refresh_supported);
    }
  }
}

TEST_F(NvencCudaGpuTest, NativeRfiRecoversWithoutIdr) {
  offline_cuda_display display {1920, 1080};
  auto frames = synthetic_frames(1920, 1080, 8);
  ASSERT_FALSE(frames.empty());
  for (int format : {0, 1}) {
    auto result = video::encode_offline_nvenc(display, true, stream_config(1920, 1080, format), frames, 60, 30);
    ASSERT_TRUE(result) << format;
    EXPECT_TRUE(result->idr[0]);
    EXPECT_FALSE(result->idr[29]) << "frame after RFI must not be an IDR";
    EXPECT_TRUE(result->after_rfi[29]);
    EXPECT_EQ(std::count(result->idr.begin(), result->idr.end(), true), 1);
    std::cout << std::format("[rfi] {} native: frame 30 {} bytes (P, after RFI), median P {} bytes, IDR {} bytes", format ? "HEVC" : "H.264", result->bytes[29], result->bytes[40], result->bytes[0]) << std::endl;
  }
}

TEST_F(NvencCudaGpuTest, FfmpegTurnsLossIntoIdr) {
  offline_cuda_display display {1920, 1080};
  auto frames = synthetic_frames(1920, 1080, 8);
  ASSERT_FALSE(frames.empty());
  auto result = video::encode_offline_nvenc(display, false, stream_config(1920, 1080, 1), frames, 60, 30);
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->idr[29]);
  std::cout << std::format("[rfi] HEVC ffmpeg: frame 30 {} bytes (IDR), P {} bytes", result->bytes[29], result->bytes[40]) << std::endl;
}

TEST_F(NvencCudaGpuTest, NativeIntraRefreshStreamHasOneIdr) {
  config::video.nv.intra_refresh = true;
  offline_cuda_display display {1920, 1080};
  auto frames = synthetic_frames(1920, 1080, 8);
  ASSERT_FALSE(frames.empty());
  for (int format : {0, 1}) {
    auto result = video::encode_offline_nvenc(display, true, stream_config(1920, 1080, format), frames, 120, 60);
    ASSERT_TRUE(result) << format;
    EXPECT_EQ(std::count(result->idr.begin(), result->idr.end(), true), 1);
    EXPECT_TRUE(result->after_rfi[59]);
  }
}

TEST_F(NvencCudaGpuTest, BitrateChangesInPlace) {
  offline_cuda_display display {1920, 1080};
  auto device = cuda::make_nvenc_encode_device(1920, 1080, platf::pix_fmt_e::nv12);
  ASSERT_TRUE(device);
  ASSERT_TRUE(device->init_encoder(stream_config(1920, 1080, 1), {video::colorspace_e::rec709, false, 8}));
  EXPECT_TRUE(device->nvenc->set_bitrate(20000));
  EXPECT_TRUE(device->nvenc->set_bitrate(80000));
}

TEST_F(NvencCudaGpuTest, EncodeLatencyNativeVsFfmpegHevc120) {
  constexpr int frame_count = 480;  // 4 s at 120 fps
  constexpr int warmup = 20;
  for (const auto &[width, height] : {std::pair {1920, 1080}, std::pair {2340, 1080}}) {
    offline_cuda_display display {width, height};
    auto frames = synthetic_frames(width, height, 12);
    ASSERT_FALSE(frames.empty());
    for (bool native : {true, false}) {
      auto result = video::encode_offline_nvenc(display, native, stream_config(width, height, 1), frames, frame_count);
      ASSERT_TRUE(result) << (native ? "native" : "ffmpeg");
      std::vector<double> ms(result->frame_ms.begin() + warmup, result->frame_ms.end());
      std::ranges::sort(ms);
      const double mean = std::accumulate(ms.begin(), ms.end(), 0.0) / static_cast<double>(ms.size());
      const auto line = std::format("HEVC {}x{}@120 {}: mean {:.3f} ms, p50 {:.3f}, p95 {:.3f}, p99 {:.3f}, max {:.3f} ({} frames)", width, height, native ? "native" : "ffmpeg", mean, percentile(ms, 0.5), percentile(ms, 0.95), percentile(ms, 0.99), ms.back(), ms.size());
      std::cout << "[latency] " << line << std::endl;
      RecordProperty(std::format("latency_{}x{}_{}", width, height, native ? "native" : "ffmpeg"), line);
    }
  }
}

/**
 * @brief Pascal preset matrix: P1-P4 x two-pass off/quarter x spatial AQ off/on, H.264 and HEVC,
 *        at the client modes Nova serves. Runs only with NOVA_NVENC_BENCH_DIR=<dir>; writes
 *        results.csv, the elementary streams and the reference frames there for PSNR/SSIM.
 */
TEST(NvencCudaGpuBench, PascalPresetMatrix) {
  const char *dir_env = std::getenv("NOVA_NVENC_BENCH_DIR");
  if (!dir_env || !*dir_env) {
    GTEST_SKIP() << "set NOVA_NVENC_BENCH_DIR=<dir> to run the NVENC preset benchmark";
  }
  ASSERT_EQ(cuda::init(), 0);
  const std::filesystem::path dir {dir_env};
  std::filesystem::create_directories(dir);

  const auto saved_nv = config::video.nv;
  struct mode_t {
    int width;
    int height;
    int fps;
    int bitrate_kbps;
  };
  const mode_t modes[] = {
    {1920, 1080, 120, 40000},
    {2340, 1080, 120, 50000},
    {2560, 1600, 60, 40000},
    {3120, 1440, 120, 80000},
  };
  constexpr int unique_frames = 24;
  constexpr int warmup = 10;

  std::ofstream csv {dir / "results.csv"};
  csv << "codec,width,height,fps,bitrate_kbps,preset,twopass,aq,frames,mean_ms,p50_ms,p95_ms,p99_ms,max_ms,sustainable_fps,avg_kB,idr_kB,stream\n";

  for (const auto &mode : modes) {
    const int frames = mode.fps * 2;
    std::vector<std::uint8_t> reference;
    std::vector<std::shared_ptr<platf::img_t>> images;
    {
      // Same generator as synthetic_frames(), keeping the pixels for the quality reference.
      std::vector<std::uint8_t> pixels(static_cast<std::size_t>(mode.width) * mode.height * 4);
      std::uint32_t seed = 12345;
      for (int f = 0; f < unique_frames; ++f) {
        for (int y = 0; y < mode.height; ++y) {
          for (int x = 0; x < mode.width; ++x) {
            auto *p = &pixels[(static_cast<std::size_t>(y) * mode.width + x) * 4];
            p[0] = static_cast<std::uint8_t>((x + f * 8) & 0xFF);
            p[1] = static_cast<std::uint8_t>((y * 255) / mode.height);
            p[2] = static_cast<std::uint8_t>(((x ^ y) + f * 3) & 0xFF);
            p[3] = 0xFF;
            const int bx = (x + f * 24) % mode.width;
            if (bx > mode.width / 3 && bx < mode.width / 3 + 200 && y > mode.height / 3 && y < mode.height / 3 + 200) {
              p[0] = p[1] = p[2] = 0xF0;
            }
            if (y > mode.height * 3 / 4 && y < mode.height * 3 / 4 + 64) {
              seed = seed * 1664525U + 1013904223U;
              p[0] = p[1] = p[2] = static_cast<std::uint8_t>(seed >> 24);
            }
          }
        }
        auto img = cuda::make_img_from_ram(mode.width, mode.height, pixels.data());
        ASSERT_TRUE(img);
        images.push_back(std::move(img));
        reference.insert(reference.end(), pixels.begin(), pixels.end());
      }
    }
    {
      std::ofstream ref {dir / std::format("ref_{}x{}.bgra", mode.width, mode.height), std::ios::binary};
      ref.write(reinterpret_cast<const char *>(reference.data()), static_cast<std::streamsize>(reference.size()));
    }
    reference.clear();
    reference.shrink_to_fit();

    offline_cuda_display display {mode.width, mode.height};
    for (int format : {0, 1}) {
      for (int preset = 1; preset <= 4; ++preset) {
        for (auto two_pass : {nvenc::nvenc_two_pass::disabled, nvenc::nvenc_two_pass::quarter_resolution}) {
          for (bool aq : {false, true}) {
            config::video.nv = {};
            config::video.nv.quality_preset = preset;
            config::video.nv.two_pass = two_pass;
            config::video.nv.adaptive_quantization = aq;
            auto config = stream_config(mode.width, mode.height, format, mode.bitrate_kbps);
            config.framerate = mode.fps;

            std::vector<std::uint8_t> bitstream;
            auto result = video::encode_offline_nvenc(display, true, config, images, frames, -1, &bitstream);
            ASSERT_TRUE(result) << std::format("{} {}x{} p{}", format, mode.width, mode.height, preset);

            const auto name = std::format("{}_{}x{}_{}_p{}_tp{}_aq{}.{}", format ? "hevc" : "h264", mode.width, mode.height, mode.fps, preset, two_pass == nvenc::nvenc_two_pass::disabled ? 0 : 1, aq ? 1 : 0, format ? "hevc" : "h264");
            {
              std::ofstream out {dir / name, std::ios::binary};
              out.write(reinterpret_cast<const char *>(bitstream.data()), static_cast<std::streamsize>(bitstream.size()));
            }

            std::vector<double> ms(result->frame_ms.begin() + warmup, result->frame_ms.end());
            std::ranges::sort(ms);
            const double mean = std::accumulate(ms.begin(), ms.end(), 0.0) / static_cast<double>(ms.size());
            const double avg_kb = std::accumulate(result->bytes.begin() + 1, result->bytes.end(), 0.0) / static_cast<double>(result->bytes.size() - 1) / 1000.0;
            csv << std::format("{},{},{},{},{},P{},{},{},{},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.0f},{:.1f},{:.1f},{}\n", format ? "HEVC" : "H.264", mode.width, mode.height, mode.fps, mode.bitrate_kbps, preset, two_pass == nvenc::nvenc_two_pass::disabled ? "off" : "quarter", aq ? "on" : "off", frames, mean, percentile(ms, 0.5), percentile(ms, 0.95), percentile(ms, 0.99), ms.back(), 1000.0 / mean, avg_kb, result->bytes[0] / 1000.0, name);
            csv.flush();
          }
        }
      }
    }
  }
  config::video.nv = saved_nv;
}

#endif
