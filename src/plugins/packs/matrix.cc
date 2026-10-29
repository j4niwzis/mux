// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.packs.matrix -- The packs over Matrix (MSC2545), the packs
// plugin's own: a room's -- its im.ponies.room_emotes and m.room.image_pack
// state, with those of the spaces it is in -- or one's own
// (im.ponies.user_emotes), read from the account as synced and written back;
// a room's pack added to one's account (im.ponies.emote_rooms); images
// uploaded for them. Through the account's door for plugins: the Matrix
// client knows nothing of editing packs.
export module mux.plugin.packs.matrix;

import std;
import splice;
import splice.bytes;
import knot;
import loom.ev;
import loom.state;
import loom.cs.room_state;
import loom.cs.account_data;
import mux.core;
import mux.proto.matrix.client;
import mux.plugin.packs.types;
export import mux.plugin.packs.changes;

export namespace mux::plugins::packs {

// An account a pack can be asked of: a Matrix one, through its door.
template <class Account>
concept matrix_account = requires(Account& account) {
  account.synced();
  account.connected();
};

namespace content {
inline knot::raw body_of(const auto& one) { return knot::raw{knot::to_json_string(one)}; }
// A pack's or an image's use, as its usage list says: missing or empty, any.
inline void use_of(const auto& usage, bool& emoji, bool& sticker) {
  emoji = loom::client::detail::allows(usage, loom::client::image_use::emoticon{});
  sticker = loom::client::detail::allows(usage, loom::client::image_use::sticker{});
}
// A usage list, as it is written: of the item and value types of the kind
// of content it goes in.
template <class Item, class Values>
[[nodiscard]] std::vector<Item> usage_list(bool emoji, bool sticker) {
  std::vector<Item> out;
  if (emoji)
    out.push_back(Item{typename Values::emoticon{}});
  if (sticker)
    out.push_back(Item{typename Values::sticker{}});
  return out;
}
// A pack read from its content -- a room's or one's own, the same shape.
template <class Content>
[[nodiscard]] emote_pack pack_of(const Content& content, std::optional<std::string> room, std::string key) {
  emote_pack pack{.chat = std::move(room), .key = std::move(key)};
  if (content.pack) {
    pack.name = content.pack->display_name.value_or("");
    pack.avatar = content.pack->avatar_url;
    pack.attribution = content.pack->attribution.value_or("");
    use_of(content.pack->usage, pack.emoji, pack.sticker);
  }
  for (const auto& [shortcode, image] : content.images) {
    pack_picture one{.shortcode = shortcode, .url = image.url, .body = image.body.value_or("")};
    if (image.usage && !image.usage->empty())
      use_of(image.usage, one.emoji, one.sticker);
    else {
      one.emoji = pack.emoji;
      one.sticker = pack.sticker;
    }
    if (image.info) {
      one.mimetype = image.info->mimetype.value_or("");
      one.width = image.info->w.value_or(0);
      one.height = image.info->h.value_or(0);
      one.size = image.info->size.value_or(0);
    }
    pack.pictures.push_back(std::move(one));
  }
  return pack;
}
// And written: an image's usage only where it is not the pack's.
template <class Content>
[[nodiscard]] Content content_of(const emote_pack& pack) {
  using image_t = typename Content::image_pack_image_t;
  using meta_t = typename Content::image_pack_meta_t;
  Content out;
  auto& meta = out.pack.emplace();
  if (!pack.name.empty())
    meta.display_name = pack.name;
  meta.avatar_url = pack.avatar;
  if (!pack.attribution.empty())
    meta.attribution = pack.attribution;
  meta.usage = usage_list<typename meta_t::usage_item_t, typename meta_t::usage_item_values>(pack.emoji, pack.sticker);
  for (const pack_picture& one : pack.pictures) {
    image_t image;
    image.url = one.url;
    if (!one.body.empty())
      image.body = one.body;
    if (one.emoji != pack.emoji || one.sticker != pack.sticker)
      image.usage = usage_list<typename image_t::usage_item_t, typename image_t::usage_item_values>(one.emoji, one.sticker);
    if (!one.mimetype.empty() || one.width > 0 || one.height > 0) {
      auto& info = image.info.emplace();
      if (!one.mimetype.empty())
        info.mimetype = one.mimetype;
      if (one.width > 0)
        info.w = one.width;
      if (one.height > 0)
        info.h = one.height;
      if (one.size > 0)
        info.size = one.size;
    }
    out.images.insert_or_assign(one.shortcode, std::move(image));
  }
  return out;
}
}  // namespace content

// The packs of a room -- its own and those of the spaces it is in -- or
// one's own (even empty: to be filled), as the account has them synced.
struct list_asked {
  std::optional<std::string> room;
  template <matrix_account Account>
  void operator()(Account& account) const {
    account.on_fiber([&account, room = room] {
      const auto& state = account.synced();
      std::vector<emote_pack> found;
      if (!room) {
        if (const auto own = state.account_data.find("im.ponies.user_emotes"); own != state.account_data.end())
          spl::visit(spl::overloaded{[&](const loom::ev::im_ponies_user_emotes_content_t& one) {
                                       found.push_back(content::pack_of(one, std::nullopt, std::string()));
                                     },
                                     [](const auto&) {}},
                     own->second.content.data());
        if (found.empty())
          found.push_back(emote_pack{});
      } else if (const auto joined = state.joined.find(*room); joined != state.joined.end()) {
        for (const std::string& source : mux::proto::matrix::client::pack_rooms(state, joined->second))
          for (const auto& event : state.joined.at(source).state.events) {
            const auto add = [&](const auto& one) {
              if (!one.images.empty() || one.pack)
                found.push_back(content::pack_of(one, source, event.first.second));
            };
            spl::visit(spl::overloaded{[&](const loom::ev::im_ponies_room_emotes_content_t& one) { add(one); },
                                       [&](const loom::ev::m_room_image_pack_content_t& one) { add(one); },
                                       [](const auto&) {}},
                       event.second.content.data());
          }
      }
      account.tell_program(packs_listed{account.id(), room, std::move(found)});
    });
  }
};
// A pack saved: a room's as its state (its key made of its name, where new),
// one's own as account data.
struct save_asked {
  emote_pack pack;
  template <matrix_account Account>
  void operator()(Account& account) const {
    account.on_fiber([&account, pack = pack]() mutable {
      if (pack.chat && pack.key.empty()) {
        pack.key = spl::bytes::key_text(pack.name);
        if (pack.key.empty())
          pack.key = "pack";
      }
      bool done = false;
      if (account.connected()) {
        if (pack.chat)
          done = static_cast<bool>(account.request(loom::cs::set_room_state_with_key{
              .room_id = *pack.chat, .event_type = "im.ponies.room_emotes", .state_key = pack.key,
              .body = content::body_of(content::content_of<loom::ev::im_ponies_room_emotes_content_t>(pack))}));
        else
          done = static_cast<bool>(account.request(loom::cs::set_account_data{
              .user_id = account.id().address, .type = "im.ponies.user_emotes",
              .body = content::body_of(content::content_of<loom::ev::im_ponies_user_emotes_content_t>(pack))}));
      }
      account.tell_program(pack_saved{.by = account.id(), .pack = pack, .done = done});
    });
  }
};
// A room's pack added to one's account, keeping every pack added before:
// its images neither uploaded again nor renamed.
struct adopt_asked {
  emote_pack pack;
  template <matrix_account Account>
  void operator()(Account& account) const {
    if (!pack.chat)
      return;
    account.on_fiber([&account, pack = pack] {
      loom::ev::im_ponies_emote_rooms_content_t chosen;
      const auto& state = account.synced();
      if (const auto current = state.account_data.find("im.ponies.emote_rooms"); current != state.account_data.end())
        spl::visit(spl::overloaded{[&](const loom::ev::im_ponies_emote_rooms_content_t& one) { chosen = one; },
                                   [](const auto&) {}},
                   current->second.content.data());
      chosen.rooms[*pack.chat].insert_or_assign(pack.key, knot::raw{"{}"});
      const bool done = account.connected() && static_cast<bool>(account.request(loom::cs::set_account_data{
          .user_id = account.id().address, .type = "im.ponies.emote_rooms", .body = content::body_of(chosen)}));
      if (!done) {
        account.tell_program(change::refused{account.id(), "The pack could not be added to your account."});
        return;
      }
      account.account_data_set("im.ponies.emote_rooms", loom::ev::account_data_event{
          .content = loom::ev::other_content{std::move(chosen)}, .type = "im.ponies.emote_rooms"});
      account.tell_program(change::notice{account.id(), "Pack added", "This pack is now available in all your rooms."});
    });
  }
};
// A room's pack taken away as the MSC has it: its state emptied.
struct remove_asked {
  emote_pack pack;
  template <matrix_account Account>
  void operator()(Account& account) const {
    account.on_fiber([&account, pack = pack] {
      const bool done = account.connected() && pack.chat &&
                        static_cast<bool>(account.request(loom::cs::set_room_state_with_key{
                            .room_id = *pack.chat, .event_type = "im.ponies.room_emotes", .state_key = pack.key, .body = knot::raw{"{}"}}));
      account.tell_program(pack_saved{.by = account.id(), .pack = pack, .removed = true, .done = done});
    });
  }
};
// An image uploaded for the pack being edited, in the editor's opening.
struct upload_asked {
  pack_picture picture;
  std::string bytes;
  pack_opening opening;
  template <matrix_account Account>
  void operator()(Account& account) const {
    account.on_fiber([&account, picture = picture, bytes = bytes, opening = opening]() mutable {
      auto uploaded = account.upload(bytes, picture.body, picture.mimetype);
      picture.url = uploaded.value_or("");
      account.tell_program(pack_picture_uploaded{.by = account.id(), .picture = std::move(picture), .done = uploaded.has_value(),
                                                 .opening = std::move(opening)});
    });
  }
};

}  // namespace mux::plugins::packs
