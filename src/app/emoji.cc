// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.emoji: the emoji panel -- opened from the chat's input or the
// thread's, the chat's own custom emoji and stickers in it, and an emoji
// picked put where the caret is in the field it was opened from. A part of
// the program: it owns which field that is, and reaches the rest through
// the services.
export module mux.app.emoji;

import std;
import splice;
import mux.core;
import mux.ui;
import mux.ui.proto;
import mux.app.services;
import mux.app.requests;

export namespace mux::app {

struct emoji_part {
  services* s_;
  request::writing_t into_ = request::writing::chat{};
};

void part_apply(emoji_part& self, const request::toggle_emoji&);
void part_apply(emoji_part& self, const request::toggle_thread_emoji&);
void part_apply(emoji_part& self, const request::close_emoji&);
void part_apply(emoji_part& self, const request::insert_emoji& one);
void open_at(emoji_part& self, float right, float top);

void part_apply(emoji_part& self, const request::toggle_emoji&) {
  if (self.s_->emoji_open()) {
    self.s_->close_emoji();
    return;
  }
  self.into_ = request::writing::chat{};
  const auto at = self.s_->root().main().line.parts.input.parts.emoji.bounds();
  open_at(self, at.fRight, at.fTop);
}

void part_apply(emoji_part& self, const request::toggle_thread_emoji&) {
  if (self.s_->emoji_open()) {
    self.s_->close_emoji();
    return;
  }
  self.into_ = request::writing::thread{};
  const auto at = self.s_->root().main().parts.threads.parts.line.parts.input.parts.emoji.bounds();
  open_at(self, at.fRight, at.fTop);
}

void part_apply(emoji_part& self, const request::close_emoji&) { self.s_->close_emoji(); }

void part_apply(emoji_part& self, const request::insert_emoji& one) {
  auto& screen = self.s_->root().main();
  // A custom emoji: its picture in the line, as the message will show it,
  // sent as its shortcode.
  const auto put = [&](auto& field) {
    if (one.picture.empty())
      field.insertText(one.text);
    else
      field.insertAtom("\u2003", one.picture, one.text, true);
    // Under a finger the panel stands where the keyboard would: focused,
    // the field brought the keyboard up over the panel at each emoji.
    if (!self.s_->by_touch)
      self.s_->scene->focus(field);
  };
  spl::visit(spl::overloaded{[&](request::writing::chat) { put(screen.line.field); },
                                   [&](request::writing::thread) { put(screen.parts.threads.parts.line.parts.input.parts.field); }},
                self.into_);
}

void open_at(emoji_part& self, float right, float top) {
  const auto chosen = self.s_->managed();
  const mux::conversation* chat = chosen ? self.s_->model->find(*chosen) : nullptr;
  self.s_->emoji.pack_account = chosen ? std::optional(chosen->account) : std::nullopt;
  self.s_->emoji.chat_emotes = chat ? chat->emotes : std::vector<mux::emote>{};
  self.s_->emoji.chat_stickers = chat ? chat->stickers : std::vector<mux::emote>{};
  // Under a finger, in place of the on-screen keyboard, as Telegram's: the
  // field let go of, the keyboard goes down; tapping the field again closes
  // the panel and brings the keyboard back.
  if (self.s_->by_touch)
    self.s_->scene->clearFocus();
  self.s_->open_emoji(right, top - 6.0f);
}

}  // namespace mux::app
