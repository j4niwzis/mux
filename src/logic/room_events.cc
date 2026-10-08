// SPDX-License-Identifier: AGPL-3.0-only
// mux.logic.room_events: which room events a chat shows -- a kind's choice
// in a set of them, found by its tag, and the choices of a chat, its account
// and every account resolved into what it shows.
export module mux.logic.room_events;

import std;
import splice;
import mux.core;
import mux.config;

export namespace mux::logic {

using kinds_t = config::room_event_kinds;
// Each kind's member in a set of choices: one overload a kind.
[[nodiscard]] constexpr std::optional<bool> kinds_t::* member_of(room_event::joins) { return &kinds_t::joins; }
[[nodiscard]] constexpr std::optional<bool> kinds_t::* member_of(room_event::invites) { return &kinds_t::invites; }
[[nodiscard]] constexpr std::optional<bool> kinds_t::* member_of(room_event::names) { return &kinds_t::names; }
[[nodiscard]] constexpr std::optional<bool> kinds_t::* member_of(room_event::avatars) { return &kinds_t::avatars; }
[[nodiscard]] constexpr std::optional<bool> kinds_t::* member_of(room_event::room_name) { return &kinds_t::room_name; }
[[nodiscard]] constexpr std::optional<bool> kinds_t::* member_of(room_event::topic) { return &kinds_t::topic; }
[[nodiscard]] constexpr std::optional<bool> kinds_t::* member_of(room_event::room_avatar) { return &kinds_t::room_avatar; }
[[nodiscard]] constexpr std::optional<bool> kinds_t::* member_of(room_event::address) { return &kinds_t::address; }
[[nodiscard]] constexpr std::optional<bool> kinds_t::* member_of(room_event::pins) { return &kinds_t::pins; }
[[nodiscard]] constexpr std::optional<bool> kinds_t::* member_of(room_event::permissions) { return &kinds_t::permissions; }
[[nodiscard]] constexpr std::optional<bool> kinds_t::* member_of(room_event::access) { return &kinds_t::access; }
[[nodiscard]] constexpr std::optional<bool> kinds_t::* member_of(room_event::encryption) { return &kinds_t::encryption; }
[[nodiscard]] constexpr std::optional<bool> kinds_t::* member_of(room_event::other) { return &kinds_t::other; }
[[nodiscard]] constexpr std::optional<bool> kinds_t::* member_of(room_event::unreadable) { return &kinds_t::unreadable; }
[[nodiscard]] constexpr std::optional<bool> kinds_t::* member_of(room_event::reactions) { return &kinds_t::reactions; }
[[nodiscard]] constexpr std::optional<bool> kinds_t::* member_of(room_event::unreactions) { return &kinds_t::unreactions; }
// What a kind is where nothing is chosen for it: shown, but reactions and
// reactions taken back -- each one a line of its own only where asked for.
[[nodiscard]] constexpr bool shown_unless_chosen(room_event::reactions) { return false; }
[[nodiscard]] constexpr bool shown_unless_chosen(room_event::unreactions) { return false; }
[[nodiscard]] constexpr bool shown_unless_chosen(const auto&) { return true; }

// A kind's word, as it is kept on disk -- the name of its member -- and a
// word read back into its kind, "other" where it is none of them.
[[nodiscard]] constexpr std::string_view word_of(room_event::joins) { return "joins"; }
[[nodiscard]] constexpr std::string_view word_of(room_event::invites) { return "invites"; }
[[nodiscard]] constexpr std::string_view word_of(room_event::names) { return "names"; }
[[nodiscard]] constexpr std::string_view word_of(room_event::avatars) { return "avatars"; }
[[nodiscard]] constexpr std::string_view word_of(room_event::room_name) { return "room_name"; }
[[nodiscard]] constexpr std::string_view word_of(room_event::topic) { return "topic"; }
[[nodiscard]] constexpr std::string_view word_of(room_event::room_avatar) { return "room_avatar"; }
[[nodiscard]] constexpr std::string_view word_of(room_event::address) { return "address"; }
[[nodiscard]] constexpr std::string_view word_of(room_event::pins) { return "pins"; }
[[nodiscard]] constexpr std::string_view word_of(room_event::permissions) { return "permissions"; }
[[nodiscard]] constexpr std::string_view word_of(room_event::access) { return "access"; }
[[nodiscard]] constexpr std::string_view word_of(room_event::encryption) { return "encryption"; }
[[nodiscard]] constexpr std::string_view word_of(room_event::other) { return "other"; }
[[nodiscard]] constexpr std::string_view word_of(room_event::unreadable) { return "unreadable"; }
[[nodiscard]] constexpr std::string_view word_of(room_event::reactions) { return "reactions"; }
[[nodiscard]] constexpr std::string_view word_of(room_event::unreactions) { return "unreactions"; }
[[nodiscard]] inline std::string_view word_of(const room_event_t& kind) {
  return spl::visit([](auto one) { return word_of(one); }, kind);
}
[[nodiscard]] inline room_event_t room_event_of(std::string_view word) {
  for (const room_event_t& kind : all_room_events)
    if (word_of(kind) == word)
      return kind;
  return room_event::other{};
}

[[nodiscard]] inline std::optional<bool>& choice_in(kinds_t& in, const room_event_t& kind) {
  return in.*spl::visit([](auto one) { return member_of(one); }, kind);
}
[[nodiscard]] inline std::optional<bool> choice_of(const std::optional<kinds_t>& in, const room_event_t& kind) {
  if (!in)
    return std::nullopt;
  return (*in).*spl::visit([](auto one) { return member_of(one); }, kind);
}

// A chat's room events, as shown: for each kind, the chat's choice of it,
// else the chat's for all; the account's of it, else for all; every
// account's of it, else for all.
[[nodiscard]] inline room_event_filter filter_of(const std::optional<kinds_t>& chat_kinds, std::optional<bool> chat_all,
                                                  const std::optional<kinds_t>& account_kinds,
                                                  std::optional<bool> account_all,
                                                  const std::optional<kinds_t>& every_kinds, bool every_all) {
  room_event_filter out;
  for (const room_event_t& kind : all_room_events) {
    std::optional<bool> shown = choice_of(chat_kinds, kind);
    if (!shown)
      shown = chat_all;
    if (!shown)
      shown = choice_of(account_kinds, kind);
    if (!shown)
      shown = account_all;
    if (!shown)
      shown = choice_of(every_kinds, kind);
    const bool fallback = spl::visit([](auto one) { return shown_unless_chosen(one); }, kind);
    out.shown[kind.index()] = shown.value_or(fallback && every_all);
  }
  return out;
}

}  // namespace mux::logic
