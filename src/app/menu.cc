// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.menu: a message's menu, as tdesktop's -- the message it is up for,
// and what is chosen from it: a reaction, Reply, Edit, Copy, Copy Message
// Link, Save As, Delete -- and a message swiped to be answered.
export module mux.app.menu;

import std;
import skiff.scene;
import mux.core;
import mux.ui;
import mux.app.network;
import mux.app.requests;
import mux.app.services;
import mux.app.outbox;
import mux.app.pictures;
import mux.logic.messages;

export namespace mux::app {

class menu_part {
 public:
  menu_part(services& shared, outbox_part& outbox, pictures_part& pictures)
      : s_(&shared), outbox_(&outbox), pictures_(&pictures) {}

  void apply(const request::message_menu& one) {
    target_ = one;
    s_->root().open_menu(one);
    // The menu takes the keys, as tdesktop's: the arrows go through it,
    // Enter does what is lit, Esc closes it. Nothing lit until an arrow.
    if (skiff::scene::Node* card = s_->root().menu_card())
      s_->scene->focus(*card);
  }
  void apply(const request::close_menu&) { s_->root().close_menu(); }

  // Swiped to the left: answered, as the menu's Reply does.
  void apply(const request::reply_to& one) {
    target_.id = one.id;
    target_.text = one.text;
    this->apply(request::menu_reply{});
  }
  // As tdesktop's: "Reply to <name>" over a line of the message.
  void apply(const request::menu_reply&) {
    s_->root().close_menu();
    std::string title = "Reply";
    const message* said = nullptr;
    if (const auto& chosen = s_->root().main().chosen)
      if (const conversation* chat = s_->model->find(*chosen))
        if (const auto it = std::ranges::find(chat->timeline, target_.id, &message::id); it != chat->timeline.end()) {
          said = &*it;
          title = "Reply to " + mux::ui::sender_name(*chat, it->sender);
        }
    outbox_->answer(target_.id, std::move(title), logic::reply_line(said, target_.text));
  }
  void apply(const request::menu_edit&) {
    s_->root().close_menu();
    outbox_->edit(target_.id, target_.text);
  }
  // What is selected in it, or all of it.
  void apply(const request::menu_copy&) {
    s_->root().close_menu();
    skiff::scene::setClipboardText(target_.copied.empty() ? target_.text : target_.copied);
  }
  void apply(const request::menu_copy_link&) {
    s_->root().close_menu();
    skiff::scene::setClipboardText(target_.link);
  }
  void apply(const request::menu_save&) {
    s_->root().close_menu();
    if (target_.media)
      pictures_->save(*target_.media, target_.media_name.empty() ? std::string("image") : target_.media_name);
  }
  void apply(const request::menu_delete&) {
    s_->root().close_menu();
    const auto& chosen = s_->root().main().chosen;
    if (!chosen)
      return;
    if (s_->demo())
      s_->box->push(change_t{change::message_redacted{*chosen, target_.id}});
    else
      s_->net->remove_message(*chosen, target_.id);
  }

  // A reaction: the user's own put where it is not, taken back where it is
  // -- shown at once, and told to the server.
  void apply(const request::menu_react& one) {
    s_->root().close_menu();
    this->apply(request::react{target_.id, one.key});
  }
  void apply(const request::react& one) {
    const auto& chosen = s_->root().main().chosen;
    const conversation* chat = chosen ? s_->model->find(*chosen) : nullptr;
    if (!chat)
      return;
    const auto said = std::ranges::find(chat->timeline, one.id, &message::id);
    if (said == chat->timeline.end())
      return;
    const std::string& me = chosen->account.address;
    const bool on = logic::reaction_turns_on(*said, one.key, me);
    s_->model->apply(change_t{change::reaction_changed{*chosen, one.id, one.key, me, on}});
    if (!s_->demo())
      s_->net->react(*chosen, one.id, one.key, on);
    s_->refresh();
  }

 private:
  services* s_;
  outbox_part* outbox_;
  pictures_part* pictures_;
  request::message_menu target_;
};

}  // namespace mux::app
