// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.threads.ui_top -- Threads, as Element's panel: in place of the
// chat's info, the room's threads, or one opened -- its root and answers as
// the chat's own list has its messages, and a field to answer in it.
module;
#include "../../inline_in_release.h"
#include "skiff/named.h"
export module mux.plugin.threads.ui_top;

import std;
import splice;
import skia;
import skiff.scene;
import skiff.compose;
import skiff.nodes.box;
import skiff.nodes.scroll;
import skiff.nodes.text;
import mux.composition;
import mux.core;
import mux.ui.base;
import mux.ui.messages;
import mux.plugin.threads.types;

export namespace mux::plugins::threads {
using namespace ::mux::ui;

// "3 replies \u00b7 12:34", the latest's author and words after it.
[[nodiscard]] inline std::string flat_line(std::string text) {
  std::ranges::replace(text, '\n', ' ');
  return text;
}
[[nodiscard]] inline std::string thread_meta_of(const conversation& chat, const message& said) {
  const thread_summary summary = said.threaded.value_or(thread_summary{});
  std::string out = std::format("{} {}", summary.count, summary.count == 1 ? "reply" : "replies");
  if (!summary.last_id.empty())
    out += std::format(" \u00b7 {} \u00b7 {}: {}", clock_of(summary.last_at), sender_name(chat, summary.last_sender), flat_line(summary.last_text));
  return out;
}
// A thread's row: its root's author and words, how many answers and the
// latest's; pressed, opened.
MUX_INLINE_IN_RELEASE auto thread_row(const palette& colours, const thread_root& one) {
  namespace c = skiff::compose;
  const auto line = [](std::string text, float size, skia::SkColor colour, bool bold = false) {
    return c::styled({.fillX = true}, elided(nodes::Text(std::move(text), size, colour, bold)));
  };
  return SKIFF_NAMED(thread_row, c::onClick(request::open_thread{one.root},
      c::row(c::hbox(10.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 12.0f, 8.0f, 12.0f}, .cornerRadius = 8.0f,
                             .hoverBackground = colours.chosen}),
             c::styled({.alignSelf = scene::align::kStart}, face_spot(avatar_of::person{}, one.sender, one.name, 36.0f)),
             c::column(c::vbox(2.0f, {.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}),
                       line(one.name, 13.0f, colours.accent, true), line(one.words, 13.0f, colours.text), line(one.meta, 12.0f, colours.dim))),
      one.name));
}
// The chat's own composer, writing into the thread: its close lets go of
// the answer, Enter and the arrow send there, its paperclip and emoji too --
// each the program's to do.
struct in_thread {
  using cancel = sends<request::thread_answer_cancel>;
  using draft = thread_draft;
  using told = draft_told<request::thread_draft_edited, request::thread_send>;
  using attach = sends<request::attach_in_thread>;
  using emoji = emoji_beside<request::toggle_thread_emoji>;
  using send = sends<request::thread_send>;
  static constexpr std::string_view placeholder = "Reply in thread\u2026";
  using reads_chat = std::false_type;
  using composing = thread_compose;
};
// Threads, as Element's panel has them: in place of the chat's info, the
// room's threads, or one opened -- its root and answers as the chat's own
// list has its messages, and a field to answer in it. As the program says.
MUX_INLINE_IN_RELEASE auto threads_panel(const ui_needs& n) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  const auto listing = [](const shown& now) { return now.listing; };
  const auto open = [](const shown& now) { return !now.listing; };
  // Filling the column the screen sizes for it: as tall alone, it was 0
  // wide, its rows' text with no room at all.
  return SKIFF_NAMED(threads_panel, c::column(c::vbox(0.0f, {.fill = true, .background = colours.sidebar}),
      c::row(c::hbox(8.0f, {.fillX = true, .height = 56.0f, .padding = {0.0f, 12.0f, 0.0f, 12.0f}}),
             c::shown_for<shown>(open, c::styled({.alignSelf = scene::align::kMiddle},
                                                         icon_button(colours, icon::back{}, sends<request::close_thread>{}))),
             c::text_for<shown>([](const shown& now) { return std::string(now.listing ? "Threads" : "Thread"); },
                 c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(nodes::Text("", 16.0f, colours.text, true)))),
             c::styled({.alignSelf = scene::align::kMiddle}, icon_button(colours, icon::close{}, sends<request::toggle_threads>{}))),
      c::styled({.fillX = true, .height = 1.0f}, nodes::Box<>(colours.band)),
      c::shown_for<shown>([](const shown& now) { return now.listing && now.roots.empty(); },
          c::styled({.margin = {16.0f, 16.0f, 0.0f, 16.0f}}, nodes::Text("No threads here yet.", 13.0f, colours.dim))),
      c::shown_for<shown>(listing,
          c::styled({.fillX = true, .grow = scene::axes::kY},
              c::scrolled(c::projected<shown>([](const shown& now) { return now.roots; },
                  c::listed<thread_root>(c::vbox(2.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 4.0f, 4.0f, 4.0f}}),
                                         [&colours](const thread_root& one) { return thread_row(colours, one); }))))),
      // Across the panel and as tall as what its head and composer leave:
      // the list fills what it is put in, and said only to grow, it kept
      // its fill down too -- the panel's whole height, the composer pushed
      // under the window.
      c::shown_for<shown>(open, c::styled({.fillX = true, .relativeSize = scene::axes::kX, .grow = scene::axes::kY},
                                                  timeline_list<thread_told, thread_list_tag>())),
      c::shown_for<shown>(open, composer_bar<in_thread>(n))));
}

// Its panel, beside the chat in the info's place (mux.ui:conversations_screen).
MUX_INLINE_IN_RELEASE auto extend(contract::beside_panels, type_tag<feature>, const ui_needs& needs) { return threads_panel(needs); }

}  // namespace mux::plugins::threads
