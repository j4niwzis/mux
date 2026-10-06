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

class packs_part {
 public:
  explicit packs_part(services& shared) : s_(&shared) {}
  packs_part(const packs_part&) = delete;
  packs_part& operator=(const packs_part&) = delete;

  // One's own packs, from Settings: by an account that has them.
  void apply(const request::open_packs&) {
    account_ = s_->account_offering(mux::proto::feature::sticker_packs{});
    s_->root().open_packs(std::nullopt, true);
    if (account_ && !s_->demo())
      s_->net->list_packs(*account_, std::nullopt);
  }
  // A room's packs, from its settings: changed only where its protocol says
  // one may.
  void apply(const request::open_room_packs&) {
    const auto chosen = s_->managed();
    const mux::conversation* chat = chosen ? s_->model->find(*chosen) : nullptr;
    if (!chat || !mux::proto::offers(mux::ui::protocol_state_of(s_->ui, chat->id.account), mux::proto::feature::sticker_packs{}))
      return;
    account_ = chat->id.account;
    s_->root().open_packs(chat->id.id, mux::proto::chat_rights(mux::ui::protocol_state_of(s_->ui, chat->id.account), *chat).edit_packs);
    if (!s_->demo())
      s_->net->list_packs(*account_, chat->id.id);
  }
  void apply(const request::close_packs&) {
    s_->root().close_packs();
    s_->ui.pack_pictures_shown.clear();
  }
  void apply(const request::save_pack& one) {
    if (account_ && !s_->demo())
      s_->net->save_pack(*account_, one.pack);
  }
  void apply(const request::delete_pack& one) {
    if (account_ && !s_->demo())
      s_->net->delete_pack(*account_, one.pack);
  }
  // Images for the pack open: the system's dialog asked for them; the files
  // it gives are the pack's (took_files).
  void apply(const request::pick_pack_images&) {
    picking_ = true;
    s_->system_dialogs->choose_files();
  }
  // Files chosen in the dialog: the pack's images, where they were asked for
  // here -- each a picture uploaded, its name its shortcode to begin with,
  // its size and type said in the pack. Whether they were.
  bool took_files(const std::vector<std::string>& paths, bool dropped) {
    if (!std::exchange(picking_, false) || dropped)
      return false;
    if (!account_ || s_->demo())
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
      s_->net->upload_pack_picture(*account_, std::move(one), std::move(bytes));
    }
    return true;
  }

 private:
  services* s_;
  // The account whose packs the dialog shows; whether the files chosen next
  // are its images.
  std::optional<mux::account_id> account_;
  bool picking_ = false;
};

}  // namespace mux::app
