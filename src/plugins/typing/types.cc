// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.typing.types -- What the typing plugin names, and what it says
// it provides: the chat's status in its head while someone types; its
// choice at each level -- whether others are told one is typing -- kept as
// its own, with its rows; and a part of the program. Light: mux.ui sees it.
export module mux.plugin.typing.types;

import std;
import knot;
import mux.core;
import mux.composition;
import mux.levels;

export namespace mux::plugins::typing {

struct feature {
  static constexpr std::string_view name = "typing";
};

consteval bool provides(contract::chat_status, type_tag<feature>) { return true; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

// Its choice at each level (mux.levels): whether others are told one is
// typing (m.typing, XEP-0085's chat states) -- never what; unsaid, as the
// level over it says; said nowhere, told.
struct choices {
  std::optional<bool> sent;
  friend bool operator==(const choices&, const choices&) = default;
  friend consteval auto json_schema(knot::type<choices>) { return knot::schema<choices>(); }
};
[[nodiscard]] inline choices filled_from(choices below, const choices& above) {
  if (!below.sent)
    below.sent = above.sent;
  return below;
}
consteval bool provides(contract::settings_kept, type_tag<feature>) { return true; }
constexpr type_tag<level_choices<choices>> settings_type(contract::settings_kept, type_tag<feature>) { return {}; }
consteval bool provides(contract::choice_rows, type_tag<feature>) { return true; }
// On an account's Privacy page: whether others are told one is typing.
consteval bool provides(contract::account_privacy_rows, type_tag<feature>) { return true; }

}  // namespace mux::plugins::typing
