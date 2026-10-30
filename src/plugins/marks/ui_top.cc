// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.marks.ui_top -- The mentions of the user or the reactions to
// theirs not yet seen, as a list of the chat's bubbles: each its message --
// a reaction's with who reacted and with what on a badge at its bottom
// right. Pressed, gone to.
module;
#include "skiff/named.h"
export module mux.plugin.marks.ui_top;

import std;
import splice;
import skia;
import skiff.scene;
import skiff.compose;
import skiff.nodes.text;
import mux.composition;
import mux.core;
import mux.protocols;
import mux.ui.base;
import mux.ui.messages;
import mux.plugin.marks.types;

export namespace mux::plugins::marks {
using namespace ::mux::ui;

inline auto mark_badge(const palette& colours, const std::string& who, const std::string& name, const std::string& key) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(mark_badge, c::row(
      c::hbox(4.0f, {.place = scene::anchor::kBottomRight, .x = -14.0f, .y = -2.0f, .autoSize = scene::axes::kBoth,
                     .padding = {2.0f, 6.0f, 2.0f, 3.0f}, .cornerRadius = 12.0f, .background = colours.sidebar}),
      face_spot(avatar_of::person{}, who, name, 20.0f),
      c::styled({.alignSelf = scene::align::kMiddle},
                nodes::Text(proto::is_media(key) ? std::string(":emoji:") : key, 15.0f, colours.text))));
}
struct go_to_act {
  using Answer = std::tuple<go_to, request::close_shown<listed>>;
  mark_kind_t kind;
  std::string event;
  Answer operator()() const { return {go_to{kind, event}, request::close_shown<listed>{}}; }
};
inline auto mark_row(const ui_needs& n, mark_kind_t which, const conversation& in, const entry& one, const model* now) {
  namespace c = skiff::compose;
  const std::string who = one.who.value_or("");
  return SKIFF_NAMED(mark_row, c::onPress(
      go_to_act{which, one.event},
      c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 12.0f, 8.0f, 12.0f},
                               .hoverBackground = n.colours->chosen}),
                message_alone(n, in, one.said, true, true, now),
                c::visible(one.who.has_value(), mark_badge(*n.colours, who, who.empty() ? std::string() : sender_name(in, who), one.key))),
      "Go to message"));
}
// Made from what the list is of; its rows from the chat as the chats hold
// it now -- none where it is gone.
inline auto marks_panel(const ui_needs& n, const listed& facts) {
  namespace c = skiff::compose;
  using close_act = sends<request::close_shown<listed>>;
  using row_t = decltype(mark_row(n, facts.kind, std::declval<const conversation&>(), std::declval<const entry&>(), facts.now));
  const conversation* in = facts.now == nullptr ? nullptr : facts.now->find(facts.in);
  auto rows = in == nullptr ? std::vector<row_t>{}
                            : facts.entries | std::views::transform([&](const entry& one) { return mark_row(n, facts.kind, *in, one, facts.now); }) |
                                  std::ranges::to<std::vector>();
  return SKIFF_NAMED(marks_panel, c::column(
      list_dialog(520.0f),
      page_header<no_back, close_act>(*n.colours,
                                      spl::visit(spl::overloaded{[](mark_kind::mention) { return std::string("Mentions"); },
                                                                 [](mark_kind::reaction) { return std::string("Reactions"); }},
                                                 facts.kind),
                                      {}, {}, false, true),
      dialog_list(c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(rows)))));
}
using box = decltype(marks_panel(std::declval<const ui_needs&>(), std::declval<const listed&>()));
inline dialog_look content_look(std::type_identity<listed>) { return {.sheet = sheet::chat{}, .size = dialog_size::fixed{460.0f, 520.0f}}; }
inline auto make_content(const ui_needs& n, const listed& facts) { return marks_panel(n, facts); }

// Its dialog, among the window's (mux.ui:window).
constexpr type_tag<box> extend(contract::window_dialogs, type_tag<feature>) { return {}; }

}  // namespace mux::plugins::marks
