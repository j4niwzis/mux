// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:info_reactions -- The list dialogs: a message's edit history, and the
// marks not yet seen; what a list dialog is made of.
export module mux.ui:info_reactions;

import std;
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

export namespace mux::ui {
// A list dialog's rows, under its top bar: the box as high as it is, the
// list scrolling in what is left of it, its rows one under another.
// A message's edit history, as AyuGram Desktop's: each version of it the
// chat's own bubble, on the chat's colour, oldest first, each at the time
// it was written -- the message as it is now last, the list scrolled to it.
// A message's earlier versions open: in which chat, the message, the chats.
struct history_facts {
  conversation_id in;
  message said;
  const model* known = nullptr;
};
// Version IDs are the events that wrote their bodies. Legacy histories
// without edit IDs remain readable, but do not guess a reaction target.
[[nodiscard]] inline std::vector<message> edit_versions_of(const message& now, const conversation* chat = nullptr) {
  const auto as_message = [&](const mux::body& body, auto at, std::string id) {
    message out = now;
    out.id = std::move(id);
    out.body = body;
    out.at = at;
    out.versions.clear();
    out.latest_edit_event.clear();
    out.edited = false;
    out.reactions.clear();
    out.reaction_events.clear();
    if (chat)
      if (const auto* event = held_message(*chat, out.id)) {
        out.reactions = event->reactions;
        out.reaction_events = event->reaction_events;
        out.redacted = event->redacted;
      }
    return out;
  };
  auto versions = std::views::iota(std::size_t{0}, now.versions.size()) | std::views::transform([&](std::size_t i) { return as_message(now.versions[i].body, i == 0 ? now.at : now.versions[i - 1].until,
          now.versions[i].event.empty() && i == 0 ? now.id : now.versions[i].event); }) | std::ranges::to<std::vector>();
  versions.push_back(as_message(now.body, now.versions.empty() ? now.at : now.versions.back().until,
      now.latest_edit_event.empty() && now.versions.empty() ? now.id : now.latest_edit_event));
  return versions;
}
// A version of the message, as the chat shows it: its menu that of a
// version, not edited again.
auto edit_history_row(const ui_needs& n, const conversation& in, const message& event, const model* known) {
  namespace c = skiff::compose;
  return c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 12.0f, 4.0f, 12.0f}}),
                   message_alone(n, in, event, true, true, known, true));
}
auto edit_history_panel(const ui_needs& n, const history_facts& facts) {
  namespace c = skiff::compose;
  const auto& chat = chat_or_none(facts.known, facts.in);
  const auto versions = edit_versions_of(facts.said, &chat);
  auto rows = versions | std::views::transform([&](const message& event) {
    return edit_history_row(n, chat, event, facts.known);
  }) | std::ranges::to<std::vector>();
  auto list = dialog_list(c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(rows)));
  list.scrollToEnd(false);
  return c::column(list_dialog(560.0f),
      page_header<no_back, sends<request::close_edit_history>>(*n.colours, "Edit History", {}, {}, false, true), std::move(list));
}
using edit_history_box = decltype(edit_history_panel(std::declval<const ui_needs&>(), std::declval<const history_facts&>()));
auto make_content(std::type_identity<edit_history_box>, const ui_needs& n, const history_facts& facts) {
  return edit_history_panel(n, facts);
}
template <class Content>
inline dialog_look content_look(std::type_identity<Content>, std::type_identity<history_facts>) {
  return {.sheet = sheet::chat{}, .size = dialog_size::fixed{460.0f, 560.0f}};
}

// What a mark list shows of each: the message, and for a reaction to it who
// reacted and with what.
struct mark_entry {
  message said;
  std::string event;  // the mark's own event
  std::optional<std::string> who;
  std::string key;
};

// A list of marks open: of which kind, in which chat, what it lists, and
// the chats it shows them from.
struct marks_facts {
  mark_kind_t kind;
  conversation_id in;
  std::vector<mark_entry> entries;
  const model* now = nullptr;
};

// The mentions of the user or the reactions to theirs not yet seen, as a
// list of the chat's bubbles: each its message -- a reaction's with who
// reacted and with what on a badge at its bottom right. Pressed, gone to.
inline auto mark_badge(const palette& colours, const std::string& who,
                       const std::string& name, const std::string& key) {
  namespace c = skiff::compose;
  return c::row(
      c::hbox(4.0f, {.place = scene::anchor::kBottomRight,
                     .x = -14.0f,
                     .y = -2.0f,
                     .autoSize = scene::axes::kBoth,
                     .padding = {2.0f, 6.0f, 2.0f, 3.0f},
                     .cornerRadius = 12.0f,
                     .background = colours.sidebar}),
      avatar_mark(avatar_of::person{}, who, name, 20.0f),
      c::styled({.alignSelf = scene::align::kMiddle},
                nodes::Text(proto::is_media(key) ? std::string(":emoji:") : key, 15.0f, colours.text)));
}
struct go_to_mark_act {
  using Answer = std::tuple<::mux::ui::request::go_to_mark, ::mux::ui::request::close_marks>;
  mark_kind_t kind;
  std::string event;
  Answer operator()() const {
    return {::mux::ui::request::go_to_mark{kind, event}, ::mux::ui::request::close_marks{}};
  }
};
inline auto mark_row(const ui_needs& n, mark_kind_t which, const conversation& in,
                     const mark_entry& one, const model* now) {
  namespace c = skiff::compose;
  const std::string who = one.who.value_or("");
  return c::onPress(
      go_to_mark_act{which, one.event},
      c::column(
          c::vbox(0.0f, {.fillX = true,
                         .autoSize = scene::axes::kY,
                         .padding = {4.0f, 12.0f, 8.0f, 12.0f},
                         .hoverBackground = n.colours->chosen}),
          message_alone(n, in, one.said, true, true, now),
          c::visible(one.who.has_value(),
                     mark_badge(*n.colours, who, who.empty() ? std::string() : sender_name(in, who), one.key))),
      "Go to message");
}
// Made from what the list is of; its rows from the chat as the chats
// hold it now -- none where it is gone.
inline auto marks_panel(const ui_needs& n, const marks_facts& facts) {
  namespace c = skiff::compose;
  using close_act = sends<::mux::ui::request::close_marks>;
  using row_t = decltype(mark_row(n, facts.kind, std::declval<const conversation&>(),
                                  std::declval<const mark_entry&>(), facts.now));
  const conversation* in = facts.now == nullptr ? nullptr : facts.now->find(facts.in);
  auto rows = in == nullptr
                  ? std::vector<row_t>{}
                  : facts.entries | std::views::transform([&](const mark_entry& one) {
                      return mark_row(n, facts.kind, *in, one, facts.now);
                    }) | std::ranges::to<std::vector>();
  return c::column(
      list_dialog(520.0f),
      page_header<no_back, close_act>(
          *n.colours,
          spl::visit(spl::overloaded{[](mark_kind::mention) { return std::string("Mentions"); },
                                     [](mark_kind::reaction) { return std::string("Reactions"); }},
                     facts.kind),
          {}, {}, false, true),
      dialog_list(c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(rows))));
}
using marks_box_t = decltype(marks_panel(std::declval<const ui_needs&>(), std::declval<const marks_facts&>()));
using marks_box = marks_box_t;
inline dialog_look content_look(std::type_identity<marks_box_t>) {
  return {.sheet = sheet::chat{}, .size = dialog_size::fixed{460.0f, 520.0f}};
}
template <class Needs>
auto make_content(std::type_identity<marks_box_t>, const Needs& needs, const marks_facts& facts) {
  return marks_panel(needs, facts);
}

}  // namespace mux::ui

// Outside a release build each child is walked through its table, and a
// stack lays itself out through its own (Stack::layoutChildren): so a unit
// that lays out or walks one of these would make its table, and the walks
// of everything under it, there -- the chats screen's took this unit past
// ten minutes. Their tables are made once, each in a walks unit of this
// module (src/ui/walks_*.cc); here, and wherever they are walked, a call.
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::edit_history_box> = true;  // walks_boxes.cc
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::edit_history_box>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::marks_box> = true;  // walks_boxes.cc
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::marks_box>() noexcept;
