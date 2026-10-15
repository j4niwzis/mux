// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:info_packs -- Emojis & Stickers: a room's packs, or one's own.
export module mux.ui:info_packs;

import std;
import splice.bytes;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.icon;
import skiff.nodes.image;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.pill;
import skiff.widgets.button;
import skiff.widgets.sliderbar;
import skiff.widgets.textbox;
import skiff.widgets.textarea;
import mux.core;
import mux.config;
import mux.logic.links;
import mux.protocols;
import mux.ui.base;
import mux.ui.messages;
import :info_common;
import :info_cards;
import :info_reactions;
import :info_new_chats;
import :info_looks;
import :info_threads;

export namespace mux::ui {
// Emojis & Stickers, as Cinny edits them (MSC2545): the packs of a room --
// or one's own pack -- listed; a pack opened: its name, attribution and use
// (as emoji, as stickers), and its images, each with its shortcode and use,
// removable; images added from files, uploaded as they are chosen; saved as
// the room's state, or one's account data.
// Emojis & Stickers open: a room's packs, or one's own; and whether they
// may be changed.
struct packs_facts {
  // Whose packs: the account's, and the room's where they are a room's --
  // what an answer from the server is matched by.
  std::optional<account_id> account;
  std::optional<std::string> room;
  bool editable = false;
  std::optional<std::string> key{};
  // As the account listed them; listed yet or not.
  std::vector<emote_pack> packs;
  bool listed = false;
  // A pack open -- a new one, not yet saved -- as it is being edited; each
  // opening its number, its fields made anew for it.
  std::optional<emote_pack> draft;
  bool new_pack = false;
  std::uint64_t opened = 0;
  // What is said over it: saving, saved, what went wrong.
  std::string note;
  friend bool operator==(const packs_facts&, const packs_facts&) = default;
};
// A pack opened -- a new one where none is named -- to edit: what is said
// over it as it may be changed or not.
inline void open_pack(packs_facts& facts, std::optional<std::size_t> at) {
  facts.new_pack = !at;
  facts.draft = at && *at < facts.packs.size() ? facts.packs[*at] : emote_pack{.chat = facts.room, .emoji = true, .sticker = true};
  ++facts.opened;
  facts.note = facts.editable && facts.draft->chat == facts.room ? std::string("Shortcodes are what the emoji are typed as, :like_this:.")
                                                                   : std::string("You may not change this room's packs.");
}
// The list again: what it is said to be.
inline void show_pack_list(packs_facts& facts) {
  facts.draft.reset();
  facts.note = facts.room ? (facts.packs.empty() ? std::string("This room has no packs yet.")
                                                 : std::string("Packs from this room and its spaces. Add a pack to your account to use it in every room."))
                          : std::string("Your own pack: its emoji and stickers are yours in every chat.");
}
// What the account listed: the packs -- the one asked for opened, or one's
// own pack at once where there is one -- else the list.
inline void packs_listed(packs_facts& facts, std::vector<emote_pack> listed) {
  facts.packs = std::move(listed);
  facts.listed = true;
  if (auto key = std::exchange(facts.key, std::nullopt))
    if (const auto found = std::ranges::find(facts.packs, *key, &emote_pack::key); found != facts.packs.end()) {
      open_pack(facts, static_cast<std::size_t>(found - facts.packs.begin()));
      return;
    }
  if (!facts.room && !facts.packs.empty()) {
    open_pack(facts, 0);
    return;
  }
  show_pack_list(facts);
}
// A pack saved -- or taken away: the list as it is now, shown again, or
// one's own left open; or what went wrong.
inline void pack_saved(packs_facts& facts, const emote_pack& pack, bool removed, bool done) {
  if (!done) {
    facts.note = removed ? "The pack was not deleted." : "The pack was not saved.";
    return;
  }
  std::erase_if(facts.packs, [&](const emote_pack& one) { return one.chat == pack.chat && one.key == pack.key; });
  if (!removed)
    facts.packs.push_back(pack);
  if (facts.room)
    show_pack_list(facts);
  else
    facts.note = "Saved.";
}
// An image uploaded for the pack open: in it, its shortcode from its file's
// name, unique in it; or why not.
inline void pack_picture_uploaded(packs_facts& facts, pack_picture one, bool done) {
  if (!done) {
    facts.note = "An image could not be uploaded: " + one.body;
    return;
  }
  if (!facts.draft)
    return;
  std::string code = spl::bytes::key_text(one.shortcode);
  if (code.empty())
    code = "image";
  std::string unique = code;
  for (int n = 2; std::ranges::contains(facts.draft->pictures, unique, &pack_picture::shortcode); ++n)
    unique = std::format("{}_{}", code, n);
  one.shortcode = unique;
  one.emoji = facts.draft->emoji;
  one.sticker = facts.draft->sticker;
  facts.draft->pictures.push_back(std::move(one));
}
// The pictures it shows: the pack open's images, else each pack's own.
[[nodiscard]] inline std::vector<emote_picture> pack_pictures_of(const packs_facts& facts) {
  if (facts.draft)
    return facts.draft->pictures | std::views::transform([](const pack_picture& one) {
             return emote_picture{one.url, one.emoji ? emote_kind::emoji : emote_kind::sticker};
           }) | std::ranges::to<std::vector>();
  return facts.packs | std::views::transform([](const emote_pack& pack) {
           return emote_picture{pack.avatar.value_or(pack.pictures.empty() ? std::string() : pack.pictures.front().url),
                                pack.emoji ? emote_kind::emoji : emote_kind::sticker};
         }) | std::views::filter([](const emote_picture& one) { return !one.source.empty(); }) | std::ranges::to<std::vector>();
}
// Whether the pack open may be changed here: the room's own, where its
// rights allow -- not one of a space it is in.
[[nodiscard]] inline bool draft_editable(const packs_facts& facts) {
  return facts.editable && facts.draft && facts.draft->chat == facts.room;
}
// A pack in the list: its picture, its name, how many images and what for;
// pressed, opened.
inline auto pack_row(const palette& colours, const std::optional<std::string>& room, std::size_t index, const emote_pack& pack) {
  namespace c = skiff::compose;
  auto picture = c::styled({.width = 40.0f, .height = 40.0f, .alignSelf = scene::align::kMiddle, .cornerRadius = 8.0f, .background = colours.tile},
      nodes::Image<from_emotes>({pack.avatar.value_or(pack.pictures.empty() ? std::string() : pack.pictures.front().url),
                                 pack.emoji ? emote_kind::emoji : emote_kind::sticker}));
  picture.keepBox();
  return c::onClick(request::pack_open{index},
      c::row(c::hbox(12.0f, {.fillX = true, .height = 56.0f, .padding = {8.0f, 14.0f, 8.0f, 14.0f}, .cornerRadius = 8.0f, .hoverBackground = colours.chosen}),
             std::move(picture),
             two_lines(colours, pack.name.empty() ? "Unnamed pack" : pack.name,
                       std::format("{} images · {}{}", pack.pictures.size(),
                                   pack.emoji && pack.sticker ? "Emoji and stickers" : pack.emoji ? "Emoji" : "Stickers",
                                   pack.chat != room ? " · From space" : ""),
                       14.0f, 2.0f),
             c::visible(pack.chat.has_value(),
                        c::onClick(request::adopt_pack{pack},
                                   c::styled({.width = 120.0f, .height = 32.0f, .alignSelf = scene::align::kMiddle, .cornerRadius = 6.0f,
                                              .hoverBackground = colours.tile},
                                             nodes::Text("Add to account", 13.0f, colours.accent)),
                                   "Add pack to account"))),
      pack.name.empty() ? "Open pack" : pack.name);
}
using pack_item = std::pair<std::size_t, emote_pack>;
struct pack_row_of {
  const palette* colours;
  std::optional<std::string> room;
  auto operator()(const pack_item& item) const { return pack_row(*colours, room, item.first, item.second); }
};
// A switch showing what the pack open is for, or one of its images: its
// use as emoji or as stickers, as the facts say; pressed, flipped -- never
// neither.
template <class Act, class On>
auto use_switch(const palette& colours, Act act, On on) {
  namespace c = skiff::compose;
  return c::applied<std::optional<packs_facts>>([on](const std::optional<packs_facts>& facts) { return facts ? on(*facts) : false; },
                                                [](widgets::Toggle<Act>& toggle, bool now) { toggle.setOn(now); },
                                                c::styled({.alignSelf = scene::align::kMiddle}, widgets::Toggle<Act>(colours.widgets, std::move(act))));
}
// What an image's shortcode field answers as it is typed.
struct picture_renamed_typed {
  static constexpr bool kEveryEdit = true;
  std::string url;
  using Answer = request::pack_picture_renamed;
  request::pack_picture_renamed operator()(std::string_view text) const { return {url, std::string(text)}; }
};
// An image of the pack open: its picture, its shortcode to edit, its use,
// and close to take it out. Kept while it is in the pack, by its picture;
// made again as its use changes.
struct picture_row_of {
  const palette* colours;
  auto operator()(const std::tuple<std::string, std::pair<bool, bool>, pack_picture>& item) const {
    namespace c = skiff::compose;
    const auto& [url, use, one] = item;
    const scene::Spec middle{.alignSelf = scene::align::kMiddle};
    auto face = c::styled({.width = 40.0f, .height = 40.0f, .alignSelf = scene::align::kMiddle, .cornerRadius = 6.0f, .background = colours->tile},
                          nodes::Image<from_emotes>({one.url, one.emoji ? emote_kind::emoji : emote_kind::sticker}));
    face.keepBox();
    auto shortcode = widgets::internal::TextBox<picture_renamed_typed>(colours->widgets, "shortcode", picture_renamed_typed{one.url});
    shortcode.setText(one.shortcode);
    auto emoji = widgets::Toggle<sends<request::pack_picture_use>>(colours->widgets, {{one.url, true}});
    emoji.setOnNow(use.first);
    auto sticker = widgets::Toggle<sends<request::pack_picture_use>>(colours->widgets, {{one.url, false}});
    sticker.setOnNow(use.second);
    return c::row(c::hbox(8.0f, {.fillX = true, .height = 52.0f, .padding = {6.0f, 10.0f, 6.0f, 10.0f}}),
                  std::move(face),
                  c::styled({.height = 32.0f, .relativeSize = scene::axes::kNone, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                            std::move(shortcode)),
                  c::styled(middle, nodes::Text("Emoji", 12.0f, colours->dim)), c::styled(middle, std::move(emoji)),
                  c::styled(middle, nodes::Text("Sticker", 12.0f, colours->dim)), c::styled(middle, std::move(sticker)),
                  c::styled(middle, icon_button(*colours, icon::close{}, sends<request::pack_picture_removed>{{one.url}})));
  }
};
// What a pack's name and attribution answer as they are typed.
struct pack_named_typed {
  static constexpr bool kEveryEdit = true;
  bool attribution = false;
  using Answer = request::pack_named;
  request::pack_named operator()(std::string_view text) const { return {std::string(text), attribution}; }
};
// The pack open, its fields as it was when opened: what is typed in them
// the program's to keep, as it is typed.
struct pack_opened {
  std::uint64_t opened = 0;
  emote_pack pack;
  bool editable = false, new_pack = false, in_room = false;
  friend bool operator==(const pack_opened& a, const pack_opened& b) { return a.opened == b.opened; }
};
inline auto pack_editor(const palette& colours, const pack_opened& opened) {
  namespace c = skiff::compose;
  const auto field = [&](std::string caption, std::string placeholder, std::string now, bool attribution) {
    auto box = widgets::internal::TextBox<pack_named_typed>(colours.widgets, std::move(placeholder), pack_named_typed{attribution});
    box.setText(std::move(now));
    return c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY, .disabled = !opened.editable}),
                     nodes::Text(std::move(caption), 13.0f, colours.dim), c::styled({.fillX = true, .height = 36.0f}, std::move(box)));
  };
  using rows_t = nodes::MemoRows<std::string, std::pair<bool, bool>,
                                 std::invoke_result_t<picture_row_of, const std::tuple<std::string, std::pair<bool, bool>, pack_picture>&>, picture_row_of>;
  const scene::Spec button{.width = 110.0f, .height = 34.0f};
  return c::column(c::vbox(8.0f, {.fillX = true, .grow = scene::axes::kY}),
      field("Name", "Pack name", opened.pack.name, false),
      field("Attribution (optional)", "Where its images are from", opened.pack.attribution, true),
      c::row(c::hbox(10.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 10.0f, 4.0f, 10.0f}, .disabled = !opened.editable}),
             c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text("Use as emoji", 13.0f, colours.text)),
             use_switch(colours, sends<request::pack_use>{{true}}, [](const packs_facts& now) { return now.draft && now.draft->emoji; }),
             c::styled({.alignSelf = scene::align::kMiddle, .margin = {0.0f, 0.0f, 0.0f, 14.0f}}, nodes::Text("Use as stickers", 13.0f, colours.text)),
             use_switch(colours, sends<request::pack_use>{{false}}, [](const packs_facts& now) { return now.draft && now.draft->sticker; })),
      c::styled({.margin = {6.0f, 10.0f, 0.0f, 10.0f}}, nodes::Text("Images", 13.0f, colours.dim, true)),
      c::styled({.fillX = true, .grow = scene::axes::kY, .disabled = !opened.editable},
                nodes::ScrollContainer(c::projected<std::optional<packs_facts>>([](const std::optional<packs_facts>& facts) {
                  return facts && facts->draft ? facts->draft->pictures | std::views::transform([](const pack_picture& one) {
                                                   return std::tuple{one.url, std::pair{one.emoji, one.sticker}, one};
                                                 }) | std::ranges::to<std::vector>()
                                               : std::vector<std::tuple<std::string, std::pair<bool, bool>, pack_picture>>{};
                },
                    c::styled({.fillX = true, .autoSize = scene::axes::kY}, rows_t(picture_row_of{&colours}))))),
      c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {6.0f, 10.0f, 0.0f, 10.0f}}),
             c::visible(opened.editable, c::styled(button, widgets::Button<sends<request::pick_pack_images>>(colours.widgets, "Add images", {}))),
             c::styled({.height = 1.0f, .grow = scene::axes::kX}, nodes::Box<>(skia::SkColor{0})),
             c::visible(opened.editable && opened.in_room && !opened.new_pack,
                        c::styled(button, widgets::Button<sends<request::pack_delete>>(colours.widgets, "Delete pack", {}))),
             c::visible(opened.in_room, c::styled(button, widgets::Button<sends<request::pack_back>>(colours.widgets, "Back", {}))),
             c::visible(opened.editable, c::styled(button, primary(widgets::Button<sends<request::pack_save>>(colours.widgets, "Save", {}))))));
}
using pack_editor_t = decltype(pack_editor(std::declval<const palette&>(), std::declval<const pack_opened&>()));
// Emojis & Stickers, as the program has it: its packs listed -- Create
// where the room's may be added to -- or the one open, edited.
inline auto packs_box(const ui_needs& n, const packs_facts& first) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  using facts_t = std::optional<packs_facts>;
  const auto listing = [](const facts_t& facts) { return facts && !facts->draft; };
  return c::column(c::vbox(8.0f, {.fillX = true, .height = 600.0f, .padding = {0.0f, 12.0f, 16.0f, 12.0f}}),
      c::row(c::hbox(8.0f, {.fillX = true, .height = kPageHeaderHeight, .padding = {0.0f, 10.0f, 0.0f, 10.0f}}),
             c::text_for<facts_t>([](const facts_t& facts) {
               return !facts || !facts->draft ? std::string("Emojis & Stickers") : facts->new_pack ? std::string("New pack")
                                                : facts->draft->name.empty() ? std::string("Pack") : facts->draft->name;
             },
                 c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(nodes::Text("", 16.0f, colours.text, true)))),
             c::styled({.alignSelf = scene::align::kMiddle}, icon_button(colours, icon::close{}, sends<request::close_packs>{}))),
      c::text_for<facts_t>([](const facts_t& facts) { return facts ? facts->note : std::string(); },
          c::styled({.fillX = true, .margin = {0.0f, 10.0f, 4.0f, 10.0f}}, wrapped(nodes::Text("", 13.0f, colours.dim)))),
      c::shown_for<facts_t>(listing,
          c::styled({.fillX = true, .grow = scene::axes::kY},
                    nodes::ScrollContainer(c::projected<facts_t>([](const facts_t& facts) {
                      return facts ? std::views::zip(std::views::iota(std::size_t{0}), facts->packs) | std::views::transform([](const auto& one) { return pack_item{std::get<0>(one), std::get<1>(one)}; }) | std::ranges::to<std::vector>()
                                   : std::vector<pack_item>{};
                    },
                        c::listed<pack_item>(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), pack_row_of{&colours, first.room}))))),
      c::shown_for<facts_t>([](const facts_t& facts) { return facts && !facts->draft && facts->room && facts->editable; },
          c::row(c::hbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {6.0f, 10.0f, 0.0f, 10.0f}}),
                 c::styled({.width = 140.0f, .height = 34.0f},
                           primary(widgets::Button<sends<request::pack_open>>(colours.widgets, "Create pack", {{std::nullopt}}))))),
      c::projected<facts_t>([](const facts_t& facts) {
        return facts && facts->draft ? std::optional(pack_opened{facts->opened, *facts->draft, draft_editable(*facts), facts->new_pack,
                                                                 facts->room.has_value()})
                                     : std::nullopt;
      },
          c::mount<pack_editor_t, pack_opened>([at = &colours](const pack_opened& opened) { return std::tuple{pack_editor(*at, opened)}; },
                                               {.fillX = true, .grow = scene::axes::kY})));
}
using packs_box_t = decltype(packs_box(std::declval<const ui_needs&>(), std::declval<const packs_facts&>()));
// The dialog it is shown in; made once while it is up -- for the room it
// is for, the rest read as the facts move.
inline dialog_look content_look(std::type_identity<packs_box_t>) { return {.size = dialog_size::fixed{620.0f, 600.0f}}; }
template <class Needs>
auto make_content(std::type_identity<packs_box_t>, const Needs& needs, const packs_facts& facts) {
  return packs_box(needs, facts);
}
constexpr bool content_persistent(std::type_identity<packs_box_t>) { return true; }

}  // namespace mux::ui

