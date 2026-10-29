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

export namespace mux::plugins::receipts {

struct feature {
  static constexpr std::string_view name = "receipts";
};

consteval bool provides(contract::message_footers, type_tag<feature>) { return true; }

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
consteval bool provides(contract::account_chat_rows, type_tag<feature>) { return true; }
// The timeline told whether a chat shows them (contract::chat_flags).
consteval bool provides(contract::chat_flags, type_tag<feature>) { return true; }
inline void flags_for(contract::chat_flags, type_tag<feature>, const level_choices<choices>& mine,
                      const std::map<conversation_id, conversation_id>& space_above, const conversation_id& chat, chat_flags& out) {
  out.receipts = choices_in_effect(mine, space_above, chat).shown.value_or(false);
}

}  // namespace mux::plugins::receipts
