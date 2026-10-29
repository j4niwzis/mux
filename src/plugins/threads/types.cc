// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.threads.types -- What the threads plugin names, and what it
// says it provides: its button in the chat's head, its panel beside the
// chat, what the panel lists among what the window shows, and a part of
// the program. Light: mux.ui sees it.
export module mux.plugin.threads.types;

import std;
import mux.core;
import mux.composition;

export namespace mux::plugins::threads {

struct feature {
  static constexpr std::string_view name = "threads";
};

// A thread in the list, as the program has it: its root, who wrote it and
// what, how many answers and the latest's time.
struct thread_root {
  std::string root, sender, name, words, meta;
  friend bool operator==(const thread_root&, const thread_root&) = default;
};
// The threads' panel, as the program says: the list -- the room's threads,
// the latest active first -- or, a thread open, its answers.
struct shown {
  bool listing = true;
  std::vector<thread_root> roots;
  friend bool operator==(const shown&, const shown&) = default;
};

consteval bool provides(contract::chat_header, type_tag<feature>) { return true; }
consteval bool provides(contract::beside_panels, type_tag<feature>) { return true; }
consteval bool provides(contract::window_state, type_tag<feature>) { return true; }
constexpr type_tag<shown> state_type(contract::window_state, type_tag<feature>) { return {}; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::threads
