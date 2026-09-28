/**
 * @file src/run_program.cpp
 * @brief Definitions for running helper programs without a shell.
 */
// local includes
#include "run_program.h"

#if defined(__linux__) || defined(__FreeBSD__)
  // standard includes
  #include <algorithm>
  #include <cerrno>
  #include <thread>

  // platform includes
  #include <fcntl.h>
  #include <poll.h>
  #include <signal.h>
  #include <spawn.h>
  #include <sys/wait.h>
  #include <unistd.h>

extern char **environ;

namespace run_program {
  namespace {
    constexpr std::size_t MAX_OUT = 1 << 20;
    constexpr std::size_t MAX_ERR = 64 << 10;

    void append(int fd, std::string &into, std::size_t cap, bool &open) {
      char buf[4096];
      const auto n = read(fd, buf, sizeof(buf));
      if (n > 0) {
        if (into.size() < cap) {
          into.append(buf, std::min<std::size_t>(static_cast<std::size_t>(n), cap - into.size()));
        }
      } else if (n == 0 || (errno != EINTR && errno != EAGAIN)) {
        open = false;
      }
    }
  }  // namespace

  result_t run(const std::vector<std::string> &argv, std::chrono::milliseconds timeout) {
    result_t result;
    if (argv.empty()) {
      return result;
    }
    int out_pipe[2];
    int err_pipe[2];
    if (pipe2(out_pipe, O_CLOEXEC) != 0) {
      return result;
    }
    if (pipe2(err_pipe, O_CLOEXEC) != 0) {
      close(out_pipe[0]);
      close(out_pipe[1]);
      return result;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, out_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, err_pipe[1], STDERR_FILENO);
  #if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
    // Nova's sockets are not all O_CLOEXEC; don't leak them into helpers.
    posix_spawn_file_actions_addclosefrom_np(&actions, STDERR_FILENO + 1);
  #endif
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    sigset_t defaults;
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGPIPE);
    sigaddset(&defaults, SIGINT);
    sigaddset(&defaults, SIGTERM);
    posix_spawnattr_setsigdefault(&attr, &defaults);
    sigset_t none;
    sigemptyset(&none);
    posix_spawnattr_setsigmask(&attr, &none);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);

    std::vector<char *> args;
    args.reserve(argv.size() + 1);
    for (const auto &a : argv) {
      args.push_back(const_cast<char *>(a.c_str()));
    }
    args.push_back(nullptr);

    pid_t pid = -1;
    const int rc = posix_spawnp(&pid, args[0], &actions, &attr, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    close(out_pipe[1]);
    close(err_pipe[1]);
    if (rc != 0) {
      close(out_pipe[0]);
      close(err_pipe[0]);
      result.exit_code = rc == ENOENT ? 127 : -1;
      return result;
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    bool out_open = true;
    bool err_open = true;
    bool timed_out = false;
    while (out_open || err_open) {
      const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
      if (left <= 0) {
        timed_out = true;
        break;
      }
      pollfd fds[2] = {{out_open ? out_pipe[0] : -1, POLLIN, 0}, {err_open ? err_pipe[0] : -1, POLLIN, 0}};
      const int n = poll(fds, 2, static_cast<int>(std::min<long long>(left, 1000)));
      if (n < 0 && errno != EINTR) {
        break;
      }
      if (out_open && (fds[0].revents & (POLLIN | POLLHUP | POLLERR))) {
        append(out_pipe[0], result.output, MAX_OUT, out_open);
      }
      if (err_open && (fds[1].revents & (POLLIN | POLLHUP | POLLERR))) {
        append(err_pipe[0], result.error, MAX_ERR, err_open);
      }
    }
    close(out_pipe[0]);
    close(err_pipe[0]);

    int status = 0;
    if (timed_out) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      result.exit_code = -1;
      return result;
    }
    // Output closed; the process is exiting. Wait for it within what is left of the timeout.
    for (;;) {
      const pid_t w = waitpid(pid, &status, WNOHANG);
      if (w == pid) {
        break;
      }
      if (w < 0 && errno != EINTR) {
        return result;
      }
      if (std::chrono::steady_clock::now() >= deadline) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        return result;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
  }
}  // namespace run_program
#else
namespace run_program {
  result_t run(const std::vector<std::string> &, std::chrono::milliseconds) {
    return {};
  }
}  // namespace run_program
#endif
