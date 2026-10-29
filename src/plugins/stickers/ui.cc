// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.stickers.ui -- The stickers' page of the media panel, as
// tdesktop's: a search over the packs -- recent, favourites, then the chat's
// and one's own, each a tab -- and a press sends one. Made of functions and
// combinators: the search a Local's field, the packs projected from it into
// sections, each cell resting to show its sticker large. And a sticker made a
// favourite, or no longer one, from its menu.
module;
#include "skiff/named.h"
export module mux.plugin.stickers.ui;

import std;
import mux.composition;
import skia;
import skiff.scene;
import skiff.compose;
import skiff.nodes.flow;
import skiff.nodes.image;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.textbox;
import skiff.widgets.model;
import skiff.model;
import mux.core;
import mux.logic.text;
import mux.ui.base;
import mux.plugin.stickers.types;

export namespace mux::plugins::stickers {
// What the media panel's pages are made of: the window's own.
using namespace ::mux::ui;

// A pack, as the page lists it: its name, its stickers, and its tab --
// a picture where it has one, else a mark.
struct pack {
  std::string name;
  std::vector<emote> stickers;
  std::optional<std::string> picture;
  std::string mark;
};
// What the search field holds: the page's own state.
struct search {
  std::string query;
  friend bool operator==(const search&, const search&) = default;
};
// A tab pressed while a search is shown: the search left, every pack shown
// again, and its pack brought.
struct leave_search {};
struct search_events {
  auto on(leave_search) const { return skiff::model::over<skiff::model::Field<&search::query>>(skiff::model::setTo(std::string())); }
};

// The packs as the chat has them: Recent -- those sent lately that the chat
// still has -- then the favourites, whichever chat they came from, then
// every pack in the order they first come, the unnamed together as
// "Stickers". Searched: those whose shortcode, words or pack have what is
// typed, by their packs.
inline std::vector<pack> packs_of(const emoji_kept& kept, std::string_view query) {
  constexpr auto lower = mux::logic::folded;
  if (!query.empty()) {
    const std::string wanted = lower(query);
    const auto found = kept.chat_stickers | std::views::filter([&](const emote& one) {
                         return lower(one.shortcode).contains(wanted) || lower(one.body).contains(wanted) ||
                                lower(one.pack).contains(wanted);
                       }) | std::ranges::to<std::vector>();
    return grouped_emotes(found, "Search results") | std::views::transform([](auto one) { return pack{std::move(one.first), std::move(one.second), std::nullopt, "\u23F2"}; }) | std::ranges::to<std::vector>();
  }
  auto recent = kept.recent_stickers | std::views::filter([&](const emote& one) {
                  return std::ranges::contains(kept.chat_stickers, one.url, &emote::url);
                }) | std::ranges::to<std::vector>();
  std::vector<pack> out;
  if (!recent.empty())
    out.push_back({"Recently used", std::move(recent), std::nullopt, "\u23F2"});
  if (!kept.favourite_stickers.empty())
    out.push_back({"Favourites", kept.favourite_stickers, std::nullopt, "\u2605"});
  out.append_range(grouped_emotes(kept.chat_stickers, "Stickers") | std::views::transform([](auto one) {
                     const emote& first = one.second.front();
                     auto picture = first.pack_avatar ? first.pack_avatar : std::optional<std::string>(first.url);
                     return pack{std::move(one.first), std::move(one.second), std::move(picture), "\u23F2"};
                   }));
  return out;
}

inline constexpr float kCell = 78.0f;
// A sticker: pressed, sent; the pointer resting on it, shown large.
inline auto cell(const palette& colours, emoji_kept& kept, const emote& one) {
  namespace c = skiff::compose;
  auto picture = nodes::Image<from_emotes>({one.url, emote_kind::sticker});
  picture.keepBox();  // the cell's size, whatever the sticker
  return SKIFF_NAMED(cell, c::resting(kPreviewAfterMs, rests_on{previewed{one.url, ":" + one.shortcode + ":", true, emote_kind::sticker}},
      c::onClick(request::send_sticker{one},
          c::column(c::vbox(0.0f, {.width = kCell, .height = kCell, .margin = {2.0f, 2.0f, 2.0f, 2.0f},
                                   .padding = {4.0f, 4.0f, 4.0f, 4.0f}, .cornerRadius = 6.0f,
                                   .hoverBackground = colours.chosen}),
              c::styled({.fill = true}, std::move(picture))))));
}
// A pack's tab: its picture, or its mark; lit while its pack is at the top.
inline auto tab(const palette& colours, const pack& one) {
  namespace c = skiff::compose;
  std::optional<nodes::Image<from_emotes>> image;
  if (one.picture) {
    image.emplace(from_emotes{*one.picture, emote_kind::sticker});
    image->apply({.width = 24.0f, .height = 24.0f, .alignSelf = scene::align::kMiddle});
    image->keepBox();
  }
  return SKIFF_NAMED(tab, c::row(c::justified(c::hbox(0.0f, {.width = 30.0f, .height = 30.0f, .minWidth = 24.0f, .alignSelf = scene::align::kMiddle,
                                            .cornerRadius = 6.0f, .hoverBackground = colours.chosen, .selectedBackground = colours.tile}),
                             nodes::justify::middle{}),
      std::move(image), c::visible(!one.picture, c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(one.mark, 16.0f, colours.text)))));
}

// The chat's stickers, as tdesktop's tab: a search at its top; the packs one
// under another in a list that scrolls; a footer of their tabs that brings
// each into view, lit for the one in view; a press sends one; the pointer
// resting on one shows it large.
inline auto page(const palette& colours, emoji_kept& kept) {
  namespace c = skiff::compose;
  auto field = widgets::TextField<std::string>(colours.widgets, "Search stickers");
  field.setSearchIcon(true);
  auto packs = c::sections<pack>(c::vbox(4.0f, {.fill = true}), c::vbox(0.0f),
      [&colours](const pack& one) { return tab(colours, one); },
      [&colours, &kept](const pack& one) {
        return cell_section(colours, one.name,
                            one.stickers | std::views::transform([&](const emote& each) { return cell(colours, kept, each); }) | std::ranges::to<std::vector>(),
                            source_pack(one.stickers, one.name), kept.pack_account);
      },
      // The footer: the tabs in rows that wrap, 36 high, scrolled to the one lit.
      c::wrapped<pack>(c::vbox(0.0f, {.fillX = true, .height = 36.0f}), c::hbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY})),
      [](std::size_t) { return leave_search{}; });
  return with_preview(colours,
      c::local<search>(search_events{},
          c::column(picker_look(),
              c::styled({.fillX = true, .height = 32.0f, .margin = {0.0f, 7.0f, 0.0f, 0.0f}},
                        c::bound<skiff::model::Field<&search::query>>(std::move(field))),
              c::visible(kept.chat_stickers.empty() && kept.favourite_stickers.empty(),
                  c::styled({.fillX = true, .margin = {12.0f, 12.0f, 0.0f, 12.0f}},
                            wrapped(nodes::Text("No stickers here. A room's sticker packs, and yours, show here.", 13.0f, colours.dim)))),
              c::styled({.fillX = true, .grow = scene::axes::kY},
                  c::projected<skiff::model::Field<&search::query>>(
                      [&kept](const std::string& query) {
                        // A search's results, every pack's tab still there, none lit.
                        return c::SectionsShown<pack>{packs_of(kept, query), packs_of(kept, {}), query.empty()};
                      },
                      std::move(packs))))));
}
using page_t = decltype(page(std::declval<const palette&>(), std::declval<emoji_kept&>()));

// Its page, in the media panel (the emoji picker's: mux.plugin.emoji_picker.ui).
inline auto extend(contract::media_pages, type_tag<feature>, const ui_needs& needs, const shown&) {
  return page(*needs.colours, *needs.emoji);
}

// A sticker's menu: made a favourite, or no longer one.
inline auto extend(contract::message_menu, type_tag<feature>, const palette& colours, const emoji_kept& kept,
            const menu_facts& facts) {
  return skiff::compose::visible(
      facts.sticker.has_value(),
      row_item(colours, facts.sticker && kept.is_favourite(facts.sticker->url) ? "Remove from Favourites" : "Add to Favourites",
          sends<favourite>{favourite{facts.sticker.value_or(emote{})}}, icon::check{}, std::nullopt, menu_row_look));
}

}  // namespace mux::plugins::stickers
