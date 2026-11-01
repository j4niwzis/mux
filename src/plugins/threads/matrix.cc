// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.threads.matrix -- Threads over Matrix, the plugin's own: what
// a message made of an event is of a thread -- its root, where it answers
// one (m.thread; its answer to the thread's latest event, where it falls
// back, is for clients that know no threads -- not one); a root's summary,
// as the server counts it (in an encrypted room, no one's word: its text not
// shown) -- and a room's threads listed, each root's own message taken
// aside. Through the account's door for plugins.
export module mux.plugin.threads.matrix;

import std;
import splice;
import loom.ev;
import loom.state;
import loom.cs.threads_list;
import mux.core;
import mux.proto.matrix.client;
import mux.plugin.threads.types;
export import mux.plugin.threads.changes;

export namespace mux::plugins::threads {

template <class Relates>
void matrix_message_made(contract::matrix_events, type_tag<feature>, const loom::ev::timeline_event& one, const std::optional<Relates>& relates,
                         bool sealed_room, message& made) {
  message_part& mine = part_of(made);
  if (relates)
    if ((mine.root = loom::client::thread_of(*relates)) && relates->is_falling_back.value_or(false))
      made.replies_to.reset();
  if (const auto said = loom::client::thread_summary_of(one))
    mine.threaded = summary{.count = said->count,
                            .last_id = said->latest_id.value_or(""),
                            .last_sender = said->latest_sender.value_or(""),
                            .last_text = sealed_room ? std::string() : said->latest_body.value_or(""),
                            .last_at = std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::milliseconds(said->latest_ts.value_or(0))),
                            .participated = said->participated};
}

template <class Account>
concept matrix_account = requires(Account& account) {
  account.synced();
  account.connected();
};
// A room's threads, as its server lists them.
struct list_asked {
  std::string room;
  template <matrix_account Account>
  void operator()(Account& account) const {
    account.on_fiber([&account, room = room] {
      using asked = loom::cs::get_thread_roots;
      auto got = account.request(asked{.room_id = room, .include = asked::include_t{asked::include_values::all{}}, .limit = 50});
      if (!got)
        return;
      const conversation_id in{account.id(), room};
      std::vector<std::string> roots;
      for (const auto& one : got->chunk) {
        roots.push_back(one.event_id);
        account.took_aside(in, one);
      }
      account.tell_program(listed{in, std::move(roots)});
    });
  }
};

}  // namespace mux::plugins::threads
