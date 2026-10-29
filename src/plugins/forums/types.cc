// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.forums.types -- What the forums plugin names, and what it says
// it provides: a space shown as one chat, its rooms in it as topics, as
// Telegram's forums -- or a protocol's own forum -- in the chat list
// (contract::chat_scopes); the choice kept as its own (settings_kept),
// shown (window_state) and made in a room's settings (room_settings'
// general_sections); and a part of the program. Light: mux.ui sees it.
export module mux.plugin.forums.types;

import std;
import knot;
import mux.core;
import mux.composition;
import mux.plugin.room_settings.types;

export namespace mux::plugins::forums {

struct feature {
  static constexpr std::string_view name = "forums";
};

// A space shown as a forum, by its account and its room.
struct forum_chat {
  std::string account;
  std::string room;
  friend bool operator==(const forum_chat&, const forum_chat&) = default;
  friend auto operator<=>(const forum_chat&, const forum_chat&) = default;
  friend consteval auto json_schema(knot::type<forum_chat>) { return knot::schema<forum_chat>(); }
};
// What it keeps: the spaces shown as forums.
struct kept {
  std::vector<forum_chat> chats;
  friend bool operator==(const kept&, const kept&) = default;
  friend consteval auto json_schema(knot::type<kept>) { return knot::schema<kept>(); }
};
// What the window shows of it: the same, for its switch to read.
struct shown {
  std::set<conversation_id> chats;
  friend bool operator==(const shown&, const shown&) = default;
};
// Asked of the program: a space shown as a forum, or as a space.
struct flip_forum {
  conversation_id chat;
};

consteval bool provides(contract::chat_scopes, type_tag<feature>) { return true; }
consteval bool provides(contract::settings_kept, type_tag<feature>) { return true; }
constexpr type_tag<kept> settings_type(contract::settings_kept, type_tag<feature>) { return {}; }
consteval bool provides(contract::window_state, type_tag<feature>) { return true; }
constexpr type_tag<shown> state_type(contract::window_state, type_tag<feature>) { return {}; }
consteval bool provides(room_settings::contract::general_sections, type_tag<feature>) { return true; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::forums
