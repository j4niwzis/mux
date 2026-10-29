// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.packs.app -- The packs' part of the program: one's own pack,
// from Settings or an account's page; a room's, from its settings, editable
// where one's power there is what the room's state asks -- listed, saved,
// deleted, added to one's account, and images chosen for them uploaded.
export module mux.plugin.packs.app;

import std;
import splice;
import skia;
import mux.composition;
import mux.core;
import mux.protocols;
import mux.media;
import mux.platform.files;
import mux.ui;
import skiff.platform.dialogs;
import mux.app.network;
import mux.app.services;
import mux.plugin.packs.types;
import mux.plugin.packs.changes;
import mux.plugin.packs.ui;
import mux.plugin.packs.matrix;

export namespace mux::plugins::packs {

struct part {
  app::services* s_ = nullptr;
  std::optional<account_id> account_;
  bool picking_ = false;
  // The dialogs opened, each counting its drafts' openings from a number of
  // its own: an image uploaded for a dialog closed and opened again is not
  // the new one's draft's, though the account and the room are the same.
  std::uint64_t dialogs_ = 0;
  [[nodiscard]] std::uint64_t first_opening() { return ++dialogs_ << 32; }

  // One's own packs, the account's: listed as they open.
  void open_own() {
    ui::show(*s_->showing, std::optional(shown{.account = account_, .room = std::nullopt, .editable = true,
                                               .opened = this->first_opening(), .note = "Loading…"}));
    if (account_ && !s_->demo())
      s_->net->ask(*account_, list_asked{std::nullopt});
  }
  // From Settings: the first account that has packs.
  void apply(const open&) {
    account_ = s_->account_offering(proto::feature::sticker_packs{});
    this->open_own();
  }
  // From an account's page: that account's -- the only way to another's
  // than the first, where there are two.
  void apply(const open_account& one) {
    account_ = account_id{ui::protocol_of(one.address), one.address};
    this->open_own();
  }
  // A room's, from its settings: one of them opened where its key is said.
  void apply(const open_room& one) {
    const auto chosen = s_->managed();
    const conversation* chat = chosen ? s_->model->find(*chosen) : nullptr;
    if (!chat || !proto::offers(ui::protocol_state_of(s_->ui, chat->id.account), proto::feature::sticker_packs{}))
      return;
    account_ = chat->id.account;
    ui::show(*s_->showing, std::optional(shown{
        .account = chat->id.account, .room = chat->id.id,
        .editable = proto::chat_rights(ui::protocol_state_of(s_->ui, chat->id.account), *chat).edit_packs,
        .key = one.key, .opened = this->first_opening(), .note = "Loading…"}));
    if (!s_->demo())
      s_->net->ask(*account_, list_asked{chat->id.id});
  }
  void apply(const ui::request::close_shown<shown>&) { ui::show<shown>(*s_->showing, std::nullopt); }

  // What an account told of them: for the dialog where it is up for it --
  // the same account's, and the same room's (none: one's own).
  template <class Change>
  void change_shown_for(const account_id& by, const std::optional<std::string>& room, Change change) {
    const auto& open = *s_->showing->look<std::optional<shown>>();
    if (!open || open->account != std::optional(by) || open->room != room)
      return;
    ui::change_shown<std::optional<shown>>(*s_->showing, [&](std::optional<shown>& now) { change(*now); });
  }
  void take(const packs_listed& one) {
    this->change_shown_for(one.by, one.room, [&](shown& now) { packs_listed_in(now, one.packs); });
  }
  void take(const pack_saved& one) {
    this->change_shown_for(one.by, one.pack.chat, [&](shown& now) { pack_saved_in(now, one.pack, one.removed, one.done); });
  }
  void take(const pack_picture_uploaded& one) {
    this->change_shown_for(one.by, one.opening.room, [&](shown& now) { picture_uploaded_in(now, one.picture, one.done, one.opening); });
  }

  // What is shown of the packs, changed as `change` says.
  template <class Change>
  void change_packs(Change change) {
    if (!s_->showing->look<std::optional<shown>>()->has_value())
      return;
    ui::change_shown<std::optional<shown>>(*s_->showing, [&](std::optional<shown>& now) { change(*now); });
  }
  // The pack open, changed as `change` says, where one is.
  template <class Change>
  void change_draft(Change change) {
    this->change_packs([&](shown& now) {
      if (now.draft)
        change(*now.draft);
    });
  }
  void apply(const pack_open& one) {
    this->change_packs([&](shown& now) { open_pack(now, one.at); });
  }
  void apply(const pack_back&) { this->change_packs([](shown& now) { show_pack_list(now); }); }
  void apply(const pack_named& one) {
    this->change_draft([&](emote_pack& pack) { (one.attribution ? pack.attribution : pack.name) = one.text; });
  }
  // Never neither: the one flipped off turns the other on.
  void apply(const pack_use& one) {
    this->change_draft([&](emote_pack& pack) {
      if (one.emoji)
        pack.emoji = !pack.emoji || !pack.sticker;
      else
        pack.sticker = !pack.sticker || !pack.emoji;
    });
  }
  void apply(const picture_renamed& one) {
    this->change_draft([&](emote_pack& pack) {
      if (const auto found = std::ranges::find(pack.pictures, one.url, &pack_picture::url); found != pack.pictures.end())
        found->shortcode = one.shortcode;
    });
  }
  void apply(const picture_use& one) {
    this->change_draft([&](emote_pack& pack) {
      if (const auto found = std::ranges::find(pack.pictures, one.url, &pack_picture::url); found != pack.pictures.end()) {
        if (one.emoji)
          found->emoji = !found->emoji || !found->sticker;
        else
          found->sticker = !found->sticker || !found->emoji;
      }
    });
  }
  void apply(const picture_removed& one) {
    this->change_draft([&](emote_pack& pack) { std::erase_if(pack.pictures, [&](const pack_picture& it) { return it.url == one.url; }); });
  }
  // The pack open saved: its avatar its first image where it has none, and
  // the images with no shortcode left out.
  void apply(const pack_save&) {
    const auto& open = *s_->showing->look<std::optional<shown>>();
    if (!open || !open->draft)
      return;
    auto pack = *open->draft;
    if (!pack.avatar && !pack.pictures.empty())
      pack.avatar = pack.pictures.front().url;
    std::erase_if(pack.pictures, [](const pack_picture& one) { return one.shortcode.empty() || one.url.empty(); });
    this->change_packs([](shown& now) { now.note = "Saving…"; });
    if (account_ && !s_->demo())
      s_->net->ask(*account_, save_asked{std::move(pack)});
  }
  // As its protocol takes a pack away; a new one, not saved yet, is nothing.
  void apply(const pack_delete&) {
    const auto& open = *s_->showing->look<std::optional<shown>>();
    if (!open || !open->draft || open->draft->key.empty())
      return;
    const auto pack = *open->draft;
    this->change_packs([](shown& now) { now.note = "Deleting…"; });
    if (account_ && !s_->demo())
      s_->net->ask(*account_, remove_asked{pack});
  }
  void apply(const adopt& one) {
    const auto by = one.by ? one.by : account_;
    if (by && one.pack.chat && !s_->demo())
      s_->net->ask(*by, adopt_asked{one.pack});
  }
  // The emoji panel's "Add to account", on a room's pack.
  void apply(const mux::ui::request::adopt_pack& one) { apply(adopt{one.pack, one.by}); }
  void apply(const pick_images&) {
    picking_ = true;
    s_->system_dialogs->choose_files();
  }
  // Files chosen for the pack open: each read, and uploaded for its opening.
  bool took_files(const std::vector<std::string>& paths, bool dropped) {
    if (!std::exchange(picking_, false) || dropped)
      return false;
    if (!account_ || s_->demo())
      return true;
    // The editor's opening the images are for: an answer for another is not
    // this draft's.
    const auto& open = *s_->showing->look<std::optional<shown>>();
    if (!open || !open->draft)
      return true;
    const pack_opening opening{open->room, open->opened};
    for (const std::string& path : paths) {
      auto bytes_read = platform::files::read(path);
      if (!bytes_read)
        continue;
      std::string bytes = std::move(*bytes_read);
      const auto type = media::picture_of(bytes);
      if (!type)
        continue;
      const auto name = platform::files::name(path);
      pack_picture one{.shortcode = std::filesystem::path(name).stem().string(),
                       .body = name,
                       .mimetype = std::string(spl::visit([](auto kind) { return media::mimetype_of(kind); }, *type)),
                       .size = static_cast<std::int64_t>(bytes.size())};
      if (auto image = skia::decodeImage(bytes.data(), bytes.size())) {
        one.width = image->width();
        one.height = image->height();
      }
      s_->net->ask(*account_, upload_asked{std::move(one), std::move(bytes), opening});
    }
    return true;
  }
  // The pictures the dialog shows, for the pictures part to fetch: the pack
  // open's images, else each pack's own.
  void tick() {
    const auto& open = *s_->showing->look<std::optional<shown>>();
    auto wanted = open ? pictures_of(*open) : std::vector<emote_picture>{};
    if (wanted != s_->ui.plugin_pictures)
      s_->ui.plugin_pictures = std::move(wanted);
  }
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::packs
