/**
 * @file src/vd_app_launch.h
 * @brief Nova: make an app launched on a virtual display open there, not on the desktop.
 *
 * Many desktop apps are single-instance. Started with DISPLAY=:20 while they already run on :0,
 * they hand the request to the running instance and exit, so the window opens on :0:
 * - Chromium-family browsers and Electron apps: one instance per profile (--user-data-dir).
 * - Firefox: one instance per profile, found over X or D-Bus remoting.
 * - GApplication/D-Bus apps (gnome-terminal, nemo, lutris...): one instance per session bus.
 *
 * On a virtual display Nova gives browsers and Electron apps a profile of their own (the same one
 * the virtual desktop's own browser uses, so signing in once is enough), and runs apps on the
 * desktop session's private D-Bus. An app opts out with "nova-vd-share-profile": true in apps.json.
 */
#pragma once

// standard includes
#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vd_app_launch {
  using env_list_t = std::vector<std::pair<std::string, std::string>>;  ///< Environment variables to set.

  /**
   * @brief How an app keeps to one instance.
   */
  enum class kind_e {
    other,  ///< Nothing Nova changes on the command line.
    chromium,  ///< Chrome, Chromium, Brave, Edge, Vivaldi, Opera: --user-data-dir.
    electron,  ///< VS Code, Discord, Slack and other Electron apps: --user-data-dir.
    firefox,  ///< Firefox and its forks: --no-remote --profile.
  };

  /**
   * @brief The program a command line runs, located in that command line.
   */
  struct program_t {
    std::string path;  ///< The program as written (unquoted), e.g. "google-chrome-stable".
    std::string name;  ///< Lower-case basename, e.g. "google-chrome-stable".
    std::size_t insert_at = 0;  ///< Offset in the command line right after the program (or after a Flatpak app id).
    std::vector<std::string> args;  ///< The words after it.
    std::string flatpak_id;  ///< The app id for `flatpak run <id>`, else empty.
  };

  /**
   * @brief Find the program in a command line, as Nova runs it (words split on spaces, double
   * quotes group). Leading `env NAME=value`, gamemoderun, mangohud, prime-run and nice are skipped;
   * `flatpak run <id>` yields the id.
   *
   * @param cmd The app's command.
   * @return The program, or nullopt for an empty command or one run through a shell (sh -c ...).
   */
  std::optional<program_t> find_program(const std::string &cmd);

  /**
   * @brief Whether a program is an Electron app: resources.pak next to it (or one directory up)
   * and an app in resources/ (app.asar or app/).
   *
   * @param program The program (a PATH lookup is done for a bare name).
   * @return True for an Electron app.
   */
  bool looks_electron(const std::string &program);

  /**
   * @brief How a program keeps to one instance.
   *
   * @param program The program.
   * @param is_electron Probe for Electron apps that aren't known by name (looks_electron() normally).
   * @return Its kind.
   */
  kind_e classify(const program_t &program, const std::function<bool(const std::string &)> &is_electron);

  /**
   * @brief The profile directory an app gets on a virtual display.
   *
   * Google Chrome shares `<config>/virtual-display-browser` with the virtual desktop's own browser;
   * other apps get `<config>/virtual-display-profiles/<family>`, and Flatpak apps a folder inside
   * their own data directory (`~/.var/app/<id>/nova-virtual-display`), which their sandbox can reach.
   *
   * @param program The program.
   * @param kind Its kind (not other).
   * @param config_dir Nova's config directory (~/.config/nova-host).
   * @param home The user's home directory (for Flatpak apps).
   * @return The directory.
   */
  std::filesystem::path profile_dir(const program_t &program, kind_e kind, const std::filesystem::path &config_dir, const std::filesystem::path &home);

  /**
   * @brief A command line adapted for a virtual display.
   */
  struct adapted_t {
    std::string cmd;  ///< The command to run (unchanged when nothing applies).
    kind_e kind = kind_e::other;  ///< What the program is.
    std::filesystem::path profile;  ///< The profile directory added, empty when none.
    std::string note;  ///< Why it was left alone (e.g. "it names its own --user-data-dir"), empty otherwise.

    /**
     * @brief Whether the command changed.
     * @return True when a profile was added.
     */
    bool changed() const {
      return !profile.empty();
    }
  };

  /**
   * @brief Give a browser or Electron app its own profile on a virtual display.
   *
   * Chromium/Electron: `--user-data-dir=<dir>` (plus `--no-first-run` for browsers) right after the
   * program; Firefox: `--no-remote --profile <dir>`. A command that already picks a profile or
   * instance (--user-data-dir, -P, --profile, --no-remote, --new-instance) is left alone.
   *
   * @param cmd The app's command.
   * @param config_dir Nova's config directory.
   * @param home The user's home directory.
   * @param is_electron Electron probe (looks_electron() normally).
   * @return The command to run.
   */
  adapted_t adapt(const std::string &cmd, const std::filesystem::path &config_dir, const std::filesystem::path &home, const std::function<bool(const std::string &)> &is_electron);

  /**
   * @brief Whether an app should join the desktop session's private D-Bus.
   *
   * Everything does except game launches through Wine, Proton or Steam (nova-proton-run, umu-run,
   * wine, proton, steam, gamescope, heroic): they aren't single-instance over D-Bus, and they use
   * services on the user's bus (GameMode).
   *
   * @param cmd The app's command.
   * @return True to run it on the session's bus.
   */
  bool uses_session_bus(const std::string &cmd);

  /**
   * @brief Variables the desktop session publishes for apps (the ones read from its file).
   * @return DBUS_SESSION_BUS_ADDRESS, DCONF_PROFILE, GIO_USE_VFS, GIO_USE_VOLUME_MONITOR, GTK_USE_PORTAL, NO_AT_BRIDGE and the accessibility switches (GNOME_ACCESSIBILITY, QT_LINUX_ACCESSIBILITY_ALWAYS_ON, ACCESSIBILITY_ENABLED).
   */
  const std::vector<std::string> &session_env_keys();

  /**
   * @brief Read the desktop session's app environment (`<session_dir>/app-env.json`).
   *
   * The session writes it once its private bus is up, which can be a moment after the display
   * starts, so this waits up to `wait` for it.
   *
   * @param session_dir The session's state directory (empty: no session, nothing to read).
   * @param wait How long to wait for the file.
   * @return The variables (only session_env_keys()), empty when there is no session or no file.
   */
  env_list_t session_env(const std::string &session_dir, std::chrono::milliseconds wait);
}  // namespace vd_app_launch
