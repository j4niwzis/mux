// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.threads.ui -- The threads' button in the chat's head: the
// panel opened, or closed.
module;
#include "../../inline_in_release.h"
#include "skiff/named.h"
export module mux.plugin.threads.ui;

import std;
import skiff.scene;
import skiff.compose;
import skiff.nodes.icon;
import mux.composition;
import mux.core;
import mux.ui.base;
import mux.plugin.threads.types;
import mux.plugin.threads.model;

export namespace mux::plugins::threads {
using namespace ::mux::ui;
// Threads' icon, as Element's: a bubble, its tail at the lower left, two
// lines said in it.
inline const nodes::IconShape threads_shape{
    {{nodes::mark::path{{nodes::path_step::move{-8.0f, -6.0f}, nodes::path_step::line{8.0f, -6.0f}, nodes::path_step::line{8.0f, 5.0f},
                         nodes::path_step::line{-3.0f, 5.0f}, nodes::path_step::line{-7.0f, 8.5f}, nodes::path_step::line{-7.0f, 5.0f},
                         nodes::path_step::line{-8.0f, 5.0f}, nodes::path_step::close{}}},
      1.6f},
     {nodes::mark::line{-4.5f, -2.0f, 4.5f, -2.0f}, 1.6f},
     {nodes::mark::line{-4.5f, 1.5f, 2.0f, 1.5f}, 1.6f}}};
[[nodiscard]] inline icon::drawn threads_icon() { return {&threads_shape, "Threads"}; }
// Reply in thread, in a message's menu where its chat has threads and it
// may be answered: the menu closed and its thread opened.
struct reply_in_thread {
  using Answer = std::tuple<request::close_menu, request::open_aside>;
  std::string root;
  Answer operator()() const { return {request::close_menu{}, request::open_aside{root}}; }
};
MUX_INLINE_IN_RELEASE auto extend(contract::message_menu, type_tag<feature>, const palette& colours, const emoji_kept&, const menu_facts& facts) {
  return skiff::compose::visible(facts.answerable && facts.can.keeps_aside && !facts.link.empty(),
      row_item(colours, "Reply in thread", reply_in_thread{facts.id}, threads_icon(), std::nullopt, menu_row_look));
}
// Asked of the program, its own (moved from mux.ui, where only it took them).
struct close_aside {};

// The thread's field: what is written sent in the thread open, an answer to
// what is answered there; the answer let go.
struct thread_send {};

struct send_in_thread {
  std::string root;
  std::string text;
  std::optional<std::string> reply_to;  // an answer in it answered
  std::vector<styled_run> styles;
  // Who it mentions: its pills, sent as a message's are.
  std::vector<mention> mentions;
};


// Its button in the chat's head (mux.ui:header), after Search.
MUX_INLINE_IN_RELEASE auto extend(contract::chat_header, type_tag<feature>, const palette& colours) {
  return SKIFF_NAMED(threads_button, skiff::compose::styled({.alignSelf = scene::align::kMiddle},
                                                            icon_button(colours, threads_icon(), sends<request::toggle_panel>{})));
}

// Keep the action on a container: Text's own pointer handler is for text
// selection and does not run an onClick inherited around the text itself.
MUX_INLINE_IN_RELEASE auto message_thread_link(const palette& colours, std::string root, std::string line) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(message_thread_link, c::onClick(request::open_aside{std::move(root)},
      c::row(c::hbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .margin = {4.0f, 0.0f, 0.0f, 0.0f},
                            .hoverBackground = colours.chosen}),
          c::styled({.grow = scene::axes::kX}, elided(nodes::Text(std::move(line), 13.0f, colours.accent, true)))),
      "Open thread"));
}
using message_thread_link_t = decltype(message_thread_link(std::declval<const palette&>(), std::string{}, std::string{}));
// A thread's root: how many answers, the latest, as Element shows it under
// the message, a line of its own (contract::message_lines); pressed, the
// thread.
inline std::optional<message_thread_link_t> extend(contract::message_lines, type_tag<feature>, const trailer_facts& facts) {
  const summary now = part_of(facts.said).threaded.value_or(summary{});
  if (now.count <= 0)
    return std::nullopt;
  std::string line = std::format("\U0001F4AC {} {}", now.count, now.count == 1 ? "reply" : "replies");
  if (!now.last_text.empty())
    line += std::format(" \u00b7 {}: {}", sender_name(facts.in, now.last_sender), now.last_text);
  std::ranges::replace(line, '\n', ' ');
  return message_thread_link(facts.colours, facts.said.id, std::move(line));
}

}  // namespace mux::plugins::threads
