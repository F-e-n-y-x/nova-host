/**
 * @file tests/unit/test_nvenc_backend.cpp
 * @brief Tests for choosing between the native and the FFmpeg NVENC encoder.
 */
// standard includes
#include <string_view>
#include <vector>

// lib includes
#include <gtest/gtest.h>

// local includes
#include "src/nvenc/nvenc_backend.h"

namespace {

  /**
   * @brief Stand-in for `video::encoder_t` in probe lists.
   */
  struct fake_encoder {
    std::string_view name;  ///< Encoder name.
  };

  using enum nvenc::nvenc_backend;

}  // namespace

TEST(NvencBackendTest, ParsesEveryConfigValue) {
  EXPECT_EQ(nvenc::nvenc_backend_from_view("auto"), automatic);
  EXPECT_EQ(nvenc::nvenc_backend_from_view("native"), native);
  EXPECT_EQ(nvenc::nvenc_backend_from_view("ffmpeg"), ffmpeg);
  EXPECT_FALSE(nvenc::nvenc_backend_from_view("cuda").has_value());
  EXPECT_FALSE(nvenc::nvenc_backend_from_view("").has_value());
  EXPECT_FALSE(nvenc::nvenc_backend_from_view("Auto").has_value());
}

TEST(NvencBackendTest, RoundTripsThroughItsConfigSpelling) {
  for (auto backend : {automatic, native, ffmpeg}) {
    EXPECT_EQ(nvenc::nvenc_backend_from_view(nvenc::to_string(backend)), backend);
  }
}

TEST(NvencBackendTest, AllowsImplementationsPerBackend) {
  EXPECT_TRUE(nvenc::native_allowed(automatic));
  EXPECT_TRUE(nvenc::ffmpeg_allowed(automatic));
  EXPECT_TRUE(nvenc::session_fallback_allowed(automatic));

  EXPECT_TRUE(nvenc::native_allowed(native));
  EXPECT_FALSE(nvenc::ffmpeg_allowed(native));
  EXPECT_FALSE(nvenc::session_fallback_allowed(native));

  EXPECT_FALSE(nvenc::native_allowed(ffmpeg));
  EXPECT_TRUE(nvenc::ffmpeg_allowed(ffmpeg));
  EXPECT_FALSE(nvenc::session_fallback_allowed(ffmpeg));
}

class NvencBackendFilterTest: public testing::Test {
protected:
  fake_encoder native_nvenc {"nvenc"};
  fake_encoder ffmpeg_nvenc {"nvenc"};
  fake_encoder vaapi {"vaapi"};
  fake_encoder software {"software"};

  std::vector<fake_encoder *> probe_list() {
    return {&native_nvenc, &ffmpeg_nvenc, &vaapi, &software};
  }
};

TEST_F(NvencBackendFilterTest, AutoKeepsNativeAheadOfFfmpeg) {
  auto list = probe_list();
  nvenc::filter_nvenc_encoders(list, automatic, &native_nvenc, &ffmpeg_nvenc);
  EXPECT_EQ(list, (std::vector<fake_encoder *> {&native_nvenc, &ffmpeg_nvenc, &vaapi, &software}));
}

TEST_F(NvencBackendFilterTest, NativeDropsFfmpegNvencOnly) {
  auto list = probe_list();
  nvenc::filter_nvenc_encoders(list, native, &native_nvenc, &ffmpeg_nvenc);
  EXPECT_EQ(list, (std::vector<fake_encoder *> {&native_nvenc, &vaapi, &software}));
}

TEST_F(NvencBackendFilterTest, FfmpegDropsNative) {
  auto list = probe_list();
  nvenc::filter_nvenc_encoders(list, ffmpeg, &native_nvenc, &ffmpeg_nvenc);
  EXPECT_EQ(list, (std::vector<fake_encoder *> {&ffmpeg_nvenc, &vaapi, &software}));
}

TEST_F(NvencBackendFilterTest, BuildWithoutNativeKeepsFfmpegForEveryBackend) {
  for (auto backend : {automatic, native, ffmpeg}) {
    std::vector<fake_encoder *> list {&ffmpeg_nvenc, &vaapi, &software};
    nvenc::filter_nvenc_encoders<fake_encoder>(list, backend, nullptr, &ffmpeg_nvenc);
    EXPECT_EQ(list, (std::vector<fake_encoder *> {&ffmpeg_nvenc, &vaapi, &software})) << nvenc::to_string(backend);
  }
}
