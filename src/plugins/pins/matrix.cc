// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.pins.matrix -- Pins over Matrix, the plugin's own: a message
// pinned in its room or unpinned, m.room.pinned_events written with it put
// in or taken out. Through the account's door for plugins.
export module mux.plugin.pins.matrix;

import std;
import splice;
import knot;
import loom.ev;
import loom.state;
import loom.cs.room_state;
import mux.core;
import mux.proto.matrix.client;

export namespace mux::plugins::pins {

// An account a pin can be asked of: a Matrix one, through its door.
template <class Account>
concept matrix_account = requires(Account& account) {
  account.synced();
  account.connected();
};

struct pin_asked {
  std::string room, target;
  bool on = true;
  template <matrix_account Account>
  void operator()(Account& account) const {
    account.on_fiber([&account, one = *this] {
      if (!account.connected())
        return;
      // The pins as the server has them now, read from its answer: the
      // room's state as synced may not hold them (lazy loading, a room just
      // joined), and a list made from nothing put this pin in place of all
      // the others. Where the server cannot say, as synced.
      std::vector<std::string> pinned;
      if (const auto got = account.request(loom::cs::get_room_state_with_key{
              .room_id = one.room, .event_type = "m.room.pinned_events", .state_key = ""})) {
        if (const auto typed = knot::try_read<loom::ev::m_room_pinned_events_content_t>(got->text))
          pinned = typed->pinned;
      } else if (const auto& server = got.error().server; !server || server->status != 404) {
        const auto& state = account.synced();
        if (const auto kept = state.joined.find(one.room); kept != state.joined.end())
          pinned = kept->second.state.pinned();
      }
      std::erase(pinned, one.target);
      if (one.on)
        pinned.push_back(one.target);
      loom::ev::m_room_pinned_events_content_t content;
      content.pinned = std::move(pinned);
      // Said, not only logged: a pin refused (too little power in the room)
      // looked like a menu item that did nothing (a tester's).
      if (auto done = account.request(loom::cs::set_room_state_with_key{.room_id = one.room, .event_type = "m.room.pinned_events",
                                                                         .state_key = "", .body = knot::raw{knot::to_json_string(content)}});
          !done)
        account.tell_program(change::notice{account.id(), one.on ? "Not pinned" : "Not unpinned", done.error().said()});
    });
  }
};

}  // namespace mux::plugins::pins
