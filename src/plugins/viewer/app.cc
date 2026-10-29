// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.viewer.app -- The viewer's part of the program: a picture, an
// avatar or a video opened in it -- the whole picture asked of the
// pictures part, a video fetched into its file and played, frame by frame,
// what is coming said -- and put away.
export module mux.plugin.viewer.app;

import std;
import splice;
import skia;
import mux.composition;
import mux.core;
import mux.config;
import mux.ui;
import mux.platform.video;
import mux.app.network;
import mux.app.services;
import mux.plugin.viewer.types;
import mux.plugin.viewer.ui;

export namespace mux::plugins::viewer {

class part {
 public:
  explicit part(app::services& shared) : s_(&shared) {}

  // A picture: over the window, with the thumbnail at once and the whole
  // picture when it comes -- from the disk, or from the account.
  void apply(const ui::request::open_picture& one) {
    ui::show(*s_->showing, std::optional(viewer_facts{one.source, one.sender, one.name, one.when, {}, std::nullopt}));
    if (const auto& chosen = s_->chosen())
      s_->whole_wanted.emplace_back(chosen->account, one.source);
  }
  void apply(const ui::request::close_shown<viewer_facts>&) { ui::show<viewer_facts>(*s_->showing, std::nullopt); }
  // A video: the viewer on its thumbnail at once; the video from its file
  // where it was fetched before, else fetched, the loader showing how far.
  void apply(const ui::request::open_video& one) {
    ui::show(*s_->showing, std::optional(viewer_facts{one.source, one.sender, one.name, one.when, one.video, std::nullopt}));
    ui::stopped_downloads().erase(one.video);
    this->watch(one.video);
  }
  // An avatar pressed: its picture in the viewer. A chat's by the chat's id;
  // a person's by theirs, as the chat being read knows them -- a member, or
  // the other side of a direct chat, whose avatar is the chat's.
  void apply(const ui::request::open_avatar& one) {
    const auto& chosen = s_->chosen();
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
    if ((!source || source->empty()) && !mux::ui::is_group(*chat) && one.key == mux::ui::contact_of(s_->ui, *chat)) {
      source = chat->avatar;
      shown = mux::ui::display_name(*chat);
    }
    // Someone not in the chat -- a forwarded message's sender, whose card was
    // opened from the forward: their profile, as the account fetched it for
    // the card, which drew its avatar from it. Looked for only among the
    // members, the press on that avatar did nothing.
    if (!source || source->empty())
      if (const auto* kept = s_->model->accounts().find(chat->id.account))
        if (const auto found = kept->profiles.find(one.key); found != kept->profiles.end() && found->second.avatar) {
          source = found->second.avatar;
          if (found->second.name && !found->second.name->empty())
            shown = *found->second.name;
        }
    if (!source || source->empty())
      return;
    this->apply(ui::request::open_picture{*source, shown, "avatar", ""});
  }
  // A video's file come: kept, and played where the viewer is still up for it.
  void take(const change::avatar_loaded& picture) {
    if (!spl::visit(spl::overloaded{[](const media_use::to_watch&) { return true; }, [](const auto&) { return false; }}, picture.use))
      return;
    fetching_.erase(picture.source);
    const auto where = video_file(picture.source);
    std::error_code failed;
    std::filesystem::create_directories(where.parent_path(), failed);
    std::ofstream(where, std::ios::binary) << picture.bytes;
    this->play_video_file(picture.source, where);
  }
  // The video's loader: its download stopped, or asked again.
  void apply(const press_video_loader& one) {
    const auto& chosen = s_->chosen();
    if (!chosen)
      return;
    if (fetching_.erase(one.video)) {
      s_->net->cancel_media(chosen->account, one.video);
      ui::download_progress().erase(one.video);
      ui::stopped_downloads().insert(one.video);
    } else {
      ui::stopped_downloads().erase(one.video);
      this->watch(one.video);
    }
  }
  // The video paused or played on; gone to a share of it.
  void apply(const toggle_video&) {
    if (player_)
      player_->toggle();
  }
  void apply(const video_seek& one) {
    if (player_)
      player_->seek(one.share * player_->length());
  }
  // Each frame, while the viewer is up: what it has coming -- the whole
  // picture, or the video's file, how far -- and the video played once its
  // file is here, its frame put where the viewer draws it from.
  void tick() {
    const auto& facts = *s_->showing->look<std::optional<viewer_facts>>();
    viewer_progress now;
    if (!facts) {
      if (player_) {
        video_frames().erase(played_);
        player_.reset();
        played_.clear();
      }
      this->put(now);
      return;
    }
    if (!facts->video.empty() && facts->file && played_ != facts->video) {
      player_ = mux::platform::video::player::open(*facts->file);
      played_ = facts->video;
    }
    if (player_) {
      const double at = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
      if (player_->advance(at) && player_->picture()) {
        video_frames().insert_or_assign(played_, player_->picture());
        viewer_waiters().wake();
        ++frame_;
      }
      now.video = video_shown{played_, !player_->paused(), player_->position(), player_->length(), frame_};
    }
    const std::string& loading = facts->video.empty() ? facts->source : facts->video;
    const bool coming = facts->video.empty() ? !ui::whole_pictures().has(facts->source) : !player_;
    now.load = viewer_load{coming, coming ? ui::progress_of(loading) : std::nullopt, ui::stopped_downloads().contains(loading)};
    this->put(std::move(now));
  }

 private:
  void watch(const std::string& video) {
    if (const auto where = video_file(video); std::filesystem::exists(where)) {
      this->play_video_file(video, where);
      return;
    }
    if (const auto& chosen = s_->chosen(); chosen && fetching_.insert(video).second)
      s_->net->fetch_media(chosen->account, video, media_use::to_watch{}, 0);
  }
  void play_video_file(const std::string& video, const std::filesystem::path& file) {
    auto now = *s_->showing->look<std::optional<viewer_facts>>();
    if (!now || now->video != video)
      return;
    now->file = file;
    ui::show(*s_->showing, std::move(now));
  }
  void put(viewer_progress now) {
    if (*s_->showing->look<viewer_progress>() != now)
      ui::show(*s_->showing, std::move(now));
  }
  // Where a video is kept once fetched: a file of its own, named by it.
  [[nodiscard]] static std::filesystem::path video_file(std::string_view source) {
    return config::cache_path("videos") / config::file_name_of(source);
  }

  app::services* s_;
  // Videos being fetched.
  std::set<std::string> fetching_;
  // The video playing, by its source; its frames counted.
  std::unique_ptr<mux::platform::video::player> player_;
  std::string played_;
  std::uint64_t frame_ = 0;
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{shared}; }

}  // namespace mux::plugins::viewer
