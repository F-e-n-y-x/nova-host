/**
 * @file src/stream_stats.h
 * @brief Declarations for live stream telemetry and session history.
 */
#pragma once

// standard includes
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// lib includes
#include <nlohmann/json.hpp>

/**
 * @brief Live per-session stream statistics, 1 Hz sparkline samples and ended-session history.
 */
namespace stream_stats {
  using clock = std::chrono::steady_clock;  ///< Clock used for all frame timing.

  constexpr std::size_t FRAME_RING_SIZE = 1024;  ///< Per-frame samples kept (covers ~4 s at 240 fps).
  constexpr std::size_t SECOND_RING_SIZE = 120;  ///< 1 Hz samples kept for sparklines (2 minutes).
  constexpr std::size_t HISTORY_LIMIT = 50;  ///< Ended sessions kept in the history file.
  constexpr auto LATENCY_WINDOW = std::chrono::seconds {2};  ///< Window for latency averages and p95.
  constexpr auto RATE_WINDOW = std::chrono::seconds {1};  ///< Window for fps and bitrate.
  constexpr auto LOSS_WINDOW = std::chrono::seconds {5};  ///< Window for packet-loss percentage.

  /**
   * @brief Timing and size of one frame, measured from capture until its last packet was sent.
   */
  struct frame_sample_t {
    clock::time_point sent;  ///< When the frame's last packet was handed to the socket.
    std::uint32_t capture_us = 0;  ///< Capture timestamp until the encoder dequeued the frame.
    std::uint32_t encode_us = 0;  ///< Encoder dequeue until the encoded packet was produced (colour conversion + encode).
    std::uint32_t send_us = 0;  ///< Encoded packet until the last network packet was sent (packetize, FEC, encrypt, pace).
    std::uint32_t bytes = 0;  ///< Encoded frame size in bytes (without FEC or headers).
    std::uint16_t packets = 0;  ///< Network packets sent for this frame, FEC included.
    std::uint16_t fec_packets = 0;  ///< Parity packets among `packets`.
    bool timed = false;  ///< Whether the stage timings are valid (false for duplicated frames).
    bool duplicate = false;  ///< Whether the frame repeated the previous image (no new capture).
  };

  /**
   * @brief One 1 Hz sample used for sparklines.
   */
  struct second_sample_t {
    std::int64_t t = 0;  ///< Unix time in seconds.
    double bitrate_kbps = 0;  ///< Encoded bitrate over the second.
    double fps = 0;  ///< Frames sent in the second.
    double latency_ms = 0;  ///< Average host latency of frames in the second (0 when none were timed).
    std::optional<double> loss_pct;  ///< Client-reported packet loss, when the client reported any.
  };

  /**
   * @brief Average and 95th-percentile of a latency stage in milliseconds.
   */
  struct latency_stat_t {
    double avg = 0;  ///< Mean over the window.
    double p95 = 0;  ///< 95th percentile over the window.
  };

  /**
   * @brief Fixed facts about a session, known when it starts.
   */
  struct session_info_t {
    std::uint32_t id = 0;  ///< Launch-session ID.
    std::string client_cert;  ///< PEM certificate of the client (used to resolve its current name and UUID).
    std::string client_name;  ///< Name the client sent at launch (fallback when the certificate is unknown).
    std::string app_name;  ///< Application running when the stream started.
    std::int64_t started_at = 0;  ///< Unix time in seconds.
    int width = 0;  ///< Stream width in pixels.
    int height = 0;  ///< Stream height in pixels.
    int fps_requested = 0;  ///< Frame rate the client requested.
    int video_format = 0;  ///< 0 = H.264, 1 = HEVC, 2 = AV1.
    bool hdr = false;  ///< Whether the stream is HDR.
    std::string virtual_display;  ///< Nova virtual display the stream captures (":20"), empty for the desktop.
    int virtual_width = 0;  ///< Virtual display width when the stream started.
    int virtual_height = 0;  ///< Virtual display height when the stream started.
    int virtual_fps = 0;  ///< Frame rate the virtual display was created for.
  };

  /**
   * @brief Point-in-time view of a live session.
   */
  struct snapshot_t {
    session_info_t info;  ///< Facts fixed at start.
    double fps_actual = 0;  ///< Frames sent over the last second.
    double bitrate_kbps = 0;  ///< Encoded bitrate over the last second.
    latency_stat_t capture;  ///< Capture-to-encoder stage.
    latency_stat_t encode;  ///< Conversion + encode stage.
    latency_stat_t send;  ///< Packetize + FEC + send stage.
    latency_stat_t total;  ///< Capture timestamp to last packet sent.
    std::optional<double> loss_pct;  ///< Client-reported packet loss over the last 5 s; empty until reported.
    double fec_pct = 0;  ///< Share of sent packets that were FEC parity over the last second.
    std::optional<std::uint32_t> rtt_ms;  ///< Control-channel round-trip time; empty before the peer connects.
    std::uint64_t frames_total = 0;  ///< Frames sent since start.
    std::uint64_t frames_duplicated = 0;  ///< Frames that repeated the previous image.
    std::uint64_t packets_total = 0;  ///< Network packets sent since start.
    std::uint64_t bytes_total = 0;  ///< Encoded bytes sent since start.
    std::vector<second_sample_t> samples;  ///< 1 Hz samples, oldest first (filled only when requested).
  };

  /**
   * @brief Summary of an ended session kept in the history file.
   */
  struct history_entry_t {
    std::uint32_t id = 0;  ///< Launch-session ID.
    std::string client_cert;  ///< Client certificate (resolved to name/UUID when served, never exposed).
    std::string client_name;  ///< Client name at the time the session ended.
    std::string app_name;  ///< Application streamed.
    std::int64_t started_at = 0;  ///< Unix time in seconds.
    std::int64_t duration_s = 0;  ///< Session length in seconds.
    int video_format = 0;  ///< 0 = H.264, 1 = HEVC, 2 = AV1.
    int width = 0;  ///< Stream width.
    int height = 0;  ///< Stream height.
    bool hdr = false;  ///< Whether the stream was HDR.
    double avg_fps = 0;  ///< Mean frames per second over the session.
    double avg_bitrate_kbps = 0;  ///< Mean encoded bitrate over the session.
    std::optional<double> avg_latency_ms;  ///< Mean host latency over timed frames; empty when none were timed.
    std::string end_reason;  ///< "client", "host", "timeout" or "ended".
  };

  /**
   * @brief Thread-safe statistics for one streaming session.
   *
   * Writers (the video broadcast thread) take a short uncontended lock and write into fixed rings, so
   * recording a frame never allocates.
   */
  class session_stats_t {
  public:
    /**
     * @brief Create statistics for a session.
     *
     * @param info Facts fixed at session start.
     * @param started Monotonic start time.
     */
    session_stats_t(session_info_t info, clock::time_point started);

    /**
     * @brief Record one sent frame.
     *
     * @param frame Timing and size of the frame.
     */
    void record_frame(const frame_sample_t &frame);

    /**
     * @brief Record a client loss report.
     *
     * @param lost_packets Packets the client lost since its previous report.
     * @param when When the report arrived.
     */
    void record_loss(std::uint32_t lost_packets, clock::time_point when);

    /**
     * @brief Store the latest control-channel round-trip time.
     *
     * @param rtt_ms Round-trip time in milliseconds.
     */
    void set_rtt(std::uint32_t rtt_ms);

    /**
     * @brief Record why the session ended; the first reason given wins.
     *
     * @param reason One of "client", "host", "timeout".
     */
    void set_end_reason(std::string_view reason);

    /**
     * @brief Build a snapshot of the session.
     *
     * @param now Current monotonic time.
     * @param with_samples Whether to include the 1 Hz sample history.
     * @return Current statistics.
     */
    snapshot_t snapshot(clock::time_point now, bool with_samples) const;

    /**
     * @brief Summarize the session for the history file.
     *
     * @param now Monotonic time the session ended.
     * @param unix_now Unix time the session ended, in seconds.
     * @return History entry.
     */
    history_entry_t finish(clock::time_point now, std::int64_t unix_now) const;

    /**
     * @brief Facts fixed at session start.
     *
     * @return Session info.
     */
    const session_info_t &info() const {
      return info_;
    }

  private:
    /**
     * @brief Close any 1 Hz buckets that ended before `now` (caller holds the lock).
     *
     * @param now Current monotonic time.
     */
    void roll_seconds_locked(clock::time_point now);

    /**
     * @brief Packet-loss percentage over the loss window (caller holds the lock).
     *
     * @param now Current monotonic time.
     * @return Percentage, or empty when the client never reported loss.
     */
    std::optional<double> loss_pct_locked(clock::time_point now) const;

    session_info_t info_;  ///< Facts fixed at start.
    clock::time_point started_;  ///< Monotonic start time.

    mutable std::mutex mutex_;  ///< Guards everything below.
    std::array<frame_sample_t, FRAME_RING_SIZE> frames_ {};  ///< Recent frames, ring buffer.
    std::size_t frame_head_ = 0;  ///< Next write position in `frames_`.
    std::size_t frame_count_ = 0;  ///< Valid entries in `frames_`.

    std::array<second_sample_t, SECOND_RING_SIZE> seconds_ {};  ///< 1 Hz samples, ring buffer.
    std::size_t second_head_ = 0;  ///< Next write position in `seconds_`.
    std::size_t second_count_ = 0;  ///< Valid entries in `seconds_`.
    clock::time_point bucket_start_;  ///< Start of the 1 Hz bucket being filled.
    std::uint64_t bucket_bytes_ = 0;  ///< Encoded bytes in the current bucket.
    std::uint32_t bucket_frames_ = 0;  ///< Frames in the current bucket.
    std::uint64_t bucket_latency_us_ = 0;  ///< Summed total latency of timed frames in the bucket.
    std::uint32_t bucket_timed_ = 0;  ///< Timed frames in the bucket.
    std::uint64_t bucket_packets_ = 0;  ///< Packets sent in the bucket.
    std::uint64_t bucket_lost_ = 0;  ///< Packets reported lost during the bucket.
    bool bucket_loss_reported_ = false;  ///< Whether a loss report arrived during the bucket.

    std::array<std::pair<clock::time_point, std::uint32_t>, 64> losses_ {};  ///< Recent loss reports, ring buffer.
    std::size_t loss_head_ = 0;  ///< Next write position in `losses_`.
    std::size_t loss_count_ = 0;  ///< Valid entries in `losses_`.
    bool loss_ever_reported_ = false;  ///< Whether the client ever sent a loss report.

    std::uint64_t frames_total_ = 0;  ///< Frames since start.
    std::uint64_t frames_duplicated_ = 0;  ///< Duplicated frames since start.
    std::uint64_t packets_total_ = 0;  ///< Packets since start.
    std::uint64_t bytes_total_ = 0;  ///< Encoded bytes since start.
    std::uint64_t latency_sum_us_ = 0;  ///< Summed total latency of timed frames since start.
    std::uint64_t timed_total_ = 0;  ///< Timed frames since start.

    std::optional<std::uint32_t> rtt_ms_;  ///< Latest control-channel RTT.
    std::string end_reason_;  ///< First end reason recorded.
  };

  /**
   * @brief Build a frame sample from the pipeline's timestamps.
   *
   * Stage timings are only valid when all three earlier timestamps exist and are ordered; otherwise the
   * sample is recorded untimed. A frame without a capture timestamp is a duplicate of the previous image.
   *
   * @param captured Capture timestamp, empty for duplicated frames.
   * @param encode_start When the encoder dequeued the image.
   * @param encode_done When the encoded packet was produced.
   * @param sent When the frame's last network packet was sent.
   * @param bytes Encoded frame size.
   * @param packets Network packets sent, FEC included.
   * @param fec_packets Parity packets among `packets`.
   * @return Frame sample.
   */
  frame_sample_t make_frame_sample(
    std::optional<clock::time_point> captured,
    std::optional<clock::time_point> encode_start,
    std::optional<clock::time_point> encode_done,
    clock::time_point sent,
    std::size_t bytes,
    std::size_t packets,
    std::size_t fec_packets
  );

  /**
   * @brief Average and 95th percentile of a set of microsecond values, in milliseconds.
   *
   * @param values_us Values in microseconds; reordered by the call.
   * @return Average and p95 (zeros for an empty set).
   */
  latency_stat_t summarize_us(std::vector<std::uint32_t> &values_us);

  /**
   * @brief Human-readable codec name for a GameStream video format number.
   *
   * @param video_format 0 = H.264, 1 = HEVC, 2 = AV1.
   * @return Codec name.
   */
  std::string codec_name(int video_format);

  /**
   * @brief Register a session that just started streaming.
   *
   * @param info Facts fixed at start.
   * @return Statistics object the stream threads write into.
   */
  std::shared_ptr<session_stats_t> start_session(session_info_t info);

  /**
   * @brief Unregister a session and append it to the history file.
   *
   * @param stats Statistics returned by start_session().
   */
  void end_session(const std::shared_ptr<session_stats_t> &stats);

  /**
   * @brief Snapshots of every live session.
   *
   * @param with_samples Whether to include 1 Hz samples.
   * @return One snapshot per live session.
   */
  std::vector<snapshot_t> active_sessions(bool with_samples);

  /**
   * @brief Ended sessions, newest first.
   *
   * @return History entries.
   */
  std::vector<history_entry_t> history();

  /**
   * @brief Location of the history file next to the state file.
   *
   * @return Path of `session_history.json`.
   */
  std::filesystem::path history_path();

  /**
   * @brief Read history entries from a file; a missing or unreadable file yields none.
   *
   * @param path File to read.
   * @return Entries, newest first, at most HISTORY_LIMIT.
   */
  std::vector<history_entry_t> load_history(const std::filesystem::path &path);

  /**
   * @brief Write history entries to a file atomically (temp file + rename).
   *
   * @param path Destination file.
   * @param entries Entries to write, newest first.
   * @return True on success.
   */
  bool save_history(const std::filesystem::path &path, const std::vector<history_entry_t> &entries);

  /**
   * @brief Serialize a history entry for the history file (includes the certificate).
   *
   * @param entry Entry to serialize.
   * @return JSON object.
   */
  nlohmann::json history_to_file_json(const history_entry_t &entry);

  /**
   * @brief Parse a history entry from the history file.
   *
   * @param j JSON object.
   * @return Entry, or empty when required fields are missing or invalid.
   */
  std::optional<history_entry_t> history_from_file_json(const nlohmann::json &j);

  /**
   * @brief Client identity resolved from a certificate at request time.
   */
  struct client_identity_t {
    std::string uuid;  ///< Paired client UUID, empty when the certificate is no longer paired.
    std::string name;  ///< Current client name.
  };

  /**
   * @brief Serialize a live snapshot for `GET /api/sessions`.
   *
   * @param snap Snapshot.
   * @param who Resolved client identity.
   * @param with_samples Whether to include the samples array.
   * @return JSON object.
   */
  nlohmann::json snapshot_to_api_json(const snapshot_t &snap, const client_identity_t &who, bool with_samples);

  /**
   * @brief Serialize a history entry for `GET /api/sessions/history` (no certificate).
   *
   * @param entry Entry.
   * @param who Resolved client identity.
   * @return JSON object.
   */
  nlohmann::json history_to_api_json(const history_entry_t &entry, const client_identity_t &who);

#ifdef SUNSHINE_TESTS
  /**
   * @brief Test hooks.
   */
  namespace test_support {
    /**
     * @brief Forget every live session and cached history.
     */
    void reset();
  }  // namespace test_support
#endif
}  // namespace stream_stats
