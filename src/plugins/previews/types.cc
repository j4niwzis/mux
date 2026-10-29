// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.previews.types -- What the link previews plugin names, and what
// it provides: a card in a message's bubble; its choices at each level --
// whether a chat shows them, and whether they come from the sites
// themselves -- kept as its own, with its rows; and the program told them.
// Light: mux.ui sees it.
export module mux.plugin.previews.types;

import std;
import knot;
import mux.core;
import mux.composition;
import mux.levels;

export namespace mux::plugins::previews {

struct feature {
  static constexpr std::string_view name = "previews";
};

consteval bool provides(contract::message_cards, type_tag<feature>) { return true; }

// Its choices at each level (mux.levels): whether a chat shows a card for
// a message's first link -- said nowhere, it does -- and whether the card
// is fetched from the site itself, through the account's proxy, not
// through its server -- said nowhere, not: the site then sees where the
// request comes from. Unsaid, as the level over it says.
struct choices {
  std::optional<bool> shown;
  std::optional<bool> direct;
  friend bool operator==(const choices&, const choices&) = default;
  friend consteval auto json_schema(knot::type<choices>) { return knot::schema<choices>(); }
};
[[nodiscard]] inline choices filled_from(choices below, const choices& above) {
  if (!below.shown)
    below.shown = above.shown;
  if (!below.direct)
    below.direct = above.direct;
  return below;
}
consteval bool provides(contract::settings_kept, type_tag<feature>) { return true; }
constexpr type_tag<level_choices<choices>> settings_type(contract::settings_kept, type_tag<feature>) { return {}; }
consteval bool provides(contract::choice_rows, type_tag<feature>) { return true; }
// The program told whether a chat shows them, and from where they come
// (contract::chat_flags).
consteval bool provides(contract::chat_flags, type_tag<feature>) { return true; }
inline void flags_for(contract::chat_flags, type_tag<feature>, const level_choices<choices>& mine,
                      const std::map<conversation_id, conversation_id>& space_above, const conversation_id& chat, chat_flags& out) {
  const choices now = choices_in_effect(mine, space_above, chat);
  out.previews = now.shown.value_or(true);
  out.previews_direct = now.direct.value_or(false);
}

}  // namespace mux::plugins::previews
