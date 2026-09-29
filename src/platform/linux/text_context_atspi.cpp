/**
 * @file src/platform/linux/text_context_atspi.cpp
 * @brief AT-SPI focus source for remote text context (see src/text_context.h).
 *
 * Talks to the accessibility bus directly over GDBus, so Nova needs no
 * libatspi. It registers for two events with the AT-SPI registry:
 *
 *  - `object:state-changed:focused` (focus gained or lost)
 *  - `object:text-caret-moved` (fallback for toolkits that move the caret
 *    into a field without a focus event)
 *
 * For a focused accessible it asks for its role, state set and screen
 * extents, and the caret's character extents. It never calls GetText or reads
 * any other content. Toolkits only emit events that someone registered for,
 * so the registration ends when the connection closes (the registry drops a
 * vanished listener).
 *
 * Which bus: the one the streamed display's apps use.
 *  1. `AT_SPI_BUS_ADDRESS` in Nova's environment (tests, manual override).
 *  2. Virtual display with a desktop session: the session's private D-Bus
 *     (app-env.json), asked for its org.a11y.Bus address. That bus is the
 *     session's own, so starting its at-spi-bus-launcher there touches
 *     nothing on the desktop.
 *  3. The `AT_SPI_BUS` property on the display's root window (read only;
 *     at-spi-bus-launcher publishes it for the display it serves).
 *  4. Nova's own session bus, org.a11y.Bus.GetAddress (the desktop's bus).
 *
 * Coverage: GTK 3/4, Qt with accessibility on, Firefox, and Chromium/Electron
 * when accessibility is enabled (toolkit-accessibility setting, or
 * --force-renderer-accessibility). On the Virtual display Nova's desktop
 * session turns these on for its own apps. Games and Wine/Proton mostly
 * expose nothing.
 */
// standard includes
#include <atomic>
#include <cstdlib>
#include <future>
#include <mutex>
#include <thread>

// lib includes
#include <gio/gio.h>

// local includes
#include "src/logging.h"
#include "src/text_context.h"
#include "src/vd_app_launch.h"
#include "src/virtual_display.h"

// Last: Xlib defines macros (None, Status, ...) that must not reach the headers above.
#ifdef SUNSHINE_BUILD_X11
  #include <X11/Xatom.h>
  #include <X11/Xlib.h>
#endif

using namespace std::literals;

namespace text_context {

  namespace {
    constexpr int kCallTimeoutMs = 300;  ///< Per-call timeout; a hung app must not stall the watcher.
    constexpr auto kSameElementRefresh = 500ms;  ///< Caret events on the focused field refresh its geometry at most this often.

    constexpr const char *kRegistryName = "org.a11y.atspi.Registry";
    constexpr const char *kRegistryPath = "/org/a11y/atspi/registry";
    constexpr const char *kRegistryIface = "org.a11y.atspi.Registry";
    constexpr const char *kEventIface = "org.a11y.atspi.Event.Object";
    constexpr const char *kAccessibleIface = "org.a11y.atspi.Accessible";
    constexpr const char *kComponentIface = "org.a11y.atspi.Component";
    constexpr const char *kTextIface = "org.a11y.atspi.Text";
    constexpr std::uint32_t kCoordScreen = 0;  ///< ATSPI_COORD_TYPE_SCREEN

    /**
     * @brief Ask a session bus for its accessibility bus address (org.a11y.Bus.GetAddress).
     * @param session Connected session bus.
     * @return The address, or empty.
     */
    std::string a11y_address_from(GDBusConnection *session) {
      GError *error = nullptr;
      GVariant *reply = g_dbus_connection_call_sync(session, "org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress", nullptr, G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, &error);
      if (!reply) {
        BOOST_LOG(debug) << "text_context: no accessibility bus: "sv << (error ? error->message : "unknown");
        g_clear_error(&error);
        return {};
      }
      const gchar *address = nullptr;
      g_variant_get(reply, "(&s)", &address);
      std::string result = address ? address : "";
      g_variant_unref(reply);
      return result;
    }

    /**
     * @brief Read the AT_SPI_BUS property of a display's root window (read only).
     * @param target Display to read.
     * @return The address, or empty when the display has none.
     */
    std::string a11y_address_from_root(const target_t &target) {
#ifdef SUNSHINE_BUILD_X11
      Display *display = nullptr;
      {
        std::optional<virtual_display::target_t> vd;
        if (!target.display.empty()) {
          vd = virtual_display::target_t {target.display, target.xauthority};
        }
        virtual_display::scoped_x_env_t scope {vd};
        if (target.display.empty() && !std::getenv("DISPLAY")) {
          return {};
        }
        display = XOpenDisplay(target.display.empty() ? nullptr : target.display.c_str());
      }
      if (!display) {
        return {};
      }
      std::string result;
      const Atom atom = XInternAtom(display, "AT_SPI_BUS", True);
      if (atom != 0) {
        Atom type = 0;
        int format = 0;
        unsigned long items = 0;
        unsigned long left = 0;
        unsigned char *data = nullptr;
        if (XGetWindowProperty(display, DefaultRootWindow(display), atom, 0, 1024, False, XA_STRING, &type, &format, &items, &left, &data) == 0 && data) {
          if (type == XA_STRING && format == 8 && items > 0) {
            result.assign(reinterpret_cast<const char *>(data), items);
          }
          XFree(data);
        }
      }
      XCloseDisplay(display);
      return result;
#else
      static_cast<void>(target);
      return {};
#endif
    }

    /**
     * @brief Find the accessibility bus the target display's apps use (see the file comment).
     * @param target Display being streamed.
     * @return The address, or empty when there is no accessibility bus.
     */
    std::string a11y_bus_address(const target_t &target) {
      if (const char *env = std::getenv("AT_SPI_BUS_ADDRESS"); env && *env) {
        return env;
      }

      if (!target.session_dir.empty()) {
        // The Virtual display's desktop session runs its own D-Bus; its apps find their
        // accessibility bus there.
        for (const auto &[key, value] : vd_app_launch::session_env(target.session_dir, std::chrono::milliseconds {0})) {
          if (key != "DBUS_SESSION_BUS_ADDRESS") {
            continue;
          }
          GError *error = nullptr;
          GDBusConnection *bus = g_dbus_connection_new_for_address_sync(value.c_str(), static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION), nullptr, nullptr, &error);
          if (!bus) {
            BOOST_LOG(debug) << "text_context: cannot reach the Virtual display session bus: "sv << (error ? error->message : "unknown");
            g_clear_error(&error);
            break;
          }
          auto address = a11y_address_from(bus);
          g_dbus_connection_close_sync(bus, nullptr, nullptr);
          g_object_unref(bus);
          if (!address.empty()) {
            return address;
          }
          break;
        }
      }

      if (auto address = a11y_address_from_root(target); !address.empty()) {
        return address;
      }

      GError *error = nullptr;
      GDBusConnection *session = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
      if (!session) {
        BOOST_LOG(debug) << "text_context: no session bus: "sv << (error ? error->message : "unknown");
        g_clear_error(&error);
        return {};
      }
      auto address = a11y_address_from(session);
      g_object_unref(session);
      return address;
    }

    /**
     * @brief AT-SPI focus watcher running its own GLib main context on a worker thread.
     */
    class atspi_source_t: public source_t {
    public:
      explicit atspi_source_t(target_t target):
          _target {std::move(target)} {
      }

      ~atspi_source_t() override {
        stop();
      }

      bool start(callback_t callback) override {
        if (_thread.joinable()) {
          return true;
        }
        _callback = std::move(callback);
        _broken = false;
        const auto address = a11y_bus_address(_target);
        if (address.empty()) {
          return false;
        }
        open_pointer_display();

        std::promise<bool> started;
        auto ready = started.get_future();
        _thread = std::thread([this, address, &started]() {
          run(address, started);
        });
        if (!ready.get()) {
          _thread.join();
          close_pointer_display();
          return false;
        }
        return true;
      }

      void stop() override {
        if (!_thread.joinable()) {
          return;
        }
        {
          std::lock_guard lock {_loop_mutex};
          _stopping = true;
          if (_loop) {
            g_main_loop_quit(_loop);
          }
          if (_context) {
            g_main_context_wakeup(_context);
          }
        }
        _thread.join();
        _stopping = false;
        close_pointer_display();
      }

      bool broken() const override {
        return _broken.load();
      }

      std::optional<point_t> pointer() override {
#ifdef SUNSHINE_BUILD_X11
        std::lock_guard lock {_display_mutex};
        if (!_display) {
          return std::nullopt;
        }
        Window root_return;
        Window child_return;
        int root_x;
        int root_y;
        int win_x;
        int win_y;
        unsigned int mask;
        if (XQueryPointer(_display, DefaultRootWindow(_display), &root_return, &child_return, &root_x, &root_y, &win_x, &win_y, &mask)) {
          return point_t {root_x, root_y};
        }
#endif
        return std::nullopt;
      }

    private:
      void open_pointer_display() {
#ifdef SUNSHINE_BUILD_X11
        std::lock_guard lock {_display_mutex};
        if (_display) {
          return;
        }
        std::optional<virtual_display::target_t> vd;
        if (!_target.display.empty()) {
          vd = virtual_display::target_t {_target.display, _target.xauthority};
        } else if (!std::getenv("DISPLAY")) {
          return;
        }
        virtual_display::scoped_x_env_t scope {vd};
        _display = XOpenDisplay(_target.display.empty() ? nullptr : _target.display.c_str());
#endif
      }

      void close_pointer_display() {
#ifdef SUNSHINE_BUILD_X11
        std::lock_guard lock {_display_mutex};
        if (_display) {
          XCloseDisplay(_display);
          _display = nullptr;
        }
#endif
      }

      void run(const std::string &address, std::promise<bool> &started) {
        GMainContext *context = g_main_context_new();
        g_main_context_push_thread_default(context);

        GError *error = nullptr;
        _bus = g_dbus_connection_new_for_address_sync(address.c_str(), static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION), nullptr, nullptr, &error);
        if (!_bus) {
          BOOST_LOG(warning) << "text_context: cannot connect to the accessibility bus: "sv << (error ? error->message : "unknown");
          g_clear_error(&error);
          g_main_context_pop_thread_default(context);
          g_main_context_unref(context);
          started.set_value(false);
          return;
        }

        // A closed bus (the Virtual display's session ended, the registry restarted) needs a new source.
        g_dbus_connection_set_exit_on_close(_bus, FALSE);
        const gulong closed_handler = g_signal_connect(_bus, "closed", G_CALLBACK(&atspi_source_t::on_closed), this);

        // Subscribing while our context is thread-default routes signals to it.
        const guint focus_sub = g_dbus_connection_signal_subscribe(_bus, nullptr, kEventIface, "StateChanged", nullptr, "focused", G_DBUS_SIGNAL_FLAGS_NONE, &atspi_source_t::on_signal, this, nullptr);
        const guint caret_sub = g_dbus_connection_signal_subscribe(_bus, nullptr, kEventIface, "TextCaretMoved", nullptr, nullptr, G_DBUS_SIGNAL_FLAGS_NONE, &atspi_source_t::on_signal, this, nullptr);
        const bool registered = register_event("object:state-changed:focused");
        register_event("object:text-caret-moved");

        GMainLoop *loop = g_main_loop_new(context, FALSE);
        {
          std::lock_guard lock {_loop_mutex};
          _loop = loop;
          _context = context;
        }
        started.set_value(true);
        if (!registered) {
          BOOST_LOG(warning) << "text_context: the AT-SPI registry refused the focus listener; apps may not report focus"sv;
        }

        if (!_stopping) {
          g_main_loop_run(loop);
        }

        {
          std::lock_guard lock {_loop_mutex};
          _loop = nullptr;
          _context = nullptr;
        }
        g_signal_handler_disconnect(_bus, closed_handler);
        g_dbus_connection_signal_unsubscribe(_bus, focus_sub);
        g_dbus_connection_signal_unsubscribe(_bus, caret_sub);
        deregister_event("object:state-changed:focused");
        deregister_event("object:text-caret-moved");
        g_dbus_connection_close_sync(_bus, nullptr, nullptr);
        g_object_unref(_bus);
        _bus = nullptr;
        g_main_loop_unref(loop);
        g_main_context_pop_thread_default(context);
        g_main_context_unref(context);
      }

      bool register_event(const char *event) {
        GError *error = nullptr;
        GVariant *reply = g_dbus_connection_call_sync(_bus, kRegistryName, kRegistryPath, kRegistryIface, "RegisterEvent", g_variant_new("(s@ass)", event, g_variant_new_strv(nullptr, 0), ""), nullptr, G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, &error);
        if (!reply) {
          // at-spi2-core before 2.36 takes a single argument.
          g_clear_error(&error);
          reply = g_dbus_connection_call_sync(_bus, kRegistryName, kRegistryPath, kRegistryIface, "RegisterEvent", g_variant_new("(s)", event), nullptr, G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, &error);
        }
        if (!reply) {
          BOOST_LOG(debug) << "text_context: RegisterEvent "sv << event << " failed: "sv << (error ? error->message : "unknown");
          g_clear_error(&error);
          return false;
        }
        g_variant_unref(reply);
        return true;
      }

      void deregister_event(const char *event) {
        GVariant *reply = g_dbus_connection_call_sync(_bus, kRegistryName, kRegistryPath, kRegistryIface, "DeregisterEvent", g_variant_new("(ss)", event, ""), nullptr, G_DBUS_CALL_FLAGS_NONE, 500, nullptr, nullptr);
        if (reply) {
          g_variant_unref(reply);
        }
      }

      GVariant *call(const char *sender, const char *path, const char *iface, const char *method, GVariant *args, const GVariantType *reply_type) {
        return g_dbus_connection_call_sync(_bus, sender, path, iface, method, args, reply_type, G_DBUS_CALL_FLAGS_NO_AUTO_START, kCallTimeoutMs, nullptr, nullptr);
      }

      std::optional<std::array<std::int32_t, 4>> caret_extents(const char *sender, const char *path) {
        GVariant *offset_reply = call(sender, path, "org.freedesktop.DBus.Properties", "Get", g_variant_new("(ss)", kTextIface, "CaretOffset"), G_VARIANT_TYPE("(v)"));
        if (!offset_reply) {
          return std::nullopt;
        }
        GVariant *boxed = nullptr;
        g_variant_get(offset_reply, "(v)", &boxed);
        gint32 offset = -1;
        if (boxed && g_variant_is_of_type(boxed, G_VARIANT_TYPE_INT32)) {
          offset = g_variant_get_int32(boxed);
        }
        if (boxed) {
          g_variant_unref(boxed);
        }
        g_variant_unref(offset_reply);
        if (offset < 0) {
          return std::nullopt;
        }

        auto extents_at = [&](gint32 at) -> std::optional<std::array<std::int32_t, 4>> {
          GVariant *reply = call(sender, path, kTextIface, "GetCharacterExtents", g_variant_new("(iu)", at, kCoordScreen), G_VARIANT_TYPE("(iiii)"));
          if (!reply) {
            return std::nullopt;
          }
          std::array<std::int32_t, 4> e {};
          g_variant_get(reply, "(iiii)", &e[0], &e[1], &e[2], &e[3]);
          g_variant_unref(reply);
          if (e[3] <= 0) {
            return std::nullopt;
          }
          return e;
        };
        if (auto e = extents_at(offset)) {
          return std::array<std::int32_t, 4> {(*e)[0], (*e)[1], 1, (*e)[3]};
        }
        // At the end of the text there is no character under the caret: use the previous one's right edge.
        if (offset > 0) {
          if (auto e = extents_at(offset - 1)) {
            return std::array<std::int32_t, 4> {(*e)[0] + (*e)[2], (*e)[1], 1, (*e)[3]};
          }
        }
        return std::nullopt;
      }

      void inspect(const char *sender, const char *path, bool caret_event) {
        std::string element = std::string {sender} + path;
        const auto now = std::chrono::steady_clock::now();
        if (caret_event && element == _last_element && now - _last_inspect < kSameElementRefresh) {
          return;  // Typing moves the caret on every key; do not query the app each time.
        }

        GVariant *role_reply = call(sender, path, kAccessibleIface, "GetRole", nullptr, G_VARIANT_TYPE("(u)"));
        if (!role_reply) {
          return;
        }
        guint32 role = 0;
        g_variant_get(role_reply, "(u)", &role);
        g_variant_unref(role_reply);

        GVariant *state_reply = call(sender, path, kAccessibleIface, "GetState", nullptr, G_VARIANT_TYPE("(au)"));
        if (!state_reply) {
          return;
        }
        std::vector<std::uint32_t> states;
        GVariantIter *iter = nullptr;
        g_variant_get(state_reply, "(au)", &iter);
        guint32 word;
        while (iter && g_variant_iter_next(iter, "u", &word)) {
          states.push_back(word);
        }
        if (iter) {
          g_variant_iter_free(iter);
        }
        g_variant_unref(state_reply);

        if (caret_event && !atspi::has_state(states, atspi::kStateFocused)) {
          return;  // A caret moving in a background field is not a focus change.
        }

        std::optional<std::array<std::int32_t, 4>> extents;
        if (GVariant *reply = call(sender, path, kComponentIface, "GetExtents", g_variant_new("(u)", kCoordScreen), G_VARIANT_TYPE("((iiii))"))) {
          std::array<std::int32_t, 4> e {};
          g_variant_get(reply, "((iiii))", &e[0], &e[1], &e[2], &e[3]);
          g_variant_unref(reply);
          extents = e;
        }

        std::optional<std::array<std::int32_t, 4>> caret;
        if (role != atspi::kRolePasswordText) {
          caret = caret_extents(sender, path);
        }

        auto observation = atspi::classify(element, role, states, extents, caret);
        _last_element = observation.focused ? element : std::string {};
        _last_inspect = now;
        if (_callback) {
          _callback(observation);
        }
      }

      static void on_closed(GDBusConnection *, gboolean, GError *, gpointer user_data) {
        auto *self = static_cast<atspi_source_t *>(user_data);
        self->_broken = true;
        std::lock_guard lock {self->_loop_mutex};
        if (self->_loop) {
          g_main_loop_quit(self->_loop);
        }
      }

      static void on_signal(GDBusConnection *, const gchar *sender, const gchar *path, const gchar *, const gchar *member, GVariant *parameters, gpointer user_data) {
        auto *self = static_cast<atspi_source_t *>(user_data);
        if (!sender || !path || !g_variant_is_of_type(parameters, G_VARIANT_TYPE("(siiva{sv})"))) {
          return;
        }
        const gchar *detail = nullptr;
        gint32 detail1 = 0;
        gint32 detail2 = 0;
        GVariant *any = nullptr;
        GVariant *props = nullptr;
        g_variant_get(parameters, "(&siiv@a{sv})", &detail, &detail1, &detail2, &any, &props);
        if (any) {
          g_variant_unref(any);
        }
        if (props) {
          g_variant_unref(props);
        }

        if (g_strcmp0(member, "StateChanged") == 0) {
          if (g_strcmp0(detail, "focused") != 0) {
            return;
          }
          if (detail1 == 0) {
            // Focus lost: no query needed, the app may already be tearing the object down.
            observation_t blur;
            blur.element = std::string {sender} + path;
            blur.focused = false;
            if (self->_last_element == blur.element) {
              self->_last_element.clear();
            }
            if (self->_callback) {
              self->_callback(blur);
            }
            return;
          }
          self->inspect(sender, path, false);
        } else if (g_strcmp0(member, "TextCaretMoved") == 0) {
          self->inspect(sender, path, true);
        }
      }

      target_t _target;
      callback_t _callback;
      std::atomic<bool> _broken {false};
      std::thread _thread;
      std::mutex _loop_mutex;
      GMainLoop *_loop {nullptr};
      GMainContext *_context {nullptr};
      std::atomic<bool> _stopping {false};
      GDBusConnection *_bus {nullptr};
      std::string _last_element;
      std::chrono::steady_clock::time_point _last_inspect {};

      std::mutex _display_mutex;
#ifdef SUNSHINE_BUILD_X11
      Display *_display {nullptr};
#endif
    };
  }  // namespace

  std::unique_ptr<source_t> make_platform_source(const target_t &target) {
    return std::make_unique<atspi_source_t>(target);
  }

  bool platform_source_available(const target_t &target) {
    if (!target.session_dir.empty()) {
      // The session's own at-spi-bus-launcher starts with its first accessible app or on our
      // first request; its private bus being there is enough.
      for (const auto &[key, value] : vd_app_launch::session_env(target.session_dir, std::chrono::milliseconds {0})) {
        if (key == "DBUS_SESSION_BUS_ADDRESS") {
          return true;
        }
      }
    }
    return !a11y_bus_address(target).empty();
  }

  target_t capture_target() {
    if (auto vd = virtual_display::capture_target()) {
      return {vd->display, vd->xauthority, vd->session_dir};
    }
    return {};
  }

}  // namespace text_context
