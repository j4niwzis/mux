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

class emoji_part {
 public:
  explicit emoji_part(services& shared) : s_(&shared) {}
  emoji_part(const emoji_part&) = delete;
  emoji_part& operator=(const emoji_part&) = delete;

  // The input's emoji panel: opened over the chat above its button, or closed.
  void apply(const request::toggle_emoji&) {
    if (s_->root().emoji_open()) {
      s_->root().close_emoji();
      return;
    }
    into_ = request::writing::chat{};
    const auto at = s_->root().main().line.parts.input.parts.emoji.bounds();
    this->open_at(at.fRight, at.fTop);
  }
  // The thread's: the same panel, over its button, writing in its field.
  void apply(const request::toggle_thread_emoji&) {
    if (s_->root().emoji_open()) {
      s_->root().close_emoji();
      return;
    }
    into_ = request::writing::thread{};
    const auto at = s_->root().main().parts.threads.parts.line.parts.input.parts.emoji.bounds();
    this->open_at(at.fRight, at.fTop);
  }
  void apply(const request::close_emoji&) { s_->root().close_emoji(); }
  // An emoji picked: into what is written, where the caret is; the input keeps
  // the keys.
  void apply(const request::insert_emoji& one) {
    auto& screen = s_->root().main();
    // A custom emoji: its picture in the line, as the message will show it,
    // sent as its shortcode.
    const auto put = [&](auto& field) {
      if (one.picture.empty())
        field.insertText(one.text);
      else
        field.insertAtom("\u2003", one.picture, one.text, true);
      // Under a finger the panel stands where the keyboard would: focused,
      // the field brought the keyboard up over the panel at each emoji.
      if (!s_->by_touch)
        s_->scene->focus(field);
    };
    spl::visit(spl::overloaded{[&](request::writing::chat) { put(screen.line.field); },
                                     [&](request::writing::thread) { put(screen.parts.threads.parts.line.parts.input.parts.field); }},
                  into_);
  }

 private:
  // The panel, by its button: the emoji and stickers of the chat it is for.
  void open_at(float right, float top) {
    const auto chosen = s_->managed();
    const mux::conversation* chat = chosen ? s_->model->find(*chosen) : nullptr;
    s_->emoji.chat_emotes = chat ? chat->emotes : std::vector<mux::emote>{};
    s_->emoji.chat_stickers = chat ? chat->stickers : std::vector<mux::emote>{};
    // Under a finger, in place of the on-screen keyboard, as Telegram's: the
    // field let go of, the keyboard goes down; tapping the field again closes
    // the panel and brings the keyboard back.
    if (s_->by_touch)
      s_->scene->clearFocus();
    s_->root().open_emoji(right, top - 6.0f);
  }

  services* s_;
  // Which field the emoji picker writes in, as its button opened it.
  request::writing_t into_ = request::writing::chat{};
};

}  // namespace mux::app
