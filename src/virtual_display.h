/**
 * @file src/virtual_display.h
 * @brief Declarations for Nova's separate headless X server ("Virtual display" mode).
 *
 * A Virtual display stream gets its own X server (`Xorg :N -seat seat-nova`) on the same NVIDIA GPU
 * as the desktop, with no outputs (NoScanout) and a screen at exactly the client's size. The app runs
 * there under a small window manager and NvFBC captures that screen, so the user's desktop on `:0`
 * keeps its resolution, windows, pointer and keyboard focus.
 */
#pragma once

// standard includes
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace virtual_display {
  /**
   * @brief An X display that capture and the app should use instead of the desktop.
   */
  struct target_t {
    std::string display;  ///< DISPLAY value, e.g. ":20".
    std::string xauthority;  ///< Cookie file for that display.
    int width = 0;  ///< Current screen width in pixels.
    int height = 0;  ///< Current screen height in pixels.
    int fps = 0;  ///< Frame rate the display was created for (the client's).
  };

  /**
   * @brief Make capture (NvFBC) use this display, or the desktop again when empty.
   *
   * @param target The virtual display, or std::nullopt for the desktop.
   */
  void set_capture_target(std::optional<target_t> target);

  /**
   * @brief The display capture should use right now.
   *
   * @return The virtual display, or std::nullopt when capture uses the desktop.
   */
  std::optional<target_t> capture_target();

  /**
   * @brief Point DISPLAY and XAUTHORITY at a display for the lifetime of this object.
   *
   * NvFBC has no display parameter: it connects to `$DISPLAY` when its handle is created. Every
   * override is serialised by one process-wide mutex, held until the destructor restores the
   * previous values, so two captures never see each other's display. Other threads that read the
   * environment during that window are not covered; Nova's own X users read it at startup.
   * With no target the object only takes the lock.
   */
  class scoped_x_env_t {
  public:
    /**
     * @brief Apply the override.
     *
     * @param target Display to point at, or std::nullopt to leave the environment alone.
     */
    explicit scoped_x_env_t(const std::optional<target_t> &target);
    ~scoped_x_env_t();

    scoped_x_env_t(const scoped_x_env_t &) = delete;
    scoped_x_env_t &operator=(const scoped_x_env_t &) = delete;

  private:
    std::unique_lock<std::recursive_mutex> lock_;
    bool applied_ = false;
    std::optional<std::string> old_display_;
    std::optional<std::string> old_xauthority_;
  };

  /**
   * @brief NvFBC display names, with one entry for a headless screen.
   *
   * NvFBC reports no outputs on a screen without heads (Nova's virtual display), so a list built
   * per output is empty and Nova would reject NvFBC. Such a screen is still capturable as a whole
   * (NVFBC_TRACKING_SCREEN), so it gets the single name "0" (upstream Sunshine #5707).
   *
   * @param names Names built from the outputs.
   * @param outputs NvFBC dwOutputNum.
   * @param screen_width NvFBC screenSize.w.
   * @param screen_height NvFBC screenSize.h.
   * @return The names, or {"0"} for a headless screen with a size.
   */
  std::vector<std::string> nvfbc_names_with_headless_fallback(std::vector<std::string> names, unsigned outputs, unsigned screen_width, unsigned screen_height);

  /**
   * @brief Size and rate for a virtual display.
   */
  struct mode_t {
    int width = 0;  ///< Width in pixels.
    int height = 0;  ///< Height in pixels.
    int fps = 0;  ///< Refresh the stream asked for (the display itself has no vblank).
  };

  using env_list_t = std::vector<std::pair<std::string, std::string>>;  ///< Extra environment for a child.

  /**
   * @brief Process and system operations used by x_server_t (replaced by fakes in tests).
   */
  struct ops_t {
    /**
     * @brief Start the X server and wait until it accepts connections.
     * Arguments: argv, extra env, log file, timeout in ms. Returns the pid, or -1.
     */
    std::function<long(const std::vector<std::string> &, const env_list_t &, const std::filesystem::path &, int)> spawn_server;
    /**
     * @brief Start a background process (the window manager) in its own session. Returns the pid, or -1.
     */
    std::function<long(const std::vector<std::string> &, const env_list_t &, const std::filesystem::path &)> spawn;
    /**
     * @brief Run a helper (xrandr) to completion with a timeout. Returns its exit code, or -1.
     */
    std::function<int(const std::vector<std::string> &, const env_list_t &)> run;
    std::function<bool(long)> alive;  ///< Whether a process is still running (reaps our own children).
    std::function<void(long, int)> signal;  ///< Send a signal to a process group (falls back to the process).
    std::function<std::string(long)> cmdline;  ///< A process's command line with spaces between arguments, or empty.
    std::function<bool(int)> display_taken;  ///< Whether an X display number is in use (socket or lock file).
    std::function<std::string()> cookie;  ///< 32 hex characters of fresh randomness.
    std::function<std::string()> gpu_bus_id;  ///< Xorg BusID of the NVIDIA GPU (e.g. "PCI:8:0:0"), or empty.
    std::function<std::string()> xorg_path;  ///< Path of the Xorg server binary, or empty.
    std::function<void(int)> sleep_ms;  ///< Wait between stop checks.
  };

  /**
   * @brief The real operations (fork/exec, /proc, /tmp/.X11-unix, /proc/driver/nvidia).
   *
   * @return Operations for the running system.
   */
  ops_t default_ops();

  /**
   * @brief Convert a PCI slot name to an Xorg BusID.
   *
   * @param slot Slot as in /proc/driver/nvidia/gpus, e.g. "0000:08:00.0" (hex fields).
   * @return "PCI:8:0:0" (decimal, "@domain" added when the domain is not 0), or empty when malformed.
   */
  std::string bus_id_from_pci_slot(std::string_view slot);

  /**
   * @brief One Xauthority file record (FamilyWild) for an MIT-MAGIC-COOKIE-1.
   *
   * @param display_number X display number.
   * @param cookie_hex 32 hex characters.
   * @return The binary record.
   */
  std::string xauth_entry(int display_number, std::string_view cookie_hex);

  /**
   * @brief Render the xorg.conf for a headless NVIDIA screen.
   *
   * @param bus_id Xorg BusID of the GPU.
   * @param width Screen width.
   * @param height Screen height.
   * @return Config file text.
   */
  std::string render_xorg_conf(const std::string &bus_id, int width, int height);

  /**
   * @brief First free display number from `first` to 99, or -1.
   *
   * @param ops Operations used to check each number.
   * @param first Lowest number to try.
   * @return The display number, or -1 when all are taken.
   */
  int pick_display_number(const ops_t &ops, int first = 20);

  /**
   * @brief Owns one headless X server and its window manager.
   */
  class x_server_t {
  public:
    /**
     * @brief Create the owner; nothing starts until start().
     *
     * @param state_dir Directory for the config, cookie, log and crash marker (created 0700).
     * @param wm Window manager command run on the display ("openbox"), or empty for none.
     * @param ops Process operations.
     */
    x_server_t(std::filesystem::path state_dir, std::string wm, ops_t ops);

    /**
     * @brief Start a new display at the given size.
     *
     * @param mode Client size and rate.
     * @return The display, or std::nullopt when it couldn't start (everything started is stopped again).
     */
    std::optional<target_t> start(const mode_t &mode);

    /**
     * @brief Change the screen size of the running display (reconnect at a new size).
     *
     * @param mode New size and rate.
     * @return True on success.
     */
    bool resize(const mode_t &mode);

    /**
     * @brief Stop the window manager and the server (SIGTERM, SIGKILL after 2 s) and remove the state.
     */
    void stop();

    /**
     * @brief Kill a display a previous run left behind (crash or kill -9), using the marker file.
     *
     * Only processes whose command line still matches what was recorded are signalled, so a
     * recycled pid is never touched.
     *
     * @return True if a leftover marker was found and cleaned up.
     */
    bool recover();

    /**
     * @brief Whether a display is running.
     *
     * @return True between a successful start() and stop().
     */
    bool running() const;

    /**
     * @brief The running display, if any.
     *
     * @return The display, or std::nullopt.
     */
    std::optional<target_t> target() const;

    /**
     * @brief Path of the crash-recovery marker.
     *
     * @return Marker path inside the state directory.
     */
    std::filesystem::path marker() const;

  private:
    bool set_screen_size(const mode_t &mode);
    void write_marker() const;
    void terminate(long pid, const std::string &expect);

    std::filesystem::path state_dir_;
    std::string wm_;
    ops_t ops_;
    std::optional<target_t> target_;
    int number_ = -1;
    long server_pid_ = -1;
    long wm_pid_ = -1;
  };

  /**
   * @brief Environment for an app launched on a virtual display.
   *
   * @param target The display.
   * @param fps_cap Frame cap: 0 uses the client's rate, a positive value is used as is, negative disables it.
   * @param pulse_sink Nova's capture sink the app should play to, or empty to leave audio alone.
   * @param base_ld_preload LD_PRELOAD the app would otherwise get (kept, the limiter is appended).
   * @param mangohud_gl Path spec of MangoHud's OpenGL library, or empty when MangoHud isn't installed.
   * @return Variables to set; the frame cap variables are only present when a cap applies.
   */
  env_list_t app_env(const target_t &target, int fps_cap, const std::string &pulse_sink, const std::string &base_ld_preload, const std::string &mangohud_gl);

  /**
   * @brief Every variable app_env() may set, so a later desktop launch can put them back.
   *
   * @return Variable names.
   */
  const std::vector<std::string> &app_env_keys();

  /**
   * @brief MangoHud's OpenGL preload spec if MangoHud is installed.
   *
   * @return "/usr/$LIB/mangohud/libMangoHud_opengl.so" (ld.so picks 32 or 64 bit), or empty.
   */
  std::string mangohud_gl_preload();

  /**
   * @brief Name suffix of Nova's input devices while a virtual display is active.
   *
   * The packaged udev rule (61-nova-host-vd-seat.rules) gives devices with this suffix
   * `ID_SEAT=seat-nova`, so the desktop's X server ignores them and the virtual one takes them.
   */
  inline constexpr std::string_view input_name_suffix = " (Nova VD)";

  /**
   * @brief Whether the seat rule for Nova's virtual-display input devices is installed.
   *
   * @param roots Directories to look in (defaults to the udev rule directories).
   * @return True when 61-nova-host-vd-seat.rules exists in one of them.
   */
  bool seat_rule_installed(const std::vector<std::filesystem::path> &roots = {});
}  // namespace virtual_display
