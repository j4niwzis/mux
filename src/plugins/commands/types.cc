// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.commands.types -- What the commands plugin names, and what it
// says it provides: its slash commands, and a part of the program that
// carries them out. Light: mux.ui sees it.
export module mux.plugin.commands.types;

import std;
import mux.core;
import mux.composition;

export namespace mux::plugins::commands {

struct feature {
  static constexpr std::string_view name = "commands";
};

// Its commands: each its name, how it is used and what it does, made from
// what follows its name.
struct shrug {
  std::string arguments;
  static constexpr std::string_view name = "shrug", usage = "/shrug [text]", summary = "Send the text with ¯\\_(ツ)_/¯ after it";
};
struct open {
  std::string arguments;
  static constexpr std::string_view name = "open", usage = "/open <chat>", summary = "Open the chat whose name or address has this in it";
};
struct join {
  std::string arguments;
  static constexpr std::string_view name = "join", usage = "/join <room>", summary = "Join a room by its address or ID";
};
struct react {
  std::string arguments;
  static constexpr std::string_view name = "react", usage = "/react <emoji>", summary = "React to the chat's last message";
};
struct rainbow {
  std::string arguments;
  static constexpr std::string_view name = "rainbow", usage = "/rainbow <text>", summary = "Send the text in the colours of the rainbow";
};
struct raw {
  std::string arguments;
  static constexpr std::string_view name = "raw", usage = "/raw <text>", summary = "Send the text as it is typed, not read as Markdown";
};
struct jump_to_date {
  std::string arguments;
  static constexpr std::string_view name = "jump-to-date", usage = "/jump-to-date <2026-10-28 [14:30] | 3d | 2h | 30m | 1w | yesterday | today>",
                                    summary = "Go to the chat's first message from then (UTC)";
};
struct help {
  std::string arguments;
  static constexpr std::string_view name = "help", usage = "/help", summary = "List the commands";
};

consteval bool provides(contract::slash_commands, type_tag<feature>) { return true; }
constexpr contract::command_list<shrug, rainbow, raw, open, join, react, jump_to_date, help> slash_commands(contract::slash_commands, type_tag<feature>) { return {}; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::commands
