// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.packs.ui -- Emojis & Stickers, as Cinny edits them (MSC2545):
// the packs of a room -- or one's own pack -- listed; a pack opened: its
// name, attribution and use (as emoji, as stickers), and its images, each
// with its shortcode and use, removable; images added from files, uploaded
// as they are chosen; saved as the room's state, or one's account data. And
// the ways to it: Settings' link, an account's row, a room's settings' tab.
module;
#include "../../inline_in_release.h"
#include "skiff/named.h"
export module mux.plugin.packs.ui;

import std;
import splice;
import splice.bytes;
import skia;
import skiff.scene;
import skiff.compose;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.image;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.nodes.icon;
import skiff.widgets.pill;
import skiff.widgets.button;
import skiff.widgets.sliderbar;
import skiff.widgets.textbox;
import skiff.widgets.textarea;
import mux.composition;
import mux.core;
import mux.protocols;
import mux.ui.base;
import mux.plugin.packs.types;
import mux.plugin.room_settings.types;
import mux.plugin.room_settings.ui;

export namespace mux::plugins::packs {
using namespace ::mux::ui;

// The ways to it: Settings' link (one's own, the first account's), an
// account's row (that account's), where its protocol has packs.
MUX_INLINE_IN_RELEASE auto extend(contract::settings_links, type_tag<feature>, const palette& colours) {
  return settings_link(colours, "Emojis & Stickers", icon::smile{}, open{});
}
template <class Protocol>
auto extend(contract::account_rows<Protocol>, type_tag<feature>, const palette& colours, const std::string& address) {
  return row_item(colours, "Emojis & Stickers", sends<open_account>{{address}}, icon::smile{});
}
// A room's settings' tab, where its protocol has packs: its packs, opened
// in the dialog -- the list, Create pack and the editor all there.
constexpr std::string_view tab_title(room_tab) { return "Emojis & Stickers"; }
inline icon_t tab_icon(room_tab) { return icon::smile{}; }
// The tab, as it was before the plugins rewrite (Cinny's Room Packs): a new
// pack made by its name, where one may; each pack a card -- its name, how
// many images, View opening it in the dialog; none, said so.
template <class Facts>
[[nodiscard]] const tab_kept& tab_of(const std::optional<Facts>& now) {
  static const tab_kept none;
  const tab_kept* found = &none;
  if (now)
    room_settings::each_kept(now->manage, spl::overloaded{[&](const tab_kept& kept) { found = &kept; }, [](const auto&) {}});
  return *found;
}
struct pack_draft {
  std::string name;
  friend bool operator==(const pack_draft&, const pack_draft&) = default;
};
MUX_INLINE_IN_RELEASE auto pack_maker(const palette& colours) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(pack_maker, c::local<pack_draft>(c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::styled({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX}, model_field<&pack_draft::name>(colours, "Name", "Pack name")),
      c::column(c::vbox(0.0f, {.relativeSize = scene::axes::kNone, .autoSize = scene::axes::kBoth, .alignSelf = scene::align::kMiddle}),
                c::asking_for<pack_draft>([](const pack_draft& now) { return create_room_pack{now.name}; }, action_face(colours, "Create")))),
      pack_draft{}));
}
using pack_maker_t = decltype(pack_maker(std::declval<const palette&>()));
struct pack_line {
  std::string key, name;
  std::size_t pictures = 0;
  friend bool operator==(const pack_line&, const pack_line&) = default;
};
struct packs_key {
  std::vector<pack_line> packs;
  friend bool operator==(const packs_key&, const packs_key&) = default;
};
MUX_INLINE_IN_RELEASE auto pack_cards(const palette& colours, const packs_key& key) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(pack_cards, c::many(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}), key.packs | std::views::transform([&](const pack_line& one) {
    return card(colours, setting_tile(colours, one.name.empty() ? "Unnamed Pack" : one.name, std::format("{} images", one.pictures),
                                      action(colours, "View", sends<open_room>{{one.key}})));
  }) | std::ranges::to<std::vector>()));
}
using pack_cards_t = decltype(pack_cards(std::declval<const palette&>(), packs_key{}));
MUX_INLINE_IN_RELEASE auto make_manage_page(room_tab, const palette& colours, const auto& facts) {
  namespace c = skiff::compose;
  using facts_t = std::optional<std::remove_cvref_t<decltype(facts)>>;
  return SKIFF_NAMED(room_tab_page, c::column(page_look(), section(colours, "Room Packs",
      c::shown_for<facts_t>([](const facts_t& now) { return tab_of(now).editable; }, card(colours,
          setting_tile(colours, "New Pack", "Add your own emoji and sticker pack to use in room.", c::column()),
          c::projected<facts_t>([](const facts_t& now) { return now ? std::optional(tab_of(now).round) : std::nullopt; },
              c::mount<pack_maker_t, std::uint32_t>([at = &colours](std::uint32_t) { return std::tuple{pack_maker(*at)}; },
                                                    {.fillX = true, .autoSize = scene::axes::kY})))),
      c::projected<facts_t>([](const facts_t& now) {
        return now ? std::optional(packs_key{tab_of(now).packs | std::views::transform([](const emote_pack& pack) {
                                     return pack_line{pack.key, pack.name, pack.pictures.size()};
                                   }) | std::ranges::to<std::vector>()})
                   : std::nullopt;
      },
          c::mount<pack_cards_t, packs_key>([at = &colours](const packs_key& key) { return std::tuple{pack_cards(*at, key)}; },
                                            {.fillX = true, .autoSize = scene::axes::kY})),
      c::shown_for<facts_t>([](const facts_t& now) { return tab_of(now).packs.empty(); }, wrapped(nodes::Text("No packs found.", 12.0f, colours.dim))))));
}

// A pack opened -- a new one where none is named -- to edit: what is said
// over it as it may be changed or not.
inline void open_pack(shown& facts, std::optional<std::size_t> at) {
  facts.new_pack = !at;
  facts.draft = at && *at < facts.packs.size() ? facts.packs[*at] : emote_pack{.chat = facts.room, .emoji = true, .sticker = true};
  ++facts.opened;
  facts.note = facts.editable && facts.draft->chat == facts.room ? std::string("Shortcodes are what the emoji are typed as, :like_this:.")
                                                                   : std::string("You may not change this room's packs.");
}
// The list again: what it is said to be.
inline void show_pack_list(shown& facts) {
  facts.draft.reset();
  facts.note = facts.room ? (facts.packs.empty() ? std::string("This room has no packs yet.")
                                                 : std::string("Packs from this room and its spaces. Add a pack to your account to use it in every room."))
                          : std::string("Your own pack: its emoji and stickers are yours in every chat.");
}
// What the account listed: the packs -- the one asked for opened, or one's
// own pack at once where there is one -- else the list.
inline void packs_listed_in(shown& facts, std::vector<emote_pack> listed) {
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
inline void pack_saved_in(shown& facts, const emote_pack& pack, bool removed, bool done) {
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
inline void picture_uploaded_in(shown& facts, pack_picture one, bool done, const pack_opening& opening) {
  // For another opening of the editor -- another pack, or this one opened
  // again since: not this draft's.
  if (opening != pack_opening{facts.room, facts.opened})
    return;
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
[[nodiscard]] inline std::vector<emote_picture> pictures_of(const shown& facts) {
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
[[nodiscard]] inline bool draft_editable(const shown& facts) {
  return facts.editable && facts.draft && facts.draft->chat == facts.room;
}
// A pack in the list: its picture, its name, how many images and what for;
// pressed, opened.
MUX_INLINE_IN_RELEASE auto pack_row(const palette& colours, const std::optional<std::string>& room, std::size_t index, const emote_pack& pack) {
  namespace c = skiff::compose;
  // A node, its box kept: its spec applied on it (styled() makes a builder).
  nodes::Image<from_emotes> picture({pack.avatar.value_or(pack.pictures.empty() ? std::string() : pack.pictures.front().url),
                                     pack.emoji ? emote_kind::emoji : emote_kind::sticker});
  picture.apply({.width = 40.0f, .height = 40.0f, .alignSelf = scene::align::kMiddle, .cornerRadius = 8.0f, .background = colours.tile});
  picture.keepBox();
  return SKIFF_NAMED(pack_row, c::onClick(pack_open{index},
      c::row(c::hbox(12.0f, {.fillX = true, .height = 56.0f, .padding = {8.0f, 14.0f, 8.0f, 14.0f}, .cornerRadius = 8.0f, .hoverBackground = colours.chosen}),
             std::move(picture),
             two_lines(colours, pack.name.empty() ? "Unnamed pack" : pack.name,
                       std::format("{} images · {}{}", pack.pictures.size(),
                                   pack.emoji && pack.sticker ? "Emoji and stickers" : pack.emoji ? "Emoji" : "Stickers",
                                   pack.chat != room ? " · From space" : ""),
                       14.0f, 2.0f),
             c::visible(pack.chat.has_value(),
                        c::onClick(adopt{pack, std::nullopt},
                                   c::styled({.width = 120.0f, .height = 32.0f, .alignSelf = scene::align::kMiddle, .cornerRadius = 6.0f,
                                              .hoverBackground = colours.tile},
                                             nodes::Text("Add to account", 13.0f, colours.accent)),
                                   "Add pack to account"))),
      pack.name.empty() ? "Open pack" : pack.name));
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
  return SKIFF_NAMED(use_switch, c::applied<std::optional<shown>>([on](const std::optional<shown>& facts) { return facts ? on(*facts) : false; },
                                                [](widgets::Toggle<Act>& toggle, bool now) { toggle.setOn(now); },
                                                c::styled({.alignSelf = scene::align::kMiddle}, widgets::Toggle<Act>(colours.widgets, std::move(act)))));
}
// What an image's shortcode field answers as it is typed.
struct picture_renamed_typed {
  static constexpr bool kEveryEdit = true;
  std::string url;
  using Answer = picture_renamed;
  picture_renamed operator()(std::string_view text) const { return {url, std::string(text)}; }
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
    nodes::Image<from_emotes> face({one.url, one.emoji ? emote_kind::emoji : emote_kind::sticker});
    face.apply({.width = 40.0f, .height = 40.0f, .alignSelf = scene::align::kMiddle, .cornerRadius = 6.0f, .background = colours->tile});
    face.keepBox();
    auto shortcode = widgets::internal::TextBox<picture_renamed_typed>(colours->widgets, "shortcode", picture_renamed_typed{one.url});
    shortcode.setText(one.shortcode);
    auto emoji = widgets::Toggle<sends<picture_use>>(colours->widgets, {{one.url, true}});
    emoji.setOnNow(use.first);
    auto sticker = widgets::Toggle<sends<picture_use>>(colours->widgets, {{one.url, false}});
    sticker.setOnNow(use.second);
    return c::row(c::hbox(8.0f, {.fillX = true, .height = 52.0f, .padding = {6.0f, 10.0f, 6.0f, 10.0f}}),
                  std::move(face),
                  c::styled({.height = 32.0f, .relativeSize = scene::axes::kNone, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                            std::move(shortcode)),
                  c::styled(middle, nodes::Text("Emoji", 12.0f, colours->dim)), c::styled(middle, std::move(emoji)),
                  c::styled(middle, nodes::Text("Sticker", 12.0f, colours->dim)), c::styled(middle, std::move(sticker)),
                  c::styled(middle, icon_button(*colours, icon::close{}, sends<picture_removed>{{one.url}})));
  }
};
// What a pack's name and attribution answer as they are typed.
struct pack_named_typed {
  static constexpr bool kEveryEdit = true;
  bool attribution = false;
  using Answer = pack_named;
  pack_named operator()(std::string_view text) const { return {std::string(text), attribution}; }
};
// The pack open, its fields as it was when opened: what is typed in them
// the program's to keep, as it is typed.
struct pack_opened {
  std::uint64_t opened = 0;
  emote_pack pack;
  bool editable = false, new_pack = false, in_room = false;
  friend bool operator==(const pack_opened& a, const pack_opened& b) { return a.opened == b.opened; }
};
MUX_INLINE_IN_RELEASE auto pack_editor(const palette& colours, const pack_opened& opened) {
  namespace c = skiff::compose;
  const auto field = [&](std::string caption, std::string placeholder, std::string now, bool attribution) {
    auto box = widgets::internal::TextBox<pack_named_typed>(colours.widgets, std::move(placeholder), pack_named_typed{attribution});
    box.setText(std::move(now));
    return c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY, .disabled = !opened.editable}),
                     nodes::Text(std::move(caption), 13.0f, colours.dim), c::styled({.fillX = true, .height = 36.0f}, std::move(box)));
  };
  // The rows kept by their keys, each built as it is made (memo_rows).
  using picture_item = std::tuple<std::string, std::pair<bool, bool>, pack_picture>;
  const scene::Spec button{.width = 110.0f, .height = 34.0f};
  return SKIFF_NAMED(pack_editor, c::column(c::vbox(8.0f, {.fillX = true, .grow = scene::axes::kY}),
      field("Name", "Pack name", opened.pack.name, false),
      field("Attribution (optional)", "Where its images are from", opened.pack.attribution, true),
      c::row(c::hbox(10.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 10.0f, 4.0f, 10.0f}, .disabled = !opened.editable}),
             c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text("Use as emoji", 13.0f, colours.text)),
             use_switch(colours, sends<pack_use>{{true}}, [](const shown& now) { return now.draft && now.draft->emoji; }),
             c::styled({.alignSelf = scene::align::kMiddle, .margin = {0.0f, 0.0f, 0.0f, 14.0f}}, nodes::Text("Use as stickers", 13.0f, colours.text)),
             use_switch(colours, sends<pack_use>{{false}}, [](const shown& now) { return now.draft && now.draft->sticker; })),
      c::styled({.margin = {6.0f, 10.0f, 0.0f, 10.0f}}, nodes::Text("Images", 13.0f, colours.dim, true)),
      c::styled({.fillX = true, .grow = scene::axes::kY, .disabled = !opened.editable},
                skiff::compose::scrolled(c::projected<std::optional<shown>>([](const std::optional<shown>& facts) {
                  return facts && facts->draft ? facts->draft->pictures | std::views::transform([](const pack_picture& one) {
                                                   return std::tuple{one.url, std::pair{one.emoji, one.sticker}, one};
                                                 }) | std::ranges::to<std::vector>()
                                               : std::vector<std::tuple<std::string, std::pair<bool, bool>, pack_picture>>{};
                },
                    c::styled({.fillX = true, .autoSize = scene::axes::kY}, c::memo_rows<std::string, std::pair<bool, bool>, picture_item>(picture_row_of{&colours}, nodes::Stack{}))))),
      c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {6.0f, 10.0f, 0.0f, 10.0f}}),
             c::visible(opened.editable, c::styled(button, widgets::Button<sends<pick_images>>(colours.widgets, "Add images", {}))),
             c::styled({.height = 1.0f, .grow = scene::axes::kX}, nodes::Box<>(skia::SkColor{0})),
             c::visible(opened.editable && opened.in_room && !opened.new_pack,
                        c::styled(button, widgets::Button<sends<pack_delete>>(colours.widgets, "Delete pack", {}))),
             c::visible(opened.in_room, c::styled(button, widgets::Button<sends<pack_back>>(colours.widgets, "Back", {}))),
             c::visible(opened.editable, c::styled(button, skiff::compose::primary(widgets::Button<sends<pack_save>>(colours.widgets, "Save", {})))))));
}
using pack_editor_t = decltype(pack_editor(std::declval<const palette&>(), std::declval<const pack_opened&>()));
// Emojis & Stickers, as the program has it: its packs listed -- Create
// where the room's may be added to -- or the one open, edited.
MUX_INLINE_IN_RELEASE auto packs_box(const ui_needs& n, const shown& first) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  using facts_t = std::optional<shown>;
  const auto listing = [](const facts_t& facts) { return facts && !facts->draft; };
  return SKIFF_NAMED(packs_box, c::column(c::vbox(8.0f, {.fillX = true, .height = 600.0f, .padding = {0.0f, 12.0f, 16.0f, 12.0f}}),
      c::row(c::hbox(8.0f, {.fillX = true, .height = kPageHeaderHeight, .padding = {0.0f, 10.0f, 0.0f, 10.0f}}),
             c::text_for<facts_t>([](const facts_t& facts) {
               return !facts || !facts->draft ? std::string("Emojis & Stickers") : facts->new_pack ? std::string("New pack")
                                                : facts->draft->name.empty() ? std::string("Pack") : facts->draft->name;
             },
                 c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(nodes::Text("", 16.0f, colours.text, true)))),
             c::styled({.alignSelf = scene::align::kMiddle}, icon_button(colours, icon::close{}, sends<request::close_shown<shown>>{}))),
      c::text_for<facts_t>([](const facts_t& facts) { return facts ? facts->note : std::string(); },
          c::styled({.fillX = true, .margin = {0.0f, 10.0f, 4.0f, 10.0f}}, wrapped(nodes::Text("", 13.0f, colours.dim)))),
      c::shown_for<facts_t>(listing,
          c::styled({.fillX = true, .grow = scene::axes::kY},
                    skiff::compose::scrolled(c::projected<facts_t>([](const facts_t& facts) {
                      return facts ? std::views::zip(std::views::iota(std::size_t{0}), facts->packs) | std::views::transform([](const auto& one) { return pack_item{std::get<0>(one), std::get<1>(one)}; }) | std::ranges::to<std::vector>()
                                   : std::vector<pack_item>{};
                    },
                        c::listed<pack_item>(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), pack_row_of{&colours, first.room}))))),
      c::shown_for<facts_t>([](const facts_t& facts) { return facts && !facts->draft && facts->room && facts->editable; },
          c::row(c::hbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {6.0f, 10.0f, 0.0f, 10.0f}}),
                 c::styled({.width = 140.0f, .height = 34.0f},
                           skiff::compose::primary(widgets::Button<sends<pack_open>>(colours.widgets, "Create pack", {{std::nullopt}}))))),
      c::projected<facts_t>([](const facts_t& facts) {
        return facts && facts->draft ? std::optional(pack_opened{facts->opened, *facts->draft, draft_editable(*facts), facts->new_pack,
                                                                 facts->room.has_value()})
                                     : std::nullopt;
      },
          c::mount<pack_editor_t, pack_opened>([at = &colours](const pack_opened& opened) { return std::tuple{pack_editor(*at, opened)}; },
                                               {.fillX = true, .grow = scene::axes::kY}))));
}
using box = decltype(packs_box(std::declval<const ui_needs&>(), std::declval<const shown&>()));
// The dialog it is shown in; made once while it is up -- for the room it is
// for, the rest read as the facts move.
inline dialog_look content_look(std::type_identity<shown>) { return {.size = dialog_size::fixed{620.0f, 600.0f}}; }
MUX_INLINE_IN_RELEASE auto make_content(const ui_needs& n, const shown& facts) { return packs_box(n, facts); }
constexpr bool content_persistent(std::type_identity<shown>) { return true; }

// Its dialog, among the window's (mux.ui:window).
constexpr type_tag<box> extend(contract::window_dialogs, type_tag<feature>) { return {}; }

}  // namespace mux::plugins::packs
