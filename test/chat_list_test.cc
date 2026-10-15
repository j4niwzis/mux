// SPDX-License-Identifier: AGPL-3.0-only
// Chat-list presentation stays stable when its scene tree is rebuilt.
import std;
import mux.core;
import mux.ui;
import gtest;

#include "gtest/gtest-macros.h"

namespace {
mux::conversation chat(std::string id) {
  mux::conversation out;
  out.id.id = std::move(id);
  out.kind = mux::conversation_kind::group{};
  out.members.push_back({.id = "@alice:example.org", .name = "Alice"});
  mux::message last;
  last.sender = "@alice:example.org";
  last.body.plain = "hello\nworld";
  out.timeline.push_back(std::move(last));
  return out;
}
}

TEST(ChatList, DraftAndInviteOverridePreviewWithoutLosingTimeOrTopic) {
  auto room = chat("precedence");
  room.forum_topic = "General";
  mux::ui::conversation_view view;
  view.invite = mux::invite_info{.from = "@bob:example.org", .from_name = "Bob"};
  view.draft = "draft\ntext";
  auto preview = mux::ui::preview_of(room, view, {});
  EXPECT_EQ(preview.sender, "Draft:");
  EXPECT_EQ(preview.text, "draft text");
  EXPECT_TRUE(preview.draft);
  EXPECT_FALSE(preview.time.empty());
  ASSERT_TRUE(preview.topic);
  EXPECT_EQ(*preview.topic, "General");
  view.chosen = true;
  preview = mux::ui::preview_of(room, view, {});
  EXPECT_EQ(preview.sender, "Invite:");
  EXPECT_EQ(preview.text, "from Bob");
  EXPECT_FALSE(preview.draft);
  view.invite.reset();
  preview = mux::ui::preview_of(room, view, {});
  EXPECT_EQ(preview.sender, "Alice:");
  EXPECT_EQ(preview.text, "hello world");
  room.timeline.back().outgoing = true;
  EXPECT_EQ(mux::ui::preview_of(room, view, {}).sender, "You:");
}

TEST(ChatList, DirectMessagesHaveNoSenderPrefixAndEmptyForumsHaveNoTopicLine) {
  auto room = chat("direct");
  room.kind = mux::conversation_kind::direct{};
  EXPECT_TRUE(mux::ui::preview_of(room, {}, {}).sender.empty());
  room.forum_topic = "General";
  room.timeline.clear();
  const auto preview = mux::ui::preview_of(room, {}, {});
  EXPECT_FALSE(preview.topic);
  EXPECT_TRUE(preview.time.empty());
  EXPECT_TRUE(preview.text.empty());
  EXPECT_EQ(mux::ui::conversation_height(room), mux::ui::kConversationRowHeight);
}

TEST(ChatList, MediaCaptionsAndKindsKeepTheirChatListLabels) {
  mux::message last;
  last.attachment = mux::attachment{.kind = mux::attachment_kind::image{}, .name = "photo.png"};
  last.body.plain = "photo.png";
  EXPECT_EQ(mux::ui::media_line(last), "\U0001F5BC Photo");
  last.body.plain = "A caption";
  EXPECT_EQ(mux::ui::media_line(last), "\U0001F5BC A caption");
  last.attachment->kind = mux::attachment_kind::image{.moves = true};
  last.body.plain = last.attachment->name;
  EXPECT_EQ(mux::ui::media_line(last), "\U0001F39E GIF");
  last.attachment->video = "video";
  EXPECT_EQ(mux::ui::media_line(last), "\U0001F3A5 Video");
  last.attachment = mux::attachment{.name = "voice.ogg", .mimetype = "audio/ogg"};
  last.body.plain = "voice.ogg";
  EXPECT_EQ(mux::ui::media_line(last), "\U0001F3A4 Voice message");
  last.attachment = mux::attachment{.name = "song.mp3", .mimetype = "audio/mpeg"};
  EXPECT_EQ(mux::ui::media_line(last), "\U0001F3B5 song.mp3");
  last.attachment = mux::attachment{};
  EXPECT_EQ(mux::ui::media_line(last), "\U0001F4CE File");
  last.attachment.reset();
  EXPECT_FALSE(mux::ui::media_line(last));
}

TEST(ChatList, HtmlPreviewKeepsMentionPillsAndDiscardsOtherLinks) {
  auto room = chat("mentions");
  room.timeline.back().body.html =
      R"(<a href="https://matrix.to/#/@alice:example.org">Alice</a> <a href="https://example.org">site</a>)";
  auto preview = mux::ui::preview_of(room, {}, {});
  EXPECT_NE(preview.text.find("Alice"), std::string::npos);
  EXPECT_NE(preview.text.find("site"), std::string::npos);
  ASSERT_EQ(preview.mentions.size(), 1u);
  EXPECT_TRUE(preview.mentions.front().pill);
  EXPECT_EQ(preview.mentions.front().target, "https://matrix.to/#/@alice:example.org");
  mux::ui::conversation_view draft;
  draft.draft = "replaced";
  EXPECT_TRUE(mux::ui::preview_of(room, draft, {}).mentions.empty());
}
