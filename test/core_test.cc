// SPDX-License-Identifier: AGPL-3.0-only
// mux.core's model: changes applied as the protocols say them.
import std;
import mux.core;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace mux;

const account_id romeo{protocol::xmpp{}, "romeo@example.net"};
const conversation_id with_juliet{romeo, "juliet@example.com"};

message said(std::string id, std::string text, bool outgoing = false) {
  return message{.in = with_juliet,
                 .id = std::move(id),
                 .sender = outgoing ? romeo.address : with_juliet.id,
                 .body = {std::move(text), std::nullopt},
                 .outgoing = outgoing};
}

TEST(Model, Conversation) {
  model kept;
  kept.apply(change::connection_changed{romeo, connection::online{}});
  kept.apply(change::conversation_updated{.id = with_juliet, .name = "Juliet", .unread = 2});
  kept.apply(change::presence_changed{romeo, "juliet@example.com", {availability::away{}, "on the balcony"}});
  ASSERT_TRUE(kept.accounts().contains(romeo));
  const account& got = kept.accounts().at(romeo);
  EXPECT_EQ(got.state, connection_t{connection::online{}});
  EXPECT_EQ(got.presences.at("juliet@example.com").status, "on the balcony");
  const conversation* one = kept.find(with_juliet);
  ASSERT_NE(one, nullptr);
  EXPECT_EQ(one->name, "Juliet");
  EXPECT_EQ(one->unread, 2);
}

// A window of history opened away from the newest: what is live comes
// only as the latest, until paging forward meets the newest.
TEST(Model, AWindowAwayFromTheNewest) {
  model kept;
  kept.apply(change::message_added{said("1", "hello")});
  kept.apply(change::window_opened{with_juliet, std::string("back"), std::string("forward")});
  kept.apply(change::message_added{said("a", "long ago"), placement::in_window{}});
  kept.apply(change::message_added{said("b", "live, meanwhile")});
  const conversation* one = kept.find(with_juliet);
  ASSERT_NE(one, nullptr);
  EXPECT_TRUE(one->detached);
  ASSERT_EQ(one->timeline.size(), 1u);
  EXPECT_EQ(one->timeline[0].id, "a");
  ASSERT_NE(newest(*one), nullptr);
  EXPECT_EQ(newest(*one)->id, "b");
  // Paged forward to the newest: live again, what comes goes in.
  kept.apply(change::message_added{said("b", "live, meanwhile"), placement::in_window{}});
  kept.apply(change::window_extended{with_juliet, std::nullopt});
  kept.apply(change::message_added{said("c", "now")});
  one = kept.find(with_juliet);
  EXPECT_FALSE(one->detached);
  ASSERT_EQ(one->timeline.size(), 3u);
  EXPECT_EQ(one->timeline.back().id, "c");
}

TEST(Model, Messages) {
  model kept;
  kept.apply(change::message_added{said("1", "hello")});
  kept.apply(change::message_added{said("2", "hi", true)});
  // History goes before what is there.
  kept.apply(change::message_added{said("0", "earlier"), placement::at_start{}});
  // The echo of one sent replaces it rather than adding a second.
  auto echo = said("2", "hi", true);
  echo.delivery = delivery::delivered{};
  kept.apply(change::message_added{echo});
  const conversation* one = kept.find(with_juliet);
  ASSERT_NE(one, nullptr);
  ASSERT_EQ(one->timeline.size(), 3u);
  EXPECT_EQ(one->timeline[0].id, "0");
  EXPECT_EQ(one->timeline[2].delivery, delivery_t{delivery::delivered{}});

  kept.apply(change::message_edited{with_juliet, "1", {"hello there", std::nullopt}});
  kept.apply(change::reaction_changed{with_juliet, "1", "❤", "romeo@example.net", true});
  kept.apply(change::reaction_changed{with_juliet, "1", "❤", "juliet@example.com", true});
  kept.apply(change::reaction_changed{with_juliet, "1", "❤", "romeo@example.net", false});
  one = kept.find(with_juliet);
  EXPECT_EQ(one->timeline[1].body.plain, "hello there");
  EXPECT_TRUE(one->timeline[1].edited);
  EXPECT_EQ(one->timeline[1].reactions.at("❤"), (std::set<std::string>{"juliet@example.com"}));

  kept.apply(change::message_redacted{with_juliet, "1"});
  one = kept.find(with_juliet);
  EXPECT_TRUE(one->timeline[1].redacted);
  EXPECT_TRUE(one->timeline[1].body.plain.empty());
  EXPECT_TRUE(one->timeline[1].reactions.empty());

  kept.apply(change::delivery_changed{with_juliet, "2", delivery::read{}});
  kept.apply(change::typing_changed{with_juliet, {"juliet@example.com"}});
  kept.apply(change::history_position{with_juliet, "mam-17"});
  one = kept.find(with_juliet);
  EXPECT_EQ(one->timeline[2].delivery, delivery_t{delivery::read{}});
  EXPECT_EQ(one->typing, (std::vector<std::string>{"juliet@example.com"}));
  EXPECT_EQ(one->history_from, "mam-17");

  kept.apply(change::members_changed{with_juliet, {{"juliet@example.com", "Juliet", "owner"}, {"nurse@example.com", "Nurse", std::nullopt}}});
  one = kept.find(with_juliet);
  ASSERT_EQ(one->members.size(), 2u);
  EXPECT_EQ(one->members[0].role, "owner");
  EXPECT_EQ(one->members[1].name, "Nurse");
}

TEST(Model, Acknowledged) {
  model kept;
  kept.apply(change::message_added{said("txn1", "sent from here", true)});
  kept.apply(change::message_acknowledged{with_juliet, "txn1", "$event1"});
  ASSERT_EQ(kept.find(with_juliet)->timeline.size(), 1u);
  EXPECT_EQ(kept.find(with_juliet)->timeline[0].id, "$event1");
  EXPECT_EQ(kept.find(with_juliet)->timeline[0].delivery, delivery_t{delivery::sent{}});
  // The echo first, then the answer: one message, not two.
  kept.apply(change::message_added{said("txn2", "again", true)});
  kept.apply(change::message_added{said("$event2", "again", true)});
  kept.apply(change::message_acknowledged{with_juliet, "txn2", "$event2"});
  EXPECT_EQ(kept.find(with_juliet)->timeline.size(), 2u);
  EXPECT_EQ(kept.find(with_juliet)->timeline[1].id, "$event2");
}

TEST(Mailbox, CrossesThreads) {
  std::atomic<int> notified = 0;
  mailbox box([&] { ++notified; });
  std::thread network([&] {
    for (int i = 0; i < 100; ++i)
      box.push(change::typing_changed{with_juliet, {}});
  });
  network.join();
  EXPECT_EQ(box.take().size(), 100u);
  EXPECT_TRUE(box.take().empty());
  EXPECT_EQ(notified.load(), 100);
}

}  // namespace
