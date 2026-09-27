/**
 * @file src/library/library.cpp
 * @brief Definitions for game library scan jobs, artwork candidates and importing games as apps.
 */
// standard includes
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>

// local includes
#include "artwork.h"
#include "folder_scan.h"
#include "heroic.h"
#include "library.h"
#include "lutris.h"
#include "src/boost_process_compat.h"
#include "src/config.h"
#include "src/logging.h"
#include "src/nova_client_api.h"
#include "src/platform/common.h"
#include "src/process.h"
#include "steam.h"
#include "title.h"

namespace fs = std::filesystem;
using namespace std::literals;

namespace library {
  const char *to_string(art_kind_e kind) {
    switch (kind) {
      case art_kind_e::poster:
        return "poster";
      case art_kind_e::hero:
        return "hero";
      case art_kind_e::logo:
        return "logo";
      case art_kind_e::icon:
        return "icon";
    }
    return "poster";
  }

  const char *to_string(source_e source) {
    switch (source) {
      case source_e::folder:
        return "folder";
      case source_e::lutris:
        return "lutris";
      case source_e::steam:
        return "steam";
      case source_e::heroic:
        return "heroic";
    }
    return "folder";
  }

  namespace {
    constexpr std::size_t max_running_jobs = 2;  ///< Scans and imports that may run at once.
    constexpr std::size_t max_kept_jobs = 16;  ///< Finished jobs kept for status queries.
    constexpr std::size_t max_candidates = 5000;  ///< Artwork candidates kept in memory.
    constexpr std::size_t max_enriched_games = 200;  ///< Games matched online per scan.
    constexpr double match_threshold = 0.72;  ///< Minimum title similarity to accept a store match.

    /**
     * @brief A background scan or import.
     */
    struct job_t {
      std::string id;  ///< Job id.
      std::string kind;  ///< "scan" or "import".
      std::string source;  ///< Source name for scans.
      std::mutex mutex;  ///< Guards everything below.
      std::string state = "running";  ///< "running", "done", "failed" or "cancelled".
      std::string stage;  ///< Current step, for progress display.
      std::size_t done = 0;  ///< Items processed in the current stage.
      std::size_t total = 0;  ///< Items in the current stage.
      nlohmann::json result = nlohmann::json::object();  ///< Result once done.
      std::string error;  ///< Failure reason.
      std::vector<detected_game_t> games;  ///< Scan results, used by imports.
      std::atomic<bool> cancel_requested {false};  ///< Set by @ref library::cancel_job; checked between steps.
    };

    /**
     * @brief Thrown inside a job when the user asked it to stop.
     */
    struct job_cancelled_t: std::exception {
      /**
       * @brief Describe the exception.
       *
       * @return A fixed message.
       */
      const char *what() const noexcept override {
        return "cancelled";
      }
    };

    /**
     * @brief Stop the current job if the user asked for it.
     *
     * @param job Job to check.
     * @throws job_cancelled_t when cancellation was requested.
     */
    void throw_if_cancelled(const std::shared_ptr<job_t> &job) {
      if (job->cancel_requested.load()) {
        throw job_cancelled_t {};
      }
    }

    /**
     * @brief Mark a job as cancelled.
     *
     * @param job Job.
     */
    void mark_cancelled(const std::shared_ptr<job_t> &job) {
      BOOST_LOG(info) << "Library "sv << job->kind << " job "sv << job->id << " cancelled"sv;
      std::scoped_lock lock(job->mutex);
      job->state = "cancelled";
      job->stage.clear();
    }

    std::mutex jobs_mutex;  ///< Guards @ref jobs and @ref job_order.
    std::map<std::string, std::shared_ptr<job_t>, std::less<>> jobs;  ///< Jobs by id.
    std::deque<std::string> job_order;  ///< Job ids, oldest first.

    std::mutex candidates_mutex;  ///< Guards the candidate registry.
    std::map<std::string, art_ref_t, std::less<>> candidates;  ///< Artwork candidates by id.
    std::deque<std::string> candidate_order;  ///< Candidate ids, oldest first.
    std::uint64_t candidate_counter = 0;  ///< Source of candidate ids.

    /**
     * @brief Random hex id.
     *
     * @return 16 hex characters.
     */
    std::string random_id() {
      static thread_local std::mt19937_64 rng {std::random_device {}()};
      static constexpr char hex[] = "0123456789abcdef";
      std::string out;
      auto v = rng();
      for (int i = 0; i < 16; ++i) {
        out += hex[v & 0xF];
        v >>= 4;
      }
      return out;
    }

    /**
     * @brief 64-bit FNV-1a hash as hex, for stable folder names.
     *
     * @param s Input.
     * @return 16 hex characters.
     */
    std::string fnv1a_hex(std::string_view s) {
      std::uint64_t h = 1469598103934665603ULL;
      for (const unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
      }
      std::ostringstream out;
      out << std::hex;
      out.width(16);
      out.fill('0');
      out << h;
      return out.str();
    }

    /**
     * @brief Create a job, or refuse when too many are running.
     *
     * @param kind Job kind.
     * @param source Source name.
     * @return The job, or nullptr.
     */
    std::shared_ptr<job_t> create_job(const std::string &kind, const std::string &source) {
      std::scoped_lock lock(jobs_mutex);
      std::size_t running = 0;
      for (const auto &[id, job] : jobs) {
        std::scoped_lock job_lock(job->mutex);
        running += job->state == "running" ? 1 : 0;
      }
      if (running >= max_running_jobs) {
        return nullptr;
      }
      while (job_order.size() >= max_kept_jobs) {
        const auto oldest = job_order.front();
        const auto it = jobs.find(oldest);
        bool finished = true;
        if (it != jobs.end()) {
          std::scoped_lock job_lock(it->second->mutex);
          finished = it->second->state != "running";
        }
        if (!finished) {
          break;
        }
        jobs.erase(oldest);
        job_order.pop_front();
      }
      auto job = std::make_shared<job_t>();
      job->id = random_id();
      job->kind = kind;
      job->source = source;
      jobs[job->id] = job;
      job_order.push_back(job->id);
      return job;
    }

    /**
     * @brief Find a job.
     *
     * @param id Job id.
     * @return The job, or nullptr.
     */
    std::shared_ptr<job_t> find_job(std::string_view id) {
      std::scoped_lock lock(jobs_mutex);
      const auto it = jobs.find(id);
      return it == jobs.end() ? nullptr : it->second;
    }

    /**
     * @brief Read apps.json.
     *
     * @param path File.
     * @return Parsed tree with an "apps" array.
     */
    nlohmann::json read_apps(const fs::path &path) {
      std::ifstream in(path);
      nlohmann::json tree = nlohmann::json::parse(in, nullptr, false);
      if (!tree.is_object()) {
        tree = nlohmann::json::object();
      }
      if (!tree.contains("apps") || !tree["apps"].is_array()) {
        tree["apps"] = nlohmann::json::array();
      }
      return tree;
    }

    /**
     * @brief Escape "$" for Nova's $(VAR) expansion in app names.
     *
     * @param s Text.
     * @return Escaped text.
     */
    std::string escape_dollars(std::string_view s) {
      std::string out;
      for (const char c : s) {
        out += c;
        if (c == '$') {
          out += '$';
        }
      }
      return out;
    }

    /**
     * @brief Whether a user-supplied single-line string is acceptable.
     *
     * @param s Text.
     * @param max_len Maximum length.
     * @return True when non-empty, short enough and free of control characters.
     */
    bool valid_line(const std::string &s, std::size_t max_len) {
      return !s.empty() && s.size() <= max_len && std::ranges::none_of(s, [](unsigned char c) {
        return c < 0x20 || c == 0x7F;
      });
    }

    /**
     * @brief Build the apps.json entry for a detected game (without artwork).
     *
     * @param game Game.
     * @return Entry.
     */
    app_entry_t entry_for(const detected_game_t &game) {
      app_entry_t e;
      e.name = game.title;
      e.cmd = game.launch_cmd;
      e.working_dir = game.working_dir;
      e.source = to_string(game.source);
      e.source_id = game.source_id;
      e.steam_appid = game.steam_appid;
      return e;
    }

    /**
     * @brief Run a scan job.
     *
     * @param job Job.
     * @param source Source.
     * @param path Folder for folder scans.
     * @param settings Settings.
     */
    void run_scan(const std::shared_ptr<job_t> &job, source_e source, const fs::path &path, const settings_t &settings) {
      try {
        {
          std::scoped_lock lock(job->mutex);
          job->stage = "Scanning";
        }
        scan_result_t result;
        switch (source) {
          case source_e::folder:
            {
              folder::options_t options;
              options.windows_launcher = settings.windows_launcher;
              options.cancelled = [job]() {
                return job->cancel_requested.load();
              };
              result = folder::scan(path, options);
              break;
            }
          case source_e::lutris:
            result = lutris::scan(settings.home);
            break;
          case source_e::steam:
            result = steam::scan(settings.home);
            break;
          case source_e::heroic:
            result = heroic::scan(settings.home);
            break;
        }

        throw_if_cancelled(job);
        {
          std::scoped_lock lock(job->mutex);
          job->stage = "Matching";
          job->total = result.games.size();
        }
        enrich(result.games, settings, [&job](std::size_t done, std::size_t total) {
          throw_if_cancelled(job);
          std::scoped_lock lock(job->mutex);
          job->done = done;
          job->total = total;
        });
        throw_if_cancelled(job);

        nlohmann::json apps;
        {
          std::scoped_lock lock(apps_file_mutex());
          apps = read_apps(settings.apps_file)["apps"];
        }
        nlohmann::json items = nlohmann::json::array();
        for (std::size_t i = 0; i < result.games.size(); ++i) {
          auto item = game_to_json(result.games[i], job->id + ":" + std::to_string(i));
          item["already_in_library"] = is_duplicate(apps, entry_for(result.games[i]));
          items.push_back(std::move(item));
        }
        nlohmann::json skipped = nlohmann::json::array();
        for (const auto &s : result.skipped) {
          skipped.push_back({{"path", s.path}, {"reason", s.reason}});
        }

        std::scoped_lock lock(job->mutex);
        job->games = std::move(result.games);
        job->result = {{"items", std::move(items)}, {"skipped", std::move(skipped)}};
        job->state = "done";
        job->stage.clear();
      } catch (const job_cancelled_t &) {
        mark_cancelled(job);
      } catch (const std::exception &e) {
        BOOST_LOG(warning) << "Library scan failed: "sv << e.what();
        std::scoped_lock lock(job->mutex);
        job->state = "failed";
        job->error = e.what();
      }
    }

    /**
     * @brief Candidates of one kind for a game, best first.
     *
     * @param game Game.
     * @param kind Kind.
     * @return References.
     */
    std::vector<art_ref_t> refs_of_kind(const detected_game_t &game, art_kind_e kind) {
      std::vector<art_ref_t> out;
      for (const auto &ref : game.artwork) {
        if (ref.kind == kind) {
          out.push_back(ref);
        }
      }
      return out;
    }

    /**
     * @brief Run an import job.
     *
     * @param job Job.
     * @param items Request items.
     * @param settings Settings.
     */
    void run_import(const std::shared_ptr<job_t> &job, const nlohmann::json &items, const settings_t &settings) {
      try {
        std::vector<app_entry_t> entries;
        std::vector<std::string> temp_ids;
        nlohmann::json failed = nlohmann::json::array();
        {
          std::scoped_lock lock(job->mutex);
          job->stage = "Downloading artwork";
          job->total = items.size();
        }

        for (std::size_t n = 0; n < items.size(); ++n) {
          throw_if_cancelled(job);
          const auto &item = items[n];
          const auto temp_id = item.value("temp_id", std::string {});
          const auto colon = temp_id.find(':');
          const auto scan = colon == std::string::npos ? nullptr : find_job(temp_id.substr(0, colon));
          std::optional<detected_game_t> game;
          if (scan) {
            std::size_t index = 0;
            try {
              index = std::stoul(temp_id.substr(colon + 1));
            } catch (const std::exception &) {
              index = SIZE_MAX;
            }
            std::scoped_lock lock(scan->mutex);
            if (index < scan->games.size()) {
              game = scan->games[index];
            }
          }
          if (!game) {
            failed.push_back({{"temp_id", temp_id}, {"reason", "Unknown or expired scan result. Scan again."}});
            continue;
          }

          auto entry = entry_for(*game);
          if (const auto t = item.value("title", std::string {}); !t.empty()) {
            if (!valid_line(t, 128)) {
              failed.push_back({{"temp_id", temp_id}, {"reason", "The title is empty, too long or has control characters."}});
              continue;
            }
            entry.name = t;
          }
          if (const auto c = item.value("launch_cmd", std::string {}); !c.empty()) {
            if (!valid_line(c, 4096)) {
              failed.push_back({{"temp_id", temp_id}, {"reason", "The launch command is too long or has control characters."}});
              continue;
            }
            entry.cmd = c;
          }
          if (const auto w = item.value("working_dir", std::string {}); !w.empty()) {
            entry.working_dir = valid_line(w, 4096) ? w : entry.working_dir;
          }

          const auto dir = art_dir(settings.covers_dir, entry.source, entry.source_id);
          for (const auto kind : {art_kind_e::poster, art_kind_e::hero, art_kind_e::logo, art_kind_e::icon}) {
            const auto key = to_string(kind);
            std::vector<art_ref_t> choices;
            if (item.contains(key) && item[key].is_string()) {
              const auto choice = item[key].get<std::string>();
              if (choice == "none") {
                continue;
              }
              if (const auto ref = candidate(choice); ref && ref->kind == kind) {
                choices.push_back(*ref);
              }
            } else {
              choices = refs_of_kind(*game, kind);
            }
            for (const auto &ref : choices) {
              if (const auto stored = artwork::store(ref, dir)) {
                switch (kind) {
                  case art_kind_e::poster:
                    entry.poster = *stored;
                    break;
                  case art_kind_e::hero:
                    entry.hero = *stored;
                    break;
                  case art_kind_e::logo:
                    entry.logo = *stored;
                    break;
                  case art_kind_e::icon:
                    entry.icon = *stored;
                    break;
                }
                break;
              }
            }
          }
          if (entry.icon.empty() && !entry.poster.empty()) {
            if (const auto icon = artwork::icon_from_poster(entry.poster, dir)) {
              entry.icon = *icon;
            }
          }
          entries.push_back(std::move(entry));
          temp_ids.push_back(temp_id);
          std::scoped_lock lock(job->mutex);
          job->done = n + 1;
        }

        throw_if_cancelled(job);
        std::vector<bool> added;
        {
          std::scoped_lock lock(apps_file_mutex());
          auto tree = read_apps(settings.apps_file);
          added = merge_into_apps(tree, entries);
          if (std::ranges::any_of(added, [](bool b) {
                return b;
              })) {
            const auto tmp = settings.apps_file.string() + ".tmp";
            {
              std::ofstream out(tmp, std::ios::trunc);
              out << tree.dump(4);
            }
            fs::rename(tmp, settings.apps_file);
            proc::refresh(settings.apps_file.string());
          }
        }

        nlohmann::json imported = nlohmann::json::array();
        nlohmann::json duplicates = nlohmann::json::array();
        std::vector<nlohmann::json> fetch_details;
        for (std::size_t i = 0; i < entries.size(); ++i) {
          (added[i] ? imported : duplicates).push_back({{"temp_id", temp_ids[i]}, {"name", entries[i].name}, {"has_poster", !entries[i].poster.empty()}});
          if (added[i] && settings.meta.auto_fetch) {
            nlohmann::json app = {{"name", entries[i].name}, {"cmd", entries[i].cmd}};
            if (entries[i].steam_appid != 0) {
              app["nova-steam-appid"] = entries[i].steam_appid;
            }
            fetch_details.push_back(std::move(app));
          }
        }
        nova_api::prefetch_details(std::move(fetch_details));
        std::scoped_lock lock(job->mutex);
        job->result = {{"imported", std::move(imported)}, {"duplicates", std::move(duplicates)}, {"failed", std::move(failed)}};
        job->state = "done";
        job->stage.clear();
      } catch (const job_cancelled_t &) {
        mark_cancelled(job);
      } catch (const std::exception &e) {
        BOOST_LOG(warning) << "Library import failed: "sv << e.what();
        std::scoped_lock lock(job->mutex);
        job->state = "failed";
        job->error = e.what();
      }
    }

    /**
     * @brief Run an "apply artwork to an existing app" job.
     *
     * @param job Job.
     * @param app_index Index into the "apps" array.
     * @param refs Chosen candidates, one per kind.
     * @param settings Settings.
     */
    void run_apply_artwork(const std::shared_ptr<job_t> &job, std::size_t app_index, const std::vector<art_ref_t> &refs, const settings_t &settings) {
      try {
        nlohmann::json before;
        {
          std::scoped_lock lock(apps_file_mutex());
          const auto tree = read_apps(settings.apps_file);
          if (app_index >= tree["apps"].size()) {
            throw std::runtime_error("That app no longer exists. Reload the library and try again.");
          }
          before = tree["apps"][app_index];
        }
        const auto source = before.value("nova-source", std::string {"app"});
        const auto source_id = before.value("nova-source-id", before.value("uuid", before.value("name", std::string {})));
        const auto dir = art_dir(settings.covers_dir, source, source_id);
        {
          std::scoped_lock lock(job->mutex);
          job->stage = "Downloading artwork";
          job->total = refs.size();
        }

        std::map<art_kind_e, fs::path> stored;
        for (std::size_t n = 0; n < refs.size(); ++n) {
          throw_if_cancelled(job);
          if (const auto file = artwork::store(refs[n], dir)) {
            stored[refs[n].kind] = *file;
          }
          std::scoped_lock lock(job->mutex);
          job->done = n + 1;
        }
        if (stored.contains(art_kind_e::poster) && !stored.contains(art_kind_e::icon) && !before.contains("nova-icon")) {
          if (const auto icon = artwork::icon_from_poster(stored[art_kind_e::poster], dir)) {
            stored[art_kind_e::icon] = *icon;
          }
        }
        throw_if_cancelled(job);

        nlohmann::json applied = nlohmann::json::array();
        {
          std::scoped_lock lock(apps_file_mutex());
          auto tree = read_apps(settings.apps_file);
          // Indexes shift when apps are added, renamed or removed meanwhile; refuse rather than
          // write artwork onto the wrong app.
          if (app_index >= tree["apps"].size() || tree["apps"][app_index].value("name", std::string {}) != before.value("name", std::string {})) {
            throw std::runtime_error("The app list changed while downloading. Reload the library and try again.");
          }
          auto &app = tree["apps"][app_index];
          for (const auto &[kind, file] : stored) {
            set_app_art(app, kind, file);
            applied.push_back(to_string(kind));
          }
          if (!stored.empty()) {
            const auto tmp = settings.apps_file.string() + ".tmp";
            {
              std::ofstream out(tmp, std::ios::trunc);
              out << tree.dump(4);
            }
            fs::rename(tmp, settings.apps_file);
            proc::refresh(settings.apps_file.string());
          }
        }

        std::scoped_lock lock(job->mutex);
        job->result = {{"app_index", app_index}, {"applied", std::move(applied)}, {"requested", refs.size()}};
        job->state = "done";
        job->stage.clear();
      } catch (const job_cancelled_t &) {
        mark_cancelled(job);
      } catch (const std::exception &e) {
        BOOST_LOG(warning) << "Applying library artwork failed: "sv << e.what();
        std::scoped_lock lock(job->mutex);
        job->state = "failed";
        job->error = e.what();
      }
    }

    /**
     * @brief Whether a path is inside a folder (after resolving "..").
     *
     * @param base Folder.
     * @param path Path.
     * @return True when inside.
     */
    bool inside(const fs::path &base, const fs::path &path) {
      std::error_code ec;
      const auto b = fs::weakly_canonical(base, ec);
      const auto p = fs::weakly_canonical(path, ec);
      if (ec) {
        return false;
      }
      const auto rel = p.lexically_relative(b);
      return !rel.empty() && *rel.begin() != "..";
    }
  }  // namespace

  std::mutex &apps_file_mutex() {
    static std::mutex m;
    return m;
  }

  std::optional<std::string> start_task(const std::string &kind, task_fn_t work) {
    auto job = create_job(kind, {});
    if (!job) {
      return std::nullopt;
    }
    std::thread([job, work = std::move(work)]() {
      const task_progress_t progress = [job](std::size_t done, std::size_t total, const std::string &stage) {
        std::scoped_lock lock(job->mutex);
        job->done = done;
        job->total = total;
        job->stage = stage;
      };
      const std::function<bool()> cancelled = [job]() {
        return job->cancel_requested.load();
      };
      try {
        auto result = work(progress, cancelled);
        if (job->cancel_requested.load()) {
          mark_cancelled(job);
          return;
        }
        std::scoped_lock lock(job->mutex);
        job->result = std::move(result);
        job->state = "done";
        job->stage.clear();
      } catch (const std::exception &e) {
        BOOST_LOG(warning) << "Library "sv << job->kind << " job failed: "sv << e.what();
        std::scoped_lock lock(job->mutex);
        job->state = "failed";
        job->error = e.what();
      }
    }).detach();
    return job->id;
  }

  nlohmann::json load_apps(const fs::path &path) {
    return read_apps(path);
  }

  std::optional<nlohmann::json> update_app(const fs::path &path, std::size_t index, const std::function<void(nlohmann::json &)> &edit) {
    std::scoped_lock lock(apps_file_mutex());
    auto tree = read_apps(path);
    if (index >= tree["apps"].size() || !tree["apps"][index].is_object()) {
      return std::nullopt;
    }
    edit(tree["apps"][index]);
    const auto tmp = path.string() + ".tmp";
    {
      std::ofstream out(tmp, std::ios::trunc);
      out << tree.dump(4);
      if (!out) {
        return std::nullopt;
      }
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
      return std::nullopt;
    }
    proc::refresh(path.string());
    return tree["apps"][index];
  }

  std::string resolve_windows_launcher(const std::string &configured) {
#ifdef _WIN32
    return configured;
#else
    if (!configured.empty()) {
      return configured;
    }
    std::error_code ec;
    if (fs::exists("/usr/local/bin/run-windows-exe", ec)) {
      return "/usr/local/bin/run-windows-exe {exe}";
    }
    if (!boost::process::v1::search_path("umu-run").empty()) {
      return "umu-run {exe}";
    }
    return "wine {exe}";
#endif
  }

  settings_t current_settings() {
    settings_t s;
    if (const char *home = std::getenv("HOME")) {
      s.home = home;
    }
    s.apps_file = config::stream.file_apps;
    s.covers_dir = platf::appdata() / "covers" / "library";
    s.steamgriddb_api_key = config::library.steamgriddb_api_key;
    s.windows_launcher = resolve_windows_launcher(config::library.windows_exe_launcher);
    s.meta = metadata::from_config();
    return s;
  }

  std::optional<source_e> parse_source(std::string_view name) {
    if (name == "folder") {
      return source_e::folder;
    }
    if (name == "lutris") {
      return source_e::lutris;
    }
    if (name == "steam") {
      return source_e::steam;
    }
    if (name == "heroic") {
      return source_e::heroic;
    }
    return std::nullopt;
  }

  std::optional<art_kind_e> parse_kind(std::string_view name) {
    for (const auto kind : {art_kind_e::poster, art_kind_e::hero, art_kind_e::logo, art_kind_e::icon}) {
      if (name == to_string(kind)) {
        return kind;
      }
    }
    return std::nullopt;
  }

  bool safe_scan_root(const fs::path &path) {
    std::error_code ec;
    if (!path.is_absolute() || !fs::is_directory(path, ec)) {
      return false;
    }
    const auto canonical = fs::weakly_canonical(path, ec).generic_string();
    if (ec || canonical == "/") {
      return false;
    }
    return std::ranges::none_of(std::array<std::string_view, 4> {"/proc", "/sys", "/dev", "/run"}, [&canonical](std::string_view pseudo) {
      return canonical == pseudo || canonical.starts_with(std::string(pseudo) + "/");
    });
  }

  std::string register_candidate(const art_ref_t &ref) {
    std::scoped_lock lock(candidates_mutex);
    while (candidate_order.size() >= max_candidates) {
      candidates.erase(candidate_order.front());
      candidate_order.pop_front();
    }
    auto id = "c" + std::to_string(++candidate_counter) + random_id().substr(0, 6);
    candidates[id] = ref;
    candidate_order.push_back(id);
    return id;
  }

  std::optional<art_ref_t> candidate(const std::string &id) {
    std::scoped_lock lock(candidates_mutex);
    const auto it = candidates.find(id);
    return it == candidates.end() ? std::nullopt : std::optional<art_ref_t> {it->second};
  }

  nlohmann::json game_to_json(const detected_game_t &game, const std::string &temp_id) {
    nlohmann::json art = {{"poster", nlohmann::json::array()}, {"hero", nlohmann::json::array()}, {"logo", nlohmann::json::array()}, {"icon", nlohmann::json::array()}};
    for (const auto &ref : game.artwork) {
      nlohmann::json c = {{"id", register_candidate(ref)}, {"label", ref.label}};
      if (!ref.url.empty()) {
        c["url"] = ref.url;
      }
      art[to_string(ref.kind)].push_back(std::move(c));
    }
    nlohmann::json out = {
      {"temp_id", temp_id},
      {"title", game.title},
      {"source", to_string(game.source)},
      {"source_id", game.source_id},
      {"launch_cmd", game.launch_cmd},
      {"working_dir", game.working_dir},
      {"artwork", std::move(art)},
    };
    out["matched"] = game.steam_appid ? nlohmann::json {{"appid", game.steam_appid}, {"name", game.matched_name}, {"confidence", game.match_confidence}} : nlohmann::json(nullptr);
    return out;
  }

  void enrich(std::vector<detected_game_t> &games, const settings_t &settings, const std::function<void(std::size_t, std::size_t)> &progress) {
    const auto total = games.size();
    for (std::size_t i = 0; i < total; ++i) {
      auto &game = games[i];
      if (settings.online && i < max_enriched_games) {
        const auto has_url = [&game](const std::string &url) {
          return std::ranges::any_of(game.artwork, [&url](const art_ref_t &r) {
            return r.url == url;
          });
        };
        if (game.steam_appid == 0 && (settings.meta.steam || settings.meta.art_source_enabled("steam"))) {
          const auto query = title::clean(game.title);
          const auto hits = artwork::store_search(query);
          double best = 0.0;
          for (const auto &hit : hits) {
            if (const double s = title::similarity(query, hit.name); s > best) {
              best = s;
              if (s >= match_threshold) {
                game.steam_appid = hit.appid;
                game.matched_name = hit.name;
                game.match_confidence = s;
              }
            }
          }
          std::this_thread::sleep_for(250ms);
        }
        if (game.steam_appid && settings.meta.art_source_enabled("steam")) {
          for (auto &ref : steam::cdn_artwork(game.steam_appid)) {
            if (!has_url(ref.url)) {
              game.artwork.push_back(std::move(ref));
            }
          }
        }
        if (settings.meta.igdb_enabled() && settings.meta.art_source_enabled("igdb")) {
          const auto query = title::clean(game.title);
          for (const auto &hit : metadata::igdb_search(settings.meta, query)) {
            if (title::similarity(query, hit.name) >= match_threshold) {
              for (auto &ref : metadata::igdb_artwork(hit)) {
                game.artwork.push_back(std::move(ref));
              }
              break;
            }
          }
        }
        if (!settings.steamgriddb_api_key.empty() && settings.meta.art_source_enabled("steamgriddb")) {
          std::uint64_t sgdb_id = 0;
          if (game.steam_appid == 0) {
            const auto query = title::clean(game.title);
            for (const auto &hit : artwork::sgdb_search(settings.steamgriddb_api_key, query)) {
              if (title::similarity(query, hit.name) >= match_threshold) {
                sgdb_id = hit.id;
                break;
              }
            }
          }
          for (auto &ref : artwork::sgdb_artwork(settings.steamgriddb_api_key, sgdb_id, game.steam_appid, settings.meta.sgdb)) {
            game.artwork.push_back(std::move(ref));
          }
        }
      }
      metadata::rank_artwork(game.artwork, settings.meta);
      if (progress) {
        progress(i + 1, total);
      }
    }
  }

  nlohmann::json artwork_search(const std::string &query, std::uint32_t appid, const settings_t &settings) {
    nlohmann::json matches = nlohmann::json::array();
    detected_game_t probe;
    probe.title = query;
    if (settings.online && appid == 0 && !query.empty() && (settings.meta.steam || settings.meta.art_source_enabled("steam"))) {
      for (const auto &hit : artwork::store_search(query)) {
        if (matches.size() >= 8) {
          break;
        }
        matches.push_back({{"appid", hit.appid}, {"name", hit.name}, {"confidence", title::similarity(query, hit.name)}});
      }
    }
    // A confident title match gets its artwork right away, so "Choose artwork" works from a title alone.
    if (appid == 0 && !matches.empty() && matches.front()["confidence"].get<double>() >= match_threshold) {
      appid = matches.front()["appid"].get<std::uint32_t>();
    }
    if (appid) {
      probe.steam_appid = appid;
      if (settings.meta.art_source_enabled("steam")) {
        probe.artwork = steam::cdn_artwork(appid);
      }
    }
    if (settings.online && settings.meta.igdb_enabled() && settings.meta.art_source_enabled("igdb") && !query.empty()) {
      const auto hits = metadata::igdb_search(settings.meta, query);
      if (!hits.empty()) {
        auto refs = metadata::igdb_artwork(hits.front());
        probe.artwork.insert(probe.artwork.end(), refs.begin(), refs.end());
      }
    }
    if (settings.online && !settings.steamgriddb_api_key.empty() && settings.meta.art_source_enabled("steamgriddb")) {
      std::uint64_t sgdb_id = 0;
      if (appid == 0 && !query.empty()) {
        const auto hits = artwork::sgdb_search(settings.steamgriddb_api_key, query);
        if (!hits.empty()) {
          sgdb_id = hits.front().id;
        }
      }
      auto refs = artwork::sgdb_artwork(settings.steamgriddb_api_key, sgdb_id, appid, settings.meta.sgdb);
      probe.artwork.insert(probe.artwork.end(), refs.begin(), refs.end());
    }
    metadata::rank_artwork(probe.artwork, settings.meta);
    auto described = game_to_json(probe, "search");
    return {{"matches", std::move(matches)}, {"artwork", std::move(described["artwork"])}};
  }

  std::optional<std::string> start_scan(source_e source, const fs::path &path, const settings_t &settings) {
    auto job = create_job("scan", to_string(source));
    if (!job) {
      return std::nullopt;
    }
    std::thread([job, source, path, settings]() {
      run_scan(job, source, path, settings);
    }).detach();
    return job->id;
  }

  std::optional<std::string> start_import(const nlohmann::json &items, const settings_t &settings) {
    auto job = create_job("import", {});
    if (!job) {
      return std::nullopt;
    }
    std::thread([job, items, settings]() {
      run_import(job, items, settings);
    }).detach();
    return job->id;
  }

  bool cancel_job(const std::string &id) {
    const auto job = find_job(id);
    if (!job) {
      return false;
    }
    std::scoped_lock lock(job->mutex);
    if (job->state != "running") {
      return false;
    }
    job->cancel_requested = true;
    return true;
  }

  std::vector<art_ref_t> parse_art_choices(const nlohmann::json &choices) {
    if (!choices.is_object()) {
      throw std::invalid_argument("Artwork choices must be an object");
    }
    std::vector<art_ref_t> refs;
    for (const auto kind : {art_kind_e::poster, art_kind_e::hero, art_kind_e::logo, art_kind_e::icon}) {
      const auto key = to_string(kind);
      if (!choices.contains(key)) {
        continue;
      }
      if (!choices[key].is_string()) {
        throw std::invalid_argument(std::string("'") + key + "' must be a candidate id");
      }
      const auto ref = candidate(choices[key].get<std::string>());
      if (!ref || ref->kind != kind) {
        throw std::invalid_argument(std::string("Unknown or expired ") + key + " choice. Search artwork again.");
      }
      refs.push_back(*ref);
    }
    if (refs.empty()) {
      throw std::invalid_argument("Choose at least one of poster, hero, logo or icon");
    }
    return refs;
  }

  void set_app_art(nlohmann::json &app, art_kind_e kind, const fs::path &file) {
    switch (kind) {
      case art_kind_e::poster:
        app["image-path"] = file.string();
        break;
      case art_kind_e::hero:
        app["nova-hero"] = file.string();
        break;
      case art_kind_e::logo:
        app["nova-logo"] = file.string();
        break;
      case art_kind_e::icon:
        app["nova-icon"] = file.string();
        break;
    }
  }

  std::optional<std::string> start_apply_artwork(std::size_t app_index, const nlohmann::json &choices, const settings_t &settings) {
    auto refs = parse_art_choices(choices);
    auto job = create_job("artwork", {});
    if (!job) {
      return std::nullopt;
    }
    std::thread([job, app_index, refs = std::move(refs), settings]() {
      run_apply_artwork(job, app_index, refs, settings);
    }).detach();
    return job->id;
  }

  std::optional<nlohmann::json> job_status(const std::string &id) {
    const auto job = find_job(id);
    if (!job) {
      return std::nullopt;
    }
    std::scoped_lock lock(job->mutex);
    nlohmann::json out = {
      {"id", job->id},
      {"kind", job->kind},
      {"state", job->state},
      {"stage", job->stage},
      {"progress", {{"done", job->done}, {"total", job->total}}},
    };
    if (!job->source.empty()) {
      out["source"] = job->source;
    }
    if (job->state == "done") {
      out["result"] = job->result;
    }
    if (job->state == "failed") {
      out["error"] = job->error;
    }
    return out;
  }

  bool is_duplicate(const nlohmann::json &apps, const app_entry_t &entry) {
    if (!apps.is_array()) {
      return false;
    }
    return std::ranges::any_of(apps, [&entry](const nlohmann::json &app) {
      if (!app.is_object()) {
        return false;
      }
      if (app.value("nova-source", std::string {}) == entry.source && app.value("nova-source-id", std::string {}) == entry.source_id && !entry.source_id.empty()) {
        return true;
      }
      const auto cmd = app.value("cmd", std::string {});
      return !cmd.empty() && cmd == entry.cmd;
    });
  }

  std::vector<bool> merge_into_apps(nlohmann::json &tree, const std::vector<app_entry_t> &entries) {
    if (!tree.is_object()) {
      tree = nlohmann::json::object();
    }
    if (!tree.contains("apps") || !tree["apps"].is_array()) {
      tree["apps"] = nlohmann::json::array();
    }
    auto &apps = tree["apps"];
    std::vector<bool> added;
    for (const auto &e : entries) {
      if (e.name.empty() || e.cmd.empty() || is_duplicate(apps, e)) {
        added.push_back(false);
        continue;
      }
      nlohmann::json app = {
        {"name", escape_dollars(e.name)},
        {"cmd", e.cmd},
        {"nova-source", e.source},
        {"nova-source-id", e.source_id},
      };
      if (e.steam_appid != 0) {
        app["nova-steam-appid"] = e.steam_appid;
      }
      if (!e.working_dir.empty()) {
        app["working-dir"] = escape_dollars(e.working_dir);
      }
      if (!e.poster.empty()) {
        app["image-path"] = e.poster.string();
      }
      if (!e.hero.empty()) {
        app["nova-hero"] = e.hero.string();
      }
      if (!e.logo.empty()) {
        app["nova-logo"] = e.logo.string();
      }
      if (!e.icon.empty()) {
        app["nova-icon"] = e.icon.string();
      }
      apps.push_back(std::move(app));
      added.push_back(true);
    }
    std::stable_sort(apps.begin(), apps.end(), [](const nlohmann::json &a, const nlohmann::json &b) {
      return a.value("name", std::string {}) < b.value("name", std::string {});
    });
    return added;
  }

  fs::path art_dir(const fs::path &covers_dir, std::string_view source, std::string_view source_id) {
    return covers_dir / (std::string(source) + "-" + fnv1a_hex(source_id));
  }

  std::optional<fs::path> app_art(const nlohmann::json &app, art_kind_e kind, const fs::path &covers_dir) {
    if (!app.is_object()) {
      return std::nullopt;
    }
    std::error_code ec;
    const std::string key = kind == art_kind_e::poster ? "image-path" : "nova-" + std::string(to_string(kind));
    if (!app.contains(key) || !app[key].is_string()) {
      return std::nullopt;
    }
    const fs::path p = app[key].get<std::string>();
    if (!fs::is_regular_file(p, ec)) {
      return std::nullopt;
    }
    if (inside(covers_dir, p)) {
      return p;
    }
    // Posters set by hand (image-path) live elsewhere; only serve them when they are PNG files.
    if (kind == art_kind_e::poster && p.is_absolute()) {
      std::ifstream in(p, std::ios::binary);
      char sig[8] = {};
      in.read(sig, 8);
      if (in.gcount() == 8 && std::string_view(sig, 8) == "\x89PNG\r\n\x1a\n") {
        return p;
      }
    }
    return std::nullopt;
  }

}  // namespace library
