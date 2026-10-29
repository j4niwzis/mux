// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.commands.matrix -- The commands over Matrix: the event of a
// room at a time, asked of its server (MSC3030, timestamp_to_event) --
// history not loaded yet gone to by date. Through the account's door for
// plugins.
export module mux.plugin.commands.matrix;

import std;
import splice;
import loom.cs.room_event_by_timestamp;
import mux.core;
import mux.proto.matrix.client;
import mux.plugin.commands.changes;

export namespace mux::plugins::commands {

// An account the event can be asked of: a Matrix one, through its door.
template <class Account>
concept matrix_account = requires(Account& account) {
  account.synced();
  account.connected();
};

// The room's first event from `at` (milliseconds since the epoch) on.
struct event_at_asked {
  std::string room;
  std::int64_t at = 0;
  template <matrix_account Account>
  void operator()(Account& account) const {
    account.on_fiber([&account, one = *this] {
      if (!account.connected())
        return;
      using asked = loom::cs::get_event_by_timestamp;
      const auto got = account.request(asked{.room_id = one.room, .ts = one.at, .dir = asked::dir_values::f{}});
      account.tell_program(event_at{{account.id(), one.room}, got ? std::optional(got->event_id) : std::nullopt});
    });
  }
};

}  // namespace mux::plugins::commands
