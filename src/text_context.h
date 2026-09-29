/**
 * @file src/text_context.h
 * @brief Remote text context (Nova): tell a client when a host text field takes focus.
 *
 * Wire compatibility with the Sunshine-Foundation ecosystem (control message
 * index 24, packet type 0x550C, `LI_FF_REMOTE_TEXT_CONTEXT`). The protocol
 * facts are reimplemented from the published client decoder
 * (moonlight-common-c `RemoteTextContextStream.c`); no host code is copied.
 *
 * Negotiation:
 *  - host → client: SDP `x-ss-general.featureFlags` bit 0x200 (kHostFeatureFlag)
 *  - client → host: RTSP `x-ml-general.featureFlags` bit 0x10 (kMlFeatureFlag)
 *  Packets go only to sessions that set the client bit.
 *
 * Payload (76 bytes, little-endian, inside an encrypted v2 control frame):
 *
 *   off size field
 *     0  u8  version = 1
 *     1  u8  size    = 76
 *     2  u16 flags   (kFlag*)
 *     4  u32 revision        per-session, +1 per packet (wraps)
 *     8  u64 activation_id   per-session, +1 per activation; a deactivation
 *                            repeats the id it closes
 *    16  u64 input_token     id of the remote click/tap that caused it
 *    24  u8  source  (2 = UIA; Nova sends AT-SPI observations as UIA)
 *    25  u8  cause   (1 = remote touch/pen, 2 = remote mouse)
 *    26  u16 reserved = 0
 *    28  i32 anchor x, y            click point, capture-relative
 *    36  i32 element l, t, r, b     focused field, capture-relative
 *    52  i32 caret l, t, r, b       caret, capture-relative (never for passwords)
 *    68  u32 capture width, height  size of the captured display
 *
 * Privacy: Nova never reads field contents. It reads the accessible role,
 * the state set, the on-screen extents and (not for password fields) the
 * caret rectangle. Password fields are reported with kFlagPassword.
 *
 * Trust: an activation is sent only when the focus change follows a click or
 * tap from that client (within kMatchWindow, inside the field's rectangle).
 * Focus changes made at the host itself, or by an app, are never sent. When
 * focus leaves the field the client is told to close its keyboard.
 */
#pragma once

// standard includes
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace text_context {

  constexpr std::uint16_t kControlPacketType = 0x550C;  ///< Control-stream packet type (message index 24).
  constexpr std::uint32_t kHostFeatureFlag = 0x200;  ///< `LI_FF_REMOTE_TEXT_CONTEXT`, advertised in x-ss-general.featureFlags.
  constexpr std::uint32_t kMlFeatureFlag = 0x10;  ///< `ML_FF_REMOTE_TEXT_CONTEXT`, sent by capable clients in x-ml-general.featureFlags.
  constexpr std::uint8_t kWireVersion = 1;  ///< Payload version byte.
  constexpr std::size_t kWireSize = 76;  ///< Payload size in bytes.

  constexpr std::uint16_t kFlagActive = 0x0001;  ///< The field has keyboard focus.
  constexpr std::uint16_t kFlagEditable = 0x0002;  ///< The field accepts text.
  constexpr std::uint16_t kFlagPassword = 0x0004;  ///< The field is a password field.
  constexpr std::uint16_t kFlagMultiline = 0x0008;  ///< The field accepts several lines.
  constexpr std::uint16_t kFlagAnchorPoint = 0x0010;  ///< The anchor point is valid.
  constexpr std::uint16_t kFlagElementRect = 0x0020;  ///< The element rectangle is valid.
  constexpr std::uint16_t kFlagCaretRect = 0x0040;  ///< The caret rectangle is valid.
  constexpr std::uint16_t kFlagInputMatched = 0x0080;  ///< Correlated with this client's own click or tap.
  constexpr std::uint16_t kFlagPaneVisible = 0x0100;  ///< Windows InputPane only; Nova never sets it.
  constexpr std::uint16_t kFlagAutoShow = 0x0200;  ///< Windows InputPane only; Nova never sets it.

  /**
   * @brief Where the focus observation came from.
   */
  enum class source_e : std::uint8_t {
    input_pane = 1,  ///< Windows touch keyboard (InputPane); unused on Linux.
    uia = 2,  ///< Accessibility tree focus (UIA on Windows, AT-SPI on Linux).
  };

  /**
   * @brief Which remote input caused the observation.
   */
  enum class cause_e : std::uint8_t {
    unknown = 0,  ///< Not correlated.
    remote_touch = 1,  ///< A tap (touch or pen) from the client.
    remote_mouse = 2,  ///< A left click from the client.
  };

  /**
   * @brief Rectangle in root-window (screen) pixels, right/bottom exclusive.
   */
  struct rect_t {
    std::int32_t left {};  ///< Left edge.
    std::int32_t top {};  ///< Top edge.
    std::int32_t right {};  ///< Right edge (exclusive).
    std::int32_t bottom {};  ///< Bottom edge (exclusive).

    /**
     * @brief Whether the rectangle has a positive area.
     * @return True when right > left and bottom > top.
     */
    bool valid() const {
      return right > left && bottom > top;
    }

    /**
     * @brief Equality.
     * @param other Rectangle to compare.
     * @return True when every edge matches.
     */
    bool operator==(const rect_t &other) const = default;
  };

  /**
   * @brief Point in root-window (screen) pixels.
   */
  struct point_t {
    std::int32_t x {};  ///< Horizontal position.
    std::int32_t y {};  ///< Vertical position.
  };

  /**
   * @brief The captured display region in root-window pixels.
   */
  struct geometry_t {
    std::int32_t x {};  ///< Left edge of the capture.
    std::int32_t y {};  ///< Top edge of the capture.
    std::uint32_t width {};  ///< Capture width.
    std::uint32_t height {};  ///< Capture height.

    /**
     * @brief Whether the geometry can be sent (the client rejects zero or huge sizes).
     * @return True when both sizes are in 1..1000000.
     */
    bool valid() const {
      return width > 0 && height > 0 && width <= 1'000'000 && height <= 1'000'000;
    }
  };

  /**
   * @brief One focus snapshot from the platform accessibility layer.
   *
   * It carries no text: only identity, role-derived booleans and geometry.
   */
  struct observation_t {
    std::string element;  ///< Opaque host-local identity (AT-SPI bus name + object path). Never sent.
    bool focused {false};  ///< The element has keyboard focus.
    bool editable {false};  ///< The element accepts text input.
    bool password {false};  ///< The element is a password field.
    bool multiline {false};  ///< The element accepts several lines.
    std::optional<rect_t> element_rect;  ///< On-screen extents, screen coordinates.
    std::optional<rect_t> caret_rect;  ///< Caret extents, screen coordinates (never set for passwords).
  };

  /**
   * @brief Decoded wire packet (host-side mirror of `LI_REMOTE_TEXT_CONTEXT`).
   */
  struct packet_t {
    std::uint16_t flags {};  ///< kFlag* bits.
    std::uint32_t revision {};  ///< Per-session revision.
    std::uint64_t activation_id {};  ///< Activation this packet opens or closes.
    std::uint64_t input_token {};  ///< Remote input that caused it.
    source_e source {source_e::uia};  ///< Observation source.
    cause_e cause {cause_e::unknown};  ///< Remote input kind.
    std::int32_t anchor_x {};  ///< Anchor x, capture-relative.
    std::int32_t anchor_y {};  ///< Anchor y, capture-relative.
    rect_t element;  ///< Element rectangle, capture-relative.
    rect_t caret;  ///< Caret rectangle, capture-relative.
    std::uint32_t capture_width {};  ///< Capture width.
    std::uint32_t capture_height {};  ///< Capture height.
  };

  /**
   * @brief Serialize a packet to the 76-byte payload.
   * @param packet Packet to encode.
   * @return Exactly kWireSize bytes.
   */
  std::vector<std::uint8_t> encode(const packet_t &packet);

  /**
   * @brief Parse a payload with the same checks as the client decoder.
   * @param bytes Payload bytes.
   * @return The packet, or nullopt when the client would reject it.
   */
  std::optional<packet_t> decode(std::span<const std::uint8_t> bytes);

  /**
   * @brief AT-SPI role and state numbers used to classify a focused accessible.
   *
   * Values from at-spi2-core `atspi-constants.h` (AtspiRole, AtspiStateType).
   */
  namespace atspi {
    constexpr std::uint32_t kRolePasswordText = 40;  ///< ATSPI_ROLE_PASSWORD_TEXT
    constexpr std::uint32_t kRoleTerminal = 60;  ///< ATSPI_ROLE_TERMINAL
    constexpr std::uint32_t kRoleText = 61;  ///< ATSPI_ROLE_TEXT
    constexpr std::uint32_t kRoleEntry = 79;  ///< ATSPI_ROLE_ENTRY
    constexpr std::uint32_t kStateEditable = 7;  ///< ATSPI_STATE_EDITABLE
    constexpr std::uint32_t kStateFocused = 12;  ///< ATSPI_STATE_FOCUSED
    constexpr std::uint32_t kStateMultiLine = 17;  ///< ATSPI_STATE_MULTI_LINE
    constexpr std::uint32_t kStateShowing = 25;  ///< ATSPI_STATE_SHOWING
    constexpr std::uint32_t kStateReadOnly = 43;  ///< ATSPI_STATE_READ_ONLY

    /**
     * @brief Test one state bit of an AT-SPI state set (two 32-bit words).
     * @param states State words as returned by `Accessible.GetState`.
     * @param state AtspiStateType value.
     * @return True when the bit is set.
     */
    bool has_state(std::span<const std::uint32_t> states, std::uint32_t state);

    /**
     * @brief Build an observation from what AT-SPI reported for an accessible.
     *
     * Editable means an editable, non-read-only text role, or a text entry,
     * password or terminal role. Web documents count only when they are
     * editable (contenteditable). The caret is dropped for password fields.
     *
     * @param element Opaque identity (bus name + path).
     * @param role AtspiRole number.
     * @param states State words.
     * @param extents Screen extents as x, y, width, height, when known.
     * @param caret Caret extents as x, y, width, height, when known.
     * @return The observation.
     */
    observation_t classify(std::string element, std::uint32_t role, std::span<const std::uint32_t> states,
                           std::optional<std::array<std::int32_t, 4>> extents,
                           std::optional<std::array<std::int32_t, 4>> caret);
  }  // namespace atspi

  /**
   * @brief One payload for one session.
   */
  struct outbound_t {
    std::uint32_t session {};  ///< Target launch-session id.
    std::vector<std::uint8_t> bytes;  ///< kWireSize payload bytes.
  };

  /**
   * @brief Gesture phase for bridge_t::on_pointer().
   */
  enum class phase_e {
    down,  ///< Button pressed or finger down.
    move,  ///< Motion while pressed (distance in capture pixels since down).
    up,  ///< Button released or finger lifted.
    cancel,  ///< Gesture abandoned (right click, scroll, touch cancel).
  };

  /**
   * @brief Timing and distance tuning. Defaults match the client's expectations.
   */
  struct tuning_t {
    std::chrono::milliseconds match_window {1200};  ///< Focus must follow the click within this time.
    std::int32_t hit_slop_px {12};  ///< Click may land this far outside the field.
    std::int32_t drag_slop_px {24};  ///< Motion beyond this makes the gesture a drag, not a click.
    std::chrono::milliseconds activation_settle {60};  ///< Wait this long for focus to settle before opening.
    std::chrono::milliseconds deactivation_grace {300};  ///< Wait this long before closing (focus may come back).
    std::chrono::milliseconds min_packet_gap {100};  ///< At most one packet per session per gap.
    std::chrono::milliseconds reopen_dedupe {350};  ///< Ignore a second activation of the same field this soon.
  };

  /**
   * @brief Correlates focus observations with remote clicks and produces packets.
   *
   * Thread-safe. Time is passed in so tests can drive it; production callers
   * use the service_t wrapper.
   */
  class bridge_t {
  public:
    using clock = std::chrono::steady_clock;  ///< Clock type for all time points.

    /**
     * @brief Construct a bridge.
     * @param tuning Timing and distance tuning.
     */
    explicit bridge_t(tuning_t tuning = {});

    /**
     * @brief Start tracking a session.
     * @param session Launch-session id.
     */
    void session_started(std::uint32_t session);

    /**
     * @brief Forget a session and anything queued for it.
     * @param session Launch-session id.
     */
    void session_stopped(std::uint32_t session);

    /**
     * @brief Number of tracked sessions.
     * @return Session count.
     */
    std::size_t session_count() const;

    /**
     * @brief Set the captured display region (root-window pixels).
     * @param geometry Capture region.
     */
    void set_capture_geometry(geometry_t geometry);

    /**
     * @brief Current capture region.
     * @return Capture region (invalid until set).
     */
    geometry_t capture_geometry() const;

    /**
     * @brief Record a remote pointer gesture step.
     *
     * @param session Launch-session id.
     * @param cause Touch/pen or mouse.
     * @param phase Gesture phase.
     * @param pointer_id Touch pointer id (0 for mouse).
     * @param point Pointer position in root-window pixels, when known.
     * @param moved_px For phase_e::move: distance travelled since down, capture pixels.
     * @param now Current time.
     */
    void on_pointer(std::uint32_t session, cause_e cause, phase_e phase, std::uint32_t pointer_id,
                    std::optional<point_t> point, std::int32_t moved_px, clock::time_point now);

    /**
     * @brief Feed a focus observation from the accessibility layer.
     * @param observation Snapshot of the focused (or just unfocused) element.
     * @param now Current time.
     */
    void observe(const observation_t &observation, clock::time_point now);

    /**
     * @brief Forget the focused element (the source restarted or moved to another display).
     *
     * Every open keyboard is closed at once, since focus on the old display means nothing now.
     */
    void observe_reset();

    /**
     * @brief Emit due packets into the outbox (debounce and rate limit).
     * @param now Current time.
     */
    void tick(clock::time_point now);

    /**
     * @brief Take every queued payload.
     * @return Payloads in send order.
     */
    std::vector<outbound_t> drain();

  private:
    struct candidate_t {
      std::uint32_t session {};
      std::uint64_t token {};
      cause_e cause {cause_e::unknown};
      clock::time_point completed {};
      std::optional<point_t> point;
      std::int32_t travelled {};
      bool consumed {false};
    };

    struct active_t {
      std::uint64_t activation_id {};
      candidate_t candidate;
      std::string element;
      clock::time_point opened {};
    };

    struct pending_t {
      bool activate {false};
      clock::time_point due {};
      candidate_t candidate;
      observation_t observation;
    };

    struct session_t {
      std::uint64_t next_token {1};
      std::uint64_t next_activation {1};
      std::uint32_t next_revision {1};
      std::optional<candidate_t> mouse;
      std::map<std::uint32_t, candidate_t> touches;
      std::optional<active_t> active;
      std::optional<pending_t> pending;
      std::optional<clock::time_point> last_sent;
    };

    void correlate_locked(const observation_t &observation, clock::time_point now);
    void schedule_close_locked(std::uint32_t session, session_t &state, clock::time_point now);
    void finish_gesture_locked(session_t &state, candidate_t candidate, clock::time_point now);
    bool emit_locked(std::uint32_t session, session_t &state, const pending_t &pending, clock::time_point now);
    bool hit_locked(const candidate_t &candidate, const observation_t &observation) const;

    tuning_t _tuning;
    mutable std::mutex _mutex;
    std::map<std::uint32_t, session_t> _sessions;
    std::deque<candidate_t> _recent;
    std::optional<observation_t> _current;
    geometry_t _geometry;
    std::vector<outbound_t> _outbox;
  };

  /**
   * @brief The X display whose text fields are watched (the one being streamed).
   */
  struct target_t {
    std::string display;  ///< DISPLAY value, e.g. ":20"; empty = Nova's own `$DISPLAY` (Mirror).
    std::string xauthority;  ///< Cookie file; empty = `$XAUTHORITY`.
    std::string session_dir;  ///< Virtual display desktop session state dir (private D-Bus), empty without one.

    /**
     * @brief Compare two targets.
     * @param other Target to compare with.
     * @return True when every field matches.
     */
    bool operator==(const target_t &other) const = default;
  };

  /**
   * @brief A platform focus source (AT-SPI on Linux; a fake in tests).
   */
  class source_t {
  public:
    using callback_t = std::function<void(const observation_t &)>;  ///< Receives observations on the source's thread.

    virtual ~source_t() = default;

    /**
     * @brief Start listening.
     * @param callback Called for every observation.
     * @return True when the source is listening.
     */
    virtual bool start(callback_t callback) = 0;

    /**
     * @brief Stop listening and join any worker thread.
     */
    virtual void stop() = 0;

    /**
     * @brief Current host pointer position in root-window pixels.
     * @return The position, or nullopt when the platform cannot tell.
     */
    virtual std::optional<point_t> pointer() = 0;

    /**
     * @brief Whether the source lost its connection for good (bus closed, display gone).
     * @return True when it must be replaced.
     */
    virtual bool broken() const {
      return false;
    }
  };

  /**
   * @brief Create the platform source for a display.
   * @param target Display to watch.
   * @return The source, or nullptr when this platform has none.
   */
  std::unique_ptr<source_t> make_platform_source(const target_t &target);

  /**
   * @brief Whether the platform source can work for a display (an accessibility bus is reachable).
   * @param target Display to check.
   * @return True when a source could start. Cached by the caller.
   */
  bool platform_source_available(const target_t &target);

  /**
   * @brief The display being streamed now: the Virtual display while one runs, else the desktop.
   * @return The target.
   */
  target_t capture_target();

  /**
   * @brief Session lifecycle, input sniffing and the source, wired together.
   *
   * A supervisor thread owns the source: it starts it with the first capable session, follows
   * the capture when it moves to another X display, restarts a source that broke, and stops it
   * with the last session. Nothing here blocks the control thread on D-Bus.
   */
  class service_t {
  public:
    using factory_t = std::function<std::unique_ptr<source_t>(const target_t &)>;  ///< Creates the focus source for a display.
    using now_t = std::function<bridge_t::clock::time_point()>;  ///< Time source.
    using target_provider_t = std::function<target_t()>;  ///< Current capture display.

    /**
     * @brief Construct a service.
     * @param factory Creates the source when the first capable session starts.
     * @param now Time source.
     * @param target Current capture display.
     * @param tuning Bridge tuning.
     */
    service_t(factory_t factory, now_t now, target_provider_t target, tuning_t tuning = {});
    ~service_t();

    service_t(const service_t &) = delete;
    service_t &operator=(const service_t &) = delete;

    /**
     * @brief A session started.
     * @param session Launch-session id.
     * @param ml_feature_flags Client x-ml-general.featureFlags.
     * @param enabled Host config allows the feature for this session.
     * @return True when the session gets text context updates.
     */
    bool session_started(std::uint32_t session, std::uint32_t ml_feature_flags, bool enabled);

    /**
     * @brief A session stopped. The source stops with the last capable session.
     * @param session Launch-session id.
     */
    void session_stopped(std::uint32_t session);

    /**
     * @brief Inspect one decrypted, permitted input packet for clicks and taps.
     * @param session Launch-session id.
     * @param packet Plaintext input packet (NV_INPUT_HEADER first).
     */
    void on_input(std::uint32_t session, std::span<const std::uint8_t> packet);

    /**
     * @brief Set the captured display region.
     * @param geometry Capture region.
     */
    void set_capture_geometry(geometry_t geometry);

    /**
     * @brief Run the debounce and take every due payload.
     * @return Payloads in send order.
     */
    std::vector<outbound_t> drain_outbound();

    /**
     * @brief Whether any capable session exists (the control loop then ticks faster).
     * @return True while at least one session gets updates.
     */
    bool active() const;

    /**
     * @brief Whether the focus source is running.
     * @return True while at least one capable session exists and the source started.
     */
    bool source_running() const;

    /**
     * @brief Wait until the supervisor has settled (tests).
     * @param timeout Longest wait.
     * @return True when the source state matches the sessions within @p timeout.
     */
    bool wait_settled(std::chrono::milliseconds timeout);

    /**
     * @brief Direct access for tests.
     * @return The bridge.
     */
    bridge_t &bridge() {
      return _bridge;
    }

  private:
    struct mouse_track_t {
      bool held {false};
      std::int64_t rel {};
      std::optional<std::pair<float, float>> abs_start;
      std::int32_t abs_moved {};
      std::optional<std::pair<float, float>> abs_last;  ///< Last absolute position (fraction of the stream).
      bool last_move_abs {false};  ///< The latest pointer motion was absolute.
    };

    struct touch_track_t {
      float x {};
      float y {};
    };

    std::optional<point_t> pointer_now(std::uint32_t session);
    void supervise(std::stop_token stop);

    factory_t _factory;
    now_t _now;
    target_provider_t _target;
    bridge_t _bridge;
    mutable std::mutex _mutex;
    std::condition_variable_any _wake;
    std::shared_ptr<source_t> _source;
    std::optional<target_t> _source_target;
    std::map<std::uint32_t, bool> _capable;
    std::map<std::uint32_t, mouse_track_t> _mouse;
    std::map<std::pair<std::uint32_t, std::uint32_t>, touch_track_t> _touch;
    std::jthread _supervisor;
  };

  // ---- Process-wide instance used by stream.cpp, rtsp.cpp and nvhttp.cpp ----

  /**
   * @brief Whether Nova can offer the feature now (config on and AT-SPI reachable on the captured display).
   * @return True to advertise kHostFeatureFlag and the "text_context" capability.
   */
  bool available();

  /**
   * @brief Process-wide service.
   * @return The service.
   */
  service_t &service();

}  // namespace text_context
