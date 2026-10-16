// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.packs: Emojis & Stickers, as Cinny has them -- one's own pack,
// from Settings; a room's, from its settings, editable where one's power
// there is what the room's state asks -- listed, saved, deleted, and images
// chosen for them uploaded. A part of the program: it owns its state, and
// reaches the rest through the services it is given.
export module mux.app.packs;

import std;
import mux.platform.files;
import splice.bytes;
import splice;
import skia;
import mux.core;
import mux.protocols;
import mux.media;
import mux.ui;
import mux.ui.proto;
import mux.platform.dialogs;
import mux.app.network;
import mux.app.services;
import mux.app.requests;

export namespace mux::app {

struct packs_part {
  services* s_;
  std::optional<mux::account_id> account_;
  bool picking_ = false;
};

void part_apply(packs_part& self, const request::open_packs&);
void part_apply(packs_part& self, const request::open_room_packs&);
void part_apply(packs_part& self, const request::close_packs&);
void part_apply(packs_part& self, const request::save_pack& one);
void part_apply(packs_part& self, const request::adopt_pack& one);
void part_apply(packs_part& self, const request::delete_pack& one);
void part_apply(packs_part& self, const request::pick_pack_images&);
void part_apply(packs_part& self, const request::pack_open& one);
void part_apply(packs_part& self, const request::pack_back&);
void part_apply(packs_part& self, const request::pack_named& one);
void part_apply(packs_part& self, const request::pack_use& one);
void part_apply(packs_part& self, const request::pack_picture_renamed& one);
void part_apply(packs_part& self, const request::pack_picture_use& one);
void part_apply(packs_part& self, const request::pack_picture_removed& one);
void part_apply(packs_part& self, const request::pack_save&);
void part_apply(packs_part& self, const request::pack_delete&);
bool took_files(packs_part& self, const std::vector<std::string>& paths, bool dropped);

void part_apply(packs_part& self, const request::open_packs&) {
  self.account_ = self.s_->account_offering(mux::proto::feature::sticker_packs{});
  mux::ui::show(*self.s_->showing,
                std::optional(mux::ui::packs_facts{.account = self.account_, .room = std::nullopt, .editable = true, .note = "Loading\u2026"}));
  if (self.account_ && !self.s_->demo())
    self.s_->net->list_packs(*self.account_, std::nullopt);
}

void part_apply(packs_part& self, const request::open_room_packs& one) {
  const auto chosen = self.s_->managed();
  const mux::conversation* chat = chosen ? self.s_->model->find(*chosen) : nullptr;
  if (!chat || !mux::proto::offers(mux::ui::protocol_state_of(self.s_->ui, chat->id.account), mux::proto::feature::sticker_packs{}))
    return;
  self.account_ = chat->id.account;
  mux::ui::show(*self.s_->showing, std::optional(mux::ui::packs_facts{
      .account = chat->id.account, .room = chat->id.id, .editable = mux::proto::chat_rights(mux::ui::protocol_state_of(self.s_->ui, chat->id.account), *chat).edit_packs,
      .key = one.key, .note = "Loading\u2026"}));
  if (!self.s_->demo())
    self.s_->net->list_packs(*self.account_, chat->id.id);
}

void part_apply(packs_part& self, const request::close_packs&) {
  mux::ui::show<mux::ui::packs_facts>(*self.s_->showing, std::nullopt);
}

// What is shown of the packs, changed as `change` says.
template <class Change>
void change_packs(packs_part& self, Change change) {
  if (!self.s_->showing->look<std::optional<mux::ui::packs_facts>>()->has_value())
    return;
  mux::ui::change_shown<std::optional<mux::ui::packs_facts>>(*self.s_->showing, [&](std::optional<mux::ui::packs_facts>& now) { change(*now); });
}
// The pack open, changed as `change` says, where one is.
template <class Change>
void change_draft(packs_part& self, Change change) {
  change_packs(self, [&](mux::ui::packs_facts& now) {
    if (now.draft)
      change(*now.draft);
  });
}
void part_apply(packs_part& self, const request::pack_open& one) {
  change_packs(self, [&](mux::ui::packs_facts& now) { mux::ui::open_pack(now, one.at); });
}
void part_apply(packs_part& self, const request::pack_back&) { change_packs(self, [](mux::ui::packs_facts& now) { mux::ui::show_pack_list(now); }); }
void part_apply(packs_part& self, const request::pack_named& one) {
  change_draft(self, [&](mux::emote_pack& pack) { (one.attribution ? pack.attribution : pack.name) = one.text; });
}
// Never neither: the one flipped off turns the other on.
void part_apply(packs_part& self, const request::pack_use& one) {
  change_draft(self, [&](mux::emote_pack& pack) {
    if (one.emoji)
      pack.emoji = !pack.emoji || !pack.sticker;
    else
      pack.sticker = !pack.sticker || !pack.emoji;
  });
}
void part_apply(packs_part& self, const request::pack_picture_renamed& one) {
  change_draft(self, [&](mux::emote_pack& pack) {
    if (const auto found = std::ranges::find(pack.pictures, one.url, &mux::pack_picture::url); found != pack.pictures.end())
      found->shortcode = one.shortcode;
  });
}
void part_apply(packs_part& self, const request::pack_picture_use& one) {
  change_draft(self, [&](mux::emote_pack& pack) {
    if (const auto found = std::ranges::find(pack.pictures, one.url, &mux::pack_picture::url); found != pack.pictures.end()) {
      if (one.emoji)
        found->emoji = !found->emoji || !found->sticker;
      else
        found->sticker = !found->sticker || !found->emoji;
    }
  });
}
void part_apply(packs_part& self, const request::pack_picture_removed& one) {
  change_draft(self, [&](mux::emote_pack& pack) { std::erase_if(pack.pictures, [&](const mux::pack_picture& it) { return it.url == one.url; }); });
}
// The pack open saved: its avatar its first image where it has none, and
// the images with no shortcode left out.
void part_apply(packs_part& self, const request::pack_save&) {
  const auto& shown = *self.s_->showing->look<std::optional<mux::ui::packs_facts>>();
  if (!shown || !shown->draft)
    return;
  auto pack = *shown->draft;
  if (!pack.avatar && !pack.pictures.empty())
    pack.avatar = pack.pictures.front().url;
  std::erase_if(pack.pictures, [](const mux::pack_picture& one) { return one.shortcode.empty() || one.url.empty(); });
  change_packs(self, [](mux::ui::packs_facts& now) { now.note = "Saving\u2026"; });
  part_apply(self, request::save_pack{std::move(pack)});
}
// As its protocol takes a pack away; a new one, not saved yet, is nothing.
void part_apply(packs_part& self, const request::pack_delete&) {
  const auto& shown = *self.s_->showing->look<std::optional<mux::ui::packs_facts>>();
  if (!shown || !shown->draft || shown->draft->key.empty())
    return;
  const auto pack = *shown->draft;
  change_packs(self, [](mux::ui::packs_facts& now) { now.note = "Deleting\u2026"; });
  part_apply(self, request::delete_pack{pack});
}
void part_apply(packs_part& self, const request::save_pack& one) {
  if (self.account_ && !self.s_->demo())
    self.s_->net->save_pack(*self.account_, one.pack);
}

void part_apply(packs_part& self, const request::adopt_pack& one) {
  const auto by = one.by ? one.by : self.account_;
  if (by && one.pack.chat && !self.s_->demo())
    self.s_->net->adopt_pack(*by, one.pack);
}

void part_apply(packs_part& self, const request::delete_pack& one) {
  if (self.account_ && !self.s_->demo())
    self.s_->net->delete_pack(*self.account_, one.pack);
}

void part_apply(packs_part& self, const request::pick_pack_images&) {
  self.picking_ = true;
  self.s_->system_dialogs->choose_files();
}

bool took_files(packs_part& self, const std::vector<std::string>& paths, bool dropped) {
  if (!std::exchange(self.picking_, false) || dropped)
    return false;
  if (!self.account_ || self.s_->demo())
    return true;
  // The editor's opening the images are for: an answer for another is not
  // this draft's.
  const auto& open = *self.s_->showing->look<std::optional<mux::ui::packs_facts>>();
  if (!open || !open->draft)
    return true;
  const mux::pack_opening opening{open->room, open->opened};
  for (const std::string& path : paths) {
    auto bytes_read = mux::platform::files::read(path);
    if (!bytes_read)
      continue;
    std::string bytes = std::move(*bytes_read);
    const auto type = mux::media::picture_of(bytes);
    if (!type)
      continue;
    const auto name = mux::platform::files::name(path);
    mux::pack_picture one{.shortcode = std::filesystem::path(name).stem().string(),
                          .body = name,
                          .mimetype = std::string(spl::visit([](auto kind) { return mux::media::mimetype_of(kind); }, *type)),
                          .size = static_cast<std::int64_t>(bytes.size())};
    if (auto image = skia::decodeImage(bytes.data(), bytes.size())) {
      one.width = image->width();
      one.height = image->height();
    }
    self.s_->net->upload_pack_picture(*self.account_, std::move(one), std::move(bytes), opening);
  }
  return true;
}

}  // namespace mux::app
