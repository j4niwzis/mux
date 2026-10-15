// SPDX-License-Identifier: AGPL-3.0-only
// The composed field follows its models without being told which bars to show.
import std;
import skia;
import skiff.paint;
import skiff.bind;
import mux.core;
import mux.ui.base;
import mux.ui.messages;
import skiff.model;
import gtest;

#include "gtest/gtest-macros.h"

namespace {
struct composer_state {
  skia::SkFont font;
  mux::ui::palette colours;
  mux::ui::ui_shared shared;
  mux::ui::ui_needs needs{.colours = &colours, .shared = &shared};
  struct shown_root {
    skiff::model::Tracked<mux::ui::chat_shown> chat;
    skiff::model::Tracked<mux::ui::chat_compose> composing;
  };
  skiff::model::Model<shown_root, skiff::bind::NoReactions> shown{shown_root{}};
  void show(auto part) {
    using Part = decltype(part);
    shown.apply(skiff::model::over<Part>(skiff::model::setTo(std::move(part))));
  }
  mux::model chats;
  composer_state() { skiff::paint::defaultFont() = &font; }
  ~composer_state() { skiff::paint::defaultFont() = nullptr; }
  void choose(const mux::conversation_id& chat) {
    show(mux::ui::chat_shown{.chosen = chat});
  }
  void refresh(auto& composer) {
    skiff::bind::refresh(composer, shown);
    skiff::bind::refresh(composer, chats.chats());
  }
};

TEST(ComposerModel, FailedMessagesFollowTheChosenChatAndTheirRemoval) {
  composer_state state;
  const mux::account_id first{mux::protocol::xmpp{}, "alice@example.org"};
  const mux::account_id second{mux::protocol::xmpp{}, "bob@example.org"};
  const mux::conversation_id one{first, "room@example.org"};
  const mux::conversation_id other{second, "room@example.org"};
  state.chats.apply(mux::change::conversation_updated{.id = one});
  state.chats.apply(mux::change::conversation_updated{.id = other});
  state.chats.apply(mux::change::message_added{mux::message{
      .in = one, .id = "unsent", .outgoing = true, .delivery = mux::delivery::failed{}}});
  state.choose(one);
  auto composer = mux::ui::composer_bar<>(state.needs);
  state.refresh(composer);
  auto& unsent = std::get<2>(composer.fParts);
  EXPECT_TRUE(unsent.visible());
  // The same room name in another account must not see the first's failure.
  state.choose(other);
  state.refresh(composer);
  EXPECT_FALSE(unsent.visible());
  state.choose(one);
  state.refresh(composer);
  EXPECT_TRUE(unsent.visible());
  state.chats.apply(mux::change::message_discarded{one, "unsent"});
  state.refresh(composer);
  EXPECT_FALSE(unsent.visible());
}

TEST(ComposerModel, ReplyContextReadsTheShownModelAndKeepsTheDraft) {
  composer_state state;
  const mux::account_id account{mux::protocol::xmpp{}, "alice@example.org"};
  const mux::conversation_id chat{account, "bob@example.org"};
  state.chats.apply(mux::change::conversation_updated{.id = chat});
  state.choose(chat);
  auto composer = mux::ui::composer_bar<>(state.needs);
  mux::ui::set_composer_text(composer, "draft");
  state.refresh(composer);
  EXPECT_FALSE(mux::ui::composer_answering(composer));
  state.show(mux::ui::chat_compose{mux::ui::compose::reply{"event"},
      mux::ui::compose_context{mux::ui::icon::reply{}, "Reply to Bob", "Earlier message"}});
  state.refresh(composer);
  EXPECT_TRUE(mux::ui::composer_answering(composer));
  EXPECT_EQ(mux::ui::composer_context(composer).parts.lines.parts.title.text(), "Reply to Bob");
  EXPECT_EQ(mux::ui::composer_plain(composer), "draft");
  state.show(mux::ui::chat_compose{});
  state.refresh(composer);
  EXPECT_FALSE(mux::ui::composer_answering(composer));
  EXPECT_EQ(mux::ui::composer_plain(composer), "draft");
}

struct thread_where {
  using cancel = mux::ui::in_chat::cancel;
  using submit = mux::ui::in_chat::submit;
  using attach = mux::ui::in_chat::attach;
  using emoji = mux::ui::in_chat::emoji;
  using send = mux::ui::in_chat::send;
  static constexpr auto placeholder = mux::ui::in_chat::placeholder;
  using reads_chat = std::false_type;
};
TEST(ComposerModel, ThreadContextIsIndependentAndFieldAccessSurvivesAMove) {
  composer_state state;
  auto composer = mux::ui::composer_bar<thread_where>(state.needs);
  mux::ui::set_composer_text(composer, "thread draft");
  mux::ui::show_composer_context(composer,
      mux::ui::compose_context{mux::ui::icon::reply{}, "Thread reply", "Message"});
  auto moved = std::move(composer);
  state.refresh(moved);
  EXPECT_TRUE(mux::ui::composer_answering(moved));
  EXPECT_EQ(mux::ui::composer_plain(moved), "thread draft");
  mux::ui::set_composer_text(moved, "changed after moving");
  EXPECT_EQ(mux::ui::composer_plain(moved), "changed after moving");
  mux::ui::show_composer_context(moved, std::nullopt);
  EXPECT_FALSE(mux::ui::composer_answering(moved));
}
}  // namespace
