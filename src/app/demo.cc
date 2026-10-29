// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.demo: Fake accounts and conversations, for mux --demo.
export module mux.app.demo;

import std;
import mux.core;
import mux.config;
import mux.proto.demo;

export namespace mux::app {

// ---- the demo ----------------------------------------------------------------

// Fake accounts and conversations, for `mux --demo`: a window to look at and
// click through with no network and no accounts file.
namespace fake {

using namespace std::chrono_literals;

struct said {
  std::string sender;
  std::string text;
  bool outgoing = false;
};

inline void conversation(mux::model& into, const mux::account_id& account, std::string id, std::string name,
                         mux::conversation_kind_t kind, std::optional<std::string> topic, bool encrypted,
                         std::int64_t unread, std::vector<said> lines, mux::room_part_t theirs = {}) {
  const mux::conversation_id where{account, id};
  into.apply(mux::change_t{mux::change::conversation_updated{.id = where,
                                                             .kind = kind,
                                                             .name = std::move(name),
                                                             .topic = std::move(topic),
                                                             .encrypted = encrypted,
                                                             .unread = unread,
                                                             .theirs = std::move(theirs)}});
  const auto now = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
  auto at = now - std::chrono::minutes(3 * static_cast<int>(lines.size()));
  int n = 0;
  for (said& line : lines) {
    mux::message one;
    one.in = where;
    one.id = std::format("{}-{}", id, n++);
    one.sender = std::move(line.sender);
    one.at = at;
    one.body.plain = std::move(line.text);
    one.outgoing = line.outgoing;
    into.apply(mux::change_t{mux::change::message_added{.message = std::move(one)}});
    at += 3min;
  }
}

// What a protocol's demo is made with: its account online, its chats with
// what was said in them, who is in a group, how someone is.
struct maker {
  mux::model& into;
  void online(const mux::account_id& account) {
    into.apply(mux::change_t{mux::change::connection_changed{account, mux::connection::online{}}});
  }
  void chat(const mux::account_id& account, std::string id, std::string name, mux::conversation_kind_t kind,
            std::optional<std::string> topic, bool encrypted, std::int64_t unread, std::vector<said> lines,
            mux::room_part_t theirs = {}) {
    conversation(into, account, std::move(id), std::move(name), kind, std::move(topic), encrypted, unread, std::move(lines),
                 std::move(theirs));
  }
  void members(const mux::account_id& account, std::string id, std::vector<mux::member> who) {
    into.apply(mux::change_t{mux::change::members_changed{{account, std::move(id)}, std::move(who)}});
  }
  void is(const mux::account_id& account, std::string contact, mux::availability_t state) {
    into.apply(mux::change_t{mux::change::presence_changed{account, std::move(contact), mux::presence{state, std::nullopt}}});
  }
  // The protocol's state: the window's, not the model's (states_taker).
  void state(const mux::account_id&, const mux::protocol_state_t&) {}
};
// What each protocol's demo says its account's protocol is -- online: what
// the window offers goes by it (XMPP lets one write only while its stream is
// up). The rest of the demo is the model's (maker), and not made twice.
struct states_taker {
  std::vector<mux::change::protocol_state_changed> took;
  void online(const mux::account_id&) {}
  void chat(const mux::account_id&, std::string, std::string, mux::conversation_kind_t, std::optional<std::string>, bool,
            std::int64_t, std::vector<said>, mux::room_part_t = {}) {}
  void members(const mux::account_id&, std::string, std::vector<mux::member>) {}
  void is(const mux::account_id&, std::string, mux::availability_t) {}
  void state(const mux::account_id& account, mux::protocol_state_t now) { took.push_back({account, std::move(now)}); }
};

// Each protocol's own demo (demo_account and demo, by ADL on its state):
// none by default.
namespace demo_defaults {
inline std::optional<mux::config::account_t> demo_account(const auto&) { return std::nullopt; }
// Generic in the maker too: a protocol's own, on its state, is then the
// better match -- not an equal one, as with a maker named here.
template <class State, class Maker>
void demo(const State&, Maker&) {}
}  // namespace demo_defaults
template <class... Tags>
[[nodiscard]] std::vector<mux::config::account_t> accounts_of(mux::protocol_list<Tags...>) {
  using demo_defaults::demo_account;
  std::vector<mux::config::account_t> out;
  (
      [&] {
        if (auto one = demo_account(mux::state_of<Tags>{}))
          out.push_back(std::move(*one));
      }(),
      ...);
  return out;
}
[[nodiscard]] inline std::vector<mux::config::account_t> accounts() { return accounts_of(mux::protocols{}); }
template <class Maker, class... Tags>
void fill_of(mux::protocol_list<Tags...>, Maker& make) {
  using demo_defaults::demo;
  (demo(mux::state_of<Tags>{}, make), ...);
}
inline void fill(mux::model& into) {
  maker make{into};
  fill_of(mux::protocols{}, make);
}
[[nodiscard]] inline std::vector<mux::change::protocol_state_changed> states() {
  states_taker take;
  fill_of(mux::protocols{}, take);
  return std::move(take.took);
}

}  // namespace fake

}  // namespace mux::app
