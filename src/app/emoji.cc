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
import mux.protocols;
import mux.app.services;
import mux.app.requests;

export namespace mux::app {

struct emoji_part {
  services* s_;
  request::writing_t into_ = request::writing::chat{};
  // A page chosen: shown; a plugin's, asked of its part as itself -- the
  // part does what opening it takes. The client's own asks nothing.
  template <class Asked>
  void apply(const request::select_emoji_page& one, const Asked& asked) {
    bool chosen = false;
    mux::ui::change_shown<std::optional<mux::ui::emoji_facts>>(*s_->showing, [&](auto& now) {
      if (!now || !mux::ui::page_available(*now, one.page)) return;
      now->page = one.page;
      chosen = true;
    });
    if (chosen)
      spl::visit(spl::overloaded{[](mux::ui::popup_page::emoji) {},
                                 // A plugin's: its page itself asked, applied by its part.
                                 [&](const auto& theirs) { asked(theirs); }},
                 one.page);
  }

};

void part_apply(emoji_part& self, const request::toggle_emoji& one);
void part_apply(emoji_part& self, const request::toggle_thread_emoji& one);
void part_apply(emoji_part& self, const request::close_emoji&);
void part_apply(emoji_part& self, const request::insert_emoji& one);
void part_apply(emoji_part& self, const request::favourite_emoji& one);
void open_at(emoji_part& self, float right, float top);

void part_apply(emoji_part& self, const request::toggle_emoji& one) {
  if (self.s_->emoji_open()) {
    self.s_->close_emoji();
    return;
  }
  self.into_ = request::writing::chat{};
  open_at(self, one.right, one.top);
}

void part_apply(emoji_part& self, const request::toggle_thread_emoji& one) {
  if (self.s_->emoji_open()) {
    self.s_->close_emoji();
    return;
  }
  self.into_ = request::writing::thread{};
  open_at(self, one.right, one.top);
}

void part_apply(emoji_part& self, const request::close_emoji&) { self.s_->close_emoji(); }

void part_apply(emoji_part& self, const request::favourite_emoji& one) {
  self.s_->emoji.flip_emoji_favourite(one.emoji);
  self.s_->close_text_menu();
  mux::ui::show<mux::ui::menu_facts>(*self.s_->showing, std::nullopt);
}

void part_apply(emoji_part& self, const request::insert_emoji& one) {
  // Put in the draft of the field it is for; that field given the keys --
  // not under a finger, where the panel stands where the keyboard would:
  // focused, the field brought the keyboard up over the panel at each emoji.
  const auto focus = [&](mux::ui::keys_target field) {
    if (!self.s_->by_touch)
      self.s_->ask_keys(std::move(field));
  };
  const auto put = [&](mux::ui::composer_doc& now) { mux::ui::put_emoji(now, one.text, one.picture); };
  spl::visit(spl::overloaded{[&](request::writing::chat) {
                               self.s_->edit_chat_writing(put);
                               focus(mux::ui::keys::chat{});
                             },
                             [&](request::writing::thread) {
                               self.s_->edit_thread_writing(put);
                               focus(mux::ui::keys::thread{});
                             }},
             self.into_);
}

void open_at(emoji_part& self, float right, float top) {
  const auto chosen = self.s_->managed();
  const mux::conversation* chat = chosen ? self.s_->model->find(*chosen) : nullptr;
  self.s_->emoji.custom_emoji = chosen && mux::proto::offers(mux::ui::protocol_state_of(self.s_->ui, chosen->account),
                                                          mux::proto::feature::sticker_packs{});
  self.s_->emoji.pack_account = chosen ? std::optional(chosen->account) : std::nullopt;
  self.s_->emoji.chat_emotes = chat ? chat->emotes : mux::emote_list{};
  self.s_->emoji.chat_stickers = chat ? chat->stickers : mux::emote_list{};
  // Under a finger, in place of the on-screen keyboard, as Telegram's: the
  // field let go of, the keyboard goes down; tapping the field again closes
  // the panel and brings the keyboard back.
  if (self.s_->by_touch)
    self.s_->scene->clearFocus();
  self.s_->open_emoji(right, top - 6.0f);
}

}  // namespace mux::app
