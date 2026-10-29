// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.devtools.matrix -- The developer tools over Matrix, the
// plugin's own, as Element's: a message's source as the server has it; a
// room's state and account data, as the account has them synced; a state
// event, account data or an event of any type written as typed. Through the
// account's door for plugins: the Matrix client knows nothing of them.
export module mux.plugin.devtools.matrix;

import std;
import splice;
import knot;
import loom.ev;
import loom.state;
import loom.cs.rooms;
import loom.cs.room_state;
import loom.cs.account_data;
import mux.core;
import mux.proto.matrix.client;
import mux.plugin.devtools.types;
export import mux.plugin.devtools.changes;

export namespace mux::plugins::devtools {

// An account the tools can be asked of: a Matrix one, through its door.
template <class Account>
concept matrix_account = requires(Account& account) {
  account.synced();
  account.connected();
};

namespace written {
// Whether what was typed is a content: a JSON object -- read as one, its
// values left as they were typed. Typed text, read where it comes in.
[[nodiscard]] inline bool is_object(const std::string& json) {
  return static_cast<bool>(knot::try_read<std::map<std::string, knot::raw>>(json));
}
// What a write came to, said as the answer to read.
template <class Answer>
[[nodiscard]] std::string said(const Answer& done) {
  return done ? "Done: " + done->event_id : "Not done: " + done.error().said();
}
template <class Answer>
[[nodiscard]] std::string said_plain(const Answer& done) {
  return done ? std::string("Done.") : "Not done: " + done.error().said();
}
}  // namespace written

// A message's source, as the server has it.
struct source_asked {
  std::string room, event;
  template <matrix_account Account>
  void operator()(Account& account) const {
    account.on_fiber([&account, room = room, event = event] {
      const std::string title = "Source of " + event;
      if (!account.connected()) {
        account.tell_program(told{{account.id(), room}, title, "Not fetched: not connected."});
        return;
      }
      auto got = account.request(loom::cs::get_one_room_event{.room_id = room, .event_id = event});
      account.tell_program(told{{account.id(), room}, title,
                                got ? knot::to_pretty_json_string(*got) : "Not fetched: " + got.error().said()});
    });
  }
};
// A room's state, as synced: every event by its type and key.
struct state_asked {
  std::string room;
  template <matrix_account Account>
  void operator()(Account& account) const {
    account.on_fiber([&account, room = room] {
      const auto& state = account.synced();
      std::vector<state_entry> entries;
      if (const auto joined = state.joined.find(room); joined != state.joined.end())
        entries = joined->second.state.events | std::views::transform([](const auto& entry) {
                    return state_entry{entry.first.first, entry.first.second, knot::to_pretty_json_string(entry.second),
                                       knot::to_pretty_json_string(entry.second.content)};
                  }) | std::ranges::to<std::vector>();
      account.tell_program(state_listed{{account.id(), room}, std::move(entries)});
    });
  }
};
// A room's account data, one's own there, as synced.
struct data_asked {
  std::string room;
  template <matrix_account Account>
  void operator()(Account& account) const {
    account.on_fiber([&account, room = room] {
      const auto& state = account.synced();
      std::vector<data_entry> entries;
      if (const auto joined = state.joined.find(room); joined != state.joined.end())
        entries = joined->second.account_data | std::views::transform([](const auto& entry) {
                    return data_entry{entry.first, knot::to_pretty_json_string(entry.second.content)};
                  }) | std::ranges::to<std::vector>();
      account.tell_program(data_listed{{account.id(), room}, std::move(entries)});
    });
  }
};
// A state event written: its content as typed.
struct save_state_asked {
  std::string room, type, key, content;
  template <matrix_account Account>
  void operator()(Account& account) const {
    account.on_fiber([&account, one = *this] {
      const std::string title = "Saved " + one.type;
      if (!written::is_object(one.content))
        account.tell_program(told{{account.id(), one.room}, title, "Not saved: the content is not a JSON object."});
      else if (!account.connected())
        account.tell_program(told{{account.id(), one.room}, title, "Not saved: not connected."});
      else
        account.tell_program(told{{account.id(), one.room}, title, written::said(account.request(loom::cs::set_room_state_with_key{
            .room_id = one.room, .event_type = one.type, .state_key = one.key, .body = knot::raw{one.content}}))});
    });
  }
};
// The room's account data written: its content as typed.
struct save_data_asked {
  std::string room, type, content;
  template <matrix_account Account>
  void operator()(Account& account) const {
    account.on_fiber([&account, one = *this] {
      const std::string title = "Saved " + one.type;
      if (!written::is_object(one.content))
        account.tell_program(told{{account.id(), one.room}, title, "Not saved: the content is not a JSON object."});
      else if (!account.connected())
        account.tell_program(told{{account.id(), one.room}, title, "Not saved: not connected."});
      else
        account.tell_program(told{{account.id(), one.room}, title, written::said_plain(account.request(loom::cs::set_account_data_per_room{
            .user_id = account.id().address, .room_id = one.room, .type = one.type, .body = knot::raw{one.content}}))});
    });
  }
};
// An event of any type sent: a state event where it has a key, else into
// the timeline -- encrypted where the room is, as a message is.
struct send_asked {
  std::string room, type;
  std::optional<std::string> key;
  std::string content;
  template <matrix_account Account>
  void operator()(Account& account) const {
    account.on_fiber([&account, one = *this] {
      const std::string title = "Sent " + one.type;
      if (!written::is_object(one.content))
        account.tell_program(told{{account.id(), one.room}, title, "Not sent: the content is not a JSON object."});
      else if (!account.connected())
        account.tell_program(told{{account.id(), one.room}, title, "Not sent: not connected."});
      else if (one.key)
        account.tell_program(told{{account.id(), one.room}, title, written::said(account.request(loom::cs::set_room_state_with_key{
            .room_id = one.room, .event_type = one.type, .state_key = *one.key, .body = knot::raw{one.content}}))});
      else
        account.tell_program(told{{account.id(), one.room}, title, written::said(account.send_written(one.room, one.type, one.content))});
    });
  }
};

}  // namespace mux::plugins::devtools
