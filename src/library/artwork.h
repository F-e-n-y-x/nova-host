/**
 * @file src/library/artwork.h
 * @brief Declarations for finding, downloading and caching game artwork.
 */
#pragma once

// standard includes
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// local includes
#include "library_types.h"

namespace library::artwork {

  /**
   * @brief A decoded RGBA image.
   */
  struct image_t {
    int width = 0;  ///< Width in pixels.
    int height = 0;  ///< Height in pixels.
    std::vector<std::uint8_t> rgba;  ///< Pixels, 4 bytes each, row-major.
  };

  /**
   * @brief Decode a PNG or JPEG image.
   *
   * Rejects other formats and images larger than 8192 pixels on a side.
   *
   * @param bytes Encoded image.
   * @return The image, or nullopt when it isn't a valid PNG/JPEG.
   */
  std::optional<image_t> decode(std::string_view bytes);

  /**
   * @brief Scale an image down (never up) so it fits inside a box, keeping its aspect ratio.
   *
   * @param img Source image.
   * @param max_width Box width.
   * @param max_height Box height.
   * @return Scaled copy (area-averaged).
   */
  image_t fit(const image_t &img, int max_width, int max_height);

  /**
   * @brief Crop the centre square of an image and scale it to a size.
   *
   * @param img Source image.
   * @param size Output width and height.
   * @return Square image.
   */
  image_t square(const image_t &img, int size);

  /**
   * @brief Encode an image as PNG.
   *
   * @param img Image.
   * @return PNG bytes, empty on failure.
   */
  std::string encode_png(const image_t &img);

  /**
   * @brief Encode an image as JPEG.
   *
   * @param img Image.
   * @param quality 1..100.
   * @return JPEG bytes, empty on failure.
   */
  std::string encode_jpeg(const image_t &img, int quality);

  /**
   * @brief Whether Nova may download images or call APIs on a host.
   *
   * Only Steam, SteamGridDB, Epic, GOG and Amazon image hosts, plus the IGDB/Twitch and RAWG
   * metadata APIs, are allowed.
   *
   * @param host Host name from a URL.
   * @return True when allowed.
   */
  bool allowed_host(std::string_view host);

  /**
   * @brief Download a URL over HTTPS from an allowed host.
   *
   * No redirects are followed; the response must be 200 and at most @p max_bytes.
   *
   * @param url HTTPS URL.
   * @param max_bytes Size limit.
   * @param bearer Optional bearer token (SteamGridDB API key).
   * @return Body, or nullopt on any failure.
   */
  std::optional<std::string> http_get(const std::string &url, std::size_t max_bytes, const std::string &bearer = {});

  /**
   * @brief HTTPS request to an allowed host with extra headers and an optional POST body.
   *
   * Same safety rules as @ref http_get (HTTPS only, allowlisted host, no redirects, size cap).
   *
   * @param url HTTPS URL.
   * @param max_bytes Size limit.
   * @param headers Extra request headers ("Name: value").
   * @param post_body When set, the request is a POST with this body.
   * @return Body, or nullopt on any failure or non-200 status.
   */
  std::optional<std::string> http_request(const std::string &url, std::size_t max_bytes, const std::vector<std::string> &headers, const std::optional<std::string> &post_body = std::nullopt);

  /**
   * @brief Filters applied to SteamGridDB image requests.
   */
  struct sgdb_options_t {
    std::string poster_style;  ///< Grid style (alternate, blurred, white_logo, material, no_logo); empty = any.
    std::string hero_style;  ///< Hero style (alternate, blurred, material); empty = any.
    bool animated = false;  ///< Also accept animated images.
    bool nsfw = false;  ///< Also accept images marked NSFW.
    bool humor = false;  ///< Also accept images marked as humor.
  };

  /**
   * @brief Query string for a SteamGridDB image endpoint.
   *
   * @param opts Filters.
   * @param kind Artwork kind (styles only apply to posters and heroes; posters also ask for 600x900).
   * @return Query without the leading '?', e.g. "types=static&nsfw=false&humor=false&dimensions=600x900".
   */
  std::string sgdb_query(const sgdb_options_t &opts, art_kind_e kind);

  /**
   * @brief A Steam store search hit.
   */
  struct store_hit_t {
    std::uint32_t appid = 0;  ///< App id.
    std::string name;  ///< Store name.
  };

  /**
   * @brief Parse Steam's storesearch API response.
   *
   * @param json Response body.
   * @return App hits in order.
   */
  std::vector<store_hit_t> parse_store_search(std::string_view json);

  /**
   * @brief Search the Steam store for a title.
   *
   * @param term Title to search for.
   * @return Hits, empty on failure.
   */
  std::vector<store_hit_t> store_search(const std::string &term);

  /**
   * @brief A SteamGridDB game.
   */
  struct sgdb_game_t {
    std::uint64_t id = 0;  ///< SteamGridDB game id.
    std::string name;  ///< Game name.
  };

  /**
   * @brief Parse a SteamGridDB /search/autocomplete response.
   *
   * @param json Response body.
   * @return Games in order.
   */
  std::vector<sgdb_game_t> parse_sgdb_search(std::string_view json);

  /**
   * @brief Parse a SteamGridDB grids/heroes/logos/icons response into artwork references.
   *
   * @param json Response body.
   * @param kind Kind of artwork the endpoint returns.
   * @param limit Maximum number of references.
   * @return References to images on SteamGridDB's CDN.
   */
  std::vector<art_ref_t> parse_sgdb_images(std::string_view json, art_kind_e kind, std::size_t limit);

  /**
   * @brief Search SteamGridDB by title.
   *
   * @param api_key User's SteamGridDB API key.
   * @param term Title.
   * @return Games, empty on failure or without a key.
   */
  std::vector<sgdb_game_t> sgdb_search(const std::string &api_key, const std::string &term);

  /**
   * @brief Fetch SteamGridDB artwork for a game.
   *
   * @param api_key User's SteamGridDB API key.
   * @param game_id SteamGridDB game id (used when @p steam_appid is 0).
   * @param steam_appid Steam app id, preferred when known.
   * @param opts Style and content filters.
   * @return Posters, heroes, logos and icons (a few of each).
   */
  std::vector<art_ref_t> sgdb_artwork(const std::string &api_key, std::uint64_t game_id, std::uint32_t steam_appid, const sgdb_options_t &opts = {});

  /**
   * @brief Load, validate and store one piece of artwork in a folder.
   *
   * Posters and icons are written as PNG (Moonlight clients require PNG box art),
   * heroes as JPEG, logos as PNG. Posters are scaled to fit 600x900 and icons are
   * square 256x256.
   *
   * @param ref Where to load the image from.
   * @param dir Destination folder, created if needed.
   * @return Written file, or nullopt when the image couldn't be loaded or isn't valid.
   */
  std::optional<std::filesystem::path> store(const art_ref_t &ref, const std::filesystem::path &dir);

  /**
   * @brief Make a square icon from a stored poster.
   *
   * @param poster Poster PNG written by @ref store.
   * @param dir Destination folder.
   * @return Written icon, or nullopt.
   */
  std::optional<std::filesystem::path> icon_from_poster(const std::filesystem::path &poster, const std::filesystem::path &dir);

}  // namespace library::artwork
