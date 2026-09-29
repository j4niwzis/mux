// SPDX-License-Identifier: AGPL-3.0-only
// The program's pure functions: values in, values out, no window, no network.
import std;
import gtest;
import mux.core;
import mux.logic.text;
import mux.logic.search;
import mux.logic.reading;
import mux.logic.drafts;

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

TEST(Text, FoldsLatinGreekAndCyrillic) {
  EXPECT_EQ(logic::folded("Hello"), "hello");
  EXPECT_EQ(logic::folded("ПРИВЕТ Ёж"), "привет ёж");
  EXPECT_EQ(logic::folded("ΣΩΜΑ"), "σωμα");
  EXPECT_EQ(logic::folded("日本"), "日本");
}

TEST(Text, AnAddressSaysItsProtocol) {
  EXPECT_EQ(logic::account_of("@me:example.org"), (account_id{protocol::matrix{}, "@me:example.org"}));
  EXPECT_EQ(logic::account_of("me@example.org"), (account_id{protocol::xmpp{}, "me@example.org"}));
}

TEST(Search, FindsInAnyCaseNewestFirst) {
  std::map<std::string, message> all;
  all.emplace("a", said("a", "Meet at the Café", false, 1));
  all.emplace("b", said("b", "nothing here", false, 2));
  all.emplace("c", said("c", "the café is closed", false, 3));
  auto gone = said("d", "café", false, 4);
  gone.redacted = true;
  all.emplace("d", gone);
  EXPECT_EQ(logic::found_in(all, "CAFÉ"), (std::vector<std::string>{"c", "a"}));
  EXPECT_TRUE(logic::found_in(all, "").empty());
}

TEST(Search, StepsHeldAtTheEnds) {
  EXPECT_EQ(logic::stepped(std::nullopt, 3, true), 0u);
  EXPECT_EQ(logic::stepped(0, 3, true), 1u);
  EXPECT_EQ(logic::stepped(2, 3, true), 2u);
  EXPECT_EQ(logic::stepped(0, 3, false), 0u);
  EXPECT_EQ(logic::stepped(std::nullopt, 0, true), std::nullopt);
}

TEST(Reading, ReadUpToTheNewestFromSomeoneElse) {
  conversation chat;
  chat.timeline = {said("1", "hi", false), said("2", "hello", false), said("3", "mine", true)};
  EXPECT_EQ(logic::to_mark_read(chat), std::optional<std::string>("2"));
  chat.read_up_to = "2";
  EXPECT_EQ(logic::to_mark_read(chat), std::nullopt);
}

TEST(Reading, TypingSaidAtMostEveryTwentySeconds) {
  using clock = std::chrono::steady_clock;
  const auto yes = [](const conversation_id&) { return true; };
  const clock::time_point t0{};
  auto step = logic::typing_after({}, true, with_juliet, t0, yes);
  ASSERT_EQ(step.say.size(), 1u);
  EXPECT_TRUE(std::visit(overloaded{[](const logic::typing_said::started&) { return true; },
                                    [](const logic::typing_said::stopped&) { return false; }},
                         step.say[0]));
  // Still typing a little later: nothing said again.
  auto again = logic::typing_after(step.next, true, with_juliet, t0 + std::chrono::seconds(5), yes);
  EXPECT_TRUE(again.say.empty());
  // Stopped: said.
  auto stop = logic::typing_after(again.next, false, with_juliet, t0 + std::chrono::seconds(6), yes);
  ASSERT_EQ(stop.say.size(), 1u);
  EXPECT_FALSE(stop.next.in.has_value());
  // Not allowed by privacy: nothing said at all.
  const auto no = [](const conversation_id&) { return false; };
  EXPECT_TRUE(logic::typing_after({}, true, with_juliet, t0, no).say.empty());
}

TEST(Drafts, KeptAndReadBack) {
  logic::drafts_t drafts;
  EXPECT_TRUE(logic::keep_draft(drafts, with_juliet, "see you"));
  EXPECT_FALSE(logic::keep_draft(drafts, with_juliet, "see you"));
  const auto read = logic::drafts_from(logic::drafts_text(drafts));
  EXPECT_EQ(read, drafts);
  EXPECT_TRUE(logic::keep_draft(drafts, with_juliet, "   "));
  EXPECT_TRUE(drafts.empty());
}

}  // namespace
