// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.receipts.ui -- Who has read up to a message, as Element shows
// it: their small faces at the row's right under it, three at most and the
// rest counted. Its row among a level's choices for chats.
module;
#include "skiff/named.h"
export module mux.plugin.receipts.ui;

import std;
import skiff.scene;
import skiff.compose;
import skiff.nodes.text;
import mux.composition;
import mux.core;
import mux.levels;
import skiff.model;
import mux.ui.base;
import mux.plugin.receipts.types;

export namespace mux::plugins::receipts {
using namespace ::mux::ui;

inline constexpr float kReaderFace = 14.0f;  // Element's read receipt avatar
inline constexpr float kReadersHeight = 16.0f;
inline constexpr std::size_t kReadersShown = 3;
inline auto readers_row(const palette& colours, const conversation& in, const std::vector<std::string>& users) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(readers_row, c::row(c::hbox(2.0f, {.place = scene::anchor::kBottomRight, .x = -4.0f, .y = kReadersHeight + 1.0f,
                                                       .autoSize = scene::axes::kBoth}),
      c::many(c::hbox(2.0f, {.autoSize = scene::axes::kBoth}),
              users | std::views::take(kReadersShown) | std::views::transform([&](const std::string& user) {
                return avatar_mark(avatar_of::person{}, user, sender_name(in, user), kReaderFace);
              }) | std::ranges::to<std::vector>()),
      c::styled({.alignSelf = scene::align::kMiddle},
                c::visible(users.size() > kReadersShown,
                           nodes::Text(users.size() > kReadersShown ? std::format("+{}", users.size() - kReadersShown) : std::string(),
                                       10.0f, colours.dim)))));
}
using readers_row_t = decltype(readers_row(std::declval<const palette&>(), std::declval<const conversation&>(), {}));

// Its footer under a message's row (mux.ui.messages:message): where anyone
// has read up to it, as the chat shows receipts.
inline std::optional<readers_row_t> extend(contract::message_footers, type_tag<feature>, const footer_facts& facts) {
  if (facts.readers.empty())
    return std::nullopt;
  return readers_row(facts.colours, facts.in, facts.readers);
}
constexpr float footer_height(contract::message_footers, type_tag<feature>) { return kReadersHeight; }

// Its row among a level's choices (contract::choice_rows): faces shown,
// hidden, or as the level over it says.
template <class Place>
inline auto extend(contract::choice_rows, type_tag<feature>, const auto& colours, const Place& place) {
  using field = skiff::model::Field<&choices::shown>;
  return choices_scoped<choices>(place, skiff::compose::bound<field>(
      show_hide_field<receipts_setting, std::optional<bool>, field>(colours, level_of(place))));
}

// The same row on an account's Chats page (contract::account_chat_rows).
template <class Place>
auto extend(contract::account_chat_rows, type_tag<feature> f, const auto& colours, const Place& place) {
  return extend(contract::choice_rows{}, f, colours, place);
}

}  // namespace mux::plugins::receipts
