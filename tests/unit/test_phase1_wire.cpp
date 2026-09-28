/**
 * @file tests/unit/test_phase1_wire.cpp
 * @brief Host parsing of client packets exactly as the V+ engine (moonlight-common-c) builds them:
 *        microphone datagrams, clipboard 0x5508 frames, controller motion/arrival/battery packets.
 *        Layouts are documented in designs/roadmap/phase1-wire-contracts.md.
 */
// test includes
#include "../tests_common.h"

// standard includes
#include <array>
#include <cstring>
#include <vector>

// lib includes
#include <openssl/evp.h>

// local includes
#include "src/client_permissions.h"
#include "src/clipboard.h"
#include "src/input.h"
#include "src/mic_packet.h"
#include "src/utility.h"

namespace {
  void put_le16(std::vector<std::uint8_t> &out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
  }

  void put_le32(std::vector<std::uint8_t> &out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
      out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
  }

  void put_be32(std::vector<std::uint8_t> &out, std::uint32_t v) {
    for (int i = 3; i >= 0; --i) {
      out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
  }

  void put_float(std::vector<std::uint8_t> &out, float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    put_le32(out, bits);
  }

  /**
   * @brief A mic datagram as MicrophoneStream.c sends it: 12-byte LE header + payload.
   */
  std::vector<std::uint8_t> vplus_mic_packet(std::uint16_t seq, const std::vector<std::uint8_t> &payload) {
    std::vector<std::uint8_t> out {0x00, 0x61};
    put_le16(out, seq);
    put_le32(out, 123456);  // timestamp (ms)
    put_le32(out, 0x12345678);  // MIC_PACKET_MAGIC as ssrc
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
  }

  std::vector<std::uint8_t> aes_cbc_encrypt(const std::array<std::uint8_t, 16> &key, const std::array<std::uint8_t, 16> &iv, const std::vector<std::uint8_t> &plain) {
    std::vector<std::uint8_t> out(plain.size() + 16);
    int len = 0;
    int total = 0;
    auto *ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr, key.data(), iv.data());
    EVP_EncryptUpdate(ctx, out.data(), &len, plain.data(), static_cast<int>(plain.size()));
    total = len;
    EVP_EncryptFinal_ex(ctx, out.data() + total, &len);
    total += len;
    EVP_CIPHER_CTX_free(ctx);
    out.resize(static_cast<std::size_t>(total));
    return out;
  }

  /**
   * @brief An input packet as InputStream.c frames it: BE32 size, LE32 magic, body.
   */
  std::vector<std::uint8_t> vplus_input_packet(std::uint32_t magic, const std::vector<std::uint8_t> &body) {
    std::vector<std::uint8_t> out;
    put_be32(out, static_cast<std::uint32_t>(4 + body.size()));
    put_le32(out, magic);
    out.insert(out.end(), body.begin(), body.end());
    return out;
  }
}  // namespace

// ---------------------------------------------------------------------------------------------
// Microphone
// ---------------------------------------------------------------------------------------------

TEST(Phase1WireMic, ParsesTheVPlusLegacyHeader) {
  const std::vector<std::uint8_t> opus {0xfc, 0xff, 0xfe, 0x01, 0x02};
  const auto packet = vplus_mic_packet(0x0102, opus);
  ASSERT_EQ(packet.size(), 12 + opus.size());
  const auto parsed = mic_packet::parse(packet.data(), packet.size());
  ASSERT_TRUE(parsed);
  EXPECT_EQ(parsed->header, mic_packet::header_e::legacy);
  EXPECT_EQ(parsed->sequence, 0x0102);
  EXPECT_EQ(parsed->timestamp, 123456u);
  EXPECT_EQ(parsed->ssrc, mic_packet::kMagicSsrc);
  EXPECT_EQ(std::vector<std::uint8_t>(parsed->payload.begin(), parsed->payload.end()), opus);
  EXPECT_TRUE(mic_packet::plausible_opus(parsed->payload));
}

TEST(Phase1WireMic, ParsesTheExtendedHeaderAndRejectsJunk) {
  std::vector<std::uint8_t> ext {0x00};
  put_le16(ext, 0x5504);
  put_le16(ext, 7);
  put_le32(ext, 1);
  put_le32(ext, 2);
  ext.push_back(0x78);
  const auto parsed = mic_packet::parse(ext.data(), ext.size());
  ASSERT_TRUE(parsed);
  EXPECT_EQ(parsed->header, mic_packet::header_e::extended);
  EXPECT_EQ(parsed->sequence, 7);
  EXPECT_EQ(parsed->payload.size(), 1u);

  const auto header_only = vplus_mic_packet(1, {});
  EXPECT_FALSE(mic_packet::parse(header_only.data(), header_only.size()));
  std::vector<std::uint8_t> video_ping(16, 0);
  EXPECT_FALSE(mic_packet::parse(video_ping.data(), video_ping.size()));
  EXPECT_FALSE(mic_packet::parse(nullptr, 20));
  EXPECT_FALSE(mic_packet::plausible_opus(std::vector<std::uint8_t> {0, 0, 0, 0, 1}));
  EXPECT_FALSE(mic_packet::plausible_opus(std::vector<std::uint8_t> {0xff, 0xff, 0xff, 0xff}));
  EXPECT_TRUE(mic_packet::plausible_opus(std::vector<std::uint8_t> {0x08}));  // DTX frame
}

TEST(Phase1WireMic, DecryptsTheVPlusAesCbcFormat) {
  // V+: key = rikey, IV = BE32(BE32(rikeyid bytes) + seq) followed by 12 zero bytes.
  const std::array<std::uint8_t, 16> key {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  const std::uint32_t ri_key_id = 0x11223344;
  const std::uint16_t seq = 0xfffe;
  std::array<std::uint8_t, 16> iv {};
  const std::uint32_t v = ri_key_id + seq;
  iv[0] = static_cast<std::uint8_t>(v >> 24);
  iv[1] = static_cast<std::uint8_t>(v >> 16);
  iv[2] = static_cast<std::uint8_t>(v >> 8);
  iv[3] = static_cast<std::uint8_t>(v);
  EXPECT_EQ(mic_packet::iv_for(ri_key_id, seq), iv);

  const std::vector<std::uint8_t> opus {0xfc, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17};
  const auto cipher = aes_cbc_encrypt(key, iv, opus);
  EXPECT_EQ(cipher.size(), 32u);
  const auto plain = mic_packet::decrypt(key, ri_key_id, seq, cipher);
  ASSERT_TRUE(plain);
  EXPECT_EQ(*plain, opus);
  EXPECT_FALSE(mic_packet::decrypt(key, ri_key_id, seq + 1, cipher).has_value() && *mic_packet::decrypt(key, ri_key_id, seq + 1, cipher) == opus);
  EXPECT_FALSE(mic_packet::decrypt(key, ri_key_id, seq, std::vector<std::uint8_t>(15, 0)));
}

TEST(Phase1WireMic, PlaintextFromTheInternetIsRefused) {
  // Only a streaming client's address may feed Nova Mic; plaintext only from LAN/Tailscale.
  EXPECT_TRUE(mic_packet::accept_source(true, false, false));
  EXPECT_FALSE(mic_packet::accept_source(true, true, false));
  EXPECT_TRUE(mic_packet::accept_source(true, true, true));
  EXPECT_FALSE(mic_packet::accept_source(false, false, false));
  EXPECT_FALSE(mic_packet::accept_source(false, false, true));
}

// ---------------------------------------------------------------------------------------------
// Clipboard 0x5508
// ---------------------------------------------------------------------------------------------

TEST(Phase1WireClipboard, DecodesAVPlusTextFrame) {
  // ClipboardSyncManager: version 1, kind, LE32 token, LE32 length, payload.
  const std::string text = "héllo from Nebula";
  std::vector<std::uint8_t> frame {1, 1};
  put_le32(frame, 0xdeadbeef);
  put_le32(frame, static_cast<std::uint32_t>(text.size()));
  frame.insert(frame.end(), text.begin(), text.end());

  const auto decoded = clipboard::decode(frame.data(), frame.size());
  ASSERT_TRUE(decoded);
  EXPECT_EQ(decoded->kind, clipboard::kind_e::text);
  EXPECT_EQ(decoded->token, 0xdeadbeefu);
  EXPECT_EQ(std::string(decoded->payload.begin(), decoded->payload.end()), text);
  EXPECT_EQ(clipboard::encode(*decoded), frame);

  auto bad_length = frame;
  bad_length[6] += 1;
  EXPECT_FALSE(clipboard::decode(bad_length.data(), bad_length.size()));
  auto bad_version = frame;
  bad_version[0] = 2;
  EXPECT_FALSE(clipboard::decode(bad_version.data(), bad_version.size()));
}

TEST(Phase1WireClipboard, BlobRefIdsAreValidatedWithoutThrowing) {
  const auto ref = [](const std::string &json) {
    return clipboard::ref_id(std::vector<std::uint8_t>(json.begin(), json.end()));
  };
  EXPECT_EQ(ref(R"({"type":"ref","id":"0f8b6c1e-8a1d-4a4e-9d6e-1a2b3c4d5e6f","mime":"image/png","size":10})"), "0f8b6c1e-8a1d-4a4e-9d6e-1a2b3c4d5e6f");
  EXPECT_EQ(ref(R"({"id":42})"), "");
  EXPECT_EQ(ref(R"({"id":"../../etc/passwd"})"), "");
  EXPECT_EQ(ref(R"({"id":""})"), "");
  EXPECT_EQ(ref("not json"), "");
  EXPECT_EQ(ref(R"(["id"])"), "");
  EXPECT_EQ(ref(std::string(R"({"id":")") + std::string(65, 'a') + "\"}"), "");
}

// ---------------------------------------------------------------------------------------------
// Controller motion / arrival / battery (input channel)
// ---------------------------------------------------------------------------------------------

TEST(Phase1WireMotion, VPlusControllerPacketsNeedTheControllerPermission) {
  namespace perm = client_permissions;
  // LiSendControllerArrivalEvent: controllerNumber, type (PS=2), capabilities (accel|gyro|touchpad), button flags.
  std::vector<std::uint8_t> arrival {0, 2};
  put_le16(arrival, 0x04 | 0x08 | 0x02);
  put_le32(arrival, 0x000FFFFF);
  const auto arrival_packet = vplus_input_packet(0x55000004, arrival);
  EXPECT_EQ(arrival_packet.size(), 16u);

  // LiSendControllerMotionEvent: controllerNumber, motionType (2 = gyro), 2 zero bytes, x/y/z floats.
  std::vector<std::uint8_t> motion {0, 2, 0, 0};
  put_float(motion, 1.5f);
  put_float(motion, -2.0f);
  put_float(motion, 90.0f);
  const auto motion_packet = vplus_input_packet(0x55000006, motion);
  EXPECT_EQ(motion_packet.size(), 24u);

  // LiSendControllerBatteryEvent: controllerNumber, state, percentage, zero.
  const auto battery_packet = vplus_input_packet(0x55000007, {0, 3, 80, 0});

  for (const auto *packet : {&arrival_packet, &motion_packet, &battery_packet}) {
    EXPECT_TRUE(input::is_packet_permitted(*packet, perm::input_controller));
    EXPECT_FALSE(input::is_packet_permitted(*packet, perm::full & ~perm::input_controller));
    EXPECT_FALSE(input::is_packet_permitted(*packet, perm::view_only));
  }
}
