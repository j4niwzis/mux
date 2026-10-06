// SPDX-License-Identifier: AGPL-3.0-only
// The program's pure functions: values in, values out, no window, no network.
import std;
import splice;
import gtest;
import mux.core;
import mux.logic.text;
import mux.logic.search;
import mux.logic.reading;
import mux.logic.drafts;
import mux.logic.links;
import mux.logic.messages;
import mux.logic.blurhash;

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

// Read as far as it was seen: the newest from someone else up to what was
// on screen, and never back from what was read.
TEST(Reading, ReadUpToWhatWasSeenOnly) {
  conversation chat;
  chat.timeline = {said("1", "hi", false), said("2", "hello", false), said("3", "mine", true),
                   said("4", "later", false)};
  chat.read_up_to = "1";
  EXPECT_EQ(logic::read_up_to_seen(chat, "3"), std::optional<std::string>("2"));
  EXPECT_EQ(logic::read_up_to_seen(chat, "1"), std::nullopt);
  EXPECT_EQ(logic::read_up_to_seen(chat, "4"), std::optional<std::string>("4"));
  chat.read_up_to = "4";
  EXPECT_EQ(logic::read_up_to_seen(chat, "2"), std::nullopt);
  // A window away from the newest, without what is read: nothing said.
  chat.read_up_to = "9";
  chat.detached = true;
  EXPECT_EQ(logic::read_up_to_seen(chat, "4"), std::nullopt);
}

TEST(Reading, TypingSaidAtMostEveryTwentySeconds) {
  using clock = std::chrono::steady_clock;
  const auto yes = [](const conversation_id&) { return true; };
  const clock::time_point t0{};
  auto step = logic::typing_after({}, true, with_juliet, t0, yes);
  ASSERT_EQ(step.say.size(), 1u);
  EXPECT_TRUE(spl::visit(spl::overloaded{[](const logic::typing_said::started&) { return true; },
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

TEST(Links, ReadIntoWhatTheyPointAt) {
  EXPECT_EQ(logic::link_of("https://matrix.to/#/%23ru4:ed25519.uk"),
            std::optional<logic::link_t>(proto::matrix::link::room{"#ru4:ed25519.uk", std::nullopt, {}}));
  EXPECT_EQ(logic::link_of("https://matrix.to/#/!r:x.org/$e?via=x.org&via=y.org"),
            std::optional<logic::link_t>(proto::matrix::link::room{"!r:x.org", "$e", {"x.org", "y.org"}}));
  EXPECT_EQ(logic::link_of("https://matrix.to/#/@me:x.org"), std::optional<logic::link_t>(proto::matrix::link::person{"@me:x.org"}));
  EXPECT_EQ(logic::link_of("matrix:r/room:x.org/e/abc"),
            std::optional<logic::link_t>(proto::matrix::link::room{"#room:x.org", "$abc", {}}));
  EXPECT_EQ(logic::link_of("matrix:u/me:x.org"), std::optional<logic::link_t>(proto::matrix::link::person{"@me:x.org"}));
  EXPECT_EQ(logic::link_of("xmpp:juliet@example.com"),
            std::optional<logic::link_t>(proto::xmpp::link::address{"juliet@example.com"}));
  EXPECT_EQ(logic::link_of("https://example.com/"), std::nullopt);
  EXPECT_EQ(logic::link_of("matrix:x/whatever"), std::nullopt);
}

TEST(Links, IdsOfEveryRoomVersion) {
  EXPECT_TRUE(proto::matrix::id_shaped("#ru4:ed25519.uk"));
  EXPECT_TRUE(proto::matrix::id_shaped("!OgeJ1T_3F4fAL2o-vE2tfsEopmJ3qZm021t1pJ_J82w"));
  EXPECT_FALSE(proto::matrix::id_shaped("!short"));
  EXPECT_FALSE(proto::matrix::id_shaped("@x"));
}

TEST(Links, ARoomNotJoinedIsJoinedThroughAMatrixAccount) {
  model now;
  const account_id me{protocol::matrix{}, "@me:x.org"};
  now.apply(change::connection_changed{me, connection::online{}});
  const auto step = logic::where_to(now, proto::matrix::link::room{"#new:x.org", std::nullopt, {"x.org"}}, std::nullopt);
  EXPECT_EQ(step, logic::link_step_t(logic::link_step::join{me, "#new:x.org", {"x.org"}}));
}

TEST(Messages, AReactionTogglesTheUsersOwn) {
  auto one = said("1", "hi");
  EXPECT_TRUE(logic::reaction_turns_on(one, "👍", "romeo@example.com"));
  one.reactions["👍"].insert("romeo@example.com");
  EXPECT_FALSE(logic::reaction_turns_on(one, "👍", "romeo@example.com"));
  EXPECT_TRUE(logic::reaction_turns_on(one, "❤️", "romeo@example.com"));
}

TEST(Messages, AReplysLineIsOneLineOrWhatItCarries) {
  EXPECT_EQ(logic::reply_line(nullptr, "two\nlines"), "two lines");
  auto picture = said("1", "");
  picture.attachment = attachment{.kind = attachment_kind::image{}, .name = "cat.jpg"};
  EXPECT_EQ(logic::reply_line(&picture, ""), "Photo");
}

TEST(Blurhash, DecodesAndRefusesWhatIsNotOne) {
  // blurha.sh's own example.
  const auto pixels = logic::blurhash_pixels("LEHV6nWB2yk8pyo0adR*.7kCMdnj", 32, 24);
  ASSERT_TRUE(pixels.has_value());
  EXPECT_EQ(pixels->size(), 32u * 24u * 4u);
  EXPECT_EQ((*pixels)[3], 255);
  EXPECT_FALSE(logic::blurhash_pixels("not a hash", 32, 24).has_value());
  EXPECT_FALSE(logic::blurhash_pixels("LEHV6n", 32, 24).has_value());
}

}  // namespace
