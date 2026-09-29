// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.demo: Fake accounts and conversations, for mux --demo.
export module mux.app.demo;

import std;
import mux.core;
import mux.config;

export namespace mux::app {

// ---- the demo ----------------------------------------------------------------

// Fake accounts and conversations, for `mux --demo`: a window to look at and
// click through with no network and no accounts file.
namespace fake {

using namespace std::chrono_literals;

[[nodiscard]] inline std::vector<mux::config::account_t> accounts() {
  return {mux::config::xmpp_account{.address = "alice@wonderland.example", .password = "demo"},
          mux::config::matrix_account{.user_id = "@alice:matrix.example", .password = "demo"}};
}

struct said {
  std::string sender;
  std::string text;
  bool outgoing = false;
};

inline void conversation(mux::model& into, const mux::account_id& account, std::string id, std::string name,
                         mux::conversation_kind_t kind, std::optional<std::string> topic, bool encrypted,
                         std::int64_t unread, std::vector<said> lines) {
  const mux::conversation_id where{account, id};
  into.apply(mux::change_t{mux::change::conversation_updated{.id = where,
                                                             .kind = kind,
                                                             .name = std::move(name),
                                                             .topic = std::move(topic),
                                                             .encrypted = encrypted,
                                                             .unread = unread}});
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

inline void fill(mux::model& into) {
  const mux::account_id xmpp{mux::protocol::xmpp{}, "alice@wonderland.example"};
  const mux::account_id matrix{mux::protocol::matrix{}, "@alice:matrix.example"};
  into.apply(mux::change_t{mux::change::connection_changed{xmpp, mux::connection::online{}}});
  into.apply(mux::change_t{mux::change::connection_changed{matrix, mux::connection::online{}}});

  conversation(into, xmpp, "hatter@wonderland.example", "The Hatter", mux::conversation_kind::direct{}, std::nullopt,
               true, 2,
               {{"hatter@wonderland.example", "Why is a raven like a writing-desk?"},
                {"alice@wonderland.example", "I give up. What's the answer?", true},
                {"hatter@wonderland.example", "I haven't the slightest idea."},
                {"hatter@wonderland.example", "Tea at six, as always. Don't be late."}});
  conversation(into, xmpp, "rabbit@wonderland.example", "White Rabbit", mux::conversation_kind::direct{}, std::nullopt,
               false, 0,
               {{"rabbit@wonderland.example", "Oh dear! Oh dear! I shall be too late!"},
                {"alice@wonderland.example", "Late for what?", true},
                {"rabbit@wonderland.example", "The Duchess! She'll have my head."}});
  conversation(into, xmpp, "croquet@rooms.wonderland.example", "Croquet club", mux::conversation_kind::group{},
               "Flamingos provided. Hedgehogs bring their own.", false, 5,
               {{"queen@wonderland.example", "Who has been painting my roses red?"},
                {"two@wonderland.example", "Not us, Your Majesty."},
                {"queen@wonderland.example", "Off with their heads!"},
                {"king@wonderland.example", "My dear, let us have the trial first."}});
  conversation(into, matrix, "!tea:matrix.example", "Mad Tea Party", mux::conversation_kind::group{},
               "No room! No room!", true, 12,
               {{"@dormouse:matrix.example", "Twinkle, twinkle, little bat…"},
                {"@march.hare:matrix.example", "Have some wine."},
                {"@alice:matrix.example", "I don't see any wine.", true},
                {"@march.hare:matrix.example", "There isn't any."}});
  conversation(into, matrix, "!cheshire:matrix.example", "Cheshire Cat", mux::conversation_kind::direct{},
               std::nullopt, true, 0,
               {{"@cheshire:matrix.example", "We're all mad here."},
                {"@alice:matrix.example", "How do you know I'm mad?", true},
                {"@cheshire:matrix.example", "You must be, or you wouldn't have come here."}});

  // Who is in the groups, and how everyone is.
  const auto members = [&into](const mux::account_id& account, std::string id, std::vector<mux::member> who) {
    into.apply(mux::change_t{mux::change::members_changed{{account, std::move(id)}, std::move(who)}});
  };
  members(xmpp, "croquet@rooms.wonderland.example",
          {{"queen@wonderland.example", "The Queen of Hearts", "owner"},
           {"king@wonderland.example", "The King of Hearts", "admin"},
           {"alice@wonderland.example", "Alice", std::nullopt},
           {"two@wonderland.example", "Two of Spades", std::nullopt},
           {"rabbit@wonderland.example", "White Rabbit", std::nullopt}});
  members(matrix, "!tea:matrix.example",
          {{"@march.hare:matrix.example", "March Hare", "owner"},
           {"@hatter:matrix.example", "The Hatter", "admin"},
           {"@dormouse:matrix.example", "Dormouse", std::nullopt},
           {"@alice:matrix.example", "Alice", std::nullopt}});
  const auto is = [&into](const mux::account_id& account, std::string contact, mux::availability_t state) {
    into.apply(mux::change_t{mux::change::presence_changed{account, std::move(contact), mux::presence{state, std::nullopt}}});
  };
  is(xmpp, "hatter@wonderland.example", mux::availability::online{});
  is(xmpp, "rabbit@wonderland.example", mux::availability::away{});
  is(xmpp, "queen@wonderland.example", mux::availability::do_not_disturb{});
  is(xmpp, "king@wonderland.example", mux::availability::online{});
  is(matrix, "@cheshire:matrix.example", mux::availability::extended_away{});
  is(matrix, "@march.hare:matrix.example", mux::availability::online{});
  is(matrix, "@hatter:matrix.example", mux::availability::online{});
}

}  // namespace fake

}  // namespace mux::app
