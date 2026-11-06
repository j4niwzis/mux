// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.typing.ui -- Who is typing in a chat, by their names, as
// Telegram says it in the chat's head: one, two ("A and B"), three ("A, B
// and C"); past three, the first two and how many more.
export module mux.plugin.typing.ui;

import std;
import mux.composition;
import mux.core;
import mux.levels;
import skiff.model;
import skiff.compose;
import mux.ui.base;
import mux.plugin.typing.types;
import mux.plugin.typing.model;

export namespace mux::plugins::typing {

// The chat's status in its head, while someone types (mux.ui:header).
inline std::optional<std::string> extend(contract::chat_status, type_tag<feature>, const conversation& one) {
  const auto& who = std::get<chat_part<message>>(one.plugins).who;
  if (who.empty())
    return std::nullopt;
  const auto names = who | std::views::transform([&](const std::string& who) { return ui::sender_name(one, who); }) |
                     std::ranges::to<std::vector>();
  switch (names.size()) {
    case 1: return names[0] + " is typing…";
    case 2: return std::format("{} and {} are typing…", names[0], names[1]);
    case 3: return std::format("{}, {} and {} are typing…", names[0], names[1], names[2]);
    default: return std::format("{}, {} and {} more are typing…", names[0], names[1], names.size() - 2);
  }
}

// Others told one is typing -- never what: sent, where nothing says
// otherwise.
struct typing_setting {
  static constexpr std::string_view label = "Send typing notifications";
  static constexpr std::string_view yes = "Send", no = "Don't";
  static constexpr bool unsaid = true;
};

// Its row among a level's choices (contract::choice_rows): told, not told,
// or as the level over it says.
template <class Place>
inline auto extend(contract::choice_rows, type_tag<feature>, const auto& colours, const Place& place) {
  using field = skiff::model::Field<&choices::sent>;
  return ::mux::ui::choices_scoped<choices>(place, skiff::compose::bound<field>(
      ::mux::ui::show_hide_field<typing_setting, std::optional<bool>, field>(colours, level_of(place))));
}

// The same row on an account's Privacy page (contract::account_page_rows).
template <class Place>
auto extend(contract::account_page_rows, type_tag<feature> f, const auto& colours, const Place& place, ::mux::ui::account_page::privacy) {
  return extend(contract::choice_rows{}, f, colours, place);
}

}  // namespace mux::plugins::typing
