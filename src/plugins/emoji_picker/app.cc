// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.emoji_picker.app -- The emoji panel's part of the program:
// opened from the chat's input or the thread's, the chat's own custom emoji
// and stickers in it, and an emoji picked put where the caret is in the
// field it was opened from; docked under a finger.
export module mux.plugin.emoji_picker.app;

import std;
import splice;
import mux.composition;
import mux.core;
import mux.ui;
import mux.protocols;
import mux.app.services;
import mux.plugin.emoji_picker.types;
import mux.plugin.emoji_picker.ui;

export namespace mux::plugins::emoji_picker {

namespace request = ::mux::ui::request;

class part {
 public:
  explicit part(app::services& shared) : s_(&shared) {}

  // A page chosen: shown; a plugin's, asked of its part as itself -- the
  // part does what opening it takes. The client's own asks nothing.
  template <class Asked>
  void apply(const select_emoji_page& one, const Asked& asked) {
    bool chosen = false;
    ui::change_shown<std::optional<emoji_facts>>(*s_->showing, [&](auto& now) {
      if (!now || !page_available(*now, one.page)) return;
      now->page = one.page;
      chosen = true;
    });
    if (chosen)
      spl::visit(spl::overloaded{[](popup_page::emoji) {},
                                 [&](const auto& theirs) { asked(theirs); }},
                 one.page);
  }
  void apply(const request::toggle_emoji& one) {
    if (this->open()) {
      this->close();
      return;
    }
    into_ = request::writing::chat{};
    this->open_at(one.right, one.top);
  }
  void apply(const request::toggle_thread_emoji& one) {
    if (this->open()) {
      this->close();
      return;
    }
    into_ = request::writing::thread{};
    this->open_at(one.right, one.top);
  }
  void apply(const request::close_shown<emoji_facts>&) { this->close(); }
  void apply(const request::insert_emoji& one) {
    // Put in the draft of the field it is for; that field given the keys --
    // not under a finger, where the panel stands where the keyboard would:
    // focused, the field brought the keyboard up over the panel at each emoji.
    const auto focus = [&](ui::keys_target field) {
      if (!s_->by_touch)
        s_->ask_keys(std::move(field));
    };
    const auto put = [&](ui::composer_doc& now) { ui::put_emoji(now, one.text, one.picture); };
    spl::visit(spl::overloaded{[&](request::writing::chat) {
                                 s_->edit_chat_writing(put);
                                 focus(ui::keys::chat{});
                               },
                               [&](request::writing::thread) {
                                 s_->edit_thread_writing(put);
                                 focus(ui::keys::thread{});
                               }},
               into_);
  }
  // Each frame: put away where a message went (services::emoji_close_due);
  // docked under a finger, the room it takes from under the field said.
  void tick() {
    if (std::exchange(s_->emoji_close_due, false))
      this->close();
    s_->ui.set_docked_panel_height(this->open() ? ui::docked_height(*s_->showing->look<ui::window_shape>()) : 0.0f);
  }

 private:
  [[nodiscard]] bool open() const { return s_->showing->look<std::optional<emoji_facts>>()->has_value(); }
  void close() const { ui::show<emoji_facts>(*s_->showing, std::nullopt); }
  void open_at(float right, float top) {
    const auto chosen = s_->managed();
    const conversation* chat = chosen ? s_->model->find(*chosen) : nullptr;
    s_->emoji.custom_emoji = chosen && proto::offers(ui::protocol_state_of(s_->ui, chosen->account), proto::feature::sticker_packs{});
    s_->emoji.pack_account = chosen ? std::optional(chosen->account) : std::nullopt;
    s_->emoji.chat_emotes = chat ? chat->emotes : emote_list{};
    s_->emoji.chat_stickers = chat ? chat->stickers : emote_list{};
    // Under a finger, in place of the on-screen keyboard, as Telegram's: the
    // field let go of, the keyboard goes down; tapping the field again closes
    // the panel and brings the keyboard back.
    if (s_->by_touch)
      s_->scene->clearFocus();
    const auto here = s_->chosen();
    const auto ops = here ? ui::ops_of(s_->ui, here->account) : proto::account_ops{};
    ui::show(*s_->showing, std::optional(emoji_facts{right, top - 6.0f, popup_page::emoji{}, ops.send_sticker,
                                                     here && ui::may_send_files(s_->ui, here->account)}));
  }

  app::services* s_;
  request::writing_t into_ = request::writing::chat{};
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{shared}; }

}  // namespace mux::plugins::emoji_picker
