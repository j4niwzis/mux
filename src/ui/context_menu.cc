// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:context_menu -- A message's menu.
module;
#include "../inline_in_release.h"
#include "skiff/named.h"
export module mux.ui:context_menu;

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
// Its rows' text, pieces the plugins shape: built with the window.
import mux.ui.lists;
import mux.ui.shaping;
import mux.plugins.ui.top;
import mux.ui.proto;
import mux.ui.messages;
import :conversations;

export import mux.ui.base;

export namespace mux::ui {

MUX_INLINE_IN_RELEASE auto emoji_favourite_row(const palette& colours, const emoji_kept& kept, const std::optional<emote>& emoji) {
  namespace c = skiff::compose;
  return c::visible(emoji.has_value(), c::onClick(request::favourite_emoji{emoji.value_or(emote{})},
      c::row(c::hbox(0.0f, {.fillX = true, .height = 33.0f, .padding = {0.0f, 17.0f, 0.0f, 17.0f}, .hoverBackground = colours.chosen}),
          c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(emoji && kept.emoji_is_favourite(emoji->url)
              ? "Remove from favorites" : "Add to favorites", 13.0f, colours.text)))));
}
// What the menu's own parts do to what it has open: a top unrolled (its
// place among the plugins' tops).
//
// Each top's unrolling a plain on() of its own, not one template for every
// feature: a debug build routes a press's answer through a table of the
// event types a Local's handlers take, found by offering on() a probe that
// converts to anything (skiff.bind:takes) -- and no conversion deduces a
// template's Feature. menu_unroll<Feature> was in no table, the arrow's
// answer went up past the menu unseen, and the emoji never unrolled (the
// user's: "I can't even open it"). The menu built alone, as a test's root,
// routes the static way, and did.
template <class Feature, std::size_t At>
struct unroll_of {
  auto on(const contract::menu_unroll<Feature>&, const contract::message_menu_state& now) const {
    auto next = now;
    next.unrolled = At;
    return skiff::model::over<contract::message_menu_state>(skiff::model::setTo(std::move(next)));
  }
};
template <class Places, class... Features>
struct menu_events_at;
template <std::size_t... At, class... Features>
struct menu_events_at<std::index_sequence<At...>, Features...> : unroll_of<Features, At + 1>... {
  using unroll_of<Features, At + 1>::on...;
};
template <class List>
struct menu_events_of;
template <class... Features>
struct menu_events_of<plugin_list<Features...>> : menu_events_at<std::index_sequence_for<Features...>, Features...> {
  // Each's place among them, from 1: found by overloading on its tag.
  template <class Feature, std::size_t At> struct slot_entry {
    static constexpr std::size_t at(type_tag<Feature>) { return At; }
  };
  template <class Seq> struct slots;
  template <std::size_t... At> struct slots<std::index_sequence<At...>> : slot_entry<Features, At + 1>... {
    using slot_entry<Features, At + 1>::at...;
  };
  template <class Feature>
  static constexpr std::size_t slot_of() {
    return slots<std::index_sequence_for<Features...>>::at(type_tag<Feature>{});
  }
};
// What plugins put at its top (contract::message_menu_top) -- the quick
// reactions -- each its head, and what it unrolls over the items once it
// asks, made then: in their order.
template <class List>
struct menu_tops;
template <class... Features>
struct menu_tops<plugin_list<Features...>> {
  template <class Feature>
  static auto head_of(const palette& colours, const menu_facts& facts) {
    static_assert(requires { extend(contract::message_menu_top{}, type_tag<Feature>{}, colours, facts); },
                  composition::broken<contract::message_menu_top, Feature>());
    return extend(contract::message_menu_top{}, type_tag<Feature>{}, colours, facts);
  }
  static auto heads(const palette& colours, const menu_facts& facts) {
    return skiff::compose::column(skiff::compose::vbox(0.0f, {.autoSize = scene::axes::kBoth}), head_of<Features>(colours, facts)...);
  }
  // Each's own, over the items: made once it unrolls, from their top,
  // filling them -- grown where they left too little room for a list.
  template <class Feature>
  static auto over_of(const palette& colours, emoji_kept& kept) {
    namespace c = skiff::compose;
    using made_t = decltype(unrolled(contract::message_menu_top{}, type_tag<Feature>{}, colours, kept));
    constexpr std::size_t slot = menu_events_of<plugin_list<Features...>>::template slot_of<Feature>();
    return c::projected<contract::message_menu_state>(
        [](const contract::message_menu_state& now) { return now.unrolled == slot ? std::optional(true) : std::nullopt; },
        c::mount<made_t, bool>([at = &colours, from = &kept](bool) { return std::tuple{unrolled(contract::message_menu_top{}, type_tag<Feature>{}, *at, *from)}; },
                               {.place = scene::anchor::kTopLeft, .fill = true, .background = colours.sidebar, .masking = true}));
  }
  static auto overs(const palette& colours, emoji_kept& kept) {
    return skiff::compose::column(skiff::compose::vbox(0.0f, {.place = scene::anchor::kTopLeft, .fill = true}), over_of<Features>(colours, kept)...);
  }
};
using menu_tops_t = menu_tops<composition::providers<contract::message_menu_top, enabled_plugins>>;
using menu_events = menu_events_of<composition::providers<contract::message_menu_top, enabled_plugins>>;
// Every top's unrolling in the table a debug build routes the menu's
// answers by (skiff.bind:takes): one missing is an arrow that does nothing.
template <class... Features>
consteval bool unrolls_listed(plugin_list<Features...>) {
  using taken = skiff::bind::takes::OnEvents<menu_events, contract::message_menu_state>;
  return (skiff::bind::takes::kIn<contract::menu_unroll<Features>, taken> && ...);
}
static_assert(unrolls_listed(composition::providers<contract::message_menu_top, enabled_plugins>{}),
              "a menu top's unrolling is not in the menu's handlers' table: its arrow would do nothing");
// What plugins put at its foot (contract::message_menu_foot): each its row,
// its popover over the window, and a state of its own around the whole
// menu, which both read -- in their order.
template <class List>
struct menu_feet;
template <class... Features>
struct menu_feet<plugin_list<Features...>> {
  static auto rows(const palette& colours, const menu_facts& facts) {
    return skiff::compose::column(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                                  extend(contract::message_menu_foot{}, type_tag<Features>{}, colours, facts)...);
  }
  static auto popovers(const palette& colours, const menu_facts& facts) {
    return skiff::compose::column(skiff::compose::vbox(0.0f, {.place = scene::anchor::kTopLeft, .fill = true}),
                                  foot_popover(contract::message_menu_foot{}, type_tag<Features>{}, colours, facts)...);
  }
  template <class Node>
  static auto held(Node node) {
    return held_by<Features...>(std::move(node));
  }
  template <class... None, class Node>
    requires(sizeof...(None) == 0)
  static Node held_by(Node node) {
    return node;
  }
  template <class First, class... Rest, class Node>
  static auto held_by(Node node) {
    using state_t = typename decltype(foot_state(contract::message_menu_foot{}, type_tag<First>{}))::type;
    return skiff::compose::local<state_t>(foot_events(contract::message_menu_foot{}, type_tag<First>{}), held_by<Rest...>(std::move(node)));
  }
};
using menu_feet_t = menu_feet<composition::providers<contract::message_menu_foot, enabled_plugins>>;
// The rows plugins add to a message's menu (contract::message_menu), after
// Save As, in the plugins' order.
template <class List>
struct menu_rows;
template <class... Features>
struct menu_rows<plugin_list<Features...>> {
  template <class Feature>
  static auto row_of(const palette& colours, const emoji_kept& kept, const menu_facts& facts) {
    static_assert(requires { extend(contract::message_menu{}, type_tag<Feature>{}, colours, kept, facts); },
                  composition::broken<contract::message_menu, Feature>());
    return extend(contract::message_menu{}, type_tag<Feature>{}, colours, kept, facts);
  }
  static auto rows(const palette& colours, const emoji_kept& kept, const menu_facts& facts) {
    return skiff::compose::column(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), row_of<Features>(colours, kept, facts)...);
  }
};
using menu_rows_t = menu_rows<composition::providers<contract::message_menu, enabled_plugins>>;
inline constexpr float kMenuListLeast = 220.0f;
// A message's menu, as tdesktop's, in its order: the quick reactions; every
// emoji, in place of the rest once asked for; Reply, Edit, Pin, Copy, Copy
// Message Link, Save As, Forward, Delete; what plugins put at the foot --
// who has seen it, and their names beside it as it is pointed at. What does
// not apply
// to the message left out. At the pointer, going down and right -- up where
// it would pass the window's bottom, left where it would pass its right --
// and kept inside the window. Up and Down go through its items, Enter does
// what is lit, Esc and a press off it close it.
MUX_INLINE_IN_RELEASE auto menu_card(const palette& colours, emoji_kept& kept, const menu_facts& facts) {
  namespace c = skiff::compose;
  const auto item = [&](bool shown, std::string label, auto act, icon_t mark) {
    return c::visible(shown, row_item(colours, std::move(label), std::move(act), mark, std::nullopt, menu_row_look));
  };
  auto items = c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      item(facts.editable && !facts.reaction && ((!facts.text.empty() && !facts.media) || facts.captioned), "Edit", sends<request::menu_edit>{},
           icon::sliders{}),
      item(!facts.copied.empty(), facts.selection ? "Copy Selected Text" : "Copy Text", sends<request::menu_copy>{}, icon::clip{}),
      item(!facts.link.empty(), "Copy Message Link", sends<request::menu_copy_link>{}, icon::info{}),
      item(!facts.pressed_link.empty(), "Copy Link", sends<request::menu_copy_url>{}, icon::clip{}),
      emoji_favourite_row(colours, kept, facts.emoji),
      item(facts.picture.has_value(), "Copy Image", sends<request::menu_copy_image>{}, icon::clip{}),
      item(facts.media.has_value(), "Save As\u2026", sends<request::menu_save>{}, icon::send{}),
      menu_rows_t::rows(colours, kept, facts),
      item(!facts.id.empty(), "Select", sends<request::menu_select>{}, icon::check{}),
      item(facts.deletable, "Delete", sends<request::menu_delete>{}, icon::close{}),
      c::styled({.fillX = true, .height = 1.0f, .margin = {4.0f, 0.0f, 4.0f, 0.0f}}, nodes::Box<>(colours.band)),
      menu_feet_t::rows(colours, facts));
  return SKIFF_NAMED(menu_card, c::onClick(scene::Taken{},
          c::focused_by<keys_asked>(keys_on<keys::menu>{},
          c::moves_focus([](const scene::key::down& press) {
                           return press.key == scene::keys::kEscape ? std::optional(request::close_menu{}) : std::nullopt;
                         },
                         c::column(c::vbox(0.0f, {.autoSize = scene::axes::kBoth, .minWidth = 220.0f, .padding = {6.0f, 0.0f, 6.0f, 0.0f},
                                                  .cornerRadius = 10.0f, .background = colours.popup(), .border = scene::Border{colours.band, 1.0f},
                                                  .shadow = scene::Shadow{skia::colorSetARGB(70, 0, 0, 0), 3.0f}}),
              menu_tops_t::heads(colours, facts),
              // The items, and over them what a top unrolled: the items kept
              // under it, unseen, so the menu keeps its size -- grown only
              // where they left too little room for a list.
              c::spec_for<contract::message_menu_state>([](const contract::message_menu_state& now) {
                return scene::Spec{.minHeight = now.unrolled ? kMenuListLeast : 0.0f};
              },
                  c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                            c::spec_for<contract::message_menu_state>([](const contract::message_menu_state& now) {
                              return scene::Spec{.alpha = now.unrolled ? 0.0f : 1.0f, .disabled = now.unrolled != 0};
                            }, std::move(items)),
                            menu_tops_t::overs(colours, kept))))))));
}
// The menu over the whole window: its card at the pointer, what the
// plugins at its foot pop beside it; a press off them closes it.
MUX_INLINE_IN_RELEASE auto context_menu(const ui_needs& n, const menu_facts& facts) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  const skiff::compose::PopAt at{.from = skia::SkRect::MakeXYWH(facts.x, facts.y, 0.0f, 0.0f)};
  // A press off it closes it -- a right press too: caught here, over the
  // whole window, a right-click on another message did nothing at all, the
  // menu left open.
  return SKIFF_NAMED(context_menu, menu_feet_t::held(c::local<contract::message_menu_state>(menu_events{},
      c::onRightPress(request::menu_pressed_off{}, c::onClick(request::close_menu{},
          c::column(c::vbox(0.0f, {.fill = true}),
              c::popped_at<contract::message_menu_state>([at](const contract::message_menu_state&) { return std::optional(at); }, 8.0f,
                                                         c::styled({}, menu_card(colours, *n.emoji, facts))),
              menu_feet_t::popovers(colours, facts)))))));
}
using context_menu_t = decltype(context_menu(std::declval<const ui_needs&>(), std::declval<const menu_facts&>()));

}  // namespace mux::ui

