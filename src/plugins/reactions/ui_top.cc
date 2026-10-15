// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.reactions.ui_top -- Who reacted to a message, listed as the
// events they are: a bubble each, from the window's own messages -- above
// them (mux.ui.messages), under the window that shows it as a dialog.
export module mux.plugin.reactions.ui_top;

import std;
import skia;
import skiff.scene;
import skiff.compose;
import skiff.nodes.flow;
import skiff.nodes.text;
import skiff.nodes.scroll;
import skiff.nodes.box;
import splice;
import mux.composition;
import mux.core;
import mux.config;
import mux.ui.base;
import mux.ui.messages;
import mux.plugin.reactions.types;
import mux.plugin.reactions.ui;

export namespace mux::plugins::reactions {
using namespace ::mux::ui;

// A message's reactions as events, as Matrix has them: a box in the middle,
// a row for each -- the person's avatar and name over what they reacted
// with and when. A press on one answers it: the reaction is an event, and a
// message can reply to it.
// Panel rows use the actual reaction event as their menu target. Inherit its
// nested reactions when known; the parent event is only the reply reference.
[[nodiscard]] inline message reaction_message_of(const conversation& in, const entry& one) {
  message out;
  if (const message* held = held_message(in, one.event)) out = *held;
  out.in = in.id;
  out.id = one.event;
  out.sender = one.who;
  out.at = one.at;
  out.body = reaction_body_of(one.key, in.emotes, one.shortcode);
  out.outgoing = one.mine;
  out.service = false;
  out.reaction = true;
  out.reaction_key = one.key;
  out.replies_to = one.to.empty() ? std::nullopt : std::optional(one.to);
  return out;
}

inline auto reaction_event_row(const ui_needs& n, const conversation& in, const entry& one,
                        bool first, bool last, const model* known) {
  namespace c = skiff::compose;
  // Its menu its row's own: what is not held under an id of its own offers
  // nothing that would be aimed at it.
  auto bubble = message_alone(n, in, reaction_message_of(in, one), first, last, known);
  return c::onClick(std::tuple{one.event.empty() ? std::optional<request::reply_to>{}
        : std::optional(request::reply_to{one.event, std::format("{} reacted {}", one.name, one.key)}), request::close_shown<shown>{}},
      c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY,
          .padding = {0.0f, 12.0f, 0.0f, 12.0f}, .hoverBackground = n.colours->chosen}), std::move(bubble)), "Reply to reaction");
}
inline auto panel(const ui_needs& n, const shown& facts) {
  namespace c = skiff::compose;
  const auto& chat = chat_or_none(facts.now, facts.in);
  const auto& entries = facts.entries;
  auto rows = std::ranges::to<std::vector>(std::views::transform(std::views::iota(std::size_t{0}, entries.size()),
      [&](std::size_t i) { return reaction_event_row(n, chat, entries[i],
          i == 0 || entries[i - 1].who != entries[i].who,
          i + 1 == entries.size() || entries[i + 1].who != entries[i].who, facts.now); }));
  return c::column(list_dialog(420.0f),
      page_header<sends<back>, sends<request::close_shown<shown>>>(*n.colours, "Reactions", {}, {}, facts.back, true),
      dialog_list(c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(rows))));
}
using box = decltype(panel(std::declval<const ui_needs&>(), std::declval<const shown&>()));
inline auto make_content(std::type_identity<box>, const ui_needs& n, const shown& facts) {
  return panel(n, facts);
}
template <class Content>
inline dialog_look content_look(std::type_identity<Content>, std::type_identity<shown>) {
  return {.sheet = sheet::chat{}, .size = dialog_size::fixed{392.0f, 420.0f}};
}


// Its dialog, among the window's (mux.ui:window).
constexpr type_tag<box> extend(contract::window_dialogs, type_tag<feature>) { return {}; }

}  // namespace mux::plugins::reactions
