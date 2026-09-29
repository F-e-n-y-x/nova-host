/**
 * @file tests/unit/platform/linux/test_nvenc_live.cpp
 * @brief Opt-in live test of the native NVENC path on real NvFBC captures (NOVA_NVENC_LIVE=1).
 *
 * Starts a spare headless X server (the Virtual display one: no scanout, the desktop's heads are
 * never touched), plays a moving test pattern on it with ffplay, probes the encoders the way Nova
 * does at startup with the configured `nvenc_backend` (NOVA_NVENC_LIVE_BACKEND, default auto), then
 * encodes real NvFBC frames through both NVENC implementations with simulated losses. Nothing is
 * sent anywhere and no input device is created. With NOVA_NVENC_LIVE_DIR set, the streams a client
 * would receive are written there for decoding (nvenc-loss.py).
 */
#if defined(__linux__) && defined(SUNSHINE_BUILD_CUDA)

  // test includes
  #include "../../../tests_common.h"

  // standard includes
  #include <algorithm>
  #include <array>
  #include <chrono>
  #include <csignal>
  #include <cstdio>
  #include <cstdlib>
  #include <filesystem>
  #include <format>
  #include <fstream>
  #include <iostream>
  #include <string>
  #include <thread>
  #include <vector>

  // local includes
  #include <src/config.h>
  #include <src/nvenc/nvenc_backend.h>
  #include <src/platform/common.h>
  #include <src/utility.h>
  #include <src/video.h>
  #include <src/virtual_display.h>

namespace platf {
  std::vector<std::string> nvfbc_display_names();
  std::shared_ptr<display_t> nvfbc_display(mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config);
}  // namespace platf

namespace {
  namespace fs = std::filesystem;
  namespace vd = virtual_display;

  std::string shell_output(const std::string &cmd) {
    std::string out;
    if (FILE *pipe = popen(cmd.c_str(), "r")) {
      std::array<char, 512> buf {};
      while (fgets(buf.data(), buf.size(), pipe)) {
        out += buf.data();
      }
      pclose(pipe);
    }
    return out;
  }

  /**
   * @brief Capture `count` distinct frames of the current capture target with Nova's NvFBC display.
   */
  std::vector<std::shared_ptr<platf::img_t>> capture_frames(platf::display_t &disp, int count) {
    std::vector<std::shared_ptr<platf::img_t>> frames;
    auto push = [&](std::shared_ptr<platf::img_t> &&img, bool captured) {
      if (captured && img) {
        frames.push_back(std::move(img));
      }
      return static_cast<int>(frames.size()) < count;
    };
    auto pull = [&](std::shared_ptr<platf::img_t> &out) {
      out = disp.alloc_img();  // a new image each time, so every captured frame is kept
      return out != nullptr;
    };
    bool cursor = false;
    if (disp.capture(push, pull, &cursor) != platf::capture_e::ok) {
      return {};
    }
    return frames;
  }

  video::config_t stream_config(int width, int height, int fps, int video_format, int bitrate_kbps) {
    video::config_t config {};
    config.width = width;
    config.height = height;
    config.framerate = fps;
    config.bitrate = bitrate_kbps;
    config.slicesPerFrame = 1;
    config.numRefFrames = 0;
    config.encoderCscMode = 0;
    config.videoFormat = video_format;
    return config;
  }
}  // namespace

TEST(NvencLive, NativeNvencOnRealNvfbcFrames) {
  const char *live = std::getenv("NOVA_NVENC_LIVE");
  if (!live || std::string {live} != "1") {
    GTEST_SKIP() << "set NOVA_NVENC_LIVE=1 to start a real headless X server and encode its frames";
  }
  const char *backend_env = std::getenv("NOVA_NVENC_LIVE_BACKEND");
  const auto backend = nvenc::nvenc_backend_from_view(backend_env ? backend_env : "auto");
  ASSERT_TRUE(backend) << "NOVA_NVENC_LIVE_BACKEND must be auto, native or ffmpeg";
  const char *out_env = std::getenv("NOVA_NVENC_LIVE_DIR");
  std::ofstream report;
  if (out_env && *out_env) {
    report.open(fs::path {out_env} / "live.txt");
  }
  auto note = [&report](const std::string &line) {
    std::cout << "[live] " << line << std::endl;
    report << line << std::endl;
  };

  constexpr int width = 1920;
  constexpr int height = 1080;
  constexpr int fps = 120;
  const fs::path dir = fs::temp_directory_path() / "nova-nvenc-live";
  fs::remove_all(dir);
  fs::create_directories(dir);
  const auto desk_before = shell_output("DISPLAY=:0 xrandr --query 2>/dev/null");

  vd::x_server_t server {dir / "vd", "", vd::default_ops()};
  auto target = server.start({width, height, fps});
  ASSERT_TRUE(target) << "headless X server didn't start";
  std::string player_pid;
  auto cleanup = util::fail_guard([&]() {
    if (!player_pid.empty()) {
      shell_output("kill " + player_pid + " 2>/dev/null");
    }
    vd::set_capture_target(std::nullopt);
    server.stop();
  });

  // A moving pattern (scrolling test card with a counter), full screen on the spare display only.
  player_pid = shell_output(std::format(
    "DISPLAY={} XAUTHORITY={} SDL_AUDIODRIVER=dummy nohup ffplay -loglevel quiet -an -fs -noborder "
    "-f lavfi -i 'testsrc2=size={}x{}:rate=120' >/dev/null 2>&1 & echo $!",
    target->display, target->xauthority, width, height));
  player_pid.erase(std::remove(player_pid.begin(), player_pid.end(), '\n'), player_pid.end());
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));

  vd::set_capture_target(*target);
  ASSERT_EQ(platf::nvfbc_display_names(), std::vector<std::string> {"0"});

  // Probe exactly as Nova does at startup.
  const auto saved_video = config::video;
  auto restore_config = util::fail_guard([&]() {
    config::video = saved_video;
  });
  config::video.capture = "nvfbc";
  config::video.encoder = "nvenc";
  config::video.hevc_mode = 2;
  config::video.nv = {};
  config::video.nv.backend = *backend;
  config::video.nv.adaptive_quantization = true;
  config::video.nv_legacy.preset = "p1";
  config::video.nv_legacy.multipass = 1;
  config::video.nv_legacy.h264_coder = 1;
  config::video.nv_legacy.spatial_aq = 1;
  // Pick the capture source from config::video.capture, as Nova's startup does.
  auto platform = platf::init();
  ASSERT_TRUE(platform);
  ASSERT_EQ(video::probe_encoders(), 0);
  const auto summary = video::get_encoder_summary();
  note(std::format("backend={} probe: encoder={} implementation={} rfi={} h264={} hevc={}", nvenc::to_string(*backend), summary.name, summary.implementation, summary.ref_frame_invalidation, summary.h264_codec, summary.hevc_codec));
  EXPECT_EQ(summary.name, "nvenc");
  EXPECT_EQ(summary.implementation, *backend == nvenc::nvenc_backend::ffmpeg ? "ffmpeg" : "native");
  EXPECT_EQ(summary.ref_frame_invalidation, *backend != nvenc::nvenc_backend::ffmpeg);
  EXPECT_EQ(video::last_encoder_probe_supported_ref_frames_invalidation, *backend != nvenc::nvenc_backend::ffmpeg);

  // Real NvFBC frames through both implementations, with losses at frames 60 and 150 (3 frames each).
  auto disp = platf::nvfbc_display(platf::mem_type_e::cuda, "0", stream_config(width, height, fps, 1, 40000));
  ASSERT_TRUE(disp);
  auto frames = capture_frames(*disp, 96);
  ASSERT_EQ(frames.size(), 96U);
  const std::vector<int> recover_at {60, 150};
  constexpr int loss_span = 3;
  constexpr int count = 240;
  for (int format : {0, 1}) {
    for (bool native : {true, false}) {
      for (bool losses : {false, true}) {
        std::vector<std::uint8_t> bitstream;
        auto result = video::encode_offline_nvenc(*disp, native, stream_config(width, height, fps, format, 40000), frames, count, losses ? recover_at : std::vector<int> {}, loss_span, &bitstream);
        ASSERT_TRUE(result) << format << native;
        const auto idrs = std::count(result->idr.begin(), result->idr.end(), true);
        std::vector<double> ms(result->frame_ms.begin() + 10, result->frame_ms.end());
        std::ranges::sort(ms);
        note(std::format("{} {} {}: IDR frames {}, recovery frames {} B / {} B, median frame {} B, encode p50 {:.2f} ms p99 {:.2f} ms", format ? "HEVC" : "H.264", native ? "native" : "ffmpeg", losses ? "loss" : "clean", idrs, result->bytes[recover_at[0] - 1], result->bytes[recover_at[1] - 1], [&] {
                           auto b = result->bytes;
                           std::ranges::sort(b);
                           return b[b.size() / 2];
                         }(), ms[ms.size() / 2], ms[ms.size() * 99 / 100]));
        if (losses) {
          EXPECT_EQ(idrs, native ? 1 : 3) << (native ? "RFI must not fall back to an IDR" : "FFmpeg answers each loss with an IDR");
          if (native) {
            EXPECT_TRUE(result->after_rfi[recover_at[0] - 1]);
            EXPECT_TRUE(result->after_rfi[recover_at[1] - 1]);
          }
        }
        if (out_env && *out_env) {
          const fs::path out_dir {out_env};
          const auto base = std::format("{}_{}x{}_{}_{}", format ? "hevc" : "h264", width, height, native ? "native" : "ffmpeg", losses ? "rfi" : "clean");
          std::ofstream out {out_dir / (base + (format ? ".hevc" : ".h264")), std::ios::binary};
          std::ofstream csv {out_dir / (base + ".csv")};
          csv << "frame,src,bytes,idr,after_rfi,kept,encode_ms\n";
          for (int i = 0; i < count; ++i) {
            const int frame_nr = i + 1;
            const bool kept = !losses || std::ranges::none_of(recover_at, [&](int r) {
                                return frame_nr >= r - loss_span && frame_nr < r;
                              });
            if (kept) {
              out.write(reinterpret_cast<const char *>(bitstream.data() + result->offsets[i]), static_cast<std::streamsize>(result->bytes[i]));
            }
            csv << std::format("{},{},{},{},{},{},{:.3f}\n", frame_nr, i % frames.size(), result->bytes[i], result->idr[i] ? 1 : 0, result->after_rfi[i] ? 1 : 0, kept ? 1 : 0, result->frame_ms[i]);
          }
        }
      }
    }
  }

  frames.clear();
  disp.reset();
  cleanup.disable();
  shell_output("kill " + player_pid + " 2>/dev/null");
  vd::set_capture_target(std::nullopt);
  const auto display = target->display;
  server.stop();
  EXPECT_FALSE(fs::exists("/tmp/.X11-unix/X" + display.substr(1)));
  EXPECT_EQ(shell_output("DISPLAY=:0 xrandr --query 2>/dev/null"), desk_before) << "the desktop must not change";
  fs::remove_all(dir);
}
#endif
