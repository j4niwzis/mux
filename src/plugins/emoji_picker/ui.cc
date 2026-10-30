// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.emoji_picker.ui -- The emoji popup, the media panel: its pages
// -- its emoji, and each a plugin gives it (contract::media_pages) -- and
// what a pick does; a layer over the window (contract::window_layers).
module;
#include "../../inline_in_release.h"
#include "skiff/named.h"
export module mux.plugin.emoji_picker.ui;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.image;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.textbox;
import mux.core;
import mux.config;
import mux.logic.emoji;
import mux.plugins.types;
import mux.ui.base;
import mux.composition;
import skiff.bind;
import mux.plugin.emoji_picker.types;

export namespace mux::plugins::emoji_picker {

using namespace ::mux::ui;

// The picker page is part of what is shown, so data refreshes retain it:
// its emoji, and each page a plugin gives the media panel
// (contract::media_pages), in the plugins' order.
namespace popup_page {
struct emoji { friend bool operator==(emoji, emoji) = default; };
}
template <class... Features>
consteval auto popup_pages_of(plugin_list<Features...>) {
  return type_tag<spl::variant<popup_page::emoji,
      typename decltype(page_type(contract::media_pages{}, type_tag<Features>{}))::type...>>{};
}
using media_page_plugins = composition::providers<contract::media_pages, enabled_plugins>;
using popup_page_t = typename decltype(popup_pages_of(media_page_plugins{}))::type;
// What is shown on each plugin's page, beside the others' (emoji_facts).
template <class... Features>
consteval auto pages_shown_of(plugin_list<Features...>) {
  return type_tag<std::tuple<typename decltype(shown_type(contract::media_pages{}, type_tag<Features>{}))::type...>>{};
}
using pages_shown_t = typename decltype(pages_shown_of(media_page_plugins{}))::type;
// A page of the media panel chosen: by its type, one a plugin may give.
struct select_emoji_page { popup_page_t page; };
// What the input's emoji do: go into what is written.
struct insert_emoji_into {
  using Answer = ::mux::ui::request::insert_emoji;
  [[nodiscard]] static constexpr bool takes_text() { return false; }
  // A glyph as itself; a custom emoji (its key its picture's, not its
  // text) as its picture.
  Answer operator()(const std::string& text, const std::string& key) const {
    return ::mux::ui::request::insert_emoji{text, key == text ? std::string() : key};
  }
};

// The input's emoji, as tdesktop's panel: a card over the chat, 345 wide
// (emojiPanWidth), 278 to 640 high, rounded 8, its bottom right at the top
// right of the button that opened it; a press off it closes it. It stays
// open while emoji are picked, and the input keeps the keys.
// The input's emoji panel open: where it is put, its right edge and its
// bottom, in the window.
struct emoji_facts {
  float right = 0.0f;
  float bottom = 0.0f;
  popup_page_t page = popup_page::emoji{};
  bool stickers = false;  // the account sends stickers
  bool files = false;     // and files
  // What is shown on each plugin's page, as the program has it.
  pages_shown_t pages;
  friend bool operator==(const emoji_facts&, const emoji_facts&) = default;
};
// Compared, or its layer's mount, unable to tell the same facts, made the
// whole popup again -- every emoji's cell -- each time they were read, and
// what it made asked for the next whole walk: the panel at a few frames a
// second.
static_assert(std::equality_comparable<emoji_facts>);
// Whether a page is offered: a plugin's says where (shown_where).
inline bool page_available(const emoji_facts& facts, const popup_page_t& page) {
  return spl::visit(spl::overloaded{
      [](popup_page::emoji) { return true; },
      [&](const auto& theirs) { return shown_where(theirs, facts); }}, page);
}
// The pages in their order: what a swipe goes through.
template <class... Features>
std::array<popup_page_t, 1 + sizeof...(Features)> all_pages(plugin_list<Features...>) {
  return {popup_page::emoji{}, typename decltype(page_type(contract::media_pages{}, type_tag<Features>{}))::type{}...};
}

MUX_INLINE_IN_RELEASE auto popup_tab(const palette& colours, popup_page_t page, std::string name, bool selected, bool available) {
  namespace c = skiff::compose;
  return c::visible(available, c::onClick(select_emoji_page{std::move(page)},
      c::row(c::justified(c::hbox(0.0f, {.width = 80.0f, .height = 28.0f, .cornerRadius = 6.0f,
          .hoverBackground = colours.chosen, .selectedBackground = colours.tile, .selected = selected}), nodes::justify::middle{}),
          c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(name, 13.0f, colours.text, true))), name));
}

// Where the popup's card goes: as tdesktop's panel, 345 wide or what the
// window leaves, 278 to 640 high, its bottom right at the top right of the
// button that opened it, kept inside; on a phone, docked along the window's
// bottom -- where the keyboard would be -- across all of it, its tabs a row
// under the panel.
inline constexpr float kPopupTabsHigh = 40.0f;
[[nodiscard]] inline skia::SkRect popup_place(const window_shape& window, float right, float bottom) {
  constexpr float kMostWidth = 345.0f, kEdge = 10.0f;
  if (window.phone()) {
    const float high = std::round(window.height * 0.4f);
    return skia::SkRect::MakeXYWH(0.0f, window.height - high, window.width, high);
  }
  const float wide = std::min(kMostWidth, std::max(160.0f, window.width - 2.0f * kEdge));
  const float high = std::min(std::clamp(window.height * 0.6f, 278.0f, 640.0f), std::max(0.0f, bottom - kEdge));
  const float x = std::clamp(right - wide, kEdge, std::max(kEdge, window.width - wide - kEdge));
  return skia::SkRect::MakeXYWH(x, std::max(kEdge, bottom - high), wide, high);
}
// A plugin's page in its slot: a node as it is; one that may not move (it
// points at its own parts) built in place from its arguments.
template <class Made> auto page_arguments(Made made) { return std::tuple{std::move(made)}; }
template <class Node, class... Args> auto page_arguments(composition::in_place<Node, Args...> made) { return std::move(made.args); }
template <class List>
struct popup_pages;
template <class... Features>
struct popup_pages<plugin_list<Features...>> {
  template <class Feature>
  using page_t = typename decltype(page_type(contract::media_pages{}, type_tag<Feature>{}))::type;
  template <class Feature>
  using shown_t = typename decltype(shown_type(contract::media_pages{}, type_tag<Feature>{}))::type;
  template <class Feature, class Needs>
  static auto made(const Needs& needs, const emoji_facts& facts) {
    static_assert(requires { extend(contract::media_pages{}, type_tag<Feature>{}, needs, std::get<shown_t<Feature>>(facts.pages)); },
                  composition::broken<contract::media_pages, Feature>());
    return extend(contract::media_pages{}, type_tag<Feature>{}, needs, std::get<shown_t<Feature>>(facts.pages));
  }
  // Each in its slot, made as the popup is: shown where it is the page
  // chosen and offered.
  template <class Feature, class Needs>
  static auto page_of(const Needs& needs, const emoji_facts& facts) {
    namespace c = skiff::compose;
    using node_t = composition::node_t<decltype(made<Feature, Needs>(needs, facts))>;
    const bool shown = facts.page == popup_page_t(page_t<Feature>{}) && page_available(facts, page_t<Feature>{});
    auto slot = c::mount<node_t, bool>([&needs, &facts](bool) { return page_arguments(made<Feature, Needs>(needs, facts)); },
                                       {.fillX = true, .grow = scene::axes::kY, .masking = true});
    slot.read(shown ? std::optional(true) : std::nullopt);
    return slot;
  }
  // Their column, there only while one of them is the page: shown with the
  // emoji's, it grew beside them, and took half the card -- the emoji in the
  // top half of it, their groups' bar over the field under it.
  template <class Needs>
  static auto pages(const Needs& needs, const emoji_facts& facts) {
    return skiff::compose::column(skiff::compose::vbox(0.0f, {.fillX = true, .grow = scene::axes::kY,
                                                              .visible = facts.page != popup_page_t(popup_page::emoji{})}),
                                  page_of<Features, Needs>(needs, facts)...);
  }
  static auto tabs(const palette& colours, const emoji_facts& facts, skiff::compose::Look look) {
    return skiff::compose::row(std::move(look),
        popup_tab(colours, popup_page::emoji{}, "Emoji", facts.page == popup_page_t(popup_page::emoji{}), true),
        popup_tab(colours, page_t<Features>{}, std::string(tab_name(page_t<Features>{})), facts.page == popup_page_t(page_t<Features>{}),
                  page_available(facts, page_t<Features>{}))...);
  }
};
using popup_pages_t = popup_pages<media_page_plugins>;
// A flick across a docked panel: to the next page offered, or the one
// before.
struct flick_pages {
  emoji_facts facts;
  std::optional<select_emoji_page> operator()(bool on) const {
    static const auto kPages = all_pages(media_page_plugins{});
    const auto offered = kPages | std::views::filter([&](const popup_page_t& page) { return page_available(facts, page); }) | std::ranges::to<std::vector>();
    const auto at = static_cast<int>(std::ranges::find(offered, facts.page) - offered.begin());
    const int to = std::clamp(at + (on ? 1 : -1), 0, static_cast<int>(offered.size()) - 1);
    return to == at ? std::nullopt : std::optional(select_emoji_page{offered[static_cast<std::size_t>(to)]});
  }
};
// The input's emoji, and each page a plugin gives the panel, as tdesktop's:
// a card over the chat, its tabs over its pages, a press off it going
// through to what is under it -- the chat scrolled with it open, a tap off
// it closing it (the window's). It stays open while emoji are picked, and
// the input keeps the keys. On a phone, docked, its tabs under it and a
// flick across it going through them.
template <class Needs>
auto emoji_popup(const Needs& n, const emoji_facts& facts) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  const scene::Spec tabs_look{.fillX = true, .height = 36.0f, .depth = 1.0f, .padding = {4.0f, 8.0f, 4.0f, 8.0f}};
  const auto phone = [](const window_shape& now) { return now.phone(); };
  auto card = c::spec_for<window_shape>([&colours, right = facts.right, bottom = facts.bottom](const window_shape& now) {
    const skia::SkRect at = popup_place(now, right, bottom);
    return scene::Spec{.place = scene::anchor::kTopLeft, .x = at.fLeft, .y = at.fTop, .width = at.width(), .height = at.height(),
                       .padding = {0.0f, 0.0f, now.phone() ? kPopupTabsHigh : 0.0f, 0.0f}, .cornerRadius = now.phone() ? 0.0f : 8.0f,
                       .background = colours.sidebar, .border = scene::Border{colours.band, 1.0f},
                       .shadow = scene::Shadow{skia::colorSetARGB(70, 0, 0, 0), 3.0f}};
  },
      c::flicked(90.0f, flick_pages{facts},
          c::column(c::vbox(0.0f, {}),
              c::shown_for<window_shape>(std::not_fn(phone), popup_pages_t::tabs(colours, facts, c::hbox(4.0f, tabs_look))),
              // In a column that grows, the panel filling it: styled so, the
              // panel's own fill took the place of the grow, and it was as tall
              // as the whole card -- its footer under the card's bottom edge.
              c::column(c::vbox(0.0f, {.fillX = true, .grow = scene::axes::kY, .masking = true,
                                       .visible = facts.page == popup_page_t(popup_page::emoji{})}),
                        emoji_panel(colours, *n.emoji, insert_emoji_into{})),
              popup_pages_t::pages(n, facts),
              c::shown_for<window_shape>(phone,
                  popup_pages_t::tabs(colours, facts,
                      c::justified(c::hbox(4.0f, {.place = scene::anchor::kBottomLeft, .y = kPopupTabsHigh, .fillX = true, .height = 36.0f,
                                                  .depth = 1.0f, .padding = {4.0f, 8.0f, 4.0f, 8.0f}}),
                                   nodes::justify::middle{}))))));
  // Its own floating backdrop painted: the full-window layer has no fill,
  // and a plate taken for an inner panel's was dropped under live blur.
  skiff::compose::state_of(card).setFloats(true);
  return SKIFF_NAMED(emoji_popup, c::column(c::vbox(0.0f, {.fill = true}), std::move(card)));
}

// Its layer over the window: there while the panel is open, a tap off it
// closing it. Made where the window is (a template on its needs): the
// pages the plugins give it are found there.
struct make_emoji_popup {
  template <class Needs>
  auto operator()(const Needs& n, const emoji_facts& facts) const { return emoji_popup(n, facts); }
};
constexpr type_tag<std::optional<emoji_facts>> facts_type(contract::window_layers, type_tag<feature>) { return {}; }
template <class Needs>
auto extend(contract::window_layers, type_tag<feature>, const Needs& needs) {
  return skiff::compose::tapped_off(sends<::mux::ui::request::close_shown<emoji_facts>>{}, layer_made_by<emoji_facts, make_emoji_popup>(needs));
}

}  // namespace mux::plugins::emoji_picker
