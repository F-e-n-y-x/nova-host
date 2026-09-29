/**
 * @file src/abr.cpp
 * @brief Definitions for Nova's rule-based adaptive bitrate (ABR) controller.
 */
// class header include
#include "abr.h"

// standard includes
#include <algorithm>
#include <cmath>
#include <format>
#include <mutex>
#include <unordered_map>

using namespace std::literals;

namespace abr {
  namespace {
    /**
     * @brief Rules per mode, indexed by `mode_e`.
     *
     * Balanced keeps Foundation's thresholds (5 % emergency ×0.70, 2 % ×0.90, raise ×1.05 after
     * 5 clean reports, a decision every 3 s at most) and adds the hold band, RTT and the
     * remembered ceiling. Quality raises sooner and harder; low latency drops at lower loss and
     * RTT and climbs slowly.
     */
    const rules_t RULES[] = {
      // quality
      {
        .emergency_loss_pct = 5.0,
        .drop_loss_pct = 2.0,
        .clear_loss_pct = 0.5,
        .emergency_factor = 0.70,
        .drop_factor = 0.90,
        .raise_factor = 1.08,
        .careful_raise_factor = 1.03,
        .sustain_ticks = 2,
        .stable_ticks = 3,
        .rtt_rise_ms = 80.0,
        .emergency_spacing = 1500ms,
        .decision_interval = 2000ms,
        .hold_after_drop = 6000ms,
        .ceiling_memory = 45000ms,
      },
      // balanced
      {
        .emergency_loss_pct = 5.0,
        .drop_loss_pct = 2.0,
        .clear_loss_pct = 0.5,
        .emergency_factor = 0.70,
        .drop_factor = 0.90,
        .raise_factor = 1.05,
        .careful_raise_factor = 1.02,
        .sustain_ticks = 2,
        .stable_ticks = 5,
        .rtt_rise_ms = 50.0,
        .emergency_spacing = 1500ms,
        .decision_interval = 3000ms,
        .hold_after_drop = 10000ms,
        .ceiling_memory = 60000ms,
      },
      // low latency
      {
        .emergency_loss_pct = 4.0,
        .drop_loss_pct = 1.5,
        .clear_loss_pct = 0.3,
        .emergency_factor = 0.65,
        .drop_factor = 0.85,
        .raise_factor = 1.03,
        .careful_raise_factor = 1.01,
        .sustain_ticks = 2,
        .stable_ticks = 8,
        .rtt_rise_ms = 30.0,
        .emergency_spacing = 1500ms,
        .decision_interval = 3000ms,
        .hold_after_drop = 20000ms,
        .ceiling_memory = 120000ms,
      },
    };

    /// Steps smaller than this share of the current bitrate are skipped (no encoder churn).
    constexpr double MIN_STEP_SHARE = 0.01;
    /// A raise that would land within this share of the remembered ceiling uses the careful step.
    constexpr double CEILING_MARGIN = 0.95;
    /// Window of the host's own loss figure (`stream_stats` loss over the last 5 s).
    constexpr auto HOST_LOSS_WINDOW = std::chrono::seconds {5};
    /// How fast the RTT baseline follows a higher RTT (per report), so a route change is learned.
    constexpr double RTT_BASELINE_DRIFT = 0.02;

    /**
     * @brief Finite, non-negative value or 0.
     * @param value Raw value.
     * @return Cleaned value.
     */
    double clean(double value) {
      return std::isfinite(value) && value > 0 ? value : 0.0;
    }

    /**
     * @brief Clamp a double bitrate into a range as an int.
     * @param kbps Bitrate.
     * @param range Range.
     * @return Clamped bitrate.
     */
    int clamp_kbps(double kbps, const range_t &range) {
      if (!std::isfinite(kbps)) {
        return range.min_kbps;
      }
      return static_cast<int>(std::clamp(kbps, static_cast<double>(range.min_kbps), static_cast<double>(range.max_kbps)));
    }

    std::mutex registry_mutex;  ///< Guards `registry`.
    std::unordered_map<std::string, controller_t> registry;  ///< Controllers by device certificate.

    /**
     * @brief Integer field of a JSON body, 0 when missing or not a number.
     * @param body JSON object.
     * @param key Field name.
     * @return Value.
     */
    double number_field(const nlohmann::json &body, const char *key) {
      const auto it = body.find(key);
      if (it == body.end() || !it->is_number()) {
        return 0.0;
      }
      return it->get<double>();
    }
  }  // namespace

  std::optional<mode_e> parse_mode(std::string_view name) {
    if (name == "quality"sv || name == "aggressive"sv) {
      return mode_e::quality;
    }
    if (name == "balanced"sv) {
      return mode_e::balanced;
    }
    if (name == "lowLatency"sv || name == "low_latency"sv || name == "conservative"sv) {
      return mode_e::low_latency;
    }
    return std::nullopt;
  }

  std::string_view mode_name(mode_e mode) {
    switch (mode) {
      case mode_e::quality:
        return "quality"sv;
      case mode_e::low_latency:
        return "lowLatency"sv;
      case mode_e::balanced:
      default:
        return "balanced"sv;
    }
  }

  const rules_t &rules_for(mode_e mode) {
    return RULES[static_cast<int>(mode)];
  }

  range_t resolve_range(mode_e mode, int initial_kbps, int requested_min_kbps, int requested_max_kbps, int host_floor_kbps, int host_cap_kbps) {
    const long long initial = std::max(initial_kbps, FLOOR_KBPS);
    long long preset_min = 0;
    switch (mode) {
      case mode_e::quality:
        preset_min = std::max(5000LL, initial / 2);
        break;
      case mode_e::low_latency:
        preset_min = 2000;
        break;
      case mode_e::balanced:
      default:
        preset_min = std::max(3000LL, initial * 3 / 10);
        break;
    }
    long long lo = requested_min_kbps > 0 ? requested_min_kbps : preset_min;
    // The user's bitrate is the cap; the client's maximum can only lower it.
    long long hi = initial;
    if (requested_max_kbps > 0) {
      hi = std::min<long long>(hi, requested_max_kbps);
    }
    hi = std::min<long long>(hi, CEILING_KBPS);
    if (host_cap_kbps > 0) {
      hi = std::min<long long>(hi, host_cap_kbps);
    }
    lo = std::max<long long>(lo, std::max(host_floor_kbps, FLOOR_KBPS));
    hi = std::max<long long>(hi, FLOOR_KBPS);
    if (lo > hi) {
      lo = hi;
    }
    return {static_cast<int>(lo), static_cast<int>(hi)};
  }

  feedback_t sanitize(feedback_t feedback) {
    feedback.packet_loss_pct = std::min(clean(feedback.packet_loss_pct), 100.0);
    feedback.rtt_ms = clean(feedback.rtt_ms);
    feedback.decode_fps = clean(feedback.decode_fps);
    feedback.dropped_frames = std::max(feedback.dropped_frames, 0);
    feedback.current_bitrate_kbps = std::max(feedback.current_bitrate_kbps, 0);
    if (feedback.host_loss_pct) {
      feedback.host_loss_pct = std::min(clean(*feedback.host_loss_pct), 100.0);
    }
    if (feedback.host_rtt_ms) {
      feedback.host_rtt_ms = clean(*feedback.host_rtt_ms);
    }
    return feedback;
  }

  feedback_t parse_feedback(const nlohmann::json &body) {
    feedback_t out;
    if (!body.is_object()) {
      return out;
    }
    out.packet_loss_pct = number_field(body, "packetLoss");
    out.rtt_ms = number_field(body, "rttMs");
    out.decode_fps = number_field(body, "decodeFps");
    const auto clamp_int = [](double v) {
      return std::isfinite(v) ? static_cast<int>(std::clamp(v, 0.0, 2e9)) : 0;
    };
    out.dropped_frames = clamp_int(number_field(body, "droppedFrames"));
    out.current_bitrate_kbps = clamp_int(number_field(body, "currentBitrate"));
    return sanitize(out);
  }

  controller_t::controller_t(mode_e mode, range_t range, int initial_kbps, clock::time_point now, bool costly_changes):
      mode_ {mode},
      range_ {range},
      current_kbps_ {clamp_kbps(initial_kbps, range)},
      costly_ {costly_changes} {
    (void) now;
  }

  decision_t controller_t::step_to(double target, std::string reason, clock::time_point now) {
    const int next = clamp_kbps(target, range_);
    const double min_share = costly_ ? COSTLY_MIN_STEP : MIN_STEP_SHARE;
    // With costly changes the steps are big, so the last one to a bound may be small: take it
    // rather than stopping just short of the user's cap.
    const bool to_bound = costly_ && (next == range_.max_kbps || next == range_.min_kbps);
    if (next == current_kbps_ || (!to_bound && std::abs(next - current_kbps_) < std::max(1.0, current_kbps_ * min_share))) {
      // Clamped to where we already are: still a decision point, but nothing to apply.
      return {};
    }
    if (next < current_kbps_) {
      ceiling_kbps_ = current_kbps_;
      last_drop_ = now;
    }
    current_kbps_ = next;
    last_step_ = now;
    last_reason_ = reason;
    ++changes_;
    return {next, std::move(reason)};
  }

  decision_t controller_t::update(const feedback_t &raw, clock::time_point now) {
    const auto fb = sanitize(raw);
    auto rules = rules_for(mode_);
    if (costly_) {
      rules.decision_interval = std::max<std::chrono::milliseconds>(rules.decision_interval, COSTLY_INTERVAL);
      rules.raise_factor = std::max(rules.raise_factor, COSTLY_RAISE);
      // A little over the smallest step, so rounding never makes a careful probe too small to take.
      rules.careful_raise_factor = std::max(rules.careful_raise_factor, 1.01 + COSTLY_MIN_STEP);
    }

    const auto since = [&](const std::optional<clock::time_point> &t) {
      return t ? now - *t : clock::duration::max();
    };

    // The client's `currentBitrate` is informational: a manual change arrives through /bitrate
    // (note_bitrate), and a stale echo must not move the controller.
    //
    // The host's loss figure covers the last 5 s, so right after a drop it still shows the loss
    // that caused it; using it then would compound the drop.
    const double host_loss = since(last_drop_) >= HOST_LOSS_WINDOW ? fb.host_loss_pct.value_or(0.0) : 0.0;
    const double loss = std::max(fb.packet_loss_pct, host_loss);
    const double rtt = fb.rtt_ms > 0 ? fb.rtt_ms : fb.host_rtt_ms.value_or(0.0);

    // RTT baseline: the lowest RTT seen, drifting slowly upwards so a new route is learned.
    bool rtt_raised = false;
    if (rtt > 0) {
      if (!rtt_baseline_ms_ || rtt < *rtt_baseline_ms_) {
        rtt_baseline_ms_ = rtt;
      } else {
        rtt_raised = rtt - *rtt_baseline_ms_ > rules.rtt_rise_ms;
        // Only drift while the link looks healthy, so congestion is not learned as normal.
        if (!rtt_raised && loss < rules.clear_loss_pct) {
          *rtt_baseline_ms_ += (rtt - *rtt_baseline_ms_) * RTT_BASELINE_DRIFT;
        }
      }
    }

    if (ceiling_kbps_ > 0 && since(last_drop_) >= rules.ceiling_memory) {
      ceiling_kbps_ = 0;
    }

    // Emergency: heavy loss steps down at once, spaced so a lagging loss figure can't compound.
    if (loss > rules.emergency_loss_pct) {
      stable_ticks_ = 0;
      rtt_ticks_ = 0;
      if (since(last_drop_) >= rules.emergency_spacing) {
        loss_ticks_ = 0;
        return step_to(current_kbps_ * rules.emergency_factor, std::format("loss {:.1f}% (emergency)", loss), now);
      }
      return {};
    }

    if (loss > rules.drop_loss_pct) {
      stable_ticks_ = 0;
      rtt_ticks_ = 0;
      if (++loss_ticks_ >= rules.sustain_ticks && since(last_step_) >= rules.decision_interval) {
        loss_ticks_ = 0;
        return step_to(current_kbps_ * rules.drop_factor, std::format("loss {:.1f}% (sustained)", loss), now);
      }
      return {};
    }
    loss_ticks_ = 0;

    if (rtt_raised) {
      stable_ticks_ = 0;
      if (++rtt_ticks_ >= rules.sustain_ticks && since(last_step_) >= rules.decision_interval) {
        rtt_ticks_ = 0;
        return step_to(current_kbps_ * rules.drop_factor, std::format("rtt {:.0f} ms (+{:.0f})", rtt, rtt - rtt_baseline_ms_.value_or(rtt)), now);
      }
      return {};
    }
    rtt_ticks_ = 0;

    if (loss >= rules.clear_loss_pct) {
      // Hysteresis band: not bad enough to drop, not clean enough to climb.
      stable_ticks_ = 0;
      return {};
    }

    ++stable_ticks_;
    if (stable_ticks_ < rules.stable_ticks || current_kbps_ >= range_.max_kbps ||
        since(last_step_) < rules.decision_interval || since(last_drop_) < rules.hold_after_drop) {
      return {};
    }
    stable_ticks_ = 0;

    double target = current_kbps_ * rules.raise_factor;
    const char *kind = "probe";
    if (ceiling_kbps_ > 0 && target >= ceiling_kbps_ * CEILING_MARGIN) {
      target = current_kbps_ * rules.careful_raise_factor;
      kind = "careful probe";
    }
    return step_to(target, std::format("stable, {}", kind), now);
  }

  void controller_t::note_bitrate(int kbps, clock::time_point now) {
    if (kbps <= 0 || kbps == current_kbps_) {
      return;
    }
    // The user's new cap. The host already limited it to `max_bitrate`.
    range_.max_kbps = std::clamp(kbps, FLOOR_KBPS, CEILING_KBPS);
    range_.min_kbps = std::min(range_.min_kbps, range_.max_kbps);
    current_kbps_ = range_.max_kbps;
    // A higher cap is a fresh start; a remembered loss ceiling above the new cap is moot.
    if (ceiling_kbps_ > range_.max_kbps) {
      ceiling_kbps_ = 0;
    }
    stable_ticks_ = 0;
    loss_ticks_ = 0;
    rtt_ticks_ = 0;
    last_step_ = now;
  }

  void enable(const std::string &cert, mode_e mode, range_t range, int initial_kbps, bool costly_changes) {
    std::lock_guard lg {registry_mutex};
    registry.insert_or_assign(cert, controller_t {mode, range, initial_kbps, clock::now(), costly_changes});
  }

  void disable(const std::string &cert) {
    std::lock_guard lg {registry_mutex};
    registry.erase(cert);
  }

  bool is_enabled(const std::string &cert) {
    std::lock_guard lg {registry_mutex};
    return registry.contains(cert);
  }

  std::optional<decision_t> feedback(const std::string &cert, const feedback_t &report, clock::time_point now) {
    std::lock_guard lg {registry_mutex};
    const auto it = registry.find(cert);
    if (it == registry.end()) {
      return std::nullopt;
    }
    return it->second.update(report, now);
  }

  void note_bitrate(const std::string &cert, int kbps) {
    std::lock_guard lg {registry_mutex};
    if (const auto it = registry.find(cert); it != registry.end()) {
      it->second.note_bitrate(kbps, clock::now());
    }
  }

  std::optional<status_t> status(const std::string &cert) {
    std::lock_guard lg {registry_mutex};
    const auto it = registry.find(cert);
    if (it == registry.end()) {
      return std::nullopt;
    }
    const auto &c = it->second;
    return status_t {c.mode(), c.range(), c.current_kbps(), c.last_reason(), c.changes()};
  }

  nlohmann::json status_json(const status_t &status) {
    return {
      {"mode", std::string {mode_name(status.mode)}},
      {"min_kbps", status.range.min_kbps},
      {"max_kbps", status.range.max_kbps},
      {"current_kbps", status.current_kbps},
      {"last_reason", status.last_reason},
      {"changes", status.changes},
    };
  }

  nlohmann::json capabilities_json(bool enabled, int host_max_kbps) {
    return {
      {"supported", enabled},
      {"version", VERSION},
      {"features", enabled ? nlohmann::json {"fallback_threshold", "bitrate_cap", "hysteresis", "host_loss"} : nlohmann::json::array()},
      {"llmEnabled", false},
      {"hostMaxBitrate", std::max(host_max_kbps, 0)},
    };
  }

  std::optional<config_request_t> parse_config_request(std::string_view body, std::string &error) {
    const auto json = nlohmann::json::parse(body, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
      error = "Invalid JSON body";
      return std::nullopt;
    }
    config_request_t out;
    if (const auto it = json.find("enabled"); it != json.end() && it->is_boolean()) {
      out.enabled = it->get<bool>();
    }
    if (!out.enabled) {
      return out;
    }
    if (const auto it = json.find("mode"); it != json.end()) {
      if (!it->is_string()) {
        error = "Invalid mode: must be 'balanced', 'quality', or 'lowLatency'";
        return std::nullopt;
      }
      out.mode_text = it->get<std::string>();
    }
    const auto mode = parse_mode(out.mode_text);
    if (!mode) {
      error = "Invalid mode: must be 'balanced', 'quality', or 'lowLatency'";
      return std::nullopt;
    }
    out.mode = *mode;
    const double lo = number_field(json, "minBitrate");
    const double hi = number_field(json, "maxBitrate");
    if (!std::isfinite(lo) || !std::isfinite(hi) || lo < 0 || hi < 0) {
      error = "minBitrate and maxBitrate must be non-negative";
      return std::nullopt;
    }
    out.min_kbps = static_cast<int>(std::min(lo, static_cast<double>(CEILING_KBPS)));
    out.max_kbps = static_cast<int>(std::min(hi, static_cast<double>(CEILING_KBPS)));
    if (out.min_kbps > 0 && out.max_kbps > 0 && out.min_kbps > out.max_kbps) {
      error = "minBitrate must not exceed maxBitrate";
      return std::nullopt;
    }
    return out;
  }

  int encoder_kbps(int client_kbps, int fec_percentage, int audio_kbps) {
    std::int64_t kbps = std::max(client_kbps, 1);
    // Same arithmetic as the RTSP setup, so a live change to the start value keeps the encoder rate.
    if (fec_percentage <= 80) {
      kbps /= 100.f / (100 - fec_percentage);
    }
    kbps -= std::min(static_cast<std::int64_t>(std::max(audio_kbps, 0)), kbps / 5);
    kbps -= std::min(static_cast<std::int64_t>(500), kbps / 10);
    return static_cast<int>(std::max<std::int64_t>(kbps, 1));
  }
}  // namespace abr
