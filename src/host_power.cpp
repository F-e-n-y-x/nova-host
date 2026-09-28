/**
 * @file src/host_power.cpp
 * @brief Definitions for suspend through logind and the pre-sleep hook.
 */
// class header include
#include "host_power.h"

// standard includes
#include <atomic>
#include <mutex>
#include <thread>

#ifdef __linux__
  #include <gio/gio.h>
  #include <gio/gunixfdlist.h>
  #include <unistd.h>
#endif

// local includes
#include "logging.h"

using namespace std::literals;

namespace host_power {
  namespace {
    std::mutex backend_mutex;
    std::function<std::string()> test_backend;  // NOSONAR(cpp:S5421): test seam

#ifdef __linux__
    constexpr auto login1_name = "org.freedesktop.login1";
    constexpr auto login1_path = "/org/freedesktop/login1";
    constexpr auto login1_manager = "org.freedesktop.login1.Manager";

    GDBusConnection *system_bus(std::string &error) {
      GError *err = nullptr;
      auto *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &err);
      if (!bus) {
        error = err ? err->message : "the system bus isn't reachable";
        if (err) {
          g_error_free(err);
        }
      }
      return bus;
    }

    std::string logind_suspend() {
      std::string reason;
      auto *bus = system_bus(reason);
      if (!bus) {
        return "Couldn't reach logind: " + reason;
      }
      GError *err = nullptr;
      auto *reply = g_dbus_connection_call_sync(bus, login1_name, login1_path, login1_manager, "Suspend", g_variant_new("(b)", FALSE), nullptr, G_DBUS_CALL_FLAGS_NONE, 10000, nullptr, &err);
      g_object_unref(bus);
      if (!reply) {
        std::string message = err ? err->message : "unknown error";
        if (err) {
          g_error_free(err);
        }
        if (message.find("Interactive authentication required") != std::string::npos || message.find("not authorized") != std::string::npos || message.find("AccessDenied") != std::string::npos) {
          return "The system didn't allow Nova to suspend (polkit). Run: sudo /usr/share/nova-host/nova-allow-suspend";
        }
        return "logind refused to suspend: " + message;
      }
      g_variant_unref(reply);
      return {};
    }

    /**
     * @brief Thread that owns a GMainContext, the PrepareForSleep subscription and the inhibitor fd.
     */
    struct watcher_t {
      std::function<void()> before_sleep;
      GMainContext *context = nullptr;
      GMainLoop *loop = nullptr;
      GDBusConnection *bus = nullptr;
      guint subscription = 0;
      int inhibit_fd = -1;
      std::thread thread;

      void take_lock() {
        if (inhibit_fd >= 0) {
          return;
        }
        GError *err = nullptr;
        GUnixFDList *fds = nullptr;
        auto *reply = g_dbus_connection_call_with_unix_fd_list_sync(bus, login1_name, login1_path, login1_manager, "Inhibit", g_variant_new("(ssss)", "sleep", "Nova", "End streams cleanly before sleep", "delay"), G_VARIANT_TYPE("(h)"), G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &fds, nullptr, &err);
        if (!reply) {
          BOOST_LOG(warning) << "Couldn't take a logind sleep inhibitor: "sv << (err ? err->message : "unknown error");
          if (err) {
            g_error_free(err);
          }
          return;
        }
        gint32 index = -1;
        g_variant_get(reply, "(h)", &index);
        g_variant_unref(reply);
        if (fds) {
          inhibit_fd = g_unix_fd_list_get(fds, index, nullptr);
          g_object_unref(fds);
        }
      }

      void release_lock() {
        if (inhibit_fd >= 0) {
          ::close(inhibit_fd);
          inhibit_fd = -1;
        }
      }

      static void on_prepare_for_sleep(GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *, GVariant *parameters, gpointer user_data) {
        auto *self = static_cast<watcher_t *>(user_data);
        gboolean going_down = FALSE;
        g_variant_get(parameters, "(b)", &going_down);
        if (going_down) {
          BOOST_LOG(info) << "The host is going to sleep; ending streams"sv;
          if (self->before_sleep) {
            self->before_sleep();
          }
          self->release_lock();
        } else {
          BOOST_LOG(info) << "The host woke up"sv;
          self->take_lock();
        }
      }

      bool start() {
        std::string reason;
        bus = system_bus(reason);
        if (!bus) {
          BOOST_LOG(info) << "Sleep watcher disabled: "sv << reason;
          return false;
        }
        context = g_main_context_new();
        loop = g_main_loop_new(context, FALSE);
        thread = std::thread([this]() {
          g_main_context_push_thread_default(context);
          subscription = g_dbus_connection_signal_subscribe(bus, login1_name, login1_manager, "PrepareForSleep", login1_path, nullptr, G_DBUS_SIGNAL_FLAGS_NONE, &watcher_t::on_prepare_for_sleep, this, nullptr);
          take_lock();
          g_main_loop_run(loop);
          g_dbus_connection_signal_unsubscribe(bus, subscription);
          release_lock();
          g_main_context_pop_thread_default(context);
        });
        return true;
      }

      ~watcher_t() {
        if (loop) {
          // Quit from inside the loop's context, so a quit that races ahead of g_main_loop_run() isn't lost.
          g_main_context_invoke(
            context,
            [](gpointer data) -> gboolean {
              g_main_loop_quit(static_cast<GMainLoop *>(data));
              return G_SOURCE_REMOVE;
            },
            loop
          );
        }
        if (thread.joinable()) {
          thread.join();
        }
        if (loop) {
          g_main_loop_unref(loop);
        }
        if (context) {
          g_main_context_unref(context);
        }
        if (bus) {
          g_object_unref(bus);
        }
      }
    };
#endif
  }  // namespace

  void set_suspend_backend_for_testing(std::function<std::string()> backend) {
    std::lock_guard lock {backend_mutex};
    test_backend = std::move(backend);
  }

  std::string suspend() {
    {
      std::lock_guard lock {backend_mutex};
      if (test_backend) {
        return test_backend();
      }
    }
#ifdef __linux__
    return logind_suspend();
#else
    return "Sleep is only supported on Linux hosts.";
#endif
  }

  std::string can_suspend() {
#ifdef __linux__
    std::string reason;
    auto *bus = system_bus(reason);
    if (!bus) {
      return "unknown";
    }
    GError *err = nullptr;
    auto *reply = g_dbus_connection_call_sync(bus, login1_name, login1_path, login1_manager, "CanSuspend", nullptr, G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, &err);
    g_object_unref(bus);
    if (!reply) {
      if (err) {
        g_error_free(err);
      }
      return "unknown";
    }
    const gchar *answer = nullptr;
    g_variant_get(reply, "(&s)", &answer);
    std::string result = answer ? answer : "unknown";
    g_variant_unref(reply);
    return result;
#else
    return "na";
#endif
  }

  std::unique_ptr<void, void (*)(void *)> start_sleep_watcher(std::function<void()> before_sleep) {
#ifdef __linux__
    auto watcher = std::make_unique<watcher_t>();
    watcher->before_sleep = std::move(before_sleep);
    if (!watcher->start()) {
      return {nullptr, [](void *) {}};
    }
    return {watcher.release(), [](void *p) {
              delete static_cast<watcher_t *>(p);
            }};
#else
    (void) before_sleep;
    return {nullptr, [](void *) {}};
#endif
  }
}  // namespace host_power
