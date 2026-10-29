// SPDX-License-Identifier: AGPL-3.0-only
// mux.levels -- Choices a plugin makes at each level: every chat's, an
// account's, a chat's own (a space's, for its rooms). One struct of the
// plugin's, each field unsaid as the level over it says; held at each level
// as a type of its own, so that a row binds to the one it is in
// (skiff::compose::scoped); kept as the plugin's own settings
// (contract::settings_kept: settings_type -> level_choices<its struct>);
// and read in effect for a chat (choices_in_effect), each level filled from
// the one over it by the plugin's filled_from(below, above), found by ADL.
// Where a row is put, as a type (level_place); and the flags the timeline
// asks of the plugins for a chat (chat_flags, contract::chat_flags).
export module mux.levels;

import std;
import skiff.model;
import mux.core;

export namespace mux {

template <class T>
struct every_choice {
  T choices;
  friend bool operator==(const every_choice&, const every_choice&) = default;
};
template <class T>
struct account_choice {
  T choices;
  friend bool operator==(const account_choice&, const account_choice&) = default;
};
template <class T>
struct chat_choice {
  T choices;
  friend bool operator==(const chat_choice&, const chat_choice&) = default;
};
template <class T>
struct level_choices {
  using choices_type = T;
  skiff::model::Tracked<every_choice<T>> every;
  skiff::model::Keyed<std::string, account_choice<T>> accounts;
  skiff::model::Keyed<conversation_id, chat_choice<T>> chats;
};

// A chat's choices in effect: its own, its space's (and theirs up), its
// account's, every chat's -- each filling what the one under it left unsaid.
template <class T>
[[nodiscard]] T choices_in_effect(const level_choices<T>& levels, const std::map<conversation_id, conversation_id>& space_above,
                                  const conversation_id& chat) {
  // Lowest first.
  std::vector<T> said;
  conversation_id at = chat;
  for (int steps = 0; steps < 17; ++steps) {
    if (const chat_choice<T>* own = levels.chats.find(at))
      said.push_back(own->choices);
    const auto up = space_above.find(at);
    if (up == space_above.end())
      break;
    at = up->second;
  }
  if (const account_choice<T>* account = levels.accounts.find(chat.account.address))
    said.push_back(account->choices);
  said.push_back(levels.every.fValue.choices);
  return std::ranges::fold_left(said | std::views::drop(1), said.front(),
                                [](T below, const T& above) { return filled_from(std::move(below), above); });
}

// Where a level's rows are put (contract::choice_rows): every chat's page,
// an account's (by its address), a chat's.
namespace level_place {
struct every {};
struct account {
  std::string address;
};
struct chat {
  conversation_id id;
};
// The same, of a protocol's: what a row offers there known from its type
// (a choice its protocol's servers have, or not) while compiling.
template <class Protocol>
struct account_in : account {
  using protocol = Protocol;
};
template <class Protocol>
struct chat_in : chat {
  using protocol = Protocol;
};
}  // namespace level_place
[[nodiscard]] inline choice_level_t level_of(level_place::every) { return choice_level::everywhere{}; }
[[nodiscard]] inline choice_level_t level_of(const level_place::account&) { return choice_level::account{}; }
[[nodiscard]] inline choice_level_t level_of(const level_place::chat&) { return choice_level::chat{}; }

// What the timeline does for a chat, as the plugins say (contract::chat_flags):
// each unsaid where no plugin says it.
struct chat_flags {
  std::optional<bool> receipts;
  std::optional<bool> previews;
  std::optional<bool> previews_direct;
};

}  // namespace mux
