// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.replies.types -- What the replies plugin names, and what it
// says it provides: its rows of a message's menu, the head of a reply's
// bubble, and a part of the program.
// Light: mux.ui sees it.
export module mux.plugin.replies.types;

import std;
import mux.core;
import mux.composition;

export namespace mux::plugins::replies {

struct feature {
  static constexpr std::string_view name = "replies";
};

// Asked of the program by its rows: the message the menu is up for
// answered; and answered with the text selected in it quoted.
struct reply {};
struct quote_reply {};

consteval bool provides(contract::message_menu, type_tag<feature>) { return true; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }
// What a reply shows of the message it answers, above its text: that
// message, fetched where it is not there, the row made again once it is.
consteval bool provides(contract::message_heads, type_tag<feature>) { return true; }
inline std::optional<std::string> head_waits_for(contract::message_heads, type_tag<feature>, const message& said) { return said.replies_to; }

}  // namespace mux::plugins::replies
