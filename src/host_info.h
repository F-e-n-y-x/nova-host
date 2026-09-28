/**
 * @file src/host_info.h
 * @brief Host facts for the web UI: hardware/encoder summary, displays, audio sinks,
 *        desktop preview and the setup-doctor health checks.
 */
#pragma once

// standard includes
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// lib includes
#include <nlohmann/json.hpp>

// local includes
#include "platform/common.h"

namespace host_info {

  /**
   * @brief Severity of a health check.
   */
  enum class health_status_e {
    ok,  ///< Working as expected.
    warn,  ///< Works, but something is degraded or needs attention.
    error,  ///< Broken; streaming or a feature won't work.
  };

  /**
   * @brief How the web UI can help fix a failed check.
   */
  enum class fix_kind_e {
    none,  ///< No fix to offer.
    command,  ///< A shell command the user can run.
    setting,  ///< A setting key to change in the web UI.
    doc,  ///< A documentation link.
  };

  /**
   * @brief One setup-doctor result.
   */
  struct health_check_t {
    std::string id;  ///< Stable identifier, e.g. `encoder`.
    health_status_e status = health_status_e::ok;  ///< Result severity.
    std::string title;  ///< Short headline.
    std::string detail;  ///< One-sentence explanation.
    fix_kind_e fix_kind = fix_kind_e::none;  ///< Kind of fix offered.
    std::string fix_value;  ///< Command, setting key or URL for the fix.
  };

  /**
   * @brief Everything the health rules look at, gathered separately so the rules stay testable.
   */
  struct health_probes_t {
    bool encoder_probed = false;  ///< Whether an encoder has been selected.
    std::string encoder_name;  ///< Chosen encoder family, e.g. `nvenc` or `software`.
    std::string capture_method;  ///< Capture backend, e.g. `nvfbc`.
    std::optional<bool> zero_copy;  ///< Whether frames stay on the GPU; unknown when empty.
    std::string window_system;  ///< `x11`, `wayland`, `windows`, `macos` or `none`.
    bool clipboard_enabled = false;  ///< Whether clipboard sync is enabled in the config.
    bool xclip_found = false;  ///< Whether `xclip` is on PATH.
    bool wl_clipboard_found = false;  ///< Whether both `wl-copy` and `wl-paste` are on PATH.
    bool check_input_devices = false;  ///< Whether uinput/uhid checks apply on this platform.
    bool uinput_writable = false;  ///< Whether the host can open uinput for writing.
    bool uhid_writable = false;  ///< Whether the host can open uhid for writing.
    bool udev_rules_installed = false;  ///< Whether Nova's udev rules are installed.
    bool check_audio_server = false;  ///< Whether the sound-server check applies on this platform.
    bool audio_server_reachable = false;  ///< Whether PulseAudio/PipeWire answered.
    bool app_running = false;  ///< Whether an app (and its prep commands) is active.
    std::vector<platf::capture_output_t> outputs;  ///< Display outputs.
    std::optional<bool> nvidia_drm_modeset;  ///< `nvidia-drm` modeset state; empty when not loaded.
    std::string origin_web_ui_allowed;  ///< `origin_web_ui_allowed` setting.
    bool check_wake_on_lan = false;  ///< Whether the Wake-on-LAN check applies on this platform.
    std::string wol_interface;  ///< Physical NIC that receives magic packets; empty when none was found.
    std::string wol_mac;  ///< Its MAC (what /serverinfo reports).
    bool ethtool_found = false;  ///< Whether `ethtool` is on PATH.
    std::optional<bool> wol_supported;  ///< NIC supports magic-packet wake; empty when unknown.
    std::optional<bool> wol_enabled;  ///< Magic-packet wake is enabled; empty when unknown.
    bool pcsleep_enabled = false;  ///< `pcsleep_enabled` setting.
    std::string can_suspend;  ///< logind CanSuspend: yes, challenge, no, na or unknown; empty when not checked.
    std::vector<std::string> exposed_files;  ///< Private files or folders readable by other users.
  };

  /**
   * @brief Turn gathered facts into health checks.
   *
   * @param probes Facts about the host.
   * @return Checks in display order.
   */
  std::vector<health_check_t> evaluate_health(const health_probes_t &probes);

  /**
   * @brief Gather health facts from the running host.
   *
   * @return Current facts.
   */
  health_probes_t collect_health_probes();

  /**
   * @brief Serialize checks for `/api/health`.
   *
   * @param checks Checks to serialize.
   * @return JSON array of check objects.
   */
  nlohmann::json health_to_json(const std::vector<health_check_t> &checks);

  /**
   * @brief Build the `/api/host/info` document.
   *
   * @return Host facts.
   */
  nlohmann::json host_info_json();

  /**
   * @brief Build the `/api/displays` document.
   *
   * @param outputs Display outputs to describe.
   * @return JSON array of outputs.
   */
  nlohmann::json displays_json(const std::vector<platf::capture_output_t> &outputs);

  /**
   * @brief Build the `/api/audio/sinks` document.
   *
   * @param sinks Sinks reported by the sound server.
   * @param configured Value of the `audio_sink` setting.
   * @return JSON object with a `sinks` array.
   */
  nlohmann::json audio_sinks_json(const std::vector<platf::sink_desc_t> &sinks, const std::string &configured);

  /**
   * @brief Downscale a BGRA frame to RGB with a box filter.
   *
   * @param frame Source frame.
   * @param max_width Largest output width; the frame is never upscaled.
   * @param out_width Receives the output width.
   * @param out_height Receives the output height.
   * @return Tightly packed RGB pixels.
   */
  std::vector<std::uint8_t> downscale_to_rgb(const platf::preview_frame_t &frame, int max_width, int &out_width, int &out_height);

  /**
   * @brief Encode RGB pixels as a baseline JPEG.
   *
   * @param rgb Tightly packed RGB pixels.
   * @param width Image width.
   * @param height Image height.
   * @param quality JPEG quality, 1–100.
   * @return JPEG bytes; empty on failure.
   */
  std::string encode_jpeg(const std::vector<std::uint8_t> &rgb, int width, int height, int quality);

  /**
   * @brief Fixed-window rate limiter.
   */
  class rate_limiter_t {
  public:
    /**
     * @brief Create a limiter.
     *
     * @param max_events Events allowed per window.
     * @param window Window length.
     */
    rate_limiter_t(int max_events, std::chrono::steady_clock::duration window);

    /**
     * @brief Try to take one slot.
     *
     * @param now Current time.
     * @return True when the event is allowed.
     */
    bool try_acquire(std::chrono::steady_clock::time_point now);

  private:
    std::mutex mutex;  ///< Guards the window state.
    int max_events;  ///< Events allowed per window.
    std::chrono::steady_clock::duration window;  ///< Window length.
    std::chrono::steady_clock::time_point window_start {};  ///< Start of the current window.
    int count = 0;  ///< Events in the current window.
  };

  /**
   * @brief Result of a preview request.
   */
  struct preview_result_t {
    std::string jpeg;  ///< JPEG bytes when successful.
    std::string error;  ///< Reason when unsuccessful.
    int http_status = 200;  ///< HTTP status to reply with.
  };

  /**
   * @brief Produce a JPEG preview of a display, rate-limited and cached for about a second.
   *
   * @param display Output name; empty for the configured or whole desktop.
   * @param width Requested width, clamped to 160–1280.
   * @return JPEG or an error with the HTTP status to use.
   */
  preview_result_t preview_jpeg(const std::string &display, int width);

  /**
   * @brief Health checks, cached for about ten seconds.
   *
   * @return Current checks.
   */
  std::vector<health_check_t> cached_health();

  /**
   * @brief Display outputs, cached for about two seconds.
   *
   * @return Current outputs.
   */
  std::vector<platf::capture_output_t> cached_outputs();

}  // namespace host_info
