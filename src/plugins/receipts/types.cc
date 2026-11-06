// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.receipts.types -- What the receipts plugin names, and what it
// says it provides: a footer under a message's row; its choice at each
// level -- whether a chat shows who has read up to where -- kept as its
// own, with its rows; and the timeline told it. Light: mux.ui sees it.
export module mux.plugin.receipts.types;

import std;
import knot;
import mux.core;
import mux.composition;
import mux.levels;
import mux.plugin.receipts.model;
import mux.plugin.receipts.changes;

export namespace mux::plugins::receipts {

struct feature {
  static constexpr std::string_view name = "receipts";
};

consteval bool provides(contract::message_footers, type_tag<feature>) { return true; }
// Who has seen a message, at its menu's foot (its UI_TOP layer): each reader,
// by their name in the chat, and when where their receipt says -- worked out
// as the menu opens (foot_facts, its UI layer).
consteval bool provides(contract::message_menu_foot, type_tag<feature>) { return true; }
struct reader {
  std::string id;
  std::string name;
  std::optional<std::chrono::sys_time<std::chrono::milliseconds>> at;
  friend bool operator==(const reader&, const reader&) = default;
};
struct seen {
  std::vector<reader> readers;
  friend bool operator==(const seen&, const seen&) = default;
};
consteval type_tag<seen> foot_facts_type(contract::message_menu_foot, type_tag<feature>) { return {}; }

// Its choice at each level (mux.levels): whether a chat shows who has read
// up to where, as faces -- unsaid, as the level over it says; said nowhere,
// not shown.
struct choices {
  std::optional<bool> shown;
  friend bool operator==(const choices&, const choices&) = default;
  friend consteval auto json_schema(knot::type<choices>) { return knot::schema<choices>(); }
};
[[nodiscard]] inline choices filled_from(choices below, const choices& above) {
  if (!below.shown)
    below.shown = above.shown;
  return below;
}
consteval bool provides(contract::settings_kept, type_tag<feature>) { return true; }
constexpr type_tag<level_choices<choices>> settings_type(contract::settings_kept, type_tag<feature>) { return {}; }
consteval bool provides(contract::choice_rows, type_tag<feature>) { return true; }
consteval bool provides(contract::account_page_rows, type_tag<feature>) { return true; }
// The timeline told whether a chat shows them (contract::chat_flags).
struct flag {
  bool shown = false;
  friend bool operator==(const flag&, const flag&) = default;
};
consteval bool provides(contract::chat_flags, type_tag<feature>) { return true; }
constexpr type_tag<flag> flag_type(contract::chat_flags, type_tag<feature>) { return {}; }
inline flag flag_of(contract::chat_flags, type_tag<feature>, const level_choices<choices>& mine,
                    const std::map<conversation_id, conversation_id>& space_above, const conversation_id& chat) {
  return {choices_in_effect(mine, space_above, chat).shown.value_or(false)};
}
// Who has read up to each message, where the chat shows it
// (contract::row_marks): each other person on the message their receipt
// points at -- or, pointing at what is not shown or not here, the nearest
// shown before it, by its time.
struct readers {
  std::vector<std::string> who;
  friend bool operator==(const readers&, const readers&) = default;
};
consteval bool provides(contract::row_marks, type_tag<feature>) { return true; }
constexpr type_tag<readers> mark_type(contract::row_marks, type_tag<feature>) { return {}; }
template <class Flags>
std::map<std::string, readers> marks_over(contract::row_marks, type_tag<feature>, const Flags& flags, const conversation& chat,
                                          const std::vector<const message*>& all, const std::vector<bool>& shown) {
  std::map<std::string, readers> out;
  if (!std::get<flag>(flags).shown)
    return out;
  std::map<std::string, std::size_t> place;
  for (std::size_t i = 0; i < all.size(); ++i)
    place.emplace(all[i]->id, i);
  const auto& mine = std::get<chat_part<message>>(chat.plugins);
  for (const auto& [user, event] : mine.read_by) {
    if (user == chat.id.account.address)
      continue;
    std::optional<std::size_t> at;
    if (const auto found = place.find(event); found != place.end())
      at = found->second;
    else if (const auto when = mine.times.find(user); when != mine.times.end())
      for (std::size_t j = all.size(); j-- > 0;)
        if (all[j]->at <= when->second) {
          at = j;
          break;
        }
    while (at && !shown[*at])
      at = *at == 0 ? std::nullopt : std::optional<std::size_t>(*at - 1);
    if (at)
      out[all[*at]->id].who.push_back(user);
  }
  return out;
}

// Who has read up to where, kept beside the chat's messages across a restart
// (contract::chat_kept): read back as the chat is first opened, before the
// server says it anew; kept again as it changes.
struct kept {
  std::map<std::string, std::string> users;
  std::optional<std::map<std::string, std::int64_t>> times;
  friend consteval auto json_schema(knot::type<kept>) { return knot::schema<kept>(); }
};
consteval bool provides(contract::chat_kept, type_tag<feature>) { return true; }
constexpr type_tag<kept> chat_kept_type(contract::chat_kept, type_tag<feature>) { return {}; }
inline kept kept_of(contract::chat_kept, type_tag<feature>, const conversation& chat) {
  const auto& mine = std::get<chat_part<message>>(chat.plugins);
  return {mine.read_by, mine.times | std::views::transform([](const auto& one) { return std::pair{one.first, one.second.time_since_epoch().count()}; }) |
                            std::ranges::to<std::map<std::string, std::int64_t>>()};
}
inline read restored(contract::chat_kept, type_tag<feature>, const conversation_id& in, kept saved) {
  using when = std::chrono::sys_time<std::chrono::milliseconds>;
  return {in, std::move(saved.users),
          saved.times.value_or(std::map<std::string, std::int64_t>{}) |
              std::views::transform([](const auto& one) { return std::pair{one.first, when{std::chrono::milliseconds{one.second}}}; }) |
              std::ranges::to<std::map<std::string, when>>()};
}
inline conversation_id kept_after(contract::chat_kept, type_tag<feature>, const read& one) { return one.in; }

}  // namespace mux::plugins::receipts
