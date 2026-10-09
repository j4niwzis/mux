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
bool took_files(packs_part& self, const std::vector<std::string>& paths, bool dropped);

void part_apply(packs_part& self, const request::open_packs&) {
  self.account_ = self.s_->account_offering(mux::proto::feature::sticker_packs{});
  mux::ui::show(*self.s_->showing, std::optional(mux::ui::packs_facts{std::nullopt, true}));
  if (self.account_ && !self.s_->demo())
    self.s_->net->list_packs(*self.account_, std::nullopt);
}

void part_apply(packs_part& self, const request::open_room_packs&) {
  const auto chosen = self.s_->managed();
  const mux::conversation* chat = chosen ? self.s_->model->find(*chosen) : nullptr;
  if (!chat || !mux::proto::offers(mux::ui::protocol_state_of(self.s_->ui, chat->id.account), mux::proto::feature::sticker_packs{}))
    return;
  self.account_ = chat->id.account;
  mux::ui::show(*self.s_->showing, std::optional(mux::ui::packs_facts{chat->id.id, mux::proto::chat_rights(mux::ui::protocol_state_of(self.s_->ui, chat->id.account), *chat).edit_packs}));
  if (!self.s_->demo())
    self.s_->net->list_packs(*self.account_, chat->id.id);
}

void part_apply(packs_part& self, const request::close_packs&) {
  mux::ui::show<mux::ui::packs_facts>(*self.s_->showing, std::nullopt);
  self.s_->ui.pack_pictures_shown.clear();
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
    self.s_->net->upload_pack_picture(*self.account_, std::move(one), std::move(bytes));
  }
  return true;
}

}  // namespace mux::app
