/**
 * @file src/library/title.h
 * @brief Declarations for cleaning up and comparing game titles.
 */
#pragma once

// standard includes
#include <string>
#include <string_view>

namespace library::title {

  /**
   * @brief Turn a folder, executable or store name into a readable game title.
   *
   * Removes release-group and repack tags ("[DODI Repack]", "-FitGirl"), version and build
   * suffixes ("v1.2.3", "Build 12345"), and replaces separator characters with spaces,
   * while keeping edition words such as "Enhanced" that distinguish real products.
   *
   * @param raw Raw name, for example a folder name.
   * @return Cleaned title, or the trimmed input when nothing is left after cleaning.
   */
  std::string clean(std::string_view raw);

  /**
   * @brief Build the comparison key for a title.
   *
   * Lowercases, folds punctuation to spaces, drops trademark symbols, marketing edition
   * words ("GOTY", "Deluxe Edition", "Remastered" is kept) and collapses whitespace, so
   * "Marvel's Spider-Man 2" and "marvels spider man 2" compare equal.
   *
   * @param title Title to normalise.
   * @return Normalised key.
   */
  std::string match_key(std::string_view title);

  /**
   * @brief Score how similar two titles are.
   *
   * Combines token overlap with an edit-distance ratio of the match keys.
   *
   * @param a First title.
   * @param b Second title.
   * @return Similarity from 0 (unrelated) to 1 (same key).
   */
  double similarity(std::string_view a, std::string_view b);

}  // namespace library::title
