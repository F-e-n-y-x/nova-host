/**
 * @file tests/unit/test_nvenc_cuda.cpp
 * @brief Tests for the Linux native NVENC encoder: SDK selection, and RFI, intra-refresh and
 *        bitrate handling against a fake NVENC driver (no GPU).
 */
#if defined(__linux__) && defined(SUNSHINE_BUILD_CUDA)

  // The fake driver below is built against the SDK 13.1 implementation of nvenc_base.
  #define NVENC_NAMESPACE nvenc_1301
  #define NVENC_SDK_VERSION 1301

  // standard includes
  #include <cstdint>
  #include <cstring>
  #include <memory>
  #include <vector>

  // lib includes
  #include <gtest/gtest.h>

  // local includes
  #include "src/nvenc/nvenc_base.h"
  #include "src/nvenc/nvenc_cuda_factory.h"

namespace {

  using namespace nvenc_1301;

  /**
   * @brief State of the fake NVENC driver.
   */
  struct fake_driver_state {
    bool rfi = true;  ///< Report NV_ENC_CAPS_SUPPORT_REF_PIC_INVALIDATION.
    bool intra_refresh = true;  ///< Report NV_ENC_CAPS_SUPPORT_INTRA_REFRESH.
    bool multiple_ref_frames = true;  ///< Report NV_ENC_CAPS_SUPPORT_MULTIPLE_REF_FRAMES.
    NVENCSTATUS invalidate_status = NV_ENC_SUCCESS;  ///< Result of nvEncInvalidateRefFrames().
    NVENCSTATUS reconfigure_status = NV_ENC_SUCCESS;  ///< Result of nvEncReconfigureEncoder().

    std::vector<std::uint64_t> invalidated;  ///< Timestamps passed to nvEncInvalidateRefFrames().
    std::vector<std::uint32_t> pic_flags;  ///< encodePicFlags of each nvEncEncodePicture().
    std::uint64_t last_timestamp = 0;  ///< inputTimeStamp of the last picture.
    bool first_picture = true;  ///< The next picture is the first of the session.
    NV_ENC_CONFIG init_config = {};  ///< Configuration passed to nvEncInitializeEncoder().
    NV_ENC_CONFIG reconfig_config = {};  ///< Configuration passed to nvEncReconfigureEncoder().
    int reconfigure_calls = 0;  ///< Number of nvEncReconfigureEncoder() calls.
    std::vector<std::uint8_t> bitstream {0, 0, 0, 1, 0x40};  ///< Payload of every encoded frame.
  };

  fake_driver_state fake;  ///< The fake driver used by the current test.

  void *const fake_session = reinterpret_cast<void *>(0x1000);  ///< Opaque session handle.
  void *const fake_output = reinterpret_cast<void *>(0x2000);  ///< Opaque bitstream buffer.
  void *const fake_input = reinterpret_cast<void *>(0x3000);  ///< Opaque mapped input.

  NVENCSTATUS NVENCAPI open_session(NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS *, void **encoder) {
    *encoder = fake_session;
    return NV_ENC_SUCCESS;
  }

  NVENCSTATUS NVENCAPI get_guid_count(void *, std::uint32_t *count) {
    *count = 2;
    return NV_ENC_SUCCESS;
  }

  NVENCSTATUS NVENCAPI get_guids(void *, GUID *guids, std::uint32_t size, std::uint32_t *count) {
    if (size < 2) {
      return NV_ENC_ERR_INVALID_PARAM;
    }
    guids[0] = NV_ENC_CODEC_H264_GUID;
    guids[1] = NV_ENC_CODEC_HEVC_GUID;
    *count = 2;
    return NV_ENC_SUCCESS;
  }

  NVENCSTATUS NVENCAPI get_caps(void *, GUID, NV_ENC_CAPS_PARAM *param, int *value) {
    switch (param->capsToQuery) {
      case NV_ENC_CAPS_WIDTH_MAX:
      case NV_ENC_CAPS_HEIGHT_MAX:
        *value = 4096;
        break;
      case NV_ENC_CAPS_SUPPORT_REF_PIC_INVALIDATION:
        *value = fake.rfi;
        break;
      case NV_ENC_CAPS_SUPPORT_INTRA_REFRESH:
        *value = fake.intra_refresh;
        break;
      case NV_ENC_CAPS_SUPPORT_MULTIPLE_REF_FRAMES:
        *value = fake.multiple_ref_frames;
        break;
      case NV_ENC_CAPS_SUPPORT_CUSTOM_VBV_BUF_SIZE:
      case NV_ENC_CAPS_SUPPORT_CABAC:
        *value = 1;
        break;
      default:
        *value = 0;
        break;
    }
    return NV_ENC_SUCCESS;
  }

  NVENCSTATUS NVENCAPI get_preset_config(void *, GUID, GUID, NV_ENC_TUNING_INFO, NV_ENC_PRESET_CONFIG *preset) {
    preset->presetCfg = {.version = NV_ENC_CONFIG_VER};
    return NV_ENC_SUCCESS;
  }

  NVENCSTATUS NVENCAPI initialize(void *, NV_ENC_INITIALIZE_PARAMS *params) {
    fake.init_config = *params->encodeConfig;
    return NV_ENC_SUCCESS;
  }

  NVENCSTATUS NVENCAPI create_bitstream(void *, NV_ENC_CREATE_BITSTREAM_BUFFER *params) {
    params->bitstreamBuffer = fake_output;
    return NV_ENC_SUCCESS;
  }

  NVENCSTATUS NVENCAPI destroy_bitstream(void *, NV_ENC_OUTPUT_PTR) {
    return NV_ENC_SUCCESS;
  }

  NVENCSTATUS NVENCAPI map_input(void *, NV_ENC_MAP_INPUT_RESOURCE *params) {
    params->mappedResource = fake_input;
    params->mappedBufferFmt = NV_ENC_BUFFER_FORMAT_NV12;
    return NV_ENC_SUCCESS;
  }

  NVENCSTATUS NVENCAPI unmap_input(void *, NV_ENC_INPUT_PTR) {
    return NV_ENC_SUCCESS;
  }

  NVENCSTATUS NVENCAPI encode_picture(void *, NV_ENC_PIC_PARAMS *params) {
    fake.pic_flags.push_back(params->encodePicFlags);
    fake.last_timestamp = params->inputTimeStamp;
    return NV_ENC_SUCCESS;
  }

  NVENCSTATUS NVENCAPI lock_bitstream(void *, NV_ENC_LOCK_BITSTREAM *params) {
    params->bitstreamBufferPtr = fake.bitstream.data();
    params->bitstreamSizeInBytes = static_cast<std::uint32_t>(fake.bitstream.size());
    params->outputTimeStamp = fake.last_timestamp;
    const bool idr = fake.first_picture || (fake.pic_flags.back() & NV_ENC_PIC_FLAG_FORCEIDR);
    params->pictureType = idr ? NV_ENC_PIC_TYPE_IDR : NV_ENC_PIC_TYPE_P;
    fake.first_picture = false;
    return NV_ENC_SUCCESS;
  }

  NVENCSTATUS NVENCAPI unlock_bitstream(void *, NV_ENC_OUTPUT_PTR) {
    return NV_ENC_SUCCESS;
  }

  NVENCSTATUS NVENCAPI invalidate(void *, std::uint64_t timestamp) {
    fake.invalidated.push_back(timestamp);
    return fake.invalidate_status;
  }

  NVENCSTATUS NVENCAPI reconfigure(void *, NV_ENC_RECONFIGURE_PARAMS *params) {
    ++fake.reconfigure_calls;
    fake.reconfig_config = *params->reInitEncodeParams.encodeConfig;
    return fake.reconfigure_status;
  }

  NVENCSTATUS NVENCAPI unregister_resource(void *, NV_ENC_REGISTERED_PTR) {
    return NV_ENC_SUCCESS;
  }

  NVENCSTATUS NVENCAPI fake_destroy_encoder(void *) {
    return NV_ENC_SUCCESS;
  }

  /**
   * @brief nvenc_base on the fake driver, with a fake registered input.
   */
  class fake_nvenc final: public nvenc_base {
  public:
    fake_nvenc():
        nvenc_base(NV_ENC_DEVICE_TYPE_CUDA) {
    }

    ~fake_nvenc() override {
      destroy_encoder();
    }

  protected:
    bool init_library() override {
      auto functions = std::make_shared<NV_ENCODE_API_FUNCTION_LIST>();
      functions->version = NV_ENCODE_API_FUNCTION_LIST_VER;
      functions->nvEncOpenEncodeSessionEx = open_session;
      functions->nvEncGetEncodeGUIDCount = get_guid_count;
      functions->nvEncGetEncodeGUIDs = get_guids;
      functions->nvEncGetEncodeCaps = get_caps;
      functions->nvEncGetEncodePresetConfigEx = get_preset_config;
      functions->nvEncInitializeEncoder = initialize;
      functions->nvEncCreateBitstreamBuffer = create_bitstream;
      functions->nvEncDestroyBitstreamBuffer = destroy_bitstream;
      functions->nvEncMapInputResource = map_input;
      functions->nvEncUnmapInputResource = unmap_input;
      functions->nvEncEncodePicture = encode_picture;
      functions->nvEncLockBitstream = lock_bitstream;
      functions->nvEncUnlockBitstream = unlock_bitstream;
      functions->nvEncInvalidateRefFrames = invalidate;
      functions->nvEncReconfigureEncoder = reconfigure;
      functions->nvEncUnregisterResource = unregister_resource;
      functions->nvEncDestroyEncoder = fake_destroy_encoder;
      nvenc = std::move(functions);
      device = reinterpret_cast<void *>(0x4000);
      return true;
    }

    bool create_and_register_input_buffer() override {
      registered_input_buffer = reinterpret_cast<void *>(0x5000);
      return true;
    }
  };

  /**
   * @brief HEVC 1080p120 stream configuration as a Moonlight client sends it.
   *
   * @param video_format 0 for H.264, 1 for HEVC.
   * @return Client configuration.
   */
  video::config_t client_config(int video_format = 1) {
    video::config_t config {};
    config.width = 1920;
    config.height = 1080;
    config.framerate = 120;
    config.bitrate = 40000;
    config.slicesPerFrame = 1;
    config.numRefFrames = 0;
    config.videoFormat = video_format;
    return config;
  }

  /**
   * @brief SDR Rec. 709 colorspace.
   *
   * @return Colorspace metadata.
   */
  video::sunshine_colorspace_t sdr_colorspace() {
    return {video::colorspace_e::rec709, false, 8};
  }

}  // namespace

class NvencBaseFakeDriverTest: public testing::Test {
protected:
  void SetUp() override {
    fake = {};
  }

  /**
   * @brief Create the encoder and encode frames `first` to `last`.
   */
  void create_and_encode(std::uint64_t first, std::uint64_t last, nvenc::nvenc_config config = {}, int video_format = 1) {
    ASSERT_TRUE(encoder.create_encoder(config, client_config(video_format), sdr_colorspace(), platf::pix_fmt_e::nv12));
    encode(first, last);
  }

  void encode(std::uint64_t first, std::uint64_t last) {
    for (auto i = first; i <= last; ++i) {
      auto frame = encoder.encode_frame(i, false);
      ASSERT_EQ(frame.frame_index, i);
      ASSERT_FALSE(frame.data.empty());
    }
  }

  fake_nvenc encoder;
};

TEST_F(NvencBaseFakeDriverTest, InvalidatesTheLostRangeUpToTheLastEncodedFrame) {
  create_and_encode(1, 10);
  fake.invalidated.clear();

  EXPECT_TRUE(encoder.invalidate_ref_frames(8, 9));
  EXPECT_EQ(fake.invalidated, (std::vector<std::uint64_t> {8, 9, 10}));

  auto frame = encoder.encode_frame(11, false);
  EXPECT_FALSE(frame.idr);
  EXPECT_TRUE(frame.after_ref_frame_invalidation);
  EXPECT_EQ(fake.pic_flags.back() & NV_ENC_PIC_FLAG_FORCEIDR, 0U);

  frame = encoder.encode_frame(12, false);
  EXPECT_FALSE(frame.after_ref_frame_invalidation);
}

TEST_F(NvencBaseFakeDriverTest, RepeatedRequestInsideTheLastRangeIsNotResent) {
  create_and_encode(1, 10);
  ASSERT_TRUE(encoder.invalidate_ref_frames(8, 9));
  fake.invalidated.clear();

  EXPECT_TRUE(encoder.invalidate_ref_frames(9, 10));
  EXPECT_TRUE(fake.invalidated.empty());
}

TEST_F(NvencBaseFakeDriverTest, RangeAsLargeAsTheDpbNeedsAnIdr) {
  create_and_encode(1, 20);
  ASSERT_EQ(encoder.capabilities().ref_frames_in_dpb, 5U);
  fake.invalidated.clear();

  EXPECT_FALSE(encoder.invalidate_ref_frames(16, 20));
  EXPECT_TRUE(fake.invalidated.empty());

  // The session then forces an IDR on the next frame.
  auto frame = encoder.encode_frame(21, true);
  EXPECT_TRUE(frame.idr);
  EXPECT_NE(fake.pic_flags.back() & NV_ENC_PIC_FLAG_FORCEIDR, 0U);
}

TEST_F(NvencBaseFakeDriverTest, RangeJustInsideTheDpbIsInvalidated) {
  create_and_encode(1, 20);
  fake.invalidated.clear();

  EXPECT_TRUE(encoder.invalidate_ref_frames(17, 18));
  EXPECT_EQ(fake.invalidated, (std::vector<std::uint64_t> {17, 18, 19, 20}));
}

TEST_F(NvencBaseFakeDriverTest, InvertedRangeNeedsAnIdr) {
  create_and_encode(1, 10);
  fake.invalidated.clear();

  EXPECT_FALSE(encoder.invalidate_ref_frames(9, 8));
  EXPECT_TRUE(fake.invalidated.empty());
}

TEST_F(NvencBaseFakeDriverTest, DriverFailureNeedsAnIdr) {
  create_and_encode(1, 10);
  fake.invalidate_status = NV_ENC_ERR_GENERIC;

  EXPECT_FALSE(encoder.invalidate_ref_frames(9, 10));
}

TEST_F(NvencBaseFakeDriverTest, WithoutRfiCapsEveryRequestNeedsAnIdr) {
  fake.rfi = false;
  create_and_encode(1, 10);
  fake.invalidated.clear();

  const auto caps = encoder.capabilities();
  EXPECT_FALSE(caps.rfi_supported);
  EXPECT_FALSE(caps.rfi_active);
  EXPECT_FALSE(encoder.invalidate_ref_frames(9, 10));
  EXPECT_TRUE(fake.invalidated.empty());
}

TEST_F(NvencBaseFakeDriverTest, WithoutMultipleReferenceFramesRfiIsOff) {
  fake.multiple_ref_frames = false;
  create_and_encode(1, 10);

  const auto caps = encoder.capabilities();
  EXPECT_TRUE(caps.rfi_supported);
  EXPECT_FALSE(caps.rfi_active);
  EXPECT_EQ(caps.ref_frames_in_dpb, 1U);
  EXPECT_FALSE(encoder.invalidate_ref_frames(10, 10));
}

TEST_F(NvencBaseFakeDriverTest, NoRequestsBeforeTheEncoderExists) {
  EXPECT_FALSE(encoder.invalidate_ref_frames(1, 2));
  EXPECT_FALSE(encoder.set_bitrate(10000));
  EXPECT_FALSE(encoder.capabilities().rfi_active);
}

TEST_F(NvencBaseFakeDriverTest, H264UsesTheSameRfiPath) {
  create_and_encode(1, 10, {}, 0);
  fake.invalidated.clear();

  EXPECT_TRUE(encoder.invalidate_ref_frames(10, 10));
  EXPECT_EQ(fake.invalidated, (std::vector<std::uint64_t> {10}));
  EXPECT_EQ(fake.init_config.encodeCodecConfig.h264Config.maxNumRefFrames, 5U);
}

TEST_F(NvencBaseFakeDriverTest, IntraRefreshIsOffUnlessRequested) {
  create_and_encode(1, 1);

  EXPECT_TRUE(encoder.capabilities().intra_refresh_supported);
  EXPECT_FALSE(encoder.capabilities().intra_refresh_active);
  EXPECT_EQ(static_cast<unsigned>(fake.init_config.encodeCodecConfig.hevcConfig.enableIntraRefresh), 0U);
}

TEST_F(NvencBaseFakeDriverTest, HostOptionEnablesIntraRefresh) {
  nvenc::nvenc_config config;
  config.intra_refresh = true;
  create_and_encode(1, 1, config);

  EXPECT_TRUE(encoder.capabilities().intra_refresh_active);
  EXPECT_EQ(static_cast<unsigned>(fake.init_config.encodeCodecConfig.hevcConfig.enableIntraRefresh), 1U);
  EXPECT_EQ(fake.init_config.encodeCodecConfig.hevcConfig.intraRefreshPeriod, 300U);
  // RFI stays available next to intra-refresh.
  EXPECT_TRUE(encoder.capabilities().rfi_active);
}

TEST_F(NvencBaseFakeDriverTest, ClientRequestEnablesIntraRefresh) {
  auto config = client_config(0);
  config.enableIntraRefresh = 1;
  ASSERT_TRUE(encoder.create_encoder({}, config, sdr_colorspace(), platf::pix_fmt_e::nv12));

  EXPECT_TRUE(encoder.capabilities().intra_refresh_active);
  EXPECT_EQ(static_cast<unsigned>(fake.init_config.encodeCodecConfig.h264Config.enableIntraRefresh), 1U);
}

TEST_F(NvencBaseFakeDriverTest, IntraRefreshNeedsTheCapability) {
  fake.intra_refresh = false;
  nvenc::nvenc_config config;
  config.intra_refresh = true;
  create_and_encode(1, 1, config);

  EXPECT_FALSE(encoder.capabilities().intra_refresh_active);
  EXPECT_EQ(static_cast<unsigned>(fake.init_config.encodeCodecConfig.hevcConfig.enableIntraRefresh), 0U);
}

TEST_F(NvencBaseFakeDriverTest, BitrateChangeReconfiguresWithoutReset) {
  nvenc::nvenc_config config;
  config.vbv_percentage_increase = 50;
  create_and_encode(1, 3, config);
  ASSERT_EQ(fake.init_config.rcParams.averageBitRate, 40000U * 1000U);

  EXPECT_TRUE(encoder.set_bitrate(20000));
  EXPECT_EQ(fake.reconfigure_calls, 1);
  EXPECT_EQ(fake.reconfig_config.rcParams.averageBitRate, 20000U * 1000U);
  EXPECT_EQ(fake.reconfig_config.rcParams.vbvBufferSize, 20000U * 1000U / 120U * 150U / 100U);
  EXPECT_EQ(fake.reconfig_config.rcParams.rateControlMode, NV_ENC_PARAMS_RC_CBR);

  fake.reconfigure_status = NV_ENC_ERR_INVALID_PARAM;
  EXPECT_FALSE(encoder.set_bitrate(10000));
  EXPECT_FALSE(encoder.set_bitrate(0));
}

TEST_F(NvencBaseFakeDriverTest, PresetAndTuningConfigAreApplied) {
  nvenc::nvenc_config config;
  config.quality_preset = 4;
  config.two_pass = nvenc::nvenc_two_pass::disabled;
  config.adaptive_quantization = true;
  create_and_encode(1, 1, config);

  EXPECT_EQ(fake.init_config.rcParams.multiPass, NV_ENC_MULTI_PASS_DISABLED);
  EXPECT_EQ(static_cast<unsigned>(fake.init_config.rcParams.enableAQ), 1U);
  EXPECT_EQ(fake.init_config.gopLength, NVENC_INFINITE_GOPLENGTH);
  EXPECT_EQ(fake.init_config.frameIntervalP, 1U);
}

TEST_F(NvencBaseFakeDriverTest, TenBitInputIsRejectedWithoutTheCapability) {
  EXPECT_FALSE(encoder.create_encoder({}, client_config(), sdr_colorspace(), platf::pix_fmt_e::p010));
}

namespace {

  std::uint32_t fake_max_version;  ///< Packed version returned by the fake NvEncodeAPIGetMaxSupportedVersion().
  std::uint32_t fake_max_version_status;  ///< Status returned by the fake NvEncodeAPIGetMaxSupportedVersion().

  std::uint32_t get_fake_max_version(std::uint32_t *version) {
    *version = fake_max_version;
    return fake_max_version_status;
  }

  /**
   * @brief Runtime API whose driver reports `packed_version`.
   */
  nvenc::nvenc_cuda_runtime_api fake_runtime(bool loads = true, bool has_symbol = true) {
    return {
      [loads]() {
        return loads ? nvenc::shared_library(reinterpret_cast<void *>(0x1), [](void *) {}) : nvenc::shared_library {};
      },
      [has_symbol](void *, const char *symbol) -> void * {
        if (!has_symbol || std::strcmp(symbol, "NvEncodeAPIGetMaxSupportedVersion") != 0) {
          return nullptr;
        }
        return reinterpret_cast<void *>(&get_fake_max_version);
      },
    };
  }

  /**
   * @brief Pack a version like NvEncodeAPIGetMaxSupportedVersion() does.
   */
  constexpr std::uint32_t packed(std::uint32_t major, std::uint32_t minor) {
    return (major << 4U) | minor;
  }

}  // namespace

TEST(NvencCudaFactoryTest, SelectsTheNewestSdkTheDriverSupports) {
  fake_max_version_status = 0;
  const std::pair<std::uint32_t, nvenc::nvenc_sdk_version> cases[] = {
    {packed(13, 0), nvenc::nvenc_sdk_version::sdk_13_0},  // driver 580 (GTX 1080 Ti on atom)
    {packed(13, 1), nvenc::nvenc_sdk_version::sdk_13_1},
    {packed(12, 2), nvenc::nvenc_sdk_version::sdk_12_0},
    {packed(11, 1), nvenc::nvenc_sdk_version::sdk_11_0},
  };
  for (const auto &[version, expected] : cases) {
    fake_max_version = version;
    auto factory = nvenc::nvenc_cuda_factory::get(fake_runtime());
    ASSERT_TRUE(factory) << version;
    EXPECT_EQ(factory->sdk_version(), expected);
  }
}

TEST(NvencCudaFactoryTest, RejectsDriversOlderThanSdk11) {
  fake_max_version_status = 0;
  fake_max_version = packed(10, 0);
  EXPECT_FALSE(nvenc::nvenc_cuda_factory::get(fake_runtime()));
}

TEST(NvencCudaFactoryTest, FailsWithoutTheDriverOrItsVersionQuery) {
  fake_max_version_status = 0;
  fake_max_version = packed(13, 0);
  EXPECT_FALSE(nvenc::nvenc_cuda_factory::get(fake_runtime(false)));
  EXPECT_FALSE(nvenc::nvenc_cuda_factory::get(fake_runtime(true, false)));

  fake_max_version_status = 1;
  EXPECT_FALSE(nvenc::nvenc_cuda_factory::get(fake_runtime()));
}

TEST(NvencCudaFactoryTest, SharedLibraryWrapsOnlyRealHandles) {
  EXPECT_FALSE(nvenc::make_shared_library(nullptr));
}

#endif
