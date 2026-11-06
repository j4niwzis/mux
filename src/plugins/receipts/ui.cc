// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.receipts.ui -- Who has read up to a message, as Element shows
// it: their small faces at the row's right under it, three at most and the
// rest counted. Its row among a level's choices for chats.
module;
#include "../../inline_in_release.h"
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
MUX_INLINE_IN_RELEASE auto readers_row(const palette& colours, const conversation& in, const std::vector<std::string>& users) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(readers_row, c::row(c::hbox(2.0f, {.place = scene::anchor::kBottomRight, .x = -4.0f, .y = kReadersHeight + 1.0f,
                                                       .autoSize = scene::axes::kBoth}),
      c::many(c::hbox(2.0f, {.autoSize = scene::axes::kBoth}),
              users | std::views::take(kReadersShown) | std::views::transform([&](const std::string& user) {
                return face_spot(avatar_of::person{}, user, sender_name(in, user), kReaderFace);
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
  const auto& mark = std::get<std::optional<readers>>(facts.marks);
  if (!mark || mark->who.empty())
    return std::nullopt;
  return readers_row(facts.colours, facts.in, mark->who);
}
constexpr float footer_height(contract::message_footers, type_tag<feature>) { return kReadersHeight; }

// Who has read a message, for its menu's foot (contract::message_menu_foot):
// those whose receipt is for it or for one after it, by their names in the
// chat -- its sender and the user aside.
inline seen foot_facts(contract::message_menu_foot, type_tag<feature>, const conversation& chat, const message& said) {
  const auto place = [&](const std::string& event) -> std::optional<std::size_t> {
    const auto at = std::ranges::find(chat.timeline, event, &message::id);
    return at == chat.timeline.end() ? std::nullopt : std::optional(static_cast<std::size_t>(at - chat.timeline.begin()));
  };
  const auto mine = place(said.id);
  if (!mine)
    return {};
  const auto when = chat.timeline[*mine].at;
  auto out = chat.read_by | std::views::filter([&](const auto& read) {
               const auto& [user, event] = read;
               if (user == said.sender || user == chat.id.account.address)
                 return false;
               // Read up to a message here at or after it; else, the receipt
               // pointing at what is not among these -- a reaction, a state
               // event -- read at or after it was sent.
               if (const auto theirs = place(event))
                 return *theirs >= *mine;
               const auto read_at = chat.receipt_times.find(user);
               return read_at != chat.receipt_times.end() && read_at->second >= when;
             }) | std::views::transform([&](const auto& read) {
               const auto read_at = chat.receipt_times.find(read.first);
               return reader{read.first, sender_name(chat, read.first),
                             read_at == chat.receipt_times.end() ? std::nullopt : std::optional(read_at->second)};
             }) | std::ranges::to<std::vector>();
  std::ranges::sort(out, {}, &reader::name);
  return {std::move(out)};
}

// Its row among a level's choices (contract::choice_rows): faces shown,
// hidden, or as the level over it says.
template <class Place>
MUX_INLINE_IN_RELEASE auto extend(contract::choice_rows, type_tag<feature>, const auto& colours, const Place& place) {
  using field = skiff::model::Field<&choices::shown>;
  return choices_scoped<choices>(place, skiff::compose::bound<field>(
      show_hide_field<receipts_setting, std::optional<bool>, field>(colours, level_of(place))));
}

// The same row on an account's Chats page (contract::account_page_rows).
template <class Place>
auto extend(contract::account_page_rows, type_tag<feature> f, const auto& colours, const Place& place, account_page::chats) {
  return extend(contract::choice_rows{}, f, colours, place);
}

}  // namespace mux::plugins::receipts
