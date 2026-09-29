/**
 * @file src/platform/linux/cursor_x11.cpp
 * @brief XFixes cursor-shape source for the local cursor (see src/cursor.h).
 *
 * Uses XCB rather than Xlib: when an X server goes away (a Virtual display session ending), Xlib's
 * default I/O error handler exits the process, while XCB just reports a broken connection.
 * libxcb and libxcb-xfixes are loaded at run time like the rest of the X11 code.
 */
// standard includes
#include <cerrno>
#include <cstdlib>
#include <mutex>
#include <optional>

// platform includes
#include <poll.h>
#include <xcb/xcb.h>
#include <xcb/xfixes.h>

// local includes
#include "misc.h"
#include "src/cursor.h"
#include "src/logging.h"
#include "src/virtual_display.h"

using namespace std::literals;

namespace cursor {
  namespace {
    /// @cond NOVA_INTERNAL
    struct xcb_api_t {
      xcb_connection_t *(*connect)(const char *, int *) = nullptr;
      void (*disconnect)(xcb_connection_t *) = nullptr;
      int (*connection_has_error)(xcb_connection_t *) = nullptr;
      const xcb_setup_t *(*get_setup)(xcb_connection_t *) = nullptr;
      xcb_screen_iterator_t (*setup_roots_iterator)(const xcb_setup_t *) = nullptr;
      void (*screen_next)(xcb_screen_iterator_t *) = nullptr;
      int (*get_file_descriptor)(xcb_connection_t *) = nullptr;
      xcb_generic_event_t *(*poll_for_event)(xcb_connection_t *) = nullptr;
      int (*flush)(xcb_connection_t *) = nullptr;
      const xcb_query_extension_reply_t *(*get_extension_data)(xcb_connection_t *, xcb_extension_t *) = nullptr;

      xcb_extension_t *xfixes_id = nullptr;
      xcb_xfixes_query_version_cookie_t (*xfixes_query_version)(xcb_connection_t *, uint32_t, uint32_t) = nullptr;
      xcb_xfixes_query_version_reply_t *(*xfixes_query_version_reply)(xcb_connection_t *, xcb_xfixes_query_version_cookie_t, xcb_generic_error_t **) = nullptr;
      xcb_void_cookie_t (*xfixes_select_cursor_input)(xcb_connection_t *, xcb_window_t, uint32_t) = nullptr;
      xcb_xfixes_get_cursor_image_cookie_t (*xfixes_get_cursor_image)(xcb_connection_t *) = nullptr;
      xcb_xfixes_get_cursor_image_reply_t *(*xfixes_get_cursor_image_reply)(xcb_connection_t *, xcb_xfixes_get_cursor_image_cookie_t, xcb_generic_error_t **) = nullptr;
      uint32_t *(*xfixes_get_cursor_image_cursor_image)(const xcb_xfixes_get_cursor_image_reply_t *) = nullptr;
    };

    /// @endcond

    /**
     * @brief Load libxcb and libxcb-xfixes once.
     * @return The entry points, or null when either library is missing.
     */
    const xcb_api_t *api() {
      static std::once_flag once;
      static std::optional<xcb_api_t> loaded;
      std::call_once(once, [] {
        void *xcb = dyn::handle({"libxcb.so.1", "libxcb.so"});
        void *fixes = dyn::handle({"libxcb-xfixes.so.0", "libxcb-xfixes.so"});
        if (!xcb || !fixes) {
          return;
        }
        xcb_api_t a;
        std::vector<std::tuple<dyn::apiproc *, const char *>> core {
          {(dyn::apiproc *) &a.connect, "xcb_connect"},
          {(dyn::apiproc *) &a.disconnect, "xcb_disconnect"},
          {(dyn::apiproc *) &a.connection_has_error, "xcb_connection_has_error"},
          {(dyn::apiproc *) &a.get_setup, "xcb_get_setup"},
          {(dyn::apiproc *) &a.setup_roots_iterator, "xcb_setup_roots_iterator"},
          {(dyn::apiproc *) &a.screen_next, "xcb_screen_next"},
          {(dyn::apiproc *) &a.get_file_descriptor, "xcb_get_file_descriptor"},
          {(dyn::apiproc *) &a.poll_for_event, "xcb_poll_for_event"},
          {(dyn::apiproc *) &a.flush, "xcb_flush"},
          {(dyn::apiproc *) &a.get_extension_data, "xcb_get_extension_data"},
        };
        std::vector<std::tuple<dyn::apiproc *, const char *>> ext {
          {(dyn::apiproc *) &a.xfixes_id, "xcb_xfixes_id"},  // a data symbol: the extension descriptor
          {(dyn::apiproc *) &a.xfixes_query_version, "xcb_xfixes_query_version"},
          {(dyn::apiproc *) &a.xfixes_query_version_reply, "xcb_xfixes_query_version_reply"},
          {(dyn::apiproc *) &a.xfixes_select_cursor_input, "xcb_xfixes_select_cursor_input"},
          {(dyn::apiproc *) &a.xfixes_get_cursor_image, "xcb_xfixes_get_cursor_image"},
          {(dyn::apiproc *) &a.xfixes_get_cursor_image_reply, "xcb_xfixes_get_cursor_image_reply"},
          {(dyn::apiproc *) &a.xfixes_get_cursor_image_cursor_image, "xcb_xfixes_get_cursor_image_cursor_image"},
        };
        if (dyn::load(xcb, core) || dyn::load(fixes, ext)) {
          return;
        }
        loaded = a;
      });
      return loaded ? &*loaded : nullptr;
    }

    /**
     * @brief XFixes cursor watcher on one X screen.
     */
    class xfixes_source_t: public source_t {
    public:
      /**
       * @brief Take ownership of an initialised connection.
       * @param a Loaded entry points.
       * @param conn Connection with XFixes selected on the root window.
       * @param event_base First event code of the XFixes extension.
       */
      xfixes_source_t(const xcb_api_t *a, xcb_connection_t *conn, uint8_t event_base):
          a_ {a},
          conn_ {conn},
          event_base_ {event_base} {
      }

      ~xfixes_source_t() override {
        a_->disconnect(conn_);
      }

      xfixes_source_t(const xfixes_source_t &) = delete;
      xfixes_source_t &operator=(const xfixes_source_t &) = delete;

      std::optional<image_t> poll(std::chrono::milliseconds timeout, bool &broken) override {
        if (first_) {
          first_ = false;
          return grab(broken);
        }
        bool changed = drain(broken);
        if (broken) {
          return std::nullopt;
        }
        if (!changed) {
          pollfd pfd {a_->get_file_descriptor(conn_), POLLIN, 0};
          int r = ::poll(&pfd, 1, (int) timeout.count());
          if (r < 0 && errno != EINTR) {
            broken = true;
            return std::nullopt;
          }
          if (r > 0) {
            changed = drain(broken);
            if (broken) {
              return std::nullopt;
            }
          }
        }
        return changed ? grab(broken) : std::nullopt;
      }

    private:
      /**
       * @brief Consume queued events.
       * @param broken Set when the connection failed.
       * @return True when a cursor-change event was among them.
       */
      bool drain(bool &broken) {
        bool changed = false;
        while (auto *ev = a_->poll_for_event(conn_)) {
          if ((ev->response_type & 0x7F) == event_base_ + XCB_XFIXES_CURSOR_NOTIFY) {
            changed = true;
            notified_serial_ = reinterpret_cast<const xcb_xfixes_cursor_notify_event_t *>(ev)->cursor_serial;
          }
          std::free(ev);
        }
        if (a_->connection_has_error(conn_)) {
          broken = true;
        }
        return changed;
      }

      /**
       * @brief Read the current cursor image.
       * @param broken Set when the connection failed.
       * @return The image, or std::nullopt when unchanged (same serial) or unreadable.
       */
      std::optional<image_t> grab(bool &broken) {
        xcb_generic_error_t *err = nullptr;
        auto *reply = a_->xfixes_get_cursor_image_reply(conn_, a_->xfixes_get_cursor_image(conn_), &err);
        const int error_code = err ? err->error_code : 0;
        if (err) {
          std::free(err);
        }
        if (!reply) {
          broken = a_->connection_has_error(conn_) != 0;
          // The X server refuses the pixels (BadAccess) of a cursor whose owner has disconnected,
          // e.g. a root cursor set by xsetroot, as the Virtual display session does. The pointer
          // is still visible, so show the standard arrow rather than nothing.
          if (!broken && error_code == XCB_ACCESS && (!have_serial_ || notified_serial_ != last_serial_)) {
            if (!warned_access_) {
              warned_access_ = true;
              BOOST_LOG(info) << "cursor: the X server won't share a cursor whose owner has exited; showing a standard arrow for it"sv;
            }
            have_serial_ = true;
            last_serial_ = notified_serial_;
            return fallback_arrow(last_height_);
          }
          return std::nullopt;
        }
        std::optional<image_t> image;
        if (reply->cursor_serial != last_serial_ || !have_serial_) {
          have_serial_ = true;
          last_serial_ = reply->cursor_serial;
          image = from_premultiplied_argb(a_->xfixes_get_cursor_image_cursor_image(reply), reply->width, reply->height, reply->xhot, reply->yhot);
          if (image && !is_blank(*image)) {
            last_height_ = image->height;
          }
        }
        std::free(reply);
        return image;
      }

      const xcb_api_t *a_;
      xcb_connection_t *conn_;
      uint8_t event_base_;
      bool first_ = true;
      bool have_serial_ = false;
      uint32_t last_serial_ = 0;
      uint32_t notified_serial_ = 0;  ///< Serial of the latest CursorNotify event.
      int last_height_ = 0;  ///< Height of the last readable, visible cursor (sizes the fallback arrow).
      bool warned_access_ = false;
    };
  }  // namespace

  std::unique_ptr<source_t> make_x11_source(const target_t &target) {
    const auto *a = api();
    if (!a) {
      return nullptr;
    }

    int screen_num = 0;
    xcb_connection_t *conn = nullptr;
    {
      // A Virtual display :N has its own cookie. xcb reads DISPLAY/XAUTHORITY while connecting, so
      // point them at the target under the same process-wide lock NvFBC uses.
      std::optional<virtual_display::target_t> vd;
      if (!target.display.empty()) {
        vd = virtual_display::target_t {target.display, target.xauthority};
      }
      virtual_display::scoped_x_env_t scope {vd};
      conn = a->connect(target.display.empty() ? nullptr : target.display.c_str(), &screen_num);
    }
    if (!conn) {
      return nullptr;
    }
    if (a->connection_has_error(conn)) {
      a->disconnect(conn);
      return nullptr;
    }

    const auto *ext = a->get_extension_data(conn, a->xfixes_id);
    if (!ext || !ext->present) {
      a->disconnect(conn);
      return nullptr;
    }
    // XFixes needs its version negotiated before any other request.
    auto *version = a->xfixes_query_version_reply(conn, a->xfixes_query_version(conn, 4, 0), nullptr);
    if (!version || version->major_version < 2) {
      std::free(version);
      a->disconnect(conn);
      return nullptr;
    }
    std::free(version);

    auto it = a->setup_roots_iterator(a->get_setup(conn));
    for (int i = 0; i < screen_num && it.rem; ++i) {
      a->screen_next(&it);
    }
    if (!it.rem || !it.data) {
      a->disconnect(conn);
      return nullptr;
    }
    a->xfixes_select_cursor_input(conn, it.data->root, XCB_XFIXES_CURSOR_NOTIFY_MASK_DISPLAY_CURSOR);
    a->flush(conn);
    return std::make_unique<xfixes_source_t>(a, conn, ext->first_event);
  }
}  // namespace cursor
