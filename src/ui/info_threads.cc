// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:info_threads -- A room's threads, in the info's place.
export module mux.ui:info_threads;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.bind;
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
import :info_reactions;
import :info_new_chats;
import :info_looks;

export namespace mux::ui {
// A thread in the list, as the program has it: its root, who wrote it and
// what, how many answers and the latest's time.
struct thread_root {
  std::string root, sender, name, words, meta;
  friend bool operator==(const thread_root&, const thread_root&) = default;
};
// The threads' panel, as the program says: the list -- the room's threads,
// the latest active first -- or, a thread open, its answers.
struct threads_shown {
  bool listing = true;
  std::vector<thread_root> roots;
  friend bool operator==(const threads_shown&, const threads_shown&) = default;
};
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
inline auto thread_row(const palette& colours, const thread_root& one) {
  namespace c = skiff::compose;
  const auto line = [](std::string text, float size, skia::SkColor colour, bool bold = false) {
    return c::styled({.fillX = true}, elided(nodes::Text(std::move(text), size, colour, bold)));
  };
  return c::onClick(request::open_thread{one.root},
      c::row(c::hbox(10.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 12.0f, 8.0f, 12.0f}, .cornerRadius = 8.0f,
                             .hoverBackground = colours.chosen}),
             c::styled({.alignSelf = scene::align::kStart}, avatar_mark(avatar_of::person{}, one.sender, one.name, 36.0f)),
             c::column(c::vbox(2.0f, {.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}),
                       line(one.name, 13.0f, colours.accent, true), line(one.words, 13.0f, colours.text), line(one.meta, 12.0f, colours.dim))),
      one.name);
}
// The chat's own composer, writing into the thread: its close lets go of
// the answer, Enter and the arrow send there, its paperclip and emoji too --
// each the program's to do.
struct thread_typed {
  using Answer = request::thread_send;
  request::thread_send operator()(std::string_view) const { return {}; }
};
struct in_thread {
  using cancel = sends<request::thread_answer_cancel>;
  using submit = thread_typed;
  using attach = sends<request::attach_in_thread>;
  using emoji = sends<request::toggle_thread_emoji>;
  using send = sends<request::thread_send>;
  static constexpr std::string_view placeholder = "Reply in thread\u2026";
  using reads_chat = std::false_type;
};
// Threads, as Element's panel has them: in place of the chat's info, the
// room's threads, or one opened -- its root and answers as the chat's own
// list has its messages, and a field to answer in it. As the program says.
inline auto threads_panel(const ui_needs& n) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  const auto listing = [](const threads_shown& now) { return now.listing; };
  const auto open = [](const threads_shown& now) { return !now.listing; };
  return c::column(c::vbox(0.0f, {.fillY = true, .background = colours.sidebar}),
      c::row(c::hbox(8.0f, {.fillX = true, .height = 56.0f, .padding = {0.0f, 12.0f, 0.0f, 12.0f}}),
             c::shown_for<threads_shown>(open, c::styled({.alignSelf = scene::align::kMiddle},
                                                         icon_button(colours, icon::back{}, sends<request::close_thread>{}))),
             c::text_for<threads_shown>([](const threads_shown& now) { return std::string(now.listing ? "Threads" : "Thread"); },
                 c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(nodes::Text("", 16.0f, colours.text, true)))),
             c::styled({.alignSelf = scene::align::kMiddle}, icon_button(colours, icon::close{}, sends<request::toggle_threads>{}))),
      c::styled({.fillX = true, .height = 1.0f}, nodes::Box<>(colours.band)),
      c::shown_for<threads_shown>([](const threads_shown& now) { return now.listing && now.roots.empty(); },
          c::styled({.margin = {16.0f, 16.0f, 0.0f, 16.0f}}, nodes::Text("No threads here yet.", 13.0f, colours.dim))),
      c::shown_for<threads_shown>(listing,
          c::styled({.fillX = true, .grow = scene::axes::kY},
              nodes::ScrollContainer(c::projected<threads_shown>([](const threads_shown& now) { return now.roots; },
                  c::listed<thread_root>(c::vbox(2.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 4.0f, 4.0f, 4.0f}}),
                                         [&colours](const thread_root& one) { return thread_row(colours, one); }))))),
      c::shown_for<threads_shown>(open, c::styled({.fillX = true, .grow = scene::axes::kY}, timeline_list<thread_told, thread_list_tag>())),
      c::shown_for<threads_shown>(open, composer_bar<in_thread>(n)));
}
using threads_panel_t = decltype(threads_panel(std::declval<const ui_needs&>()));
// Its field, where what is written into the thread is.
inline constexpr std::size_t kThreadLine = 5;

}  // namespace mux::ui

// Outside a release build each child is walked through its table, and a
// stack lays itself out through its own (Stack::layoutChildren): so a unit
// that lays out or walks one of these would make its table, and the walks
// of everything under it, there -- the chats screen's took this unit past
// ten minutes. Their tables are made once, each in a walks unit of this
// module (src/ui/walks_*.cc); here, and wherever they are walked, a call.
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::threads_panel_t> = true;  // walks_info.cc
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::threads_panel_t>() noexcept;
