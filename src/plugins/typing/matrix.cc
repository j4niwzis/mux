// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.typing.matrix -- Typing over Matrix, the plugin's own: who is
// typing in a room, as its sync brings it in its ephemeral events (m.typing)
// -- but the account itself: its own typing, from this or another device,
// is not news to it (as in Telegram) -- told to the program as the plugin's
// change; and the user typing in a room, or not, told to it (for thirty
// seconds, or until said). Through the account's door for plugins.
export module mux.plugin.typing.matrix;

import std;
import splice;
import loom.ev;
import loom.cs.typing;
import mux.core;
import mux.proto.matrix.client;
import mux.plugin.typing.types;
export import mux.plugin.typing.changes;

export namespace mux::plugins::typing {

template <class Events, class Account>
void matrix_ephemeral(contract::matrix_events, type_tag<feature>, const conversation_id& in, const Events& events, Account& account) {
  for (const auto& event : events)
    spl::visit(spl::overloaded{[&](const loom::ev::m_typing_content_t& content) {
                                 account.tell_program(typing_now{in, content.user_ids | std::views::filter([&](const std::string& who) {
                                                                       return who != account.id().address;
                                                                     }) | std::ranges::to<std::vector>()});
                               },
                               [](const auto&) {}},
               event.content.data());
}

template <class Account>
concept matrix_account = requires(Account& account) {
  account.synced();
  account.connected();
};
// The user typing in a room, or not (typing_ask, from the program's part).
template <matrix_account Account>
void told_typing(Account& account, const typing_ask& ask) {
  account.on_fiber([&account, ask] {
    if (!account.connected())
      return;
    (void)account.request(loom::cs::set_typing{.user_id = account.id().address,
                                               .room_id = ask.to,
                                               .body = {.typing = ask.on,
                                                        .timeout = ask.on ? std::optional<std::int64_t>(30000) : std::nullopt}});
  });
}

}  // namespace mux::plugins::typing
