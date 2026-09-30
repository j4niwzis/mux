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
    const auto& chosen = s_->root().main().chosen;
    const conversation* chat = chosen ? s_->model->find(*chosen) : nullptr;
    mux::ui::chat_emotes() = chat ? chat->emotes : std::vector<emote>{};
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
  // Quote & Reply, as Telegram's: the message answered, and what is
  // selected of it quoted at the start of the field -- each of its lines
  // after "> ", as Markdown quotes -- the rest of what was written after.
  void apply(const request::menu_quote_reply&) {
    const std::string selected = target_.selection ? target_.copied : std::string();
    this->apply(request::menu_reply{});
    if (selected.empty())
      return;
    std::string quote;
    for (std::size_t at = 0; at <= selected.size();) {
      const std::size_t end = std::min(selected.find('\n', at), selected.size());
      quote += "> ";
      quote += std::string_view(selected).substr(at, end - at);
      quote += '\n';
      at = end + 1;
    }
    quote += '\n';
    auto& line = s_->root().main().line;
    line.set_text(quote + std::string(line.text()));
  }
  // As tdesktop's: "Reply to <name>" over a line of the message.
  void apply(const request::menu_reply&) {
    s_->root().close_menu();
    std::string title = "Reply";
    const message* said = nullptr;
    std::string line;
    if (const auto& chosen = s_->root().main().chosen)
      if (const conversation* chat = s_->model->find(*chosen))
        if (const auto it = std::ranges::find(chat->timeline, target_.id, &message::id); it != chat->timeline.end()) {
          said = &*it;
          title = "Reply to " + mux::ui::sender_name(*chat, it->sender);
          // Its mentions by name, as the quote in the bubble shows them.
          if (!it->body.plain.empty())
            line = mux::ui::quote_line_of(*it, *chat, s_->model);
        }
    outbox_->answer(target_.id, std::move(title), line.empty() ? logic::reply_line(said, target_.text) : line);
  }
  void apply(const request::menu_edit&) {
    s_->root().close_menu();
    // A picture with no caption says its file's name: nothing to edit then.
    outbox_->edit(target_.id, target_.captioned && target_.text == target_.media_name ? std::string() : target_.text);
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
  void apply(const request::menu_copy_image&) {
    s_->root().close_menu();
    if (target_.picture)
      pictures_->copy(*target_.picture);
  }
  void apply(const request::menu_save&) {
    s_->root().close_menu();
    if (target_.media)
      pictures_->save(*target_.media, target_.media_name.empty() ? std::string("image") : target_.media_name);
  }
  // Pinned in its chat, or unpinned where it is: the room's list, set.
  void apply(const request::menu_pin&) {
    s_->root().close_menu();
    const auto& chosen = s_->root().main().chosen;
    if (!chosen || s_->demo())
      return;
    s_->net->pin(*chosen, target_.id, !target_.pinned);
  }
  // The message's reactions as the events they are, newest last.
  void apply(const request::menu_reactions&) {
    s_->root().close_menu();
    const auto& chosen = s_->root().main().chosen;
    const conversation* chat = chosen ? s_->model->find(*chosen) : nullptr;
    if (!chat)
      return;
    const auto said = std::ranges::find(chat->timeline, target_.id, &message::id);
    if (said == chat->timeline.end())
      return;
    auto events = said->reaction_events;
    std::ranges::stable_sort(events, {}, &message::reaction_event::at);
    std::vector<mux::ui::reaction_entry> entries;
    for (const auto& one : events)
      entries.push_back(
          {one.event, one.who, mux::ui::sender_name(*chat, one.who), one.key, one.at, one.who == chat->id.account.address});
    // Those whose reaction came without its event -- read back from the
    // history -- listed too, at the message's time.
    for (const auto& [key, who] : said->reactions)
      for (const std::string& user : who)
        if (std::ranges::none_of(events, [&](const auto& one) { return one.key == key && one.who == user; }))
          entries.push_back({std::string(), user, mux::ui::sender_name(*chat, user), key, said->at,
                             user == chat->id.account.address});
    s_->root().open_reactions(*chat, entries, &*s_->model);
  }
  void apply(const request::close_reactions&) { s_->root().close_reactions(); }
  // Forward: the chats of the account, to choose where; then sent there.
  void apply(const request::menu_forward&) {
    s_->root().close_menu();
    const auto& chosen = s_->root().main().chosen;
    if (!chosen)
      return;
    forwarding_ = std::pair{*chosen, target_.id};
    std::vector<mux::ui::forward_target> chats;
    for (const auto& [id, account] : s_->model->accounts())
      if (id == chosen->account)
        for (const auto& [key, one] : account.conversations)
          chats.push_back({one.id, mux::ui::display_name(one)});
    std::ranges::sort(chats, {}, &mux::ui::forward_target::name);
    s_->root().open_forward(chats);
  }
  void apply(const request::close_forward&) { s_->root().close_forward(); }
  // The message as the server has it.
  void apply(const request::menu_view_source&) {
    s_->root().close_menu();
    if (const auto& chosen = s_->root().main().chosen; chosen && !s_->demo())
      s_->net->view_source(*chosen, target_.id);
  }
  void apply(const request::forward_to& one) {
    s_->root().close_forward();
    if (!forwarding_ || s_->demo())
      return;
    const auto [from, event] = *std::exchange(forwarding_, std::nullopt);
    s_->net->forward(from, event, one.to);
    s_->root().show_message("Forward", "Forwarded to " + [&] {
      const conversation* to = s_->model->find(one.to);
      return to ? mux::ui::display_name(*to) : one.to.id;
    }());
  }
  // A GIF kept among the saved ones, for the input's GIF tab.
  void apply(const request::menu_save_gif&) {
    s_->root().close_menu();
    if (target_.media)
      pictures_->save_gif(*target_.media);
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
    s_->refresh_due = true;
  }

 private:
  // The message being forwarded, and the chat it is in, until a chat is
  // chosen to forward it to.
  std::optional<std::pair<conversation_id, std::string>> forwarding_;
  services* s_;
  outbox_part* outbox_;
  pictures_part* pictures_;
  request::message_menu target_;
};

}  // namespace mux::app
