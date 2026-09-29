/**
 * @file src/abr.h
 * @brief Declarations for Nova's rule-based adaptive bitrate (ABR) controller.
 *
 * Wire-compatible with Sunshine-Foundation's `/api/abr` endpoints (used by moonlight-vplus and
 * Nebula): the client enables ABR for its running stream, then posts network feedback about once a
 * second and the host answers with a new bitrate when one is due. There is no LLM tier; decisions
 * come from loss and latency thresholds with hysteresis, fused with the host's own view of the
 * stream (client loss reports on the control channel).
 */
#pragma once

// standard includes
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// lib includes
#include <nlohmann/json.hpp>

/**
 * @brief Adaptive bitrate: rules, the per-stream controller and the per-device registry.
 */
namespace abr {
  using clock = std::chrono::steady_clock;  ///< Clock used for every decision.

  constexpr int VERSION = 1;  ///< `version` reported by `GET /api/abr/capabilities`.
  constexpr int FLOOR_KBPS = 500;  ///< No mode goes below this bitrate.
  constexpr int CEILING_KBPS = 800000;  ///< No mode goes above this bitrate.
  constexpr std::chrono::milliseconds COSTLY_INTERVAL {8000};  ///< Least time between regular steps when each change costs a keyframe.
  constexpr double COSTLY_MIN_STEP = 0.05;  ///< Smallest step (share of the bitrate) when each change costs a keyframe.
  constexpr double COSTLY_RAISE = 1.10;  ///< Smallest probe up when each change costs a keyframe.

  /**
   * @brief How the controller trades picture quality against latency.
   *
   * The wire names are Foundation's; Nebula's "aggressive" and "conservative" are accepted as aliases.
   */
  enum class mode_e {
    quality,  ///< "quality" / "aggressive": raise quickly, tolerate more latency.
    balanced,  ///< "balanced": the default.
    low_latency,  ///< "lowLatency" / "conservative": drop early, raise slowly.
  };

  /**
   * @brief Parse a mode name.
   *
   * @param name "quality", "balanced", "lowLatency", or the aliases "aggressive", "conservative", "low_latency".
   * @return The mode, or nullopt for anything else.
   */
  std::optional<mode_e> parse_mode(std::string_view name);

  /**
   * @brief Foundation wire name of a mode.
   *
   * @param mode Mode.
   * @return "quality", "balanced" or "lowLatency".
   */
  std::string_view mode_name(mode_e mode);

  /**
   * @brief Thresholds and steps of one mode.
   *
   * Loss is split in three bands so the controller has hysteresis: above `drop_loss_pct` it steps
   * down, below `clear_loss_pct` it counts a stable tick, and in between it holds (no step up, and
   * the stable count restarts).
   */
  struct rules_t {
    double emergency_loss_pct;  ///< Loss above this steps down at once (no need to be sustained).
    double drop_loss_pct;  ///< Loss above this for `sustain_ticks` reports steps down.
    double clear_loss_pct;  ///< Loss below this counts as a stable report.
    double emergency_factor;  ///< Bitrate multiplier for an emergency step down.
    double drop_factor;  ///< Bitrate multiplier for a sustained-loss or latency step down.
    double raise_factor;  ///< Bitrate multiplier for a step up.
    double careful_raise_factor;  ///< Step up used just below the bitrate that last caused loss.
    int sustain_ticks;  ///< Reports in a row above `drop_loss_pct` (or with raised RTT) before stepping down.
    int stable_ticks;  ///< Stable reports in a row before stepping up.
    double rtt_rise_ms;  ///< RTT this far above the stream's baseline counts as congestion.
    std::chrono::milliseconds emergency_spacing;  ///< Least time between two emergency steps.
    std::chrono::milliseconds decision_interval;  ///< Least time between two regular steps.
    std::chrono::milliseconds hold_after_drop;  ///< No step up for this long after a step down.
    std::chrono::milliseconds ceiling_memory;  ///< How long the bitrate that caused loss is remembered.
  };

  /**
   * @brief The rules of a mode.
   *
   * @param mode Mode.
   * @return Its thresholds and steps.
   */
  const rules_t &rules_for(mode_e mode);

  /**
   * @brief The bitrate range a controller may move in.
   */
  struct range_t {
    int min_kbps = 0;  ///< Lowest bitrate.
    int max_kbps = 0;  ///< Highest bitrate.
  };

  /**
   * @brief Work out the range for a stream.
   *
   * The ceiling is the user's bitrate cap: the bitrate the stream runs at (the client's slider, a
   * per-game preset or a later manual change), lowered by the client's `maxBitrate` when it sent one
   * and by the host `max_bitrate`. ABR only works below it. The floor is the client's `minBitrate`
   * when above 0, else the mode preset (Foundation's numbers): quality max(5000, ½·initial),
   * balanced max(3000, 0.3·initial), low latency 2000. The host floor is applied last, and the range
   * is never inverted.
   *
   * @param mode Mode.
   * @param initial_kbps Bitrate the stream runs at (the user's cap).
   * @param requested_min_kbps Client's `minBitrate` (0 = preset).
   * @param requested_max_kbps Client's `maxBitrate` (0 = none; it can only lower the cap).
   * @param host_floor_kbps Host `abr_min_bitrate` (0 = none).
   * @param host_cap_kbps Host `max_bitrate` (0 = none).
   * @return The range.
   */
  range_t resolve_range(mode_e mode, int initial_kbps, int requested_min_kbps, int requested_max_kbps, int host_floor_kbps, int host_cap_kbps);

  /**
   * @brief One network report, from the client's `POST /api/abr/feedback` plus the host's own view.
   */
  struct feedback_t {
    double packet_loss_pct = 0;  ///< Client `packetLoss`, percent.
    double rtt_ms = 0;  ///< Client `rttMs` (0 = unknown).
    double decode_fps = 0;  ///< Client `decodeFps` (informational).
    int dropped_frames = 0;  ///< Client `droppedFrames` (informational).
    int current_bitrate_kbps = 0;  ///< Client `currentBitrate` (0 = unknown; informational, the host's own value is authoritative).
    std::optional<double> host_loss_pct;  ///< Loss the client reported on the control channel over the last 5 s (ignored for 5 s after a drop, while the window still holds the old loss).
    std::optional<double> host_rtt_ms;  ///< Control-channel RTT measured by the host.
  };

  /**
   * @brief Replace non-finite and out-of-range values with safe ones.
   *
   * @param feedback Raw report.
   * @return Loss within [0, 100], other values finite and non-negative.
   */
  feedback_t sanitize(feedback_t feedback);

  /**
   * @brief Read a feedback body (Foundation field names).
   *
   * @param body Parsed JSON object.
   * @return The report; missing or mistyped fields read as 0.
   */
  feedback_t parse_feedback(const nlohmann::json &body);

  /**
   * @brief What the controller decided for one report.
   */
  struct decision_t {
    int new_bitrate_kbps = 0;  ///< New bitrate, or 0 for no change.
    std::string reason;  ///< Short reason, e.g. "loss 6.2% (emergency)"; empty when holding silently.
  };

  /**
   * @brief Rule-based controller for one stream.
   *
   * Not thread-safe; the registry serializes access.
   */
  class controller_t {
  public:
    /**
     * @brief Start controlling a stream.
     *
     * @param mode Mode.
     * @param range Allowed range.
     * @param initial_kbps Bitrate the stream runs at (clamped into the range).
     * @param now Current time.
     * @param costly_changes The encoder can't retarget in place (FFmpeg NVENC starts a new keyframe
     *        on every change): changes are spaced at least COSTLY_INTERVAL apart and at least
     *        COSTLY_MIN_STEP of the bitrate, and probes step up by at least COSTLY_RAISE. Emergency
     *        drops are not held back: loss costs more than a keyframe.
     */
    controller_t(mode_e mode, range_t range, int initial_kbps, clock::time_point now, bool costly_changes = false);

    /**
     * @brief Feed one report and maybe step.
     *
     * @param feedback Report (sanitized inside).
     * @param now When it arrived.
     * @return The decision; `new_bitrate_kbps` is 0 when the bitrate stays.
     */
    decision_t update(const feedback_t &feedback, clock::time_point now);

    /**
     * @brief Tell the controller the bitrate changed outside it (the user moved the slider).
     *
     * The same value it just chose is ignored, so a client that echoes the decision through
     * `/bitrate` does not reset the stable count. Any other value is the user's new cap: the
     * ceiling moves to it (up or down) and the controller continues from there.
     *
     * @param kbps New bitrate (already within the host `max_bitrate`).
     * @param now Current time.
     */
    void note_bitrate(int kbps, clock::time_point now);

    /**
     * @brief Current bitrate.
     * @return Kilobits per second.
     */
    int current_kbps() const {
      return current_kbps_;
    }

    /**
     * @brief Mode in use.
     * @return Mode.
     */
    mode_e mode() const {
      return mode_;
    }

    /**
     * @brief Range in use.
     * @return Range.
     */
    range_t range() const {
      return range_;
    }

    /**
     * @brief Reason of the last step, empty before the first.
     * @return Reason.
     */
    const std::string &last_reason() const {
      return last_reason_;
    }

    /**
     * @brief Steps taken so far.
     * @return Count.
     */
    int changes() const {
      return changes_;
    }

    /**
     * @brief The RTT baseline (lowest recent RTT), if any RTT was reported.
     * @return Milliseconds.
     */
    std::optional<double> rtt_baseline_ms() const {
      return rtt_baseline_ms_;
    }

  private:
    /**
     * @brief Move to a new bitrate if it differs enough from the current one.
     *
     * @param target Wanted bitrate (clamped into the range).
     * @param reason Reason to report.
     * @param now Current time.
     * @return The decision.
     */
    decision_t step_to(double target, std::string reason, clock::time_point now);

    mode_e mode_;  ///< Mode.
    range_t range_;  ///< Allowed range.
    int current_kbps_;  ///< Current bitrate.
    int loss_ticks_ = 0;  ///< Reports in a row above the drop threshold.
    int rtt_ticks_ = 0;  ///< Reports in a row with raised RTT.
    int stable_ticks_ = 0;  ///< Stable reports in a row.
    std::optional<double> rtt_baseline_ms_;  ///< Lowest recent RTT.
    std::optional<clock::time_point> last_step_;  ///< When the last step happened.
    std::optional<clock::time_point> last_drop_;  ///< When the last step down happened.
    int ceiling_kbps_ = 0;  ///< Bitrate that last caused a step down (0 = none remembered).
    std::string last_reason_;  ///< Reason of the last step.
    int changes_ = 0;  ///< Steps taken.
    bool costly_ = false;  ///< Each change costs a keyframe (no in-place retarget).
  };

  /**
   * @brief ABR state of one device, for `/api/sessions` and logs.
   */
  struct status_t {
    mode_e mode = mode_e::balanced;  ///< Mode.
    range_t range;  ///< Range.
    int current_kbps = 0;  ///< Current bitrate.
    std::string last_reason;  ///< Reason of the last step.
    int changes = 0;  ///< Steps taken.
  };

  /**
   * @brief Turn ABR on for a device's stream (replaces any earlier state).
   *
   * @param cert Device certificate.
   * @param mode Mode.
   * @param range Range.
   * @param initial_kbps Bitrate the stream runs at.
   * @param costly_changes The stream's encoder starts a keyframe on every bitrate change.
   */
  void enable(const std::string &cert, mode_e mode, range_t range, int initial_kbps, bool costly_changes = false);

  /**
   * @brief Turn ABR off for a device.
   *
   * @param cert Device certificate.
   */
  void disable(const std::string &cert);

  /**
   * @brief Whether ABR is on for a device.
   *
   * @param cert Device certificate.
   * @return True when enabled.
   */
  bool is_enabled(const std::string &cert);

  /**
   * @brief Feed a report for a device.
   *
   * @param cert Device certificate.
   * @param feedback Report.
   * @param now When it arrived.
   * @return The decision, or nullopt when ABR is off for the device.
   */
  std::optional<decision_t> feedback(const std::string &cert, const feedback_t &feedback, clock::time_point now = clock::now());

  /**
   * @brief Tell a device's controller that its bitrate changed through `/bitrate`.
   *
   * @param cert Device certificate.
   * @param kbps New bitrate.
   */
  void note_bitrate(const std::string &cert, int kbps);

  /**
   * @brief ABR state of a device.
   *
   * @param cert Device certificate.
   * @return State, or nullopt when ABR is off.
   */
  std::optional<status_t> status(const std::string &cert);

  /**
   * @brief Status as JSON for `/api/sessions` (`{"mode","min_kbps","max_kbps","current_kbps","last_reason","changes"}`).
   *
   * @param status State.
   * @return JSON object.
   */
  nlohmann::json status_json(const status_t &status);

  /**
   * @brief Body of `GET /api/abr/capabilities`.
   *
   * @param enabled `abr_enabled` in the host config.
   * @param host_max_kbps Host `max_bitrate` (0 = none).
   * @return Foundation-shaped reply; `supported` is false when ABR is disabled on the host.
   */
  nlohmann::json capabilities_json(bool enabled, int host_max_kbps);

  /**
   * @brief A parsed `POST /api/abr` body.
   */
  struct config_request_t {
    bool enabled = false;  ///< `enabled`.
    mode_e mode = mode_e::balanced;  ///< `mode`.
    std::string mode_text = "balanced";  ///< `mode` as sent (echoed back).
    int min_kbps = 0;  ///< `minBitrate` (0 = preset).
    int max_kbps = 0;  ///< `maxBitrate` (0 = preset).
  };

  /**
   * @brief Parse and validate a `POST /api/abr` body.
   *
   * @param body Raw request body.
   * @param error Set to a Foundation-style message when the body is rejected.
   * @return The request, or nullopt with @p error set.
   */
  std::optional<config_request_t> parse_config_request(std::string_view body, std::string &error);

  /**
   * @brief Video encoder bitrate for a client-facing bitrate.
   *
   * Leaves room for FEC parity, audio and packet overhead exactly like the RTSP setup does for
   * the bitrate a stream starts at, so a live change to the same value keeps the same encoder rate.
   *
   * @param client_kbps Bitrate the client asked for.
   * @param fec_percentage Host `fec_percentage`.
   * @param audio_kbps Audio bitrate (channels × 96 or 256 kbps).
   * @return Encoder bitrate in kbps (at least 1).
   */
  int encoder_kbps(int client_kbps, int fec_percentage, int audio_kbps);
}  // namespace abr
