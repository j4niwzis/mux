// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui.lists:row_pieces -- The text of rows the window puts together above
// the plugins (mux.ui): pieces of their own, under them, so that a plugin's
// rule may name them -- an account in the drawer, who read a message, a
// room in a server's directory, a notification's card -- and shape them
// where they are put -- built, with the window, under its rules.
module;
#include "skiff/named.h"
export module mux.ui.lists:row_pieces;

import std;
import skia;
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
// (What a rule on a row's text sees, face_facts, is the base's: whose it is.)
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

// A notification as mux shows it itself, as Telegram Desktop's own: a card
// in a small window of its own, the title over the text -- a piece, about
// its chat (the avatars plugin's picture of it beside the text).
inline constexpr struct toast_text_fn : skiff::compose::piece<toast_text_fn> {
  using facts_type = face_facts;
  static face_facts facts(const palette&, const std::string& key, const std::string& title, const std::string&) { return {key, title}; }
  auto make(const palette& colours, const std::string&, std::string title, std::string text) const {
    return two_lines(colours, std::move(title), std::move(text), 14.0f, 4.0f);
  }
} toast_text{};
inline auto toast_card(const palette& colours, std::string key, std::string title, std::string text) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(toast_card, c::row(
      c::hbox(12.0f, {.fill = true, .padding = {12.0f, 14.0f, 12.0f, 14.0f}, .background = colours.sidebar,
                      .border = scene::Border{colours.band, 1.0f}}),
      toast_text(colours, std::move(key), std::move(title), std::move(text))));
}

// A chat's heading in its info: its name over how it is, in the middle,
// each to select -- a piece, about the chat (the avatars plugin's big
// picture of it above).
inline constexpr struct chat_heading_fn : skiff::compose::piece<chat_heading_fn> {
  using facts_type = face_facts;
  static face_facts facts(const palette&, const std::string& key, const std::string& name, const std::string&) { return {key, name}; }
  auto make(const palette& colours, const std::string&, std::string name, std::string status) const {
    namespace c = skiff::compose;
    const auto centred = [](std::string text, float size, skia::SkColor colour, bool bold) {
      auto node = nodes::Text(std::move(text), size, colour, bold);
      node.setSelectable(true);
      return c::styled({.alignSelf = scene::align::kMiddle, .margin = {4.0f, 20.0f, 0.0f, 20.0f}}, elided(std::move(node)));
    };
    return SKIFF_NAMED(chat_heading, c::column(c::vbox(2.0f, {.fillX = true, .autoSize = scene::axes::kY}),
        centred(std::move(name), 17.0f, colours.text, true), centred(std::move(status), 13.0f, colours.dim, false)));
  }
} chat_heading{};

// Who read a message, as the menu's first row says it: how many, or who --
// a piece, about the first few readers (the avatars plugin's faces of them
// at its right, overlapping).
inline constexpr float kSeenFace = 22.0f, kSeenOverlap = 8.0f;
inline constexpr std::size_t kSeenMostFaces = 3;
struct seen_faces_facts {
  std::vector<face_facts> faces;
  skia::SkColor ring = 0;
};
inline constexpr struct seen_summary_fn : skiff::compose::piece<seen_summary_fn> {
  using facts_type = seen_faces_facts;
  static seen_faces_facts facts(const palette& colours, const std::vector<seen_reader>& readers, const std::string&) {
    return {readers | std::views::take(kSeenMostFaces) | std::views::transform([](const seen_reader& one) { return face_facts{one.id, one.name}; }) |
                std::ranges::to<std::vector>(),
            colours.sidebar};
  }
  auto make(const palette& colours, const std::vector<seen_reader>&, std::string said) const {
    namespace c = skiff::compose;
    return c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(nodes::Text(std::move(said), 13.0f, colours.text)));
  }
} seen_summary{};

// A person's name over how they are, at the top of their card, each to
// select -- a piece, about them (the avatars plugin's picture of them
// beside it, pressed to open it).
inline constexpr struct person_cover_text_fn : skiff::compose::piece<person_cover_text_fn> {
  using facts_type = face_facts;
  static face_facts facts(const palette&, const std::string& key, const std::string& name, const std::string&) { return {key, name}; }
  auto make(const palette& colours, const std::string&, std::string name, std::string status) const {
    return two_lines(colours, std::move(name), std::move(status), 17.0f, 6.0f, two_line_style{.selectable = true});
  }
} person_cover_text{};

}  // namespace mux::ui
