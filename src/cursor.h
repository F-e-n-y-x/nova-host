/**
 * @file src/cursor.h
 * @brief Local cursor (control message 21, type 0x5509): cursor shapes for client-side drawing.
 *
 * With the local cursor the client draws the pointer itself, so it moves the moment the finger or
 * mouse moves instead of one stream latency later. The host then leaves the cursor out of the video
 * and sends the cursor's shape and visibility instead. Clients track the position themselves (they
 * are the ones moving the pointer), so the wire carries no position.
 *
 * Wire compatibility: the qiin2333 moonlight-common-c fork used by Moonlight V+ and Nebula
 * (`ControlStream.c` `LiSetCursorMode`, `CursorStream.c`). The protocol facts are reimplemented
 * here; no code is copied.
 *
 * - The host advertises bit 0x40 (`LI_FF_CURSOR_SHAPE`) in `x-ss-general.featureFlags`.
 * - Client to host: `{u8 version = 1, u8 mode (0 video, 1 local), u8 0, u8 0}`.
 * - Host to client, reliable, one or more packets per update (little-endian):
 *
 *       u8  version = 1
 *       u8  flags        0x01 shape present, 0x02 cursor visible
 *       u16 header_size  = 28
 *       u32 shape_id
 *       u16 width        0 without a shape, else 1..256
 *       u16 height       0 without a shape, else 1..256
 *       i16 hotspot_x    0 <= x < width
 *       i16 hotspot_y    0 <= y < height
 *       u32 total_size   width * height * 4 (0 without a shape)
 *       u32 offset       byte offset of this chunk (chunks arrive in order)
 *       u16 chunk_size   == packet length - 28
 *       u16 reserved     0
 *       bytes            chunk_size bytes of tightly packed, straight-alpha BGRA
 *
 *   A packet without the shape flag switches the client to an already sent shape id (it caches
 *   them) and/or changes visibility. Every chunk of one shape repeats the same header fields.
 *
 * Shapes are sent at stream scale: the host scales the desktop's cursor by the same factor the
 * encoder scales the desktop, so the client only has to apply its own video scaling.
 *
 * Source: an X11 XFixes watcher on the captured X display (see make_x11_source()): the Virtual
 * display's own X server `:N` while a Virtual display session runs (`virtual_display::capture_target()`),
 * otherwise the desktop (`$DISPLAY`, Mirror). The watcher checks every second and follows the
 * capture when it moves. Tests replace the choice with set_target_provider().
 */
#pragma once

// standard includes
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cursor {

  constexpr std::uint8_t kWireVersion = 1;  ///< Protocol version byte (`CURSOR_STREAM_PROTOCOL_VERSION`).
  constexpr std::uint8_t kFlagShape = 0x01;  ///< Packet carries shape pixels.
  constexpr std::uint8_t kFlagVisible = 0x02;  ///< The cursor is visible.
  constexpr std::size_t kHeaderSize = 28;  ///< Fixed header length of every host packet.
  constexpr std::uint16_t kMaxDimension = 256;  ///< Largest width or height a client accepts.
  constexpr std::size_t kChunkBytes = 16 * 1024;  ///< Pixel bytes per packet (the control frame limit is ~64 KiB).
  constexpr std::uint32_t kFeatureFlag = 0x40;  ///< `LI_FF_CURSOR_SHAPE` bit in `x-ss-general.featureFlags`.
  constexpr std::size_t kClientCacheBudget = 4 * 1024 * 1024;  ///< Shape bytes the host assumes a client still caches (V+ keeps 8 MiB).

  /**
   * @brief Who draws the cursor.
   */
  enum class mode_e : std::uint8_t {
    video = 0,  ///< Composited into the video (the default).
    local = 1,  ///< Drawn by the client from shape updates.
  };

  /**
   * @brief Parse a client mode request (payload of a 0x5509 packet from the client).
   *
   * @param data Payload bytes (after the control header).
   * @param size Payload length.
   * @return The requested mode, or std::nullopt for a malformed request or unknown version/mode.
   */
  std::optional<mode_e> parse_mode_request(const std::uint8_t *data, std::size_t size);

  /**
   * @brief A cursor image in the wire's pixel format.
   */
  struct image_t {
    std::uint16_t width = 0;  ///< Width in pixels.
    std::uint16_t height = 0;  ///< Height in pixels.
    std::int16_t hotspot_x = 0;  ///< Hotspot column.
    std::int16_t hotspot_y = 0;  ///< Hotspot row.
    std::vector<std::uint8_t> bgra;  ///< width * height * 4 bytes, straight (not premultiplied) alpha.

    /**
     * @brief Compare two images field by field.
     * @param other Image to compare with.
     * @return True when size, hotspot and pixels match.
     */
    bool operator==(const image_t &other) const = default;
  };

  /**
   * @brief Build an image from premultiplied 32-bit ARGB pixels (the XFixes format).
   *
   * Out-of-range hotspots are clamped into the image.
   *
   * @param argb Pixels, row-major, `0xAARRGGBB` in the low 32 bits of each element.
   * @param width Width in pixels.
   * @param height Height in pixels.
   * @param hotspot_x Hotspot column.
   * @param hotspot_y Hotspot row.
   * @return The image, or std::nullopt when the size is zero or implausibly large (over 4096).
   */
  std::optional<image_t> from_premultiplied_argb(const std::uint32_t *argb, int width, int height, int hotspot_x, int hotspot_y);

  /**
   * @brief The standard arrow pointer (black outline, white fill, hotspot at the tip).
   *
   * Stands in for a cursor the X server won't hand out (its owner has exited).
   *
   * @param height Height to draw it at, like the desktop's other cursors; <= 0 means 24.
   * @return The arrow, as tall as asked (within 8..256 pixels).
   */
  image_t fallback_arrow(int height);

  /**
   * @brief Whether an image draws nothing (every pixel fully transparent), as a hidden cursor does.
   *
   * @param image Image to check.
   * @return True for an empty or fully transparent image.
   */
  bool is_blank(const image_t &image);

  /**
   * @brief Scale an image for the stream and fit it into the client's 256 x 256 limit.
   *
   * Area-averaged in premultiplied space, so edges don't pick up dark fringes.
   *
   * @param image Source image.
   * @param scale Stream pixels per desktop pixel; values <= 0 mean 1.
   * @return The scaled image (a copy when nothing changes).
   */
  image_t fit(const image_t &image, double scale);

  /**
   * @brief Content id for a shape: equal images get equal ids, so the client's cache can be reused.
   *
   * @param image Image to identify.
   * @return A 32-bit hash of size, hotspot and pixels (never 0).
   */
  std::uint32_t shape_id(const image_t &image);

  /**
   * @brief Encode a full shape update, split into chunks.
   *
   * @param id Shape id (see shape_id()).
   * @param image Shape; must be 1..256 on each side with the hotspot inside.
   * @param visible Whether the cursor is visible.
   * @param chunk_bytes Pixel bytes per packet (> 0).
   * @return Packets in send order, or none for an invalid image.
   */
  std::vector<std::vector<std::uint8_t>> encode_shape(std::uint32_t id, const image_t &image, bool visible, std::size_t chunk_bytes = kChunkBytes);

  /**
   * @brief Encode a state-only update: use cached shape @p id and set visibility.
   *
   * @param id Shape id the client should show (0 when none was ever sent).
   * @param visible Whether the cursor is visible.
   * @return One 28-byte packet.
   */
  std::vector<std::uint8_t> encode_state(std::uint32_t id, bool visible);

  /**
   * @brief What the host cursor looks like right now, as published by the watcher.
   */
  struct snapshot_t {
    std::uint64_t serial = 0;  ///< Increments on every change; 0 = nothing captured yet.
    std::shared_ptr<const image_t> image;  ///< Latest shape at desktop scale (null when hidden or unknown).
    bool visible = false;  ///< False while the cursor is hidden (blank shape).
  };

  /**
   * @brief Per-session cursor state: the mode the client asked for and what it already has.
   *
   * Not thread safe; the control thread owns it.
   */
  class sender_t {
  public:
    /**
     * @brief Apply the mode the client asked for.
     *
     * Switching to local forgets which shapes the client has, so the next update() sends the
     * current shape in full.
     *
     * @param mode Requested mode.
     */
    void set_mode(mode_e mode);

    /**
     * @brief The client's current mode.
     * @return Mode.
     */
    mode_e mode() const {
      return mode_;
    }

    /**
     * @brief Packets that bring the client up to date with @p snapshot.
     *
     * Returns nothing in video mode, before the first capture, and when the client already shows
     * this shape and visibility. A shape the client has cached is referenced by id without pixels.
     *
     * @param snapshot Current host cursor.
     * @param scale Stream pixels per desktop pixel.
     * @return Packets to send, in order.
     */
    std::vector<std::vector<std::uint8_t>> update(const snapshot_t &snapshot, double scale);

  private:
    /**
     * @brief Whether the client still caches shape @p id (and mark it recently used).
     * @param id Shape id.
     * @return True when the shape was sent within the cache budget.
     */
    bool known(std::uint32_t id);

    /**
     * @brief Record that shape @p id was sent, evicting the oldest beyond the budget.
     * @param id Shape id.
     * @param bytes Pixel bytes of the shape.
     */
    void remember(std::uint32_t id, std::size_t bytes);

    mode_e mode_ = mode_e::video;  ///< Mode the client asked for.
    bool dirty_ = true;  ///< Send the state even if the snapshot didn't change.
    std::uint64_t last_serial_ = 0;  ///< Snapshot serial handled last.
    double last_scale_ = 0;  ///< Video scale handled last.
    std::optional<std::uint32_t> shown_id_;  ///< Shape the client shows (or would show).
    std::optional<bool> shown_visible_;  ///< Visibility the client has.
    std::list<std::pair<std::uint32_t, std::size_t>> known_;  ///< Shapes sent (id, bytes), most recent first.
    std::unordered_map<std::uint32_t, std::list<std::pair<std::uint32_t, std::size_t>>::iterator> known_index_;  ///< Index into known_.
    std::size_t known_bytes_ = 0;  ///< Sum of bytes in known_.
    std::shared_ptr<const image_t> scaled_source_;  ///< Image scaled_ was made from.
    double scaled_for_ = 0;  ///< Scale scaled_ was made for.
    image_t scaled_;  ///< Current shape at stream scale.
    std::uint32_t scaled_id_ = 0;  ///< Id of scaled_.
  };

  /**
   * @brief The X display the cursor is read from.
   */
  struct target_t {
    std::string display;  ///< DISPLAY value, e.g. ":0"; empty = `$DISPLAY`.
    std::string xauthority;  ///< Cookie file; empty = `$XAUTHORITY`.

    /**
     * @brief Compare two targets.
     * @param other Target to compare with.
     * @return True when both fields match.
     */
    bool operator==(const target_t &other) const = default;
  };

  /**
   * @brief A platform cursor source (XFixes on X11; fakes in tests).
   */
  class source_t {
  public:
    virtual ~source_t() = default;

    /**
     * @brief Wait up to @p timeout for the cursor to change.
     *
     * The first call returns the current cursor at once.
     *
     * @param timeout Longest wait.
     * @param broken Set to true when the source failed for good (display gone).
     * @return The new cursor image when it changed, else std::nullopt.
     */
    virtual std::optional<image_t> poll(std::chrono::milliseconds timeout, bool &broken) = 0;
  };

  using source_factory_t = std::function<std::unique_ptr<source_t>(const target_t &)>;  ///< Opens a source for a target, or returns null.

  /**
   * @brief Open an XFixes cursor source on @p target.
   *
   * @param target X display to watch.
   * @return The source, or null when X11/XFixes is unavailable (or not built in).
   */
  std::unique_ptr<source_t> make_x11_source(const target_t &target);

  /**
   * @brief Whether local cursor can be offered right now: the `local_cursor` setting is on and a
   *        source opens on the current target.
   * @return True when the feature flag and capability should be advertised.
   */
  bool available();

  /**
   * @brief A session entered local mode: start watching and take the cursor out of the video.
   */
  void acquire();

  /**
   * @brief A session left local mode or ended: stop when it was the last one.
   */
  void release();

  /**
   * @brief Drop every acquisition (the control thread is exiting).
   */
  void release_all();

  /**
   * @brief Whether any session currently uses local mode.
   * @return True while at least one acquisition is held.
   */
  bool local_active();

  /**
   * @brief The latest cursor published by the watcher.
   * @return Snapshot (serial 0 before the first capture).
   */
  snapshot_t current();

  /**
   * @brief Record how the encoder scales the desktop (stream pixels per desktop pixel).
   * @param scale Scale factor; values <= 0 are ignored.
   */
  void set_video_scale(double scale);

  /**
   * @brief The last recorded video scale.
   * @return Scale factor (1 until the capture reports one).
   */
  double video_scale();

  /**
   * @brief The user's own cursor toggle (Ctrl+Alt+Shift+N) flipped.
   *
   * The video shows the cursor when the user wants it and no session draws it locally.
   */
  void toggle_user_composite();

  /**
   * @brief Choose which X display the cursor is read from (tests).
   * @param provider Returns the current capture target; null restores the default (the Virtual
   *                 display while one runs, else `$DISPLAY`).
   */
  void set_target_provider(std::function<target_t()> provider);

  /**
   * @brief Replace the source factory (tests).
   * @param factory Factory to use; null restores make_x11_source().
   */
  void set_source_factory(source_factory_t factory);

}  // namespace cursor
