/**
 * @file tests/unit/platform/linux/x_cursor_owner.h
 * @brief Test helper: a live X client that sets the root window's cursor on a spare X server.
 *
 * The X server only hands out the pixels of a cursor while the client that made it is still
 * connected, as with real applications, so the live cursor tests keep this client open instead of
 * running xsetroot (which exits at once). It changes a window attribute only; no input is sent.
 */
#pragma once

#if defined(__linux__) && defined(SUNSHINE_BUILD_X11)
  // standard includes
  #include <optional>
  #include <string>

  // platform includes
  #include <X11/cursorfont.h>
  #include <X11/Xlib.h>

  // local includes
  #include <src/virtual_display.h>

/**
 * @brief Owns an X connection and the cursors it defines on the root window.
 */
class x_cursor_owner_t {
public:
  /**
   * @brief Connect to @p display (never :0 in tests).
   * @param display DISPLAY value.
   * @param xauthority Cookie file, or empty.
   */
  x_cursor_owner_t(const std::string &display, const std::string &xauthority) {
    std::optional<virtual_display::target_t> env;
    if (!xauthority.empty()) {
      env = virtual_display::target_t {display, xauthority};
    }
    virtual_display::scoped_x_env_t scope {env};
    dpy_ = XOpenDisplay(display.c_str());
  }

  ~x_cursor_owner_t() {
    if (dpy_) {
      XCloseDisplay(dpy_);
    }
  }

  x_cursor_owner_t(const x_cursor_owner_t &) = delete;
  x_cursor_owner_t &operator=(const x_cursor_owner_t &) = delete;

  /**
   * @brief Whether the connection opened.
   * @return True when connected.
   */
  explicit operator bool() const {
    return dpy_ != nullptr;
  }

  /**
   * @brief Show a cursor-font shape (XC_*) on the root window, keeping it owned by this client.
   * @param shape Cursor font glyph, e.g. XC_hand2.
   */
  void show(unsigned int shape) {
    Cursor c = XCreateFontCursor(dpy_, shape);
    XDefineCursor(dpy_, DefaultRootWindow(dpy_), c);
    XSync(dpy_, False);
  }

private:
  Display *dpy_ = nullptr;
};
#endif
