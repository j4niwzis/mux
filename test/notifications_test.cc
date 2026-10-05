// SPDX-License-Identifier: AGPL-3.0-only
import std;
import mux.core.ids;
import mux.logic.notifications;
import gtest;

#include "gtest/gtest-macros.h"

namespace {
using namespace mux;

const conversation_id with_juliet{{protocol::xmpp{}, "romeo@example.com"}, "juliet@example.com"};

message said(std::string id, std::string text, bool outgoing = false, int minute = 0) {
  return message{.in = with_juliet,
                 .id = std::move(id),
                 .sender = outgoing ? "romeo@example.com" : "juliet@example.com",
                 .at = std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::minutes(minute)),
                 .body = {std::move(text), std::nullopt},
                 .outgoing = outgoing};
}

TEST(Notifications, ReplayedMessagesAreNotNewAfterAReconnect) {
  logic::notification_history history(std::chrono::sys_time<std::chrono::milliseconds>{});
  const auto first = said("one", "already notified", false, 1);
  const auto second = said("two", "also notified", false, 2);
  EXPECT_TRUE(history.first(first));
  EXPECT_TRUE(history.first(second));
  // Restoring the account's saved timeline and then repeating it in sync.
  for (int replay = 0; replay < 2; ++replay) {
    EXPECT_FALSE(history.first(first));
    EXPECT_FALSE(history.first(second));
  }
  EXPECT_TRUE(history.first(said("three", "arrived while disconnected", false, 3)));
  // Server clocks and delivery order can differ; an older timestamp alone
  // does not make an unseen message a replay.
  EXPECT_TRUE(history.first(said("late", "delayed delivery", false, 1)));
}

TEST(Notifications, IdentityIncludesTheRoomAndAccount) {
  logic::notification_history history(std::chrono::sys_time<std::chrono::milliseconds>{});
  auto one = said("same-id", "hello", false, 1);
  EXPECT_TRUE(history.first(one));
  one.in.id = "another-room";
  EXPECT_TRUE(history.first(one));
  one.in.account.address = "another@example.com";
  EXPECT_TRUE(history.first(one));
  EXPECT_FALSE(history.first(one));
}

TEST(Notifications, SuppressedMessagesStayHandledWhenReplayed) {
  logic::notification_history history(std::chrono::sys_time<std::chrono::milliseconds>{});
  auto one = said("read", "read in the focused chat", false, 1);
  // The app consumes eligibility before deciding whether focus/settings
  // suppress the sound or popup; neither should defer it until reconnect.
  EXPECT_TRUE(history.first(one));
  EXPECT_FALSE(history.first(one));
  one.id = "muted";
  EXPECT_TRUE(history.first(one));
  EXPECT_FALSE(history.first(one));
  one.body.plain = "the same event with updated content";
  EXPECT_FALSE(history.first(one));
}

TEST(Notifications, KeepsTheStartupCutoffAndDoesNotMergeMissingIds) {
  logic::notification_history history(std::chrono::sys_time<std::chrono::milliseconds>{std::chrono::minutes(10)});
  EXPECT_FALSE(history.first(said("old", "before startup", false, 8)));
  EXPECT_TRUE(history.first(said("grace", "within clock grace", false, 9)));
  EXPECT_FALSE(history.first(said("mine", "outgoing", true, 11)));
  auto service = said("service", "joined the room", false, 11);
  service.service = true;
  EXPECT_FALSE(history.first(service));
  EXPECT_TRUE(history.first(said("", "without an id", false, 11)));
  EXPECT_TRUE(history.first(said("", "another without an id", false, 11)));
}

}  // namespace
