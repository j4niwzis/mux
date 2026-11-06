// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.receipts.matrix -- Read receipts over Matrix, the plugin's own:
// a room's, as its sync brings them in its ephemeral events (m.receipt:
// event -> kind -> user; the public and the private m.read both say how far
// someone has read), told to the program as the plugin's change. Through the
// account's door for plugins (matrix_ephemeral).
export module mux.plugin.receipts.matrix;

import std;
import loom.state;
import mux.core;
import mux.proto.matrix.client;
import mux.plugin.receipts.types;
export import mux.plugin.receipts.changes;

export namespace mux::plugins::receipts {

template <class Events, class Account>
void matrix_ephemeral(contract::matrix_events, type_tag<feature>, const conversation_id& in, const Events& events, Account& account) {
  read heard{in};
  for (const auto& event : events)
    for (const loom::client::read_receipt& one : loom::client::receipts_of(event)) {
      heard.read_by.insert_or_assign(one.user, one.event_id);
      if (one.ts)
        heard.read_at.insert_or_assign(one.user, std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::milliseconds(*one.ts)));
    }
  if (!heard.read_by.empty())
    account.tell_program(std::move(heard));
}

}  // namespace mux::plugins::receipts
