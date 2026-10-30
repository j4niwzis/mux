// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui.lists:row_pieces -- The text of rows the window puts together above
// the plugins (mux.ui): pieces of their own, under them, so that a plugin's
// rule may name them -- an account in the drawer, who read a message, a
// room in a server's directory -- and shape them where they are put (a::built).
module;
#include "skiff/named.h"
export module mux.ui.lists:row_pieces;

import std;
import skiff.scene;
import skiff.compose;
import skiff.nodes.text;
import mux.core;
import mux.ui.base;

export namespace mux::ui {

// An account in the drawer: its address, its protocol and how it is.
struct drawer_account {
  std::string address;
  std::string protocol;
  std::string how;
  bool failed = false;
  bool current = false;
  friend bool operator==(const drawer_account&, const drawer_account&) = default;
};
// Its text: its address over its protocol and state.
// What a rule on a row's text sees: whose it is -- a person or a room.
struct face_facts {
  std::string id;
  std::string name;
};
inline constexpr struct drawer_account_text_fn : skiff::compose::piece<drawer_account_text_fn> {
  using facts_type = face_facts;
  static face_facts facts(const palette&, const drawer_account& one) { return {one.address, one.address}; }
  auto make(const palette& colours, const drawer_account& one) const {
    return two_lines(colours, one.address, std::format("{} · {}", one.protocol, one.how), 14.0f, 3.0f,
                     two_line_style{.second_ink = one.failed ? colours.error : colours.dim, .show_second = true});
  }
} drawer_account_text{};

// Who read a message, in the menu's list: their name over when.
inline constexpr struct reader_text_fn : skiff::compose::piece<reader_text_fn> {
  using facts_type = face_facts;
  static face_facts facts(const palette&, const seen_reader& one) { return {one.id, one.name}; }
  auto make(const palette& colours, const seen_reader& one) const {
    namespace c = skiff::compose;
    return SKIFF_NAMED(reader_text, c::column(c::vbox(0.0f, {.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}),
        elided(nodes::Text(one.name, 13.0f, colours.text)),
        elided(nodes::Text(one.at ? clock_of(*one.at) : std::string("seen"), 12.0f, colours.dim))));
  }
} reader_text{};

// A room in a server's directory: its name, its address and members, its topic.
inline constexpr struct directory_room_text_fn : skiff::compose::piece<directory_room_text_fn> {
  using facts_type = face_facts;
  static face_facts facts(const palette&, const directory_room& one) { return {one.id, one.name.empty() ? one.alias : one.name}; }
  auto make(const palette& colours, const directory_room& one) const {
    namespace c = skiff::compose;
    return SKIFF_NAMED(directory_room_text, c::column(c::vbox(2.0f, {.autoSize = scene::axes::kY, .grow = scene::axes::kX,
                                                                     .shrink = scene::axes::kX, .alignSelf = scene::align::kMiddle}),
        c::styled({.fillX = true}, elided(nodes::Text(
            one.name.empty() ? (one.alias.empty() ? one.id : one.alias) : one.name, 14.0f, colours.text, true))),
        c::styled({.fillX = true}, elided(nodes::Text(
            one.members > 0 ? std::format("{} · {} members", one.alias, one.members) : one.alias, 12.0f, colours.dim))),
        c::visible(!one.topic.empty(), c::styled({.fillX = true}, elided(nodes::Text(one.topic, 13.0f, colours.text))))));
  }
} directory_room_text{};

}  // namespace mux::ui
