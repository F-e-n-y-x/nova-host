/**
 * @file src/secure_files.cpp
 * @brief Definitions for owner-only permissions on Nova's private files.
 */
// class header include
#include "secure_files.h"

// standard includes
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <format>
#include <fstream>
#include <random>

#ifndef _WIN32
  #include <fcntl.h>
  #include <sys/stat.h>
  #include <unistd.h>
#endif

// local includes
#include "config.h"
#include "logging.h"
#include "platform/common.h"

namespace fs = std::filesystem;
using namespace std::literals;

namespace secure_files {
  bool write_private(const fs::path &path, const std::string_view contents) {
#ifdef _WIN32
    std::ofstream out {path, std::ios::binary | std::ios::trunc};
    if (!out) {
      return false;
    }
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return static_cast<bool>(out);
#else
    std::random_device rd;
    const auto tmp = fs::path {path}.concat(std::format(".tmp{:08x}", rd()));
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) {
      return false;
    }
    std::size_t written = 0;
    while (written < contents.size()) {
      const auto n = ::write(fd, contents.data() + written, contents.size() - written);
      if (n < 0) {
        if (errno == EINTR) {
          continue;
        }
        ::close(fd);
        ::unlink(tmp.c_str());
        return false;
      }
      written += static_cast<std::size_t>(n);
    }
    // The umask may have masked bits off, never on; set exactly 0600 anyway.
    ::fchmod(fd, 0600);
    if (::fsync(fd) != 0 || ::close(fd) != 0) {
      ::unlink(tmp.c_str());
      return false;
    }
    if (::rename(tmp.c_str(), path.c_str()) != 0) {
      ::unlink(tmp.c_str());
      return false;
    }
    return true;
#endif
  }

  bool is_exposed(const fs::path &path) {
    std::error_code ec;
    const auto status = fs::symlink_status(path, ec);
    if (ec || !fs::exists(status)) {
      return false;
    }
    const auto group_other = fs::perms::group_all | fs::perms::others_all;
    return (status.permissions() & group_other) != fs::perms::none;
  }

  bool restrict(const fs::path &path) {
#ifdef _WIN32
    (void) path;
    return true;
#else
    struct stat st {};
    if (::lstat(path.c_str(), &st) != 0) {
      return errno == ENOENT;
    }
    if (S_ISLNK(st.st_mode) || st.st_uid != ::geteuid()) {
      // Don't chase links or touch files another user owns (e.g. a system-wide cert).
      return (st.st_mode & 077) == 0;
    }
    const mode_t wanted = S_ISDIR(st.st_mode) ? 0700 : 0600;
    if ((st.st_mode & 0777) == wanted || (st.st_mode & 077) == 0) {
      return true;
    }
    if (::chmod(path.c_str(), (st.st_mode & 0700) | (S_ISDIR(st.st_mode) ? 0700 : 0600)) != 0) {
      return false;
    }
    BOOST_LOG(info) << "Restricted "sv << path.string() << " to owner-only access ("sv << std::format("{:o}", wanted) << ')';
    return true;
#endif
  }

  std::vector<fs::path> private_paths() {
    std::vector<fs::path> paths;
    const auto add = [&paths](const fs::path &path) {
      if (!path.empty() && std::ranges::find(paths, path) == paths.end()) {
        paths.push_back(path);
      }
    };

    // Directories first, so nothing is reachable through them even before the files are fixed.
    add(platf::appdata());
    add(fs::path {config::nvhttp.pkey}.parent_path());
    add(fs::path {config::nvhttp.cert}.parent_path());
    add(fs::path {config::nvhttp.file_state}.parent_path());

    add(config::sunshine.config_file);
    add(config::sunshine.credentials_file);
    add(config::nvhttp.file_state);
    add(config::nvhttp.pkey);
    add(config::nvhttp.cert);
    add(config::stream.file_apps);
    add(fs::path {config::nvhttp.file_state}.parent_path() / "session_history.json");
    add(fs::path {config::nvhttp.file_state}.parent_path() / "app_stats.json");
    add(fs::path {config::nvhttp.file_state}.parent_path() / "web_sessions.json");
    add(config::sunshine.log_file);
    for (int i = 1; i <= 5; ++i) {
      add(fs::path {config::sunshine.log_file}.concat(std::format(".{}", i)));
    }
    return paths;
  }

  int harden_all() {
    int failures = 0;
    for (const auto &path : private_paths()) {
      if (!restrict(path)) {
        ++failures;
        BOOST_LOG(warning) << "Couldn't restrict "sv << path.string() << " to owner-only access"sv;
      }
    }
    return failures;
  }
}  // namespace secure_files
