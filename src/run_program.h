/**
 * @file src/run_program.h
 * @brief Run a helper program without a shell and capture its output (used by power mode and the updater).
 */
#pragma once

// standard includes
#include <chrono>
#include <string>
#include <vector>

namespace run_program {
  /**
   * @brief Exit status and output of a finished program.
   */
  struct result_t {
    int exit_code = -1;  ///< Exit status; 127 when the program is not installed, -1 when it failed to run or timed out.
    std::string output;  ///< Standard output (capped at 1 MiB).
    std::string error;  ///< Standard error (capped at 64 KiB).
  };

  /**
   * @brief Run argv[0] (looked up in PATH) with the given arguments and wait for it.
   *
   * No shell is involved, so arguments are passed verbatim. Descriptors other than stdin/out/err
   * are closed in the child, and stdin is /dev/null.
   *
   * @param argv Program and arguments; must not be empty.
   * @param timeout Kill the program (SIGKILL) after this long and return -1.
   * @return The result.
   */
  result_t run(const std::vector<std::string> &argv, std::chrono::milliseconds timeout);
}  // namespace run_program
