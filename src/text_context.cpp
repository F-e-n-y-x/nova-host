/**
 * @file src/text_context.cpp
 * @brief Remote text context: wire codec, click/focus correlation, debounce. See text_context.h.
 */
// standard includes
#include <algorithm>
#include <cmath>
#include <cstring>

// local includes
#include "config.h"
#include "logging.h"
#include "text_context.h"

extern "C" {
#include <moonlight-common-c/src/Input.h>
#include <moonlight-common-c/src/Limelight.h>
}

using namespace std::literals;

namespace text_context {

  namespace {
    constexpr std::size_t kMaxRecentCandidates = 128;
    constexpr std::size_t kMaxTouchesPerSession = 16;

    void put_u16(std::vector<std::uint8_t> &out, std::size_t offset, std::uint16_t value) {
      out[offset] = static_cast<std::uint8_t>(value);
      out[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    }

    void put_u32(std::vector<std::uint8_t> &out, std::size_t offset, std::uint32_t value) {
      for (unsigned i = 0; i < 4; ++i) {
        out[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
      }
    }

    void put_i32(std::vector<std::uint8_t> &out, std::size_t offset, std::int32_t value) {
      put_u32(out, offset, static_cast<std::uint32_t>(value));
    }

    void put_u64(std::vector<std::uint8_t> &out, std::size_t offset, std::uint64_t value) {
      for (unsigned i = 0; i < 8; ++i) {
        out[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
      }
    }

    std::uint16_t get_u16(const std::uint8_t *p) {
      return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
    }

    std::uint32_t get_u32(const std::uint8_t *p) {
      return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
             (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
    }

    std::uint64_t get_u64(const std::uint8_t *p) {
      return static_cast<std::uint64_t>(get_u32(p)) | (static_cast<std::uint64_t>(get_u32(p + 4)) << 32);
    }

    rect_t relative(const rect_t &rect, const geometry_t &geometry) {
      return {rect.left - geometry.x, rect.top - geometry.y, rect.right - geometry.x, rect.bottom - geometry.y};
    }

    std::int16_t be16(const std::uint8_t *p) {
      return static_cast<std::int16_t>((p[0] << 8) | p[1]);
    }

    float le_float(const std::uint8_t *p) {
      auto bits = get_u32(p);
      float value;
      std::memcpy(&value, &bits, sizeof(value));
      return value;
    }
  }  // namespace

  // ---- Wire codec ---------------------------------------------------------

  std::vector<std::uint8_t> encode(const packet_t &packet) {
    std::vector<std::uint8_t> out(kWireSize, 0);
    out[0] = kWireVersion;
    out[1] = static_cast<std::uint8_t>(kWireSize);
    put_u16(out, 2, packet.flags);
    put_u32(out, 4, packet.revision);
    put_u64(out, 8, packet.activation_id);
    put_u64(out, 16, packet.input_token);
    out[24] = static_cast<std::uint8_t>(packet.source);
    out[25] = static_cast<std::uint8_t>(packet.cause);
    // 26..27 reserved, zero.
    put_i32(out, 28, packet.anchor_x);
    put_i32(out, 32, packet.anchor_y);
    put_i32(out, 36, packet.element.left);
    put_i32(out, 40, packet.element.top);
    put_i32(out, 44, packet.element.right);
    put_i32(out, 48, packet.element.bottom);
    put_i32(out, 52, packet.caret.left);
    put_i32(out, 56, packet.caret.top);
    put_i32(out, 60, packet.caret.right);
    put_i32(out, 64, packet.caret.bottom);
    put_u32(out, 68, packet.capture_width);
    put_u32(out, 72, packet.capture_height);
    return out;
  }

  std::optional<packet_t> decode(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != kWireSize || bytes[0] != kWireVersion || bytes[1] != kWireSize || bytes[26] != 0 || bytes[27] != 0) {
      return std::nullopt;
    }
    const auto *p = bytes.data();
    packet_t packet;
    packet.flags = get_u16(p + 2);
    packet.revision = get_u32(p + 4);
    packet.activation_id = get_u64(p + 8);
    packet.input_token = get_u64(p + 16);
    packet.source = static_cast<source_e>(p[24]);
    packet.cause = static_cast<cause_e>(p[25]);
    packet.anchor_x = static_cast<std::int32_t>(get_u32(p + 28));
    packet.anchor_y = static_cast<std::int32_t>(get_u32(p + 32));
    packet.element = {static_cast<std::int32_t>(get_u32(p + 36)), static_cast<std::int32_t>(get_u32(p + 40)), static_cast<std::int32_t>(get_u32(p + 44)), static_cast<std::int32_t>(get_u32(p + 48))};
    packet.caret = {static_cast<std::int32_t>(get_u32(p + 52)), static_cast<std::int32_t>(get_u32(p + 56)), static_cast<std::int32_t>(get_u32(p + 60)), static_cast<std::int32_t>(get_u32(p + 64))};
    packet.capture_width = get_u32(p + 68);
    packet.capture_height = get_u32(p + 72);
    const geometry_t check {0, 0, packet.capture_width, packet.capture_height};
    if (!check.valid()) {
      return std::nullopt;
    }
    return packet;
  }

  // ---- AT-SPI classification ----------------------------------------------

  namespace atspi {
    bool has_state(std::span<const std::uint32_t> states, std::uint32_t state) {
      const auto word = state / 32;
      return word < states.size() && (states[word] & (1u << (state % 32))) != 0;
    }

    observation_t classify(std::string element, std::uint32_t role, std::span<const std::uint32_t> states,
                           std::optional<std::array<std::int32_t, 4>> extents,
                           std::optional<std::array<std::int32_t, 4>> caret) {
      observation_t observation;
      observation.element = std::move(element);
      observation.focused = has_state(states, kStateFocused);
      const bool read_only = has_state(states, kStateReadOnly);
      const bool text_role = role == kRoleEntry || role == kRolePasswordText || role == kRoleTerminal;
      observation.editable = !read_only && (has_state(states, kStateEditable) || text_role);
      observation.password = role == kRolePasswordText;
      observation.multiline = has_state(states, kStateMultiLine) || role == kRoleTerminal;
      auto to_rect = [](const std::array<std::int32_t, 4> &e) -> std::optional<rect_t> {
        // x, y, width, height; AT-SPI reports -1/0 sizes for off-screen or unknown.
        if (e[2] <= 0 || e[3] <= 0) {
          return std::nullopt;
        }
        return rect_t {e[0], e[1], e[0] + e[2], e[1] + e[3]};
      };
      if (extents) {
        observation.element_rect = to_rect(*extents);
      }
      // Never keep the caret of a password field: its x position reveals the length.
      if (caret && !observation.password) {
        auto c = *caret;
        if (c[2] <= 0 && c[3] > 0) {
          c[2] = 1;  // Zero-width insertion caret.
        }
        observation.caret_rect = to_rect(c);
      }
      return observation;
    }
  }  // namespace atspi

  // ---- Bridge -------------------------------------------------------------

  bridge_t::bridge_t(tuning_t tuning):
      _tuning {tuning} {
  }

  void bridge_t::session_started(std::uint32_t session) {
    std::lock_guard lock {_mutex};
    _sessions.try_emplace(session);
  }

  void bridge_t::session_stopped(std::uint32_t session) {
    std::lock_guard lock {_mutex};
    _sessions.erase(session);
    std::erase_if(_recent, [session](const candidate_t &c) {
      return c.session == session;
    });
    std::erase_if(_outbox, [session](const outbound_t &o) {
      return o.session == session;
    });
  }

  std::size_t bridge_t::session_count() const {
    std::lock_guard lock {_mutex};
    return _sessions.size();
  }

  void bridge_t::set_capture_geometry(geometry_t geometry) {
    std::lock_guard lock {_mutex};
    _geometry = geometry;
  }

  geometry_t bridge_t::capture_geometry() const {
    std::lock_guard lock {_mutex};
    return _geometry;
  }

  void bridge_t::on_pointer(std::uint32_t session, cause_e cause, phase_e phase, std::uint32_t pointer_id,
                            std::optional<point_t> point, std::int32_t moved_px, clock::time_point now) {
    std::lock_guard lock {_mutex};
    auto it = _sessions.find(session);
    if (it == _sessions.end()) {
      return;
    }
    auto &state = it->second;
    const bool touch = cause == cause_e::remote_touch;

    if (phase == phase_e::down) {
      candidate_t candidate {session, state.next_token++, cause, now, point, 0, false};
      if (touch) {
        if (state.touches.contains(pointer_id) || state.touches.size() >= kMaxTouchesPerSession) {
          return;
        }
        state.touches.emplace(pointer_id, candidate);
      } else {
        state.mouse = candidate;
      }
      return;
    }

    candidate_t *in_flight = nullptr;
    if (touch) {
      auto t = state.touches.find(pointer_id);
      if (t != state.touches.end()) {
        in_flight = &t->second;
      }
    } else if (state.mouse) {
      in_flight = &*state.mouse;
    }
    if (!in_flight) {
      return;
    }

    switch (phase) {
      case phase_e::move:
        in_flight->travelled = std::max(in_flight->travelled, moved_px);
        break;
      case phase_e::cancel:
        if (touch) {
          state.touches.erase(pointer_id);
        } else {
          state.mouse.reset();
        }
        break;
      case phase_e::up:
        {
          auto candidate = *in_flight;
          candidate.travelled = std::max(candidate.travelled, moved_px);
          if (point) {
            candidate.point = point;
          }
          if (touch) {
            state.touches.erase(pointer_id);
          } else {
            state.mouse.reset();
          }
          finish_gesture_locked(state, candidate, now);
          break;
        }
      case phase_e::down:
        break;
    }
  }

  void bridge_t::finish_gesture_locked(session_t &state, candidate_t candidate, clock::time_point now) {
    static_cast<void>(state);
    if (candidate.travelled > _tuning.drag_slop_px) {
      return;  // A drag (text selection, window move), not a click on a field.
    }
    // The match window runs from gesture completion so a long press still gets it.
    candidate.completed = now;
    _recent.push_back(candidate);
    if (_recent.size() > kMaxRecentCandidates) {
      _recent.pop_front();
    }
    // Clicking into a field that already has focus produces no new focus
    // event, so correlate the click against the latest snapshot right away.
    if (_current && _current->focused && _current->editable) {
      correlate_locked(*_current, now);
    }
  }

  bool bridge_t::hit_locked(const candidate_t &candidate, const observation_t &observation) const {
    if (!observation.element_rect || !observation.element_rect->valid()) {
      return false;  // The client requires an element rectangle.
    }
    if (!candidate.point) {
      return true;  // No pointer position on this platform: timing alone.
    }
    const auto &r = *observation.element_rect;
    const auto slop = _tuning.hit_slop_px;
    return candidate.point->x >= r.left - slop && candidate.point->x <= r.right + slop &&
           candidate.point->y >= r.top - slop && candidate.point->y <= r.bottom + slop;
  }

  void bridge_t::schedule_close_locked(std::uint32_t session, session_t &state, clock::time_point now) {
    static_cast<void>(session);
    if (!state.active) {
      return;  // Nothing was sent, so there is nothing to close.
    }
    if (state.pending && !state.pending->activate) {
      return;  // A close is already on its way; keep its original deadline.
    }
    pending_t close;
    close.activate = false;
    close.due = now + _tuning.deactivation_grace;
    close.candidate = state.active->candidate;
    state.pending = close;
  }

  void bridge_t::observe(const observation_t &observation, clock::time_point now) {
    std::lock_guard lock {_mutex};
    if (observation.focused) {
      _current = observation;
    } else if (_current && _current->element == observation.element) {
      _current.reset();
    }

    if (observation.focused && observation.editable) {
      correlate_locked(observation, now);
      return;
    }

    // Focus left a field, or moved to something that takes no text.
    for (auto &[session, state] : _sessions) {
      const bool opening = state.pending && state.pending->activate;
      if (!observation.focused) {
        // A blur. Order is not guaranteed: the next field's focus event may
        // arrive before or after this one, so only react to the field we track.
        if (opening) {
          if (state.pending->observation.element != observation.element) {
            continue;  // Another field is being opened; it supersedes this one.
          }
          state.pending.reset();  // Opened and left before it was sent: send nothing for it.
        } else if (!state.active || state.active->element != observation.element) {
          continue;
        }
      } else if (opening) {
        state.pending.reset();
      }
      schedule_close_locked(session, state, now);
    }
  }

  void bridge_t::observe_reset() {
    std::lock_guard lock {_mutex};
    const auto now = clock::now();
    _current.reset();
    _recent.clear();
    for (auto &[session, state] : _sessions) {
      if (state.pending && state.pending->activate) {
        state.pending.reset();
      }
      schedule_close_locked(session, state, now);
      if (state.pending && !state.pending->activate) {
        state.pending->due = now;
      }
    }
  }

  void bridge_t::correlate_locked(const observation_t &observation, clock::time_point now) {
    while (!_recent.empty() && now - _recent.front().completed > _tuning.match_window) {
      _recent.pop_front();
    }

    candidate_t *match = nullptr;
    bool ambiguous = false;
    for (auto &candidate : _recent) {
      if (candidate.consumed || now - candidate.completed > _tuning.match_window ||
          !_sessions.contains(candidate.session) || !hit_locked(candidate, observation)) {
        continue;
      }
      if (match && match->session != candidate.session) {
        ambiguous = true;
      }
      if (!match || candidate.completed >= match->completed) {
        match = &candidate;
      }
    }

    if (!match || ambiguous) {
      // Not caused by a client click (Tab key, app autofocus, the person at
      // the host). If a client's keyboard is already open, focus moving to
      // another field is a continuation: keep it open and follow the field.
      for (auto &[session, state] : _sessions) {
        if (state.pending && !state.pending->activate) {
          state.pending.reset();
        }
        if (state.active) {
          state.active->element = observation.element;
        }
      }
      return;
    }

    match->consumed = true;
    auto &state = _sessions.at(match->session);

    // Double click, or a second click in the field that just opened.
    if (state.active && state.active->element == observation.element &&
        now - state.active->opened < _tuning.reopen_dedupe && !(state.pending && !state.pending->activate)) {
      return;
    }

    pending_t open;
    open.activate = true;
    // Keep an earlier deadline when focus bounces between fields after one click.
    open.due = (state.pending && state.pending->activate) ? state.pending->due : now + _tuning.activation_settle;
    open.candidate = *match;
    open.observation = observation;
    state.pending = open;

    // One keyboard focus on the host: close it on every other client.
    for (auto &[session, other] : _sessions) {
      if (session != match->session) {
        if (other.pending && other.pending->activate) {
          other.pending.reset();
        }
        schedule_close_locked(session, other, now);
        if (other.pending && !other.pending->activate) {
          other.pending->due = now;  // No grace: the focus has definitely moved on.
        }
      }
    }
  }

  bool bridge_t::emit_locked(std::uint32_t session, session_t &state, const pending_t &pending, clock::time_point now) {
    if (!_geometry.valid()) {
      BOOST_LOG(debug) << "text_context: no capture geometry yet; dropping update for session "sv << session;
      return true;
    }

    packet_t packet;
    packet.source = source_e::uia;
    packet.cause = pending.candidate.cause;
    packet.input_token = pending.candidate.token;
    packet.capture_width = _geometry.width;
    packet.capture_height = _geometry.height;
    packet.flags = kFlagInputMatched;
    if (pending.candidate.point) {
      packet.flags |= kFlagAnchorPoint;
      packet.anchor_x = pending.candidate.point->x - _geometry.x;
      packet.anchor_y = pending.candidate.point->y - _geometry.y;
    }

    if (pending.activate) {
      const auto &o = pending.observation;
      packet.flags |= kFlagActive | kFlagEditable;
      if (o.password) {
        packet.flags |= kFlagPassword;
      }
      if (o.multiline) {
        packet.flags |= kFlagMultiline;
      }
      if (o.element_rect && o.element_rect->valid()) {
        packet.flags |= kFlagElementRect;
        packet.element = relative(*o.element_rect, _geometry);
      }
      if (o.caret_rect && o.caret_rect->valid() && !o.password) {
        packet.flags |= kFlagCaretRect;
        packet.caret = relative(*o.caret_rect, _geometry);
      }
      packet.activation_id = state.next_activation++;
      state.active = active_t {packet.activation_id, pending.candidate, o.element, now};
    } else {
      if (!state.active) {
        return true;
      }
      packet.activation_id = state.active->activation_id;
      state.active.reset();
    }

    packet.revision = state.next_revision++;
    _outbox.push_back({session, encode(packet)});
    state.last_sent = now;
    BOOST_LOG(debug) << "text_context: "sv << (pending.activate ? "open"sv : "close"sv) << " activation "sv << packet.activation_id
                     << " revision "sv << packet.revision << " session "sv << session
                     << ((packet.flags & kFlagPassword) ? " (password)"sv : ""sv);
    return true;
  }

  void bridge_t::tick(clock::time_point now) {
    std::lock_guard lock {_mutex};
    for (auto &[session, state] : _sessions) {
      if (!state.pending || now < state.pending->due) {
        continue;
      }
      if (state.last_sent && now - *state.last_sent < _tuning.min_packet_gap) {
        continue;  // Rate limit: try again on a later tick; the latest state wins.
      }
      auto pending = *state.pending;
      state.pending.reset();
      emit_locked(session, state, pending, now);
    }
  }

  std::vector<outbound_t> bridge_t::drain() {
    std::lock_guard lock {_mutex};
    std::vector<outbound_t> out;
    out.swap(_outbox);
    return out;
  }

  // ---- Service ------------------------------------------------------------

  namespace {
    constexpr auto kSupervisePeriod = 1s;  ///< How often the supervisor checks the capture display.
    constexpr auto kRetryPeriod = 5s;  ///< How long to wait before retrying a source that would not start.
  }  // namespace

  service_t::service_t(factory_t factory, now_t now, target_provider_t target, tuning_t tuning):
      _factory {std::move(factory)},
      _now {std::move(now)},
      _target {std::move(target)},
      _bridge {tuning} {
  }

  service_t::~service_t() {
    std::jthread supervisor;
    {
      std::lock_guard lock {_mutex};
      supervisor = std::move(_supervisor);
    }
    if (supervisor.joinable()) {
      supervisor.request_stop();  // jthread's stop wakes the condition_variable_any wait.
      supervisor.join();
    }
  }

  bool service_t::session_started(std::uint32_t session, std::uint32_t ml_feature_flags, bool enabled) {
    if (!enabled || (ml_feature_flags & kMlFeatureFlag) == 0) {
      return false;
    }
    _bridge.session_started(session);
    {
      std::lock_guard lock {_mutex};
      _capable[session] = true;
      if (!_supervisor.joinable()) {
        // Long-lived: it idles while no session wants updates, so starting and stopping sessions
        // never waits for D-Bus on the caller's (control) thread.
        _supervisor = std::jthread([this](std::stop_token stop) {
          supervise(stop);
        });
      }
    }
    _wake.notify_all();
    BOOST_LOG(info) << "text_context: session "sv << session << " gets text field focus updates"sv;
    return true;
  }

  void service_t::session_stopped(std::uint32_t session) {
    _bridge.session_stopped(session);
    {
      std::lock_guard lock {_mutex};
      if (_capable.erase(session) == 0) {
        return;
      }
      _mouse.erase(session);
      std::erase_if(_touch, [session](const auto &entry) {
        return entry.first.first == session;
      });
    }
    _wake.notify_all();
  }

  void service_t::supervise(std::stop_token stop) {
    std::shared_ptr<source_t> source;
    std::optional<target_t> source_target;
    std::optional<bridge_t::clock::time_point> retry_after;

    const auto stop_source = [&]() {
      {
        std::lock_guard lock {_mutex};
        _source.reset();
        _source_target.reset();
      }
      if (source) {
        source->stop();
        source.reset();
        // Focus seen on the old display means nothing on the new one.
        _bridge.observe_reset();
        BOOST_LOG(info) << "text_context: stopped watching host text fields"sv;
      }
      source_target.reset();
      _wake.notify_all();
    };

    while (!stop.stop_requested()) {
      bool wanted;
      {
        std::lock_guard lock {_mutex};
        wanted = !_capable.empty();
      }

      if (!wanted) {
        if (source || source_target) {
          stop_source();
        }
        retry_after.reset();
      } else {
        const auto target = _target ? _target() : target_t {};
        const auto now = bridge_t::clock::now();

        if (source && (!(source_target == target) || source->broken())) {
          BOOST_LOG(info) << "text_context: "sv << (source->broken() ? "the accessibility connection closed"sv : "capture moved to another display"sv) << "; reconnecting"sv;
          stop_source();
          retry_after.reset();
        } else if (!source && source_target && !(source_target == target)) {
          retry_after.reset();  // A failed display was left: try the new one at once.
        }

        if (!source && (!retry_after || now >= *retry_after) && _factory) {
          std::shared_ptr<source_t> fresh {_factory(target)};
          if (fresh && fresh->start([this](const observation_t &observation) {
                _bridge.observe(observation, _now());
              })) {
            source = std::move(fresh);
            retry_after.reset();
            BOOST_LOG(info) << "text_context: watching text fields on display ["sv << (target.display.empty() ? "$DISPLAY"s : target.display) << "]"sv;
          } else {
            retry_after = now + kRetryPeriod;
            BOOST_LOG(info) << "text_context: no accessibility bus for display ["sv << (target.display.empty() ? "$DISPLAY"s : target.display) << "] yet; retrying in 5 s"sv;
          }
          source_target = target;
          {
            std::lock_guard lock {_mutex};
            _source = source;
            _source_target = target;  // Settled either way: watching, or nothing to watch there now.
          }
          _wake.notify_all();
        }
      }

      std::unique_lock lock {_mutex};
      _wake.wait_for(lock, stop, kSupervisePeriod, [this, wanted] {
        return _capable.empty() == wanted;  // Woken early when sessions come or go.
      });
    }
    stop_source();
  }

  bool service_t::wait_settled(std::chrono::milliseconds timeout) {
    std::unique_lock lock {_mutex};
    return _wake.wait_for(lock, timeout, [this] {
      if (_capable.empty()) {
        return _source == nullptr;
      }
      return _source_target.has_value();
    });
  }

  bool service_t::active() const {
    std::lock_guard lock {_mutex};
    return !_capable.empty();
  }

  bool service_t::source_running() const {
    std::lock_guard lock {_mutex};
    return _source != nullptr;
  }

  std::optional<point_t> service_t::pointer_now(std::uint32_t session) {
    std::shared_ptr<source_t> source;
    {
      std::lock_guard lock {_mutex};
      if (auto it = _mouse.find(session); it != _mouse.end() && it->second.last_move_abs && it->second.abs_last) {
        // The client positioned the pointer absolutely (touch as mouse): use its own coordinates,
        // they do not wait for the injected motion to reach the X server.
        const auto geometry = _bridge.capture_geometry();
        if (geometry.valid()) {
          return point_t {geometry.x + static_cast<std::int32_t>(it->second.abs_last->first * geometry.width), geometry.y + static_cast<std::int32_t>(it->second.abs_last->second * geometry.height)};
        }
      }
      source = _source;
    }
    return source ? source->pointer() : std::nullopt;
  }

  void service_t::set_capture_geometry(geometry_t geometry) {
    _bridge.set_capture_geometry(geometry);
  }

  void service_t::on_input(std::uint32_t session, std::span<const std::uint8_t> packet) {
    if (packet.size() < sizeof(NV_INPUT_HEADER)) {
      return;
    }
    {
      std::lock_guard lock {_mutex};
      if (!_capable.contains(session)) {
        return;
      }
    }
    const auto *p = packet.data();
    const auto magic = get_u32(p + 4);
    const auto now = _now();
    const auto geometry = _bridge.capture_geometry();

    switch (magic) {
      case MOUSE_BUTTON_DOWN_EVENT_MAGIC_GEN5:
      case MOUSE_BUTTON_UP_EVENT_MAGIC_GEN5:
        {
          if (packet.size() < sizeof(NV_MOUSE_BUTTON_PACKET)) {
            return;
          }
          const auto button = p[offsetof(NV_MOUSE_BUTTON_PACKET, button)];
          const bool release = magic == MOUSE_BUTTON_UP_EVENT_MAGIC_GEN5;
          if (button != BUTTON_LEFT) {
            if (!release) {
              // A right or middle press during a left gesture is not a click on a field.
              _bridge.on_pointer(session, cause_e::remote_mouse, phase_e::cancel, 0, std::nullopt, 0, now);
            }
            return;
          }
          std::int32_t moved = 0;
          {
            std::lock_guard lock {_mutex};
            auto &track = _mouse[session];
            if (!release) {
              track.held = true;
              track.rel = 0;
              track.abs_start = track.abs_last;
              track.abs_moved = 0;
            } else {
              moved = static_cast<std::int32_t>(std::min<std::int64_t>(std::max<std::int64_t>(track.rel, track.abs_moved), INT32_MAX));
              track.held = false;
            }
          }
          _bridge.on_pointer(session, cause_e::remote_mouse, release ? phase_e::up : phase_e::down, 0, pointer_now(session), moved, now);
          return;
        }
      case MOUSE_MOVE_REL_MAGIC_GEN5:
        {
          if (packet.size() < sizeof(NV_REL_MOUSE_MOVE_PACKET)) {
            return;
          }
          std::lock_guard lock {_mutex};
          auto &track = _mouse[session];
          track.last_move_abs = false;
          if (track.held) {
            track.rel += std::abs(be16(p + offsetof(NV_REL_MOUSE_MOVE_PACKET, deltaX))) +
                         std::abs(be16(p + offsetof(NV_REL_MOUSE_MOVE_PACKET, deltaY)));
          }
          return;
        }
      case MOUSE_MOVE_ABS_MAGIC:
        {
          if (packet.size() < sizeof(NV_ABS_MOUSE_MOVE_PACKET)) {
            return;
          }
          const auto w = be16(p + offsetof(NV_ABS_MOUSE_MOVE_PACKET, width));
          const auto h = be16(p + offsetof(NV_ABS_MOUSE_MOVE_PACKET, height));
          if (w <= 0 || h <= 0) {
            return;
          }
          const float fx = static_cast<float>(be16(p + offsetof(NV_ABS_MOUSE_MOVE_PACKET, x))) / w;
          const float fy = static_cast<float>(be16(p + offsetof(NV_ABS_MOUSE_MOVE_PACKET, y))) / h;
          std::lock_guard lock {_mutex};
          auto &track = _mouse[session];
          track.abs_last = std::pair {std::clamp(fx, 0.0f, 1.0f), std::clamp(fy, 0.0f, 1.0f)};
          track.last_move_abs = true;
          if (!track.held) {
            return;
          }
          if (!track.abs_start) {
            track.abs_start = std::pair {fx, fy};
            return;
          }
          const auto dx = std::abs(fx - track.abs_start->first) * static_cast<float>(geometry.width ? geometry.width : 1920);
          const auto dy = std::abs(fy - track.abs_start->second) * static_cast<float>(geometry.height ? geometry.height : 1080);
          track.abs_moved = std::max(track.abs_moved, static_cast<std::int32_t>(std::hypot(dx, dy)));
          return;
        }
      case SCROLL_MAGIC_GEN5:
      case SS_HSCROLL_MAGIC:
        _bridge.on_pointer(session, cause_e::remote_mouse, phase_e::cancel, 0, std::nullopt, 0, now);
        return;
      case SS_TOUCH_MAGIC:
      case SS_PEN_MAGIC:
        {
          std::uint8_t event_type;
          std::uint32_t pointer_id = 0;
          float fx;
          float fy;
          if (magic == SS_TOUCH_MAGIC) {
            if (packet.size() < sizeof(SS_TOUCH_PACKET)) {
              return;
            }
            event_type = p[offsetof(SS_TOUCH_PACKET, eventType)];
            pointer_id = get_u32(p + offsetof(SS_TOUCH_PACKET, pointerId));
            fx = le_float(p + offsetof(SS_TOUCH_PACKET, x));
            fy = le_float(p + offsetof(SS_TOUCH_PACKET, y));
          } else {
            if (packet.size() < sizeof(SS_PEN_PACKET)) {
              return;
            }
            event_type = p[offsetof(SS_PEN_PACKET, eventType)];
            pointer_id = 0xFFFF'FFFF;  // One pen; keep it apart from finger ids.
            fx = le_float(p + offsetof(SS_PEN_PACKET, x));
            fy = le_float(p + offsetof(SS_PEN_PACKET, y));
          }
          if (!std::isfinite(fx) || !std::isfinite(fy)) {
            return;
          }
          fx = std::clamp(fx, 0.0f, 1.0f);
          fy = std::clamp(fy, 0.0f, 1.0f);
          std::optional<point_t> point;
          if (geometry.valid()) {
            point = point_t {geometry.x + static_cast<std::int32_t>(fx * geometry.width), geometry.y + static_cast<std::int32_t>(fy * geometry.height)};
          }
          const auto key = std::pair {session, pointer_id};
          std::int32_t moved = 0;
          {
            std::lock_guard lock {_mutex};
            if (event_type == LI_TOUCH_EVENT_DOWN) {
              _touch[key] = touch_track_t {fx, fy};
            } else if (auto it = _touch.find(key); it != _touch.end()) {
              const auto dx = (fx - it->second.x) * static_cast<float>(geometry.width);
              const auto dy = (fy - it->second.y) * static_cast<float>(geometry.height);
              moved = static_cast<std::int32_t>(std::hypot(dx, dy));
              if (event_type != LI_TOUCH_EVENT_MOVE) {
                _touch.erase(it);
              }
            }
            if (event_type == LI_TOUCH_EVENT_CANCEL_ALL) {
              std::erase_if(_touch, [session](const auto &entry) {
                return entry.first.first == session;
              });
            }
          }
          switch (event_type) {
            case LI_TOUCH_EVENT_DOWN:
              _bridge.on_pointer(session, cause_e::remote_touch, phase_e::down, pointer_id, point, 0, now);
              break;
            case LI_TOUCH_EVENT_MOVE:
              _bridge.on_pointer(session, cause_e::remote_touch, phase_e::move, pointer_id, point, moved, now);
              break;
            case LI_TOUCH_EVENT_UP:
              _bridge.on_pointer(session, cause_e::remote_touch, phase_e::up, pointer_id, point, moved, now);
              break;
            case LI_TOUCH_EVENT_CANCEL:
              _bridge.on_pointer(session, cause_e::remote_touch, phase_e::cancel, pointer_id, std::nullopt, 0, now);
              break;
            default:
              break;
          }
          return;
        }
      default:
        return;
    }
  }

  std::vector<outbound_t> service_t::drain_outbound() {
    _bridge.tick(_now());
    return _bridge.drain();
  }

  // ---- Process-wide instance ----------------------------------------------

#ifndef __linux__
  std::unique_ptr<source_t> make_platform_source(const target_t &) {
    return nullptr;
  }

  bool platform_source_available(const target_t &) {
    return false;
  }

  target_t capture_target() {
    return {};
  }
#endif

  bool available() {
    if (!config::input.remote_text_context || !config::input.mouse) {
      return false;
    }
    // Probing costs a D-Bus round trip; cache it per display.
    static std::mutex mutex;
    static std::optional<bool> cached;
    static target_t cached_for;
    static bridge_t::clock::time_point checked;
    const auto target = capture_target();
    std::lock_guard lock {mutex};
    const auto now = bridge_t::clock::now();
    if (!cached || !(cached_for == target) || now - checked > 30s) {
      cached = platform_source_available(target);
      cached_for = target;
      checked = now;
      if (!*cached) {
        BOOST_LOG(info) << "text_context: no accessibility bus for display ["sv << (target.display.empty() ? "$DISPLAY"s : target.display) << "]; not offering remote text context"sv;
      }
    }
    return *cached;
  }

  service_t &service() {
    static service_t instance {
      [](const target_t &target) {
        return make_platform_source(target);
      },
      [] {
        return bridge_t::clock::now();
      },
      [] {
        return capture_target();
      }
    };
    return instance;
  }

}  // namespace text_context
