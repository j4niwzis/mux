// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.pictures: the pictures and files a chat shows and saves -- the
// avatars of chats and people, the thumbnails of the pictures in messages,
// whole pictures for the viewer -- fetched once each, kept on disk under a
// budget, read back from there, and files saved to Downloads.
export module mux.app.pictures;

import std;
import splice;
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
import mux.audio;

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
    // Decoded on a worker -- no larger than it is shown, where that is
    // known: `most` pixels on its longer side -- and put in its cache on the
    // UI's thread. What waits for it is woken by the cache; the window was
    // repainted whole for each picture that came.
    const auto shown = [&](mux::ui::image_cache& cache, const std::string& key, int most) {
      auto bytes = std::make_shared<const std::string>(picture.bytes);
      s_->work->run([bytes, target = &cache, key, most]() -> workers::done_t {
        auto image = skia::decodeImageAtMost(bytes->data(), bytes->size(), most);
        return [image = std::move(image), target, key]() mutable {
          if (image)
            target->put(key, std::move(image));
        };
      });
    };
    // A whole picture: its frames, where it moves; else it, still.
    const auto shown_whole = [&](const std::string& key) {
      auto bytes = std::make_shared<const std::string>(picture.bytes);
      s_->work->run([bytes, key]() -> workers::done_t {
        auto frames = skia::decodeFrames(bytes->data(), bytes->size());
        return [frames = std::move(frames), key]() mutable {
          if (frames.size() > 1)
            mux::ui::animations().put(key, std::move(frames));
          else if (!frames.empty())
            mux::ui::whole_pictures().put(key, std::move(frames.front().image));
        };
      });
    };
    splice::visit(splice::overloaded{[&](const media_use::avatar& one) {
                            // An avatar is shown at 120 px at most -- twice that on a dense screen.
                            shown(mux::ui::avatar_images(), one.of, 256);
                            if (fresh)
                              avatars_fetched_.erase(picture.source);
                          },
                          [&](const media_use::thumbnail&) {
                            shown(mux::ui::thumbnails(), picture.source, 0);
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
                          [&](const media_use::to_play&) { this->play(picture.source, picture.bytes); },
                          [&](const media_use::to_copy&) { copy_bytes(picture.bytes); },
                          // A video: into its file, and played where the viewer waits for it.
                          [&](const media_use::to_watch&) {
                            videos_fetching_.erase(picture.source);
                            const auto where = video_file(picture.source);
                            std::error_code failed;
                            std::filesystem::create_directories(where.parent_path(), failed);
                            std::ofstream(where, std::ios::binary) << picture.bytes;
                            s_->root().play_video(picture.source, where);
                          },
                          // Saved where the dialog said, where it said; else into Downloads.
                          [&](const media_use::to_save& one) {
                            if (save_path_)
                              this->write_chosen(picture.bytes, *std::exchange(save_path_, std::nullopt));
                            else
                              this->save_download(picture.bytes, one.name, false);
                          }},
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
    // The images of the pack being edited, by the account of the chat in view.
    if (screen.current) {
      for (const std::string& url : mux::ui::pack_pictures_shown())
        want(*screen.current, url, url);
      // Those a dialog lists -- Explore's rooms, people found -- by their keys.
      for (const auto& [key, url] : mux::ui::listed_avatars())
        want(*screen.current, url, key);
    }
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
          // Those the forwards made are from: a member's picture as theirs,
          // anyone else's profile asked of their server, once, and its
          // picture then.
          for (std::size_t i = first; i < last && i < one.timeline.size(); ++i)
            if (const auto& forwarded = one.timeline[i].forwarded; forwarded && forwarded->from.starts_with('@')) {
              const std::string& from = forwarded->from;
              if (const auto in_room = std::ranges::find(one.members, from, &member::id); in_room != one.members.end() && in_room->avatar)
                want(id, in_room->avatar, from);
              else if (const auto known = profile_avatars.find(from); known != profile_avatars.end())
                want(id, known->second, from);
              else if (profiles_asked.insert(from).second)
                s_->net->fetch_profile(id, from);
            }
          // The reactions that are pictures -- custom emoji, mxc:// URLs --
          // and the custom emoji in the text (an <img> of the server's) of a
          // message shown: fetched as avatars are, keyed by the URL.
          const auto emoji_of = [&](const message& said) {
            for (const auto& [reaction, who] : said.reactions)
              if (reaction.starts_with("mxc://"))
                want(id, reaction, reaction);
            if (const auto& html = said.body.html)
              for (const auto& span : mux::ui::read_html(*html).spans)
                if (span.picture)
                  want(id, span.target, span.target);
          };
          for (std::size_t i = first; i < last && i < one.timeline.size(); ++i)
            emoji_of(one.timeline[i]);
          // The chat's pinned messages not loaded: fetched on their own, as
          // a quoted one is, for the pinned bar.
          for (const std::string& pinned : one.pinned)
            if (!one.quoted.contains(pinned) &&
                std::ranges::find(one.timeline, pinned, &message::id) == one.timeline.end() &&
                this->quote_due(pinned))
              s_->net->fetch_quoted(one.id, pinned);
          // The chat's own custom emoji and stickers, for its panels.
          for (const emote& custom : one.emotes)
            want(id, custom.url, custom.url);
          for (const emote& sticker : one.stickers) {
            want(id, sticker.url, sticker.url);
            // Its pack picture, for the panel tab of its pack.
            if (sticker.pack_avatar)
              want(id, sticker.pack_avatar, *sticker.pack_avatar);
          }
          // Those on screen and near it, at twice the size they are drawn
          // at -- not every picture in its history, which pushed the rest out.
          // While a message is being jumped to, only those right around it:
          // what was loaded on the way is not looked at.
          // Rooms named in its messages, not joined: their server asked,
          // once, whether they are there and what their picture is.
          for (const std::string& room : std::exchange(screen.rooms_wanted, {}))
            if (pill_rooms.insert(room).second)
              s_->net->preview_room(id, room, {});
          auto [from, to] = screen.made_indices(one.timeline);
          if (const auto& target = screen.jump_target()) {
            const auto found = std::ranges::find(one.timeline, *target, &message::id);
            if (found == one.timeline.end()) {
              from = to = 0;
            } else {
              constexpr std::size_t kAround = 5;
              const auto at = static_cast<std::size_t>(found - one.timeline.begin());
              from = at > kAround ? at - kAround : 0;
              to = std::min(one.timeline.size(), at + kAround + 1);
            }
          }
          // A message's pictures, its link's preview, and what it quotes.
          const auto pictures_of = [&](const message& said) {
            if (said.attachment && is_picture(said.attachment->kind)) {
              this->want_thumbnail(id, said.attachment->source);
              this->make_preview(*said.attachment);
              // One that moves: the whole of it, for its frames.
              if (moves(said.attachment->kind))
                this->want_whole(id, said.attachment->source);
            }
            // An album's pictures, each as one alone.
            for (const attachment& item : said.album)
              if (is_picture(item.kind)) {
                this->want_thumbnail(id, item.source);
                this->make_preview(item);
              }
            // Its first link's preview, once, where the chat shows them;
            // and the preview's picture.
            if (const auto link = s_->root().main().previews_off.contains(one.id)
                                      ? std::nullopt
                                      : mux::ui::first_link_of(said)) {
              if (const auto found = s_->model->previews.find(*link); found != s_->model->previews.end()) {
                if (found->second.image && !found->second.from_site)
                  want(id, found->second.image, *found->second.image);
                // From the site: only where this chat fetches previews so.
                else if (const std::string& image = found->second.image.value_or(std::string());
                         found->second.image && s_->kept->previews_direct(one.id) &&
                         !mux::ui::avatar_images().has(image) && !avatars_fetched_.contains(image) &&
                         !this->read_back(media_use::avatar{image}, image)) {
                  avatars_fetched_.insert(image);
                  s_->net->fetch_preview_picture(id, image);
                }
              } else if (links_asked_.insert(*link).second) {
                s_->net->fetch_preview(id, *link, s_->kept->previews_direct(one.id));
              }
            }
            // A message quoted that is neither in the timeline nor fetched:
            // fetched on its own, once.
            if (said.replies_to && !mux::ui::held_message(one, *said.replies_to) && this->quote_due(*said.replies_to))
              s_->net->fetch_quoted(one.id, *said.replies_to);
            // And of a picture a message made quotes, for its quote.
            if (said.replies_to)
              if (const message* quoted = mux::ui::held_message(one, *said.replies_to);
                  quoted && quoted->attachment && is_picture(quoted->attachment->kind))
                this->want_thumbnail(id, quoted->attachment->source);
          };
          for (std::size_t i = from; i < to && i < one.timeline.size(); ++i)
            pictures_of(one.timeline[i]);
          // The thread open beside the chat: its answers' pictures, senders
          // and emoji, as the timeline's -- they were never asked for, and
          // its pictures never came.
          if (const auto thread = screen.thread_open())
            if (const auto found = one.threads.find(*thread); found != one.threads.end())
              for (const message& said : found->second) {
                pictures_of(said);
                emoji_of(said);
                if (const auto by = std::ranges::find(one.members, said.sender, &member::id); by != one.members.end())
                  want(id, by->avatar, by->id);
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
  // A video: the viewer on its thumbnail at once; the video from its file
  // where it was fetched before, else fetched, the loader showing how far.
  void apply(const request::open_video& one) {
    s_->root().open_video(one.source, one.video, one.sender, one.name, one.when);
    videos_.insert(one.video);
    mux::ui::stopped_downloads().erase(one.video);
    this->watch(one.video);
  }
  void watch(const std::string& video) {
    if (const auto where = video_file(video); std::filesystem::exists(where)) {
      s_->root().play_video(video, where);
      return;
    }
    if (const auto& chosen = s_->root().main().chosen; chosen && videos_fetching_.insert(video).second)
      s_->net->fetch_media(chosen->account, video, media_use::to_watch{}, 0);
  }
  // Where a video is kept once fetched: a file of its own, named by it.
  [[nodiscard]] static std::filesystem::path video_file(std::string_view source) {
    return mux::config::cache_path("videos") / mux::config::file_name_of(source);
  }
  // The viewer's cross: the whole picture's download stopped, the thumbnail
  // left; pressed again (an arrow then), asked for again.
  void apply(const request::press_loader& one) {
    const auto& chosen = s_->root().main().chosen;
    if (!chosen)
      return;
    // A video's: its download stopped, or asked again.
    if (videos_.contains(one.source)) {
      if (videos_fetching_.erase(one.source)) {
        s_->net->cancel_media(chosen->account, one.source);
        mux::ui::download_progress().erase(one.source);
        mux::ui::stopped_downloads().insert(one.source);
      } else {
        mux::ui::stopped_downloads().erase(one.source);
        this->watch(one.source);
      }
      return;
    }
    if (wholes_fetched_.erase(one.source)) {
      s_->net->cancel_media(chosen->account, one.source);
      mux::ui::download_progress().erase(one.source);
      mux::ui::stopped_downloads().insert(one.source);
    } else {
      mux::ui::stopped_downloads().erase(one.source);
      this->want_whole(chosen->account, one.source);
    }
  }
  void apply(const request::save_picture& one) { this->save(one.source, "image"); }
  void apply(const request::copy_picture& one) { this->copy(one.source); }
  // A picture copied: onto the clipboard as PNG, from what is kept whole --
  // fetched whole first where it is not.
  void copy(const std::string& source) {
    if (const auto bytes = this->kept_whole(source)) {
      this->copy_bytes(*bytes);
      return;
    }
    if (const auto& chosen = s_->root().main().chosen)
      s_->net->fetch_media(chosen->account, source, media_use::to_copy{}, 0);
  }
  // Its pixels written anew as PNG: nothing of its file goes with them.
  static void copy_bytes(const std::string& bytes) {
    const auto image = skia::decodeImage(bytes.data(), bytes.size());
    if (!image)
      return;
    if (std::string png = skia::encodeImage(*image, false); !png.empty())
      mux::host::copy_picture(std::move(png));
  }
  // A file in a message, pressed: fetched, saved to Downloads, and opened.
  void apply(const request::open_file& one) {
    if (const auto& chosen = s_->root().main().chosen)
      s_->net->fetch_media(chosen->account, one.source, media_use::to_open{one.name}, 0);
  }

  // Sound pressed: the one playing paused or played on; another decoded --
  // from the disk where it was fetched before, from the account where not
  // -- on a worker, and played.
  void apply(const request::play_audio& one) {
    auto& speaker = mux::audio::the_speaker();
    if (speaker.holds(one.source)) {
      speaker.toggle();
      return;
    }
    if (const auto bytes = this->kept_whole(one.source)) {
      this->play(one.source, *bytes);
      return;
    }
    if (const auto& chosen = s_->root().main().chosen)
      s_->net->fetch_media(chosen->account, one.source, media_use::to_play{}, 0);
  }
  void play(const std::string& source, const std::string& bytes) {
    auto kept = std::make_shared<const std::string>(bytes);
    auto* scene = s_->scene;
    s_->work->run([kept, source, scene]() -> workers::done_t {
      auto sound = mux::audio::decode(*kept);
      return [sound = std::move(sound), source, scene]() {
        if (sound)
          mux::audio::the_speaker().play(source, *sound);
        scene->state().markDamaged();
      };
    });
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
    std::ofstream(gifs() / mux::config::file_name_of(source), std::ios::binary) << bytes;
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

  // A picture or a file saved where the user says: the system's dialog
  // first, offering Downloads and its name -- a picture's type added where
  // the name has none and the bytes are here to tell it -- then the bytes,
  // from the disk where the whole of it is kept, from the account where not.
  void save(const std::string& source, const std::string& name) {
    std::string offered = name;
    if (const auto bytes = this->kept_whole(source))
      if (const auto type = mux::media::picture_of(*bytes); type && !offered.contains('.'))
        offered += std::format(".{}", mux::media::extension_of(*type));
    pending_save_ = std::pair{source, name};
    mux::host::choose_save_path((downloads() / std::filesystem::path(offered).filename()).string());
  }
  // The path chosen: what was asked to be saved, written there.
  void save_to(std::string path) {
    if (!pending_save_)
      return;
    const auto [source, name] = *std::exchange(pending_save_, std::nullopt);
    if (const auto bytes = this->kept_whole(source)) {
      this->write_chosen(*bytes, path);
      return;
    }
    save_path_ = std::move(path);
    if (const auto& chosen = s_->root().main().chosen)
      s_->net->fetch_media(chosen->account, source, media_use::to_save{name}, 0);
  }
  // An avatar pressed: its picture in the viewer. A chat's by the chat's id;
  // a person's by theirs, as the chat being read knows them -- a member, or
  // the other side of a direct chat, whose avatar is the chat's.
  void apply(const request::open_avatar& one) {
    const auto& chosen = s_->root().main().chosen;
    const conversation* chat = chosen ? s_->model->find(*chosen) : nullptr;
    if (!chat)
      return;
    std::optional<std::string> source;
    std::string shown = one.key;
    if (one.key == chat->id.id) {
      source = chat->avatar;
      shown = mux::ui::display_name(*chat);
    } else if (const auto found = std::ranges::find(chat->members, one.key, &member::id); found != chat->members.end()) {
      source = found->avatar;
      if (!found->name.empty())
        shown = found->name;
    }
    if ((!source || source->empty()) && !mux::ui::is_group(*chat) && one.key == mux::ui::contact_of(*chat)) {
      source = chat->avatar;
      shown = mux::ui::display_name(*chat);
    }
    if (!source || source->empty())
      return;
    this->apply(request::open_picture{*source, shown, "avatar", ""});
  }

 private:
  // The whole of a picture, where it is kept on disk.
  std::optional<std::string> kept_whole(const std::string& source) const {
    const auto kept = kept_file(media_use::whole{}, source);
    if (!kept)
      return std::nullopt;
    std::ifstream file{*kept, std::ios::binary};
    if (!file.is_open())
      return std::nullopt;
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  }
  // Bytes written where the dialog said, and said.
  void write_chosen(const std::string& bytes, const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    out << bytes;
    s_->root().show_message("Saved", out ? std::format("Saved to {}", path) : std::format("Could not write {}", path));
  }
  // What Save As… was asked for, until the dialog answers; and where the
  // bytes go when they have to be fetched first.
  std::optional<std::pair<std::string, std::string>> pending_save_;
  std::optional<std::string> save_path_;

  // Where a kind of picture is kept on disk, by its source; nothing for a
  // file fetched to be saved, which goes to Downloads instead. Named as
  // before -- an avatar by its source, a thumbnail and a whole picture with
  // thumb_ and full_ before it -- so what was kept is found.
  static std::optional<std::filesystem::path> kept_file(const media_use_t& use, std::string_view source) {
    const auto named = [&](std::string_view kind) {
      return std::optional(mux::config::cache_path("avatars") / (std::string(kind) + mux::config::file_name_of(source)));
    };
    return splice::visit(splice::overloaded{[&](const media_use::avatar&) { return named(""); },
                                 [&](const media_use::thumbnail&) { return named("thumb_"); },
                                 [&](const media_use::whole&) { return named("full_"); },
                                 [](const media_use::to_open&) { return std::optional<std::filesystem::path>(); },
                                 [](const media_use::to_save&) { return std::optional<std::filesystem::path>(); },
                                 [&](const media_use::to_play&) { return named("full_"); },
                                 [&](const media_use::to_copy&) { return named("full_"); },
                                 [](const media_use::to_watch&) { return std::optional<std::filesystem::path>(); }},
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
                           ? static_cast<int>(std::clamp<std::int64_t>(std::int64_t{width} * picture.height / picture.width, 8, 96))
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
  // Whether a file's name says it runs as a program where it is opened: on
  // Windows, macOS or a Linux desktop. Such a file a message brought is
  // saved and shown where, never opened from here (review 5).
  [[nodiscard]] static bool runs_when_opened(const std::filesystem::path& name) {
    static constexpr std::array<std::string_view, 34> kinds{
        ".exe", ".com", ".bat", ".cmd", ".scr", ".pif", ".msi", ".msp", ".lnk", ".url", ".js",   ".jse",
        ".vbs", ".vbe", ".wsf", ".wsh", ".ps1", ".psm1", ".hta", ".cpl", ".reg", ".jar", ".desktop", ".sh",
        ".run", ".appimage", ".command", ".app", ".pkg", ".dmg", ".apk", ".py", ".pl", ".deb"};
    const std::string extension = name.extension().string() | std::views::transform([](char c) {
                                    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                                  }) |
                                  std::ranges::to<std::string>();
    return std::ranges::contains(kinds, std::string_view(extension));
  }
  void save_download(const std::string& bytes, std::string name, bool open) {
    if (const auto type = mux::media::picture_of(bytes); type && !name.contains('.'))
      name += std::format(".{}", mux::media::extension_of(*type));
    const std::filesystem::path base = std::filesystem::path(name).filename();
    std::error_code failed;
    std::filesystem::create_directories(downloads(), failed);
    auto where = downloads() / (base.empty() ? std::filesystem::path("file") : base);
    const std::string stem = base.stem().string(), extension = base.extension().string();
    // Through vformat, not std::format: clang 23 crashes now and then on
    // basic_format_string<...>::__handles_ for this list of arguments, where
    // libc++'s format headers are in a unit twice -- `import std`, and the
    // skia module's global fragment (Ganesh's headers include <chrono>).
    // make_format_args makes no basic_format_string.
    for (int n = 1; std::filesystem::exists(where, failed); ++n)
      where = downloads() / std::vformat("{} ({}){}", std::make_format_args(stem, n, extension));
    std::ofstream(where, std::ios::binary) << bytes;
    if (open && runs_when_opened(where))
      s_->root().show_message("Saved, not opened",
                              std::format("Saved to {}. It was not opened: a file like it runs as a program.", where.string()));
    else if (open)
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
  // The quoted messages asked for, not to be asked twice.
  // The quotes asked for, and when: asked again where one has not come
  // in a while -- a fetch that failed, or one dropped since.
  std::map<std::string, std::chrono::steady_clock::time_point> quotes_asked_;
  [[nodiscard]] bool quote_due(const std::string& id) {
    const auto now = std::chrono::steady_clock::now();
    const auto [at, fresh] = quotes_asked_.try_emplace(id, now);
    if (fresh)
      return true;
    if (now - at->second < std::chrono::seconds(30))
      return false;
    at->second = now;
    return true;
  }
  // The links whose previews were asked for, not to be asked twice.
  std::set<std::string> links_asked_;
  // Videos opened, and those being fetched.
  std::set<std::string> videos_, videos_fetching_;

public:
  // Rooms asked of for a message's pill: what their server says goes to the
  // pill, not a card.
  std::set<std::string> pill_rooms;
  // People met outside the rooms -- a forward's sender: their pictures, by
  // their ID, as their servers gave them; and whose were asked, once.
  std::map<std::string, std::optional<std::string>> profile_avatars;
  std::set<std::string> profiles_asked;

private:
  std::size_t written_ = 0;
  std::uintmax_t on_disk_ = 512u << 20;
};

}  // namespace mux::app
