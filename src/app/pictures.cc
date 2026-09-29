// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.pictures: the pictures and files a chat shows and saves -- the
// avatars of chats and people, the thumbnails of the pictures in messages,
// whole pictures for the viewer -- fetched once each, kept on disk under a
// budget, read back from there, and files saved to Downloads.
export module mux.app.pictures;

import std;
import skia;
import mux.core;
import mux.config;
import mux.media;
import mux.host;
import mux.ui;
import mux.app.network;
import mux.app.requests;
import mux.app.services;
import mux.app.workers;
import mux.logic.blurhash;

export namespace mux::app {

class pictures_part {
 public:
  explicit pictures_part(services& shared) : s_(&shared) {}

  // How much is kept: in memory by the caches, on disk by the files.
  void set_limits(const mux::config::cache_limits& limits) {
    // The pictures of messages under the limit set, their thumbnails and the
    // whole ones apart; avatars under a quarter of it, never under 16 MiB.
    const std::size_t pictures = static_cast<std::size_t>(limits.pictures_in_memory_mb) << 20;
    mux::ui::thumbnails().budget = pictures;
    mux::ui::whole_pictures().budget = pictures / 2;
    mux::ui::avatar_images().budget = std::max<std::size_t>(pictures / 4, 16u << 20);
    on_disk_ = static_cast<std::uintmax_t>(limits.pictures_on_disk_mb) << 20;
  }

  // The bytes of a picture or a file fetched: shown, kept, or saved -- as
  // what it was fetched for says.
  void take(const change::avatar_loaded& picture, bool fresh) {
    // Decoded on a worker, put in its cache on the UI's thread.
    auto* scene = s_->scene;
    const auto shown = [&](mux::ui::image_cache& cache, const std::string& key) {
      auto bytes = std::make_shared<const std::string>(picture.bytes);
      s_->work->run([bytes, target = &cache, key, scene]() -> workers::done_t {
        auto image = skia::decodeImage(bytes->data(), bytes->size());
        return [image = std::move(image), target, key, scene]() mutable {
          if (image) {
            target->put(key, std::move(image));
            scene->state().markDamaged();
          }
        };
      });
    };
    // A whole picture: its frames, where it moves; else it, still.
    const auto shown_whole = [&](const std::string& key) {
      auto bytes = std::make_shared<const std::string>(picture.bytes);
      s_->work->run([bytes, key, scene]() -> workers::done_t {
        auto frames = skia::decodeFrames(bytes->data(), bytes->size());
        return [frames = std::move(frames), key, scene]() mutable {
          if (frames.size() > 1)
            mux::ui::animations().put(key, std::move(frames));
          else if (!frames.empty())
            mux::ui::whole_pictures().put(key, std::move(frames.front().image));
          scene->state().markDamaged();
        };
      });
    };
    std::visit(overloaded{[&](const media_use::avatar& one) {
                            shown(mux::ui::avatar_images(), one.of);
                            if (fresh)
                              avatars_fetched_.erase(picture.source);
                          },
                          [&](const media_use::thumbnail&) {
                            shown(mux::ui::thumbnails(), picture.source);
                            if (fresh)
                              thumbnails_fetched_.erase(picture.source);
                          },
                          [&](const media_use::whole&) {
                            shown_whole(picture.source);
                            if (fresh)
                              wholes_fetched_.erase(picture.source);
                          },
                          // A file fetched to be saved: into Downloads, a number
                          // added where the name is taken, and opened; or only saved.
                          [&](const media_use::to_open& one) { this->save_download(picture.bytes, one.name, true); },
                          [&](const media_use::to_save& one) { this->save_download(picture.bytes, one.name, false); }},
               picture.use);
    if (!fresh)
      return;
    if (const auto where = kept_file(picture.use, picture.source)) {
      std::error_code failed;
      std::filesystem::create_directories(where->parent_path(), failed);
      std::ofstream(*where, std::ios::binary) << picture.bytes;
      if (++written_ % 50 == 1)
        this->prune();
    }
  }

  // What the model has pictures of and the window has not: from the disk
  // where they were fetched before, from the account where not -- the chats'
  // avatars, the people of the chat being read, and the thumbnails of the
  // pictures in the bubbles made.
  void ask() {
    if (s_->demo())
      return;
    const auto want = [&](const account_id& of, const std::optional<std::string>& source, const std::string& key) {
      // Shown already, or on its way: nothing to do.
      if (!source || source->empty() || mux::ui::avatar_images().has(key) || avatars_fetched_.contains(*source))
        return;
      if (this->read_back(media_use::avatar{key}, *source))
        return;
      avatars_fetched_.insert(*source);
      s_->net->fetch_avatar(of, *source, key);
    };
    auto& screen = s_->root().main();
    for (const auto& [id, account] : s_->model->accounts())
      for (const auto& [key, one] : account.conversations) {
        want(id, one.avatar, one.id.id);
        if (screen.chosen == one.id) {
          // The people of the bubbles made -- not every member: a big room
          // has thousands, and asking for all of them at each refresh read
          // them from the disk, pushed out what is on screen, and read them
          // again.
          std::set<std::string_view> senders;
          const auto [first, last] = screen.made_indices(one.timeline);
          for (std::size_t i = first; i < last && i < one.timeline.size(); ++i)
            senders.insert(one.timeline[i].sender);
          for (const member& each : one.members)
            if (senders.contains(each.id))
              want(id, each.avatar, each.id);
          // Those on screen and near it, at twice the size they are drawn
          // at -- not every picture in its history, which pushed the rest out.
          const auto [from, to] = screen.made_indices(one.timeline);
          for (std::size_t i = from; i < to && i < one.timeline.size(); ++i) {
            const message& said = one.timeline[i];
            if (said.attachment && is_picture(said.attachment->kind)) {
              this->want_thumbnail(id, said.attachment->source);
              this->make_preview(*said.attachment);
              // One that moves: the whole of it, for its frames.
              if (moves(said.attachment->kind))
                this->want_whole(id, said.attachment->source);
            }
            // And of a picture a message made quotes, for its quote.
            if (said.replies_to)
              if (const auto quoted = std::ranges::find(one.timeline, *said.replies_to, &message::id);
                  quoted != one.timeline.end() && quoted->attachment && is_picture(quoted->attachment->kind))
                this->want_thumbnail(id, quoted->attachment->source);
          }
        }
      }
  }

  // Everything kept let go: the stored pictures cleared.
  void clear() {
    std::error_code failed;
    std::filesystem::remove_all(mux::config::cache_path("avatars"), failed);
    mux::ui::avatar_images().clear();
    mux::ui::thumbnails().clear();
    mux::ui::whole_pictures().clear();
    avatars_fetched_.clear();
    thumbnails_fetched_.clear();
    wholes_fetched_.clear();
  }

  // The viewer: over the window, with the thumbnail at once and the whole
  // picture when it comes -- from the disk, or from the account.
  void apply(const request::open_picture& one) {
    s_->root().open_picture(one.source, one.sender, one.name, one.when);
    const auto& chosen = s_->root().main().chosen;
    if (chosen && !mux::ui::whole_pictures().has(one.source) && !this->read_back(media_use::whole{}, one.source) &&
        wholes_fetched_.insert(one.source).second)
      s_->net->fetch_media(chosen->account, one.source, media_use::whole{}, 0);
  }
  void apply(const request::close_picture&) { s_->root().close_picture(); }
  void apply(const request::save_picture& one) { this->save(one.source, "image"); }
  // A file in a message, pressed: fetched, saved to Downloads, and opened.
  void apply(const request::open_file& one) {
    if (const auto& chosen = s_->root().main().chosen)
      s_->net->fetch_media(chosen->account, one.source, media_use::to_open{one.name}, 0);
  }

  // A GIF kept among the saved ones: its whole, from the disk where it is
  // kept once it has played. Named by its source, so saving it twice keeps
  // one; touched, so it comes first.
  void save_gif(const std::string& source) {
    const auto kept = kept_file(media_use::whole{}, source);
    std::ifstream file;
    if (kept)
      file.open(*kept, std::ios::binary);
    if (!file.is_open()) {
      s_->root().show_message("GIFs", "The GIF has not loaded yet. Save it once it plays.");
      return;
    }
    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::error_code failed;
    std::filesystem::create_directories(gifs(), failed);
    std::string name;
    for (const char c : source)
      name += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    std::ofstream(gifs() / name, std::ios::binary) << bytes;
    s_->root().show_message("GIFs", "Saved to your GIFs.");
  }
  // The saved GIFs, newest first, to the input's GIF tab; each decoded on a
  // worker into the frames it plays, where it is not already.
  void apply(const request::show_gifs&) {
    std::error_code failed;
    std::vector<std::pair<std::filesystem::file_time_type, std::string>> found;
    for (const auto& entry : std::filesystem::directory_iterator(gifs(), failed))
      if (entry.is_regular_file(failed))
        found.emplace_back(entry.last_write_time(failed), entry.path().string());
    std::ranges::sort(found, std::greater{});
    std::vector<std::string> paths;
    auto* scene = s_->scene;
    for (const auto& [when, path] : found) {
      paths.push_back(path);
      const std::string key = "gif:" + path;
      if (mux::ui::animations().has(key) || mux::ui::whole_pictures().has(key) || !gifs_decoding_.insert(key).second)
        continue;
      s_->work->run([path, key, scene]() -> workers::done_t {
        std::ifstream in(path, std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto frames = skia::decodeFrames(bytes.data(), bytes.size());
        return [frames = std::move(frames), key, scene]() mutable {
          if (frames.size() > 1)
            mux::ui::animations().put(key, std::move(frames));
          else if (!frames.empty())
            mux::ui::whole_pictures().put(key, std::move(frames.front().image));
          scene->state().markDamaged();
        };
      });
    }
    s_->root().show_gifs(paths);
  }
  // Where the saved GIFs are kept: with the program's state, not its cache,
  // which is pruned.
  static std::filesystem::path gifs() { return mux::config::state_path("gifs"); }

  // A picture or a file saved to Downloads under `name`: from the disk
  // where the whole of it is kept, from the account where not.
  void save(const std::string& source, const std::string& name) {
    if (const auto kept = kept_file(media_use::whole{}, source))
      if (std::ifstream file{*kept, std::ios::binary}) {
        std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        this->save_download(bytes, name, false);
        return;
      }
    if (const auto& chosen = s_->root().main().chosen)
      s_->net->fetch_media(chosen->account, source, media_use::to_save{name}, 0);
  }

 private:
  // Where a kind of picture is kept on disk, by its source; nothing for a
  // file fetched to be saved, which goes to Downloads instead. Named as
  // before -- an avatar by its source, a thumbnail and a whole picture with
  // thumb_ and full_ before it -- so what was kept is found.
  static std::optional<std::filesystem::path> kept_file(const media_use_t& use, std::string_view source) {
    const auto named = [&](std::string_view kind) {
      std::string name(kind);
      for (const char c : source)
        name += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
      return std::optional(mux::config::cache_path("avatars") / name);
    };
    return std::visit(overloaded{[&](const media_use::avatar&) { return named(""); },
                                 [&](const media_use::thumbnail&) { return named("thumb_"); },
                                 [&](const media_use::whole&) { return named("full_"); },
                                 [](const media_use::to_open&) { return std::optional<std::filesystem::path>(); },
                                 [](const media_use::to_save&) { return std::optional<std::filesystem::path>(); }},
                      use);
  }

  // A picture read back from the disk, where it was kept: shown again.
  bool read_back(const media_use_t& use, const std::string& source) {
    const auto where = kept_file(use, source);
    if (!where)
      return false;
    std::ifstream file{*where, std::ios::binary};
    if (!file)
      return false;
    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::error_code failed;
    std::filesystem::last_write_time(*where, std::filesystem::file_time_type::clock::now(), failed);  // used now
    this->take(change::avatar_loaded{use, source, std::move(bytes)}, false);
    return true;
  }

  // A picture's blurred preview, from its blurhash, until its thumbnail
  // comes: 32 across, in its proportions; made on a worker.
  void make_preview(const attachment& picture) {
    if (!picture.blurhash || mux::ui::thumbnails().has(picture.source) || mux::ui::previews().has(picture.source) ||
        !previews_asked_.insert(picture.source).second)
      return;
    const int width = 32;
    const int height = picture.width > 0 && picture.height > 0
                           ? std::clamp(width * picture.height / picture.width, 8, 96)
                           : 24;
    s_->work->run([hash = *picture.blurhash, source = picture.source, width, height,
                   scene = s_->scene]() -> workers::done_t {
      skia::Sp<skia::SkImage> image;
      if (const auto pixels = logic::blurhash_pixels(hash, width, height))
        image = skia::imageFromRGBA(width, height, pixels->data());
      return [image = std::move(image), source, scene]() mutable {
        if (image) {
          mux::ui::previews().put(source, std::move(image));
          scene->state().markDamaged();
        }
      };
    });
  }
  std::set<std::string> previews_asked_;

  // The whole of a picture that moves, for its frames: from the disk where
  // it was fetched before, from the account where not.
  void want_whole(const account_id& of, const std::string& source) {
    if (source.empty() || mux::ui::animations().has(source) || mux::ui::whole_pictures().has(source) ||
        wholes_fetched_.contains(source))
      return;
    if (this->read_back(media_use::whole{}, source))
      return;
    wholes_fetched_.insert(source);
    s_->net->fetch_media(of, source, media_use::whole{}, 0);
  }

  // A message's picture's thumbnail: from the disk where it was fetched
  // before, from the account where not.
  void want_thumbnail(const account_id& of, const std::string& source) {
    if (source.empty() || mux::ui::thumbnails().has(source) || thumbnails_fetched_.contains(source))
      return;
    if (this->read_back(media_use::thumbnail{}, source))
      return;
    thumbnails_fetched_.insert(source);
    s_->net->fetch_media(of, source, media_use::thumbnail{}, 860);
  }

  // The pictures on disk held to their budget: the least recently used go
  // first, a file's time being when it was last read or written.
  void prune() const {
    std::error_code failed;
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> files;
    std::uintmax_t total = 0;
    for (const auto& entry : std::filesystem::directory_iterator(mux::config::cache_path("avatars"), failed)) {
      if (!entry.is_regular_file(failed))
        continue;
      total += entry.file_size(failed);
      files.emplace_back(entry.last_write_time(failed), entry.path());
    }
    if (total <= on_disk_)
      return;
    std::ranges::sort(files);
    for (const auto& [when, path] : files) {
      if (total <= on_disk_)
        break;
      const auto size = std::filesystem::file_size(path, failed);
      if (std::filesystem::remove(path, failed))
        total -= size;
    }
  }

  // Bytes into Downloads, named as given -- a picture's type added where the
  // name has none, a number where the name is taken -- and opened, or said.
  void save_download(const std::string& bytes, std::string name, bool open) {
    if (const auto type = mux::media::picture_of(bytes); type && !name.contains('.'))
      name += std::format(".{}", mux::media::extension_of(*type));
    const std::filesystem::path base = std::filesystem::path(name).filename();
    std::error_code failed;
    std::filesystem::create_directories(downloads(), failed);
    auto where = downloads() / (base.empty() ? std::filesystem::path("file") : base);
    for (int n = 1; std::filesystem::exists(where, failed); ++n)
      where = downloads() / std::format("{} ({}){}", base.stem().string(), n, base.extension().string());
    std::ofstream(where, std::ios::binary) << bytes;
    if (open)
      mux::host::open_url("file://" + where.string());
    else
      s_->root().show_message("Saved", std::format("Saved to {}", where.string()));
  }
  static std::filesystem::path downloads() {
    if (const char* home = std::getenv("HOME"); home && *home)
      return std::filesystem::path(home) / "Downloads";
    return std::filesystem::current_path();
  }

  services* s_;
  // What is being fetched from a server, not to be asked for twice: avatars
  // by their source, thumbnails and whole pictures by theirs.
  std::set<std::string> avatars_fetched_, thumbnails_fetched_, wholes_fetched_;
  // The saved GIFs being decoded, not to be decoded twice.
  std::set<std::string> gifs_decoding_;
  std::size_t written_ = 0;
  std::uintmax_t on_disk_ = 512u << 20;
};

}  // namespace mux::app
