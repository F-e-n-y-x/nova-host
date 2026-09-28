/**
 * @file src/mic_packet.h
 * @brief Pure parser and decryptor for remote-microphone UDP datagrams.
 *
 * Wire format (see docs/design/remote-mic.md and
 * designs/roadmap/phase1-wire-contracts.md), all fields little-endian:
 *
 *   legacy (V+ / moonlight-common-c MicrophoneStream.c), 12 bytes:
 *     u8 flags | u8 type = 0x61 | u16 seq | u32 timestamp | u32 ssrc (0x12345678)
 *   extended (other Foundation clients), 13 bytes:
 *     u8 flags | u16 type = 0x5504 | u16 seq | u32 timestamp | u32 ssrc
 *
 * followed by one Opus packet, or its AES-128-CBC/PKCS#7 ciphertext when the
 * session negotiated SS_ENC_MICROPHONE (0x08). The mic IV is the audio-stream
 * IV: BE32(avRiKeyId + seq) followed by 12 zero bytes.
 *
 * Header-only so stream.cpp and the unit tests share one implementation.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <openssl/evp.h>

namespace mic_packet {

  constexpr std::uint8_t kTypeOpus = 0x61;  ///< Legacy 8-bit packet type.
  constexpr std::uint16_t kTypeExt = 0x5504;  ///< Extended 16-bit packet type.
  constexpr std::uint32_t kMagicSsrc = 0x12345678;  ///< SSRC V+ puts in every packet (informational).
  constexpr std::size_t kLegacyHeaderSize = 12;  ///< Legacy header length.
  constexpr std::size_t kExtHeaderSize = 13;  ///< Extended header length.
  constexpr std::size_t kMaxDatagram = 1400;  ///< Largest datagram a client sends (MAX_MIC_PACKET_SIZE).
  constexpr std::uint32_t kEncryptionFlag = 0x08;  ///< SS_ENC_MICROPHONE.

  /**
   * @brief Header variant of a parsed datagram.
   */
  enum class header_e {
    legacy,  ///< 12-byte header, type 0x61.
    extended,  ///< 13-byte header, type 0x5504.
  };

  /**
   * @brief One parsed mic datagram; `payload` points into the caller's buffer.
   */
  struct parsed_t {
    header_e header;  ///< Which header layout matched.
    std::uint16_t sequence;  ///< Sequence number (decoded from little-endian).
    std::uint32_t timestamp;  ///< Sender timestamp in ms (decoded from little-endian).
    std::uint32_t ssrc;  ///< SSRC (decoded from little-endian).
    std::span<const std::uint8_t> payload;  ///< Opus bytes or ciphertext; never empty.
  };

  namespace detail {
    inline std::uint16_t le16(const std::uint8_t *p) {
      return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
    }

    inline std::uint32_t le32(const std::uint8_t *p) {
      return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
             (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
    }
  }  // namespace detail

  /**
   * @brief Parse a mic datagram header.
   * @param data Datagram bytes.
   * @param size Datagram length.
   * @return The parsed datagram, or std::nullopt when it is not a mic packet
   *         or carries no payload.
   */
  inline std::optional<parsed_t> parse(const std::uint8_t *data, std::size_t size) {
    if (!data) {
      return std::nullopt;
    }
    // Extended first: a legacy header always has byte 1 == 0x61, which can
    // never be the low byte (0x04) of the extended type, so there is no overlap.
    if (size > kExtHeaderSize && detail::le16(data + 1) == kTypeExt) {
      return parsed_t {
        header_e::extended,
        detail::le16(data + 3),
        detail::le32(data + 5),
        detail::le32(data + 9),
        {data + kExtHeaderSize, size - kExtHeaderSize},
      };
    }
    if (size > kLegacyHeaderSize && data[1] == kTypeOpus) {
      return parsed_t {
        header_e::legacy,
        detail::le16(data + 2),
        detail::le32(data + 4),
        detail::le32(data + 8),
        {data + kLegacyHeaderSize, size - kLegacyHeaderSize},
      };
    }
    return std::nullopt;
  }

  /**
   * @brief Build the 16-byte AES-CBC IV for one mic packet.
   * @param av_ri_key_id BE32 of the first four bytes of the launch IV
   *        (session->audio.avRiKeyId on the host, micRiKeyId in V+).
   * @param sequence Packet sequence number.
   * @return The IV.
   */
  inline std::array<std::uint8_t, 16> iv_for(std::uint32_t av_ri_key_id, std::uint16_t sequence) {
    std::array<std::uint8_t, 16> iv {};
    const std::uint32_t v = av_ri_key_id + sequence;
    iv[0] = static_cast<std::uint8_t>(v >> 24);
    iv[1] = static_cast<std::uint8_t>(v >> 16);
    iv[2] = static_cast<std::uint8_t>(v >> 8);
    iv[3] = static_cast<std::uint8_t>(v);
    return iv;
  }

  /**
   * @brief Decrypt an encrypted mic payload (AES-128-CBC, PKCS#7).
   * @param key 16-byte AES key (launch session rikey / gcm_key).
   * @param av_ri_key_id See iv_for().
   * @param sequence Packet sequence number from the header.
   * @param ciphertext Payload bytes after the header.
   * @return The Opus bytes, or std::nullopt on a bad key size, bad length or bad padding.
   */
  inline std::optional<std::vector<std::uint8_t>> decrypt(std::span<const std::uint8_t> key, std::uint32_t av_ri_key_id, std::uint16_t sequence, std::span<const std::uint8_t> ciphertext) {
    if (key.size() != 16 || ciphertext.empty() || ciphertext.size() % 16 != 0) {
      return std::nullopt;
    }
    const auto iv = iv_for(av_ri_key_id, sequence);
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
      return std::nullopt;
    }
    std::vector<std::uint8_t> out(ciphertext.size() + 16);
    int len = 0;
    int total = 0;
    bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr, key.data(), iv.data()) == 1 &&
              EVP_CIPHER_CTX_set_padding(ctx, 1) == 1 &&
              EVP_DecryptUpdate(ctx, out.data(), &len, ciphertext.data(), static_cast<int>(ciphertext.size())) == 1;
    if (ok) {
      total = len;
      ok = EVP_DecryptFinal_ex(ctx, out.data() + total, &len) == 1;
      total += len;
    }
    EVP_CIPHER_CTX_free(ctx);
    if (!ok || total <= 0) {
      return std::nullopt;
    }
    out.resize(static_cast<std::size_t>(total));
    return out;
  }

  /**
   * @brief Cheap sanity check used before handing bytes to the Opus decoder.
   *
   * Rejects the patterns Foundation also drops (first four bytes all 0x00 or
   * all 0xFF), which is what ciphertext decoded as plaintext or garbage looks
   * like most often. 1-3 byte DTX frames pass.
   * @param payload Candidate Opus bytes.
   * @return true when the payload may be Opus.
   */
  inline bool plausible_opus(std::span<const std::uint8_t> payload) {
    if (payload.empty()) {
      return false;
    }
    if (payload.size() < 4) {
      return true;
    }
    const bool zeros = payload[0] == 0 && payload[1] == 0 && payload[2] == 0 && payload[3] == 0;
    const bool ones = payload[0] == 0xFF && payload[1] == 0xFF && payload[2] == 0xFF && payload[3] == 0xFF;
    return !zeros && !ones;
  }

  /**
   * @brief Whether a mic datagram's source may feed the Nova Mic.
   *
   * Only the client of an active stream may speak, and plaintext audio is refused from WAN
   * (public) addresses, where it could be read or forged in transit. LAN and Tailscale
   * (100.64.0.0/10, fd7a::/48) count as private.
   *
   * @param from_session_peer The source address belongs to a client with an active stream.
   * @param is_wan The source address is public.
   * @param encrypted The payload is encrypted (SS_ENC_MICROPHONE negotiated).
   * @return true when the datagram may be decoded.
   */
  inline bool accept_source(bool from_session_peer, bool is_wan, bool encrypted) {
    return from_session_peer && (encrypted || !is_wan);
  }

}  // namespace mic_packet
