/**
 * @file src/host_power.h
 * @brief Suspend through systemd-logind, and a pre-sleep hook that ends streams cleanly.
 */
#pragma once

// standard includes
#include <functional>
#include <memory>
#include <string>

namespace host_power {
  /**
   * @brief Ask logind to suspend (`org.freedesktop.login1.Manager.Suspend(false)`).
   *
   * Non-interactive: polkit must allow it without a password (see
   * /usr/share/nova-host/nova-allow-suspend).
   *
   * @return Empty on success, otherwise a user-facing reason.
   */
  std::string suspend();

  /**
   * @brief logind's `CanSuspend` answer.
   * @return "yes", "no", "challenge" (needs a password), "na", or "unknown" when logind can't be reached.
   */
  std::string can_suspend();

  /**
   * @brief Replace suspend() for tests.
   * @param backend Function returning the same as suspend(); empty restores logind.
   */
  void set_suspend_backend_for_testing(std::function<std::string()> backend);

  /**
   * @brief Watch logind's PrepareForSleep and run @p before_sleep before the machine sleeps,
   *        however the sleep was started (Nova, the desktop, a lid, a timer).
   *
   * Holds a logind "delay" inhibitor lock, which gives the hook up to logind's InhibitDelayMaxSec
   * (5 s by default) to end the streams, and takes it again after resume.
   *
   * @param before_sleep Runs on the watcher thread when sleep is imminent.
   * @return Guard that stops the watcher when destroyed; nullptr when logind isn't available.
   */
  std::unique_ptr<void, void (*)(void *)> start_sleep_watcher(std::function<void()> before_sleep);
}  // namespace host_power
