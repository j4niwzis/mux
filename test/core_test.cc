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

TEST(Model, SavedReadMarkerSurvivesOlderAndUnknownReceipts) {
  model kept;
  kept.apply(change::conversation_updated{.id = with_juliet});
  kept.apply(change::message_added{said("old", "older")});
  kept.apply(change::message_added{said("new", "newer")});
  kept.read_up_to(with_juliet, "new");
  kept.apply(change::receipts_changed{with_juliet, {{romeo.address, "old"}}});
  EXPECT_EQ(kept.find(with_juliet)->read_up_to, std::optional<std::string>("new"));
  kept.read_up_to(with_juliet, "outside-window");
  kept.apply(change::receipts_changed{with_juliet, {{romeo.address, "old"}}});
  EXPECT_EQ(kept.find(with_juliet)->read_up_to, std::optional<std::string>("outside-window"));
  kept.read_up_to(with_juliet, "old");
  kept.apply(change::receipts_changed{with_juliet, {{romeo.address, "new"}}});
  EXPECT_EQ(kept.find(with_juliet)->read_up_to, std::optional<std::string>("new"));
}

TEST(Model, TypingExpiresAndEmptyUpdateCancelsWake) {
  model kept;
  const auto began = std::chrono::steady_clock::now();
  kept.apply(change::typing_changed{with_juliet, {"juliet@example.com"}});
  EXPECT_FALSE(kept.expire_typing(began + std::chrono::seconds(29)));
  EXPECT_FALSE(kept.find(with_juliet)->typing.empty());
  EXPECT_TRUE(kept.expire_typing(began + std::chrono::seconds(31)));
  EXPECT_TRUE(kept.find(with_juliet)->typing.empty());
  EXPECT_FALSE(std::isfinite(kept.typing_wake_in()));
  kept.apply(change::typing_changed{with_juliet, {"juliet@example.com"}});
  kept.apply(change::typing_changed{with_juliet, {"juliet@example.com"}});
  EXPECT_TRUE(std::isfinite(kept.typing_wake_in()));
  kept.apply(change::typing_changed{with_juliet, {}});
  EXPECT_TRUE(kept.find(with_juliet)->typing.empty());
  EXPECT_FALSE(std::isfinite(kept.typing_wake_in()));
}

TEST(Model, LiveMessageEndsItsSendersTypingButHistoryDoesNot) {
  model kept;
  kept.apply(change::typing_changed{with_juliet, {"juliet@example.com", "nurse@example.com"}});
  kept.apply(change::message_added{said("old", "history"), placement::at_start{}});
  EXPECT_EQ(kept.find(with_juliet)->typing.size(), 2u);
  kept.apply(change::message_added{said("new", "finished typing")});
  EXPECT_EQ(kept.find(with_juliet)->typing, (std::vector<std::string>{"nurse@example.com"}));
}

TEST(Model, LateHistoryAndTimestampCorrectionsKeepChronologicalOrder) {
  model kept;
  const auto timed = [](std::string id, int minutes) {
    auto one = said(std::move(id), "text");
    one.at = std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::minutes(minutes));
    return one;
  };
  kept.apply(change::message_added{timed("0037", 37)});
  kept.apply(change::message_added{timed("0008", 8), placement::in_window{}});
  kept.apply(change::message_added{timed("0038", 38)});
  auto timeline = kept.find(with_juliet)->timeline;
  ASSERT_EQ(timeline.size(), 3u);
  EXPECT_EQ(timeline[0].id, "0008");
  EXPECT_EQ(timeline[1].id, "0037");
  EXPECT_EQ(timeline[2].id, "0038");
  // A live event a minute behind must not break subsequent history insertion.
  kept.apply(change::message_added{timed("0036", 36)});
  kept.apply(change::message_added{timed("0035", 35), placement::at_start{}});
  EXPECT_TRUE(std::ranges::is_sorted(kept.find(with_juliet)->timeline, {}, &message::at));
  // The server corrects a timestamp on an event we already have.
  kept.apply(change::message_added{timed("0037", 7)});
  EXPECT_EQ(kept.find(with_juliet)->timeline.front().id, "0037");
  EXPECT_TRUE(std::ranges::is_sorted(kept.find(with_juliet)->timeline, {}, &message::at));
}

TEST(Model, ConfirmedEchoMovesBeforePendingMessagesByServerTime) {
  model kept;
  auto recent = said("recent", "received");
  recent.at = std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::minutes(37));
  kept.apply(change::message_added{recent});
  auto echo = said("echo", "local", true);
  echo.delivery = delivery::sending{};
  echo.at = std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::minutes(38));
  kept.apply(change::message_added{echo});
  echo.delivery = delivery::sent{};
  echo.at = std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::minutes(8));
  kept.apply(change::message_added{echo});
  EXPECT_EQ(kept.find(with_juliet)->timeline.front().id, "echo");
  EXPECT_TRUE(std::ranges::is_sorted(kept.find(with_juliet)->timeline, {}, &message::at));
}

TEST(Model, AcknowledgingAnOlderLocalEchoRestoresChronologicalOrder) {
  model kept;
  auto received = said("recent", "received");
  received.at = std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::minutes(37));
  kept.apply(change::message_added{received});
  auto local = said("txn", "sent with a slow local clock", true);
  local.delivery = delivery::sending{};
  local.at = std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::minutes(8));
  kept.apply(change::message_added{local});
  EXPECT_EQ(kept.find(with_juliet)->timeline.back().id, "txn");
  kept.apply(change::message_acknowledged{with_juliet, "txn", "confirmed"});
  EXPECT_EQ(kept.find(with_juliet)->timeline.front().id, "confirmed");
  EXPECT_TRUE(std::ranges::is_sorted(kept.find(with_juliet)->timeline, {}, &message::at));
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

  // Deleted, where deleted messages are kept: in its place, all it said
  // kept, marked.
  kept.show_deleted = true;
  kept.apply(change::message_redacted{with_juliet, "1"});
  one = kept.find(with_juliet);
  EXPECT_TRUE(one->timeline[1].redacted);
  EXPECT_EQ(one->timeline[1].body.plain, "hello there");

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

// Deleted, where deleted messages are not kept: gone from the chat; and a
// deleted one read back from the disk is not put back.
TEST(Model, ADeletedMessageGoesWhereNotKept) {
  model kept;
  const conversation_id in{account_id{protocol::xmpp{}, "romeo@example.net"}, "juliet@example.com"};
  message first{.in = in, .id = "1", .body = {"hi", std::nullopt}};
  message second{.in = in, .id = "2", .body = {"there", std::nullopt}};
  kept.apply(change::message_added{first});
  kept.apply(change::message_added{second});
  kept.apply(change::message_redacted{in, "1"});
  const conversation* one = kept.find(in);
  ASSERT_EQ(one->timeline.size(), 1u);
  EXPECT_EQ(one->timeline[0].id, "2");
  message back = first;
  back.redacted = true;
  kept.apply(change::message_added{back});
  EXPECT_EQ(kept.find(in)->timeline.size(), 1u);
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

TEST(Model, DirectMessageReceiptsNeverMoveBackOrLoseTheirTimestamp) {
  model kept;
  kept.apply(change::conversation_updated{.id = with_juliet});
  kept.apply(change::message_added{said("old", "older")});
  kept.apply(change::message_added{said("new", "newer")});
  const auto time = [](int ms) { return std::chrono::sys_time<std::chrono::milliseconds>{std::chrono::milliseconds{ms}}; };
  kept.apply(change::receipts_changed{with_juliet, {{with_juliet.id, "new"}}, {{with_juliet.id, time(200)}}});
  kept.apply(change::receipts_changed{with_juliet, {{with_juliet.id, "old"}}, {{with_juliet.id, time(100)}}});
  EXPECT_EQ(kept.find(with_juliet)->read_by.at(with_juliet.id), "new");
  EXPECT_EQ(kept.find(with_juliet)->receipt_times.at(with_juliet.id), time(200));
  kept.apply(change::receipts_changed{with_juliet, {{with_juliet.id, "outside-window"}}, {{with_juliet.id, time(300)}}});
  kept.apply(change::receipts_changed{with_juliet, {{with_juliet.id, "new"}}, {{with_juliet.id, time(200)}}});
  EXPECT_EQ(kept.find(with_juliet)->read_by.at(with_juliet.id), "outside-window");
  EXPECT_EQ(kept.find(with_juliet)->receipt_times.at(with_juliet.id), time(300));
}
