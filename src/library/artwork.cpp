/**
 * @file src/library/artwork.cpp
 * @brief Definitions for finding, downloading and caching game artwork.
 */
// standard includes
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <sstream>
#include <system_error>

// lib includes
#include <curl/curl.h>
#include <nlohmann/json.hpp>

// local includes
#include "artwork.h"

// stb single-header libraries (public domain / MIT), restricted to the formats Nova needs.
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 8192
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(__GNUC__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wall"
  #pragma GCC diagnostic ignored "-Wextra"
  #pragma GCC diagnostic ignored "-Wconversion"
  #pragma GCC diagnostic ignored "-Wsign-conversion"
  #pragma GCC diagnostic ignored "-Wunused-function"
  #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include <stb/stb_image.h>
#include <stb/stb_image_write.h>
#if defined(__GNUC__)
  #pragma GCC diagnostic pop
#endif

namespace fs = std::filesystem;

namespace library::artwork {
  namespace {
    constexpr std::size_t max_image_bytes = 20 * 1024 * 1024;  ///< Largest image Nova downloads or reads.
    constexpr const char *user_agent = "Nova-Host (+https://github.com/F-e-n-y-x/nova-host)";  ///< HTTP User-Agent.

    /**
     * @brief stb_image_write callback that appends to a string.
     *
     * @param context Target std::string.
     * @param data Bytes.
     * @param size Byte count.
     */
    void append_to_string(void *context, void *data, int size) {
      static_cast<std::string *>(context)->append(static_cast<const char *>(data), static_cast<std::size_t>(size));
    }

    /**
     * @brief State for the libcurl write callback.
     */
    struct sink_t {
      std::string body;  ///< Response body.
      std::size_t limit;  ///< Abort when the body would grow past this.
    };

    /**
     * @brief libcurl write callback with a size limit.
     *
     * @param ptr Data.
     * @param size Element size.
     * @param nmemb Element count.
     * @param userdata sink_t.
     * @return Bytes consumed; fewer aborts the transfer.
     */
    std::size_t write_body(char *ptr, std::size_t size, std::size_t nmemb, void *userdata) {
      auto *sink = static_cast<sink_t *>(userdata);
      const auto n = size * nmemb;
      if (sink->body.size() + n > sink->limit) {
        return 0;
      }
      sink->body.append(ptr, n);
      return n;
    }

    /**
     * @brief Host part of a URL.
     *
     * @param url URL.
     * @return Host, or empty when unparseable or not HTTPS.
     */
    std::string https_host(const std::string &url) {
      if (!url.starts_with("https://")) {
        return {};
      }
      CURLU *u = curl_url();
      std::string host;
      if (curl_url_set(u, CURLUPART_URL, url.c_str(), 0) == CURLUE_OK) {
        char *h = nullptr;
        if (curl_url_get(u, CURLUPART_HOST, &h, 0) == CURLUE_OK && h) {
          host = h;
          curl_free(h);
        }
      }
      curl_url_cleanup(u);
      return host;
    }

    /**
     * @brief Percent-encode for a URL path segment or query value.
     *
     * @param s Text.
     * @return Encoded text.
     */
    std::string escape(const std::string &s) {
      char *e = curl_easy_escape(nullptr, s.c_str(), static_cast<int>(s.size()));
      std::string out = e ? e : "";
      curl_free(e);
      return out;
    }

    /**
     * @brief Read a local image file with a size limit.
     *
     * @param path File.
     * @return Bytes, or nullopt.
     */
    std::optional<std::string> read_local(const fs::path &path) {
      std::error_code ec;
      if (!fs::is_regular_file(path, ec) || fs::file_size(path, ec) > max_image_bytes) {
        return std::nullopt;
      }
      std::ifstream in(path, std::ios::binary);
      std::ostringstream ss;
      ss << in.rdbuf();
      return ss.str();
    }

    /**
     * @brief Write bytes to a file atomically (temporary file + rename).
     *
     * @param path Destination.
     * @param bytes Contents.
     * @return True on success.
     */
    bool write_atomic(const fs::path &path, const std::string &bytes) {
      std::error_code ec;
      fs::create_directories(path.parent_path(), ec);
      const auto tmp = path.string() + ".tmp";
      {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
          return false;
        }
      }
      fs::rename(tmp, path, ec);
      return !ec;
    }
  }  // namespace

  std::optional<image_t> decode(std::string_view bytes) {
    if (bytes.size() < 8 || bytes.size() > max_image_bytes) {
      return std::nullopt;
    }
    const auto *data = reinterpret_cast<const stbi_uc *>(bytes.data());
    const bool png = std::memcmp(bytes.data(), "\x89PNG\r\n\x1a\n", 8) == 0;
    const bool jpeg = static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xD8;
    if (!png && !jpeg) {
      return std::nullopt;
    }
    int w = 0;
    int h = 0;
    int channels = 0;
    // Refuse huge images before allocating: 16 megapixels is far above any cover art.
    if (!stbi_info_from_memory(data, static_cast<int>(bytes.size()), &w, &h, &channels) || static_cast<long long>(w) * h > 16LL * 1024 * 1024) {
      return std::nullopt;
    }
    stbi_uc *pixels = stbi_load_from_memory(data, static_cast<int>(bytes.size()), &w, &h, &channels, 4);
    if (!pixels) {
      return std::nullopt;
    }
    image_t img;
    img.width = w;
    img.height = h;
    img.rgba.assign(pixels, pixels + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    stbi_image_free(pixels);
    return img;
  }

  image_t fit(const image_t &img, int max_width, int max_height) {
    if (img.width <= max_width && img.height <= max_height) {
      return img;
    }
    const double scale = std::min(static_cast<double>(max_width) / img.width, static_cast<double>(max_height) / img.height);
    image_t out;
    out.width = std::max(1, static_cast<int>(img.width * scale));
    out.height = std::max(1, static_cast<int>(img.height * scale));
    out.rgba.assign(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height) * 4, 0);
    for (int y = 0; y < out.height; ++y) {
      const int y0 = y * img.height / out.height;
      const int y1 = std::max(y0 + 1, (y + 1) * img.height / out.height);
      for (int x = 0; x < out.width; ++x) {
        const int x0 = x * img.width / out.width;
        const int x1 = std::max(x0 + 1, (x + 1) * img.width / out.width);
        std::array<std::uint32_t, 4> sum {};
        for (int sy = y0; sy < y1; ++sy) {
          for (int sx = x0; sx < x1; ++sx) {
            const auto *p = &img.rgba[(static_cast<std::size_t>(sy) * static_cast<std::size_t>(img.width) + static_cast<std::size_t>(sx)) * 4];
            for (int c = 0; c < 4; ++c) {
              sum[static_cast<std::size_t>(c)] += p[c];
            }
          }
        }
        const auto count = static_cast<std::uint32_t>((y1 - y0) * (x1 - x0));
        auto *q = &out.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width) + static_cast<std::size_t>(x)) * 4];
        for (int c = 0; c < 4; ++c) {
          q[c] = static_cast<std::uint8_t>(sum[static_cast<std::size_t>(c)] / count);
        }
      }
    }
    return out;
  }

  image_t square(const image_t &img, int size) {
    const int side = std::min(img.width, img.height);
    const int ox = (img.width - side) / 2;
    const int oy = (img.height - side) / 2;
    image_t crop;
    crop.width = side;
    crop.height = side;
    crop.rgba.resize(static_cast<std::size_t>(side) * static_cast<std::size_t>(side) * 4);
    for (int y = 0; y < side; ++y) {
      const auto *src = &img.rgba[(static_cast<std::size_t>(y + oy) * static_cast<std::size_t>(img.width) + static_cast<std::size_t>(ox)) * 4];
      std::memcpy(&crop.rgba[static_cast<std::size_t>(y) * static_cast<std::size_t>(side) * 4], src, static_cast<std::size_t>(side) * 4);
    }
    return fit(crop, size, size);
  }

  std::string encode_png(const image_t &img) {
    std::string out;
    if (img.width <= 0 || img.height <= 0) {
      return out;
    }
    stbi_write_png_to_func(append_to_string, &out, img.width, img.height, 4, img.rgba.data(), img.width * 4);
    return out;
  }

  std::string encode_jpeg(const image_t &img, int quality) {
    std::string out;
    if (img.width <= 0 || img.height <= 0) {
      return out;
    }
    stbi_write_jpg_to_func(append_to_string, &out, img.width, img.height, 4, img.rgba.data(), quality);
    return out;
  }

  bool allowed_host(std::string_view host) {
    static constexpr std::array<std::string_view, 7> exact {
      "store.steampowered.com", "steamcdn-a.akamaihd.net", "www.steamgriddb.com", "images.gog.com", "m.media-amazon.com",
      "images-na.ssl-images-amazon.com", "cdn2.unrealengine.com"
    };
    static constexpr std::array<std::string_view, 4> suffixes {".steamstatic.com", ".steamgriddb.com", ".epicgames.com", ".gog-statics.com"};
    if (std::ranges::find(exact, host) != exact.end()) {
      return true;
    }
    return std::ranges::any_of(suffixes, [host](std::string_view suffix) {
      return host.size() > suffix.size() && host.ends_with(suffix);
    });
  }

  std::optional<std::string> http_get(const std::string &url, std::size_t max_bytes, const std::string &bearer) {
    if (!allowed_host(https_host(url))) {
      return std::nullopt;
    }
    CURL *curl = curl_easy_init();
    if (!curl) {
      return std::nullopt;
    }
    sink_t sink {{}, max_bytes};
    curl_slist *headers = nullptr;
    if (!bearer.empty()) {
      headers = curl_slist_append(headers, ("Authorization: Bearer " + bearer).c_str());
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 25L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    if (headers) {
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }
    const auto code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (code != CURLE_OK || status != 200) {
      return std::nullopt;
    }
    return std::move(sink.body);
  }

  std::vector<store_hit_t> parse_store_search(std::string_view json) {
    std::vector<store_hit_t> out;
    const auto doc = nlohmann::json::parse(json, nullptr, false);
    if (!doc.is_object() || !doc.contains("items") || !doc["items"].is_array()) {
      return out;
    }
    for (const auto &item : doc["items"]) {
      if (!item.is_object() || item.value("type", std::string {}) != "app" || !item.contains("id") || !item["id"].is_number_unsigned()) {
        continue;
      }
      out.push_back({item["id"].get<std::uint32_t>(), item.value("name", std::string {})});
    }
    return out;
  }

  std::vector<store_hit_t> store_search(const std::string &term) {
    const auto body = http_get("https://store.steampowered.com/api/storesearch/?term=" + escape(term) + "&l=english&cc=US", 512 * 1024);
    return body ? parse_store_search(*body) : std::vector<store_hit_t> {};
  }

  std::vector<sgdb_game_t> parse_sgdb_search(std::string_view json) {
    std::vector<sgdb_game_t> out;
    const auto doc = nlohmann::json::parse(json, nullptr, false);
    if (!doc.is_object() || !doc.value("success", false) || !doc.contains("data") || !doc["data"].is_array()) {
      return out;
    }
    for (const auto &item : doc["data"]) {
      if (item.is_object() && item.contains("id") && item["id"].is_number_unsigned()) {
        out.push_back({item["id"].get<std::uint64_t>(), item.value("name", std::string {})});
      }
    }
    return out;
  }

  std::vector<art_ref_t> parse_sgdb_images(std::string_view json, art_kind_e kind, std::size_t limit) {
    std::vector<art_ref_t> out;
    const auto doc = nlohmann::json::parse(json, nullptr, false);
    if (!doc.is_object() || !doc.value("success", false) || !doc.contains("data") || !doc["data"].is_array()) {
      return out;
    }
    for (const auto &item : doc["data"]) {
      if (out.size() >= limit) {
        break;
      }
      if (!item.is_object() || item.value("nsfw", false)) {
        continue;
      }
      const auto url = item.value("url", std::string {});
      const auto mime = item.value("mime", std::string {});
      if (url.starts_with("https://") && (mime == "image/png" || mime == "image/jpeg" || mime.empty())) {
        out.push_back({kind, url, {}, "SteamGridDB"});
      }
    }
    return out;
  }

  std::vector<sgdb_game_t> sgdb_search(const std::string &api_key, const std::string &term) {
    if (api_key.empty()) {
      return {};
    }
    const auto body = http_get("https://www.steamgriddb.com/api/v2/search/autocomplete/" + escape(term), 512 * 1024, api_key);
    return body ? parse_sgdb_search(*body) : std::vector<sgdb_game_t> {};
  }

  std::vector<art_ref_t> sgdb_artwork(const std::string &api_key, std::uint64_t game_id, std::uint32_t steam_appid) {
    std::vector<art_ref_t> out;
    if (api_key.empty() || (game_id == 0 && steam_appid == 0)) {
      return out;
    }
    const std::string target = steam_appid ? "steam/" + std::to_string(steam_appid) : "game/" + std::to_string(game_id);
    const std::array<std::pair<const char *, art_kind_e>, 4> endpoints {{
      {"grids", art_kind_e::poster},
      {"heroes", art_kind_e::hero},
      {"logos", art_kind_e::logo},
      {"icons", art_kind_e::icon},
    }};
    for (const auto &[endpoint, kind] : endpoints) {
      std::string url = "https://www.steamgriddb.com/api/v2/" + std::string(endpoint) + "/" + target + "?types=static&nsfw=false";
      if (kind == art_kind_e::poster) {
        url += "&dimensions=600x900";
      }
      if (const auto body = http_get(url, 1024 * 1024, api_key)) {
        auto refs = parse_sgdb_images(*body, kind, 6);
        out.insert(out.end(), refs.begin(), refs.end());
      }
    }
    return out;
  }

  std::optional<fs::path> store(const art_ref_t &ref, const fs::path &dir) {
    const auto bytes = ref.url.empty() ? read_local(ref.path) : http_get(ref.url, max_image_bytes);
    if (!bytes) {
      return std::nullopt;
    }
    const auto img = decode(*bytes);
    if (!img) {
      return std::nullopt;
    }
    fs::path target;
    std::string encoded;
    switch (ref.kind) {
      case art_kind_e::poster:
        target = dir / "poster.png";
        encoded = encode_png(fit(*img, 600, 900));
        break;
      case art_kind_e::hero:
        target = dir / "hero.jpg";
        encoded = encode_jpeg(fit(*img, 1920, 1080), 86);
        break;
      case art_kind_e::logo:
        target = dir / "logo.png";
        encoded = encode_png(fit(*img, 800, 400));
        break;
      case art_kind_e::icon:
        target = dir / "icon.png";
        encoded = encode_png(square(*img, 256));
        break;
    }
    if (encoded.empty() || !write_atomic(target, encoded)) {
      return std::nullopt;
    }
    return target;
  }

  std::optional<fs::path> icon_from_poster(const fs::path &poster, const fs::path &dir) {
    const auto bytes = read_local(poster);
    if (!bytes) {
      return std::nullopt;
    }
    const auto img = decode(*bytes);
    if (!img) {
      return std::nullopt;
    }
    // Box art usually has the title at the top; the upper-middle square makes a recognisable icon.
    const int side = std::min(img->width, img->height);
    image_t crop;
    crop.width = side;
    crop.height = side;
    crop.rgba.resize(static_cast<std::size_t>(side) * static_cast<std::size_t>(side) * 4);
    const int ox = (img->width - side) / 2;
    const int oy = std::max(0, (img->height - side) / 3);
    for (int y = 0; y < side; ++y) {
      std::memcpy(&crop.rgba[static_cast<std::size_t>(y) * static_cast<std::size_t>(side) * 4],
                  &img->rgba[(static_cast<std::size_t>(y + oy) * static_cast<std::size_t>(img->width) + static_cast<std::size_t>(ox)) * 4],
                  static_cast<std::size_t>(side) * 4);
    }
    const auto encoded = encode_png(fit(crop, 256, 256));
    const auto target = dir / "icon.png";
    if (encoded.empty() || !write_atomic(target, encoded)) {
      return std::nullopt;
    }
    return target;
  }

}  // namespace library::artwork
