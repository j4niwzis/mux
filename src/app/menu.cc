// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.menu: a message's menu, as tdesktop's -- the message it is up for,
// and what is chosen from it: a reaction, Reply, Edit, Copy, Copy Message
// Link, Save As, Delete -- and a message swiped to be answered.
export module mux.app.menu;

import std;
import skiff.scene;
import mux.core;
import mux.proto;
import mux.ui;
import mux.app.network;
import mux.app.requests;
import mux.app.services;
import mux.app.outbox;
import mux.app.pictures;
import mux.logic.messages;

export namespace mux::app {

struct menu_part {
  services* s_;
  outbox_part* outbox_;
  pictures_part* pictures_;
  request::message_menu target_;
  std::set<std::string> selected_;
  std::optional<conversation_id> selected_chat_;
  std::optional<std::pair<conversation_id, std::vector<std::string>>> forwarding_;
  std::optional<std::pair<conversation_id, std::vector<std::string>>> deleting_;
};

void part_apply(menu_part& self, const request::message_menu& one);
void part_apply(menu_part& self, const request::close_menu&);
void part_apply(menu_part& self, const request::reply_to& one);
void part_apply(menu_part& self, const request::menu_quote_reply&);
void part_apply(menu_part& self, const request::menu_reply&);
void part_apply(menu_part& self, const request::menu_edit&);
void part_apply(menu_part& self, const request::menu_copy&);
void part_apply(menu_part& self, const request::menu_copy_link&);
void part_apply(menu_part& self, const request::menu_fave_sticker&);
void part_apply(menu_part& self, const request::menu_copy_url&);
void part_apply(menu_part& self, const request::menu_copy_image&);
void part_apply(menu_part& self, const request::menu_thread&);
void part_apply(menu_part& self, const request::menu_save&);
void part_apply(menu_part& self, const request::menu_pin&);
void part_apply(menu_part& self, const request::menu_reactions&);
void part_apply(menu_part& self, const request::close_reactions&);
void part_apply(menu_part& self, const request::close_edit_history&);
void part_apply(menu_part& self, const request::menu_forward&);
void forward_from(menu_part& self, const conversation_id& chosen, std::vector<std::string> events);
void part_apply(menu_part& self, const request::close_forward&);
void part_apply(menu_part& self, const request::menu_view_source&);
void part_apply(menu_part& self, const request::menu_select&);
void part_apply(menu_part& self, const request::toggle_selected& one);
void part_apply(menu_part& self, const request::selection_cancel&);
void part_apply(menu_part& self, const request::selection_span&);
void part_apply(menu_part& self, const request::selection_copy&);
void part_apply(menu_part& self, const request::selection_delete&);
void part_apply(menu_part& self, const request::selection_delete_confirm&);
void part_apply(menu_part& self, const request::selection_forward&);
void keep_selection(menu_part& self);
void part_apply(menu_part& self, const request::menu_edit_history&);
void part_apply(menu_part& self, const request::forward_to& one);
void part_apply(menu_part& self, const request::menu_save_gif&);
void part_apply(menu_part& self, const request::menu_delete&);
void part_apply(menu_part& self, const request::menu_react& one);
void part_apply(menu_part& self, const request::react& one);
[[nodiscard]] std::vector<const message*> selected_messages(const menu_part& self);
void show_selection(menu_part& self);

void part_apply(menu_part& self, const request::message_menu& one) {
  self.target_ = one;
  if (self.selected_chat_ == self.s_->root().main().chosen && self.selected_.contains(one.id)) {
    skiff::scene::textMenusAsked().clear();
    const auto chosen = selected_messages(self);
    const conversation* chat = self.selected_chat_ ? self.s_->model->find(*self.selected_chat_) : nullptr;
    if (chat && !chosen.empty()) {
      const auto now = mux::ui::protocol_state_of(self.s_->ui, chat->id.account);
      self.s_->root().show_selection_menu(chosen.size(), mux::ui::ops_of(self.s_->ui, chat->id.account).forward,
          std::ranges::any_of(chosen, [&](const message* item) { return proto::may_delete(now, *chat, item->outgoing); }));
      return;
    }
  }
  const auto& chosen = self.s_->root().main().chosen;
  const conversation* chat = chosen ? self.s_->model->find(*chosen) : nullptr;
  self.s_->emoji.pack_account = chosen ? std::optional(chosen->account) : std::nullopt;
  self.s_->emoji.chat_emotes = chat ? chat->emotes : std::vector<emote>{};
  mux::ui::show(*self.s_->showing, std::optional(one));
  // The menu takes the keys, as tdesktop's: the arrows go through it,
  // Enter does what is lit, Esc closes it. Nothing lit until an arrow.
  // Focused once it is made, as what is shown is read.
  self.s_->menu_focus_due = true;
}

void part_apply(menu_part& self, const request::close_menu&) { mux::ui::show<mux::ui::menu_facts>(*self.s_->showing, std::nullopt); }

void part_apply(menu_part& self, const request::reply_to& one) {
  self.target_.id = one.id;
  self.target_.text = one.text;
  part_apply(self, request::menu_reply{});
}

void part_apply(menu_part& self, const request::menu_quote_reply&) {
  const std::string selected = self.target_.selection ? self.target_.copied : std::string();
  part_apply(self, request::menu_reply{});
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
  // Into the field that answers it: the thread's, where it is answered there.
  auto& screen = self.s_->root().main();
  if (screen.parts.threads.answering == self.target_.id) {
    auto& field = screen.parts.threads.parts.line.parts.input.parts.field;
    field.setText(quote + std::string(field.text()));
    return;
  }
  auto& line = screen.line;
  line.set_text(quote + std::string(line.text()));
}

void part_apply(menu_part& self, const request::menu_reply&) {
  mux::ui::show<mux::ui::menu_facts>(*self.s_->showing, std::nullopt);
  std::string title = "Reply";
  const message* said = nullptr;
  std::string line;
  if (const auto& chosen = self.s_->root().main().chosen)
    if (const conversation* chat = self.s_->model->find(*chosen))
      if (const message* it = mux::ui::held_message(*chat, self.target_.id)) {
        said = &*it;
        title = "Reply to " + mux::ui::sender_name(*chat, it->sender);
        // Its mentions by name, as the quote in the bubble shows them.
        if (!it->body.plain.empty())
          line = mux::ui::quote_line_of(*it, *chat, self.s_->model);
      }
  // A thread's root or answer, its thread open: answered there.
  if (said) {
    const std::string root = said->thread ? *said->thread : said->id;
    if (self.s_->root().main().answer_in_thread(
            root, self.target_.id,
            mux::ui::compose_context{mux::ui::icon::reply{}, title, line.empty() ? logic::reply_line(said, self.target_.text) : line}))
      return;
  }
  self.outbox_->answer(self.target_.id, std::move(title), line.empty() ? logic::reply_line(said, self.target_.text) : line);
}

void part_apply(menu_part& self, const request::menu_edit&) {
  mux::ui::show<mux::ui::menu_facts>(*self.s_->showing, std::nullopt);
  // A picture with no caption says its file's name: nothing to edit then.
  self.outbox_->edit(self.target_.id, self.target_.captioned && self.target_.text == self.target_.media_name ? std::string() : self.target_.text);
}

void part_apply(menu_part& self, const request::menu_copy&) {
  mux::ui::show<mux::ui::menu_facts>(*self.s_->showing, std::nullopt);
  skiff::scene::setClipboardText(self.target_.copied.empty() ? self.target_.text : self.target_.copied);
}

void part_apply(menu_part& self, const request::menu_copy_link&) {
  mux::ui::show<mux::ui::menu_facts>(*self.s_->showing, std::nullopt);
  skiff::scene::setClipboardText(self.target_.link);
}

void part_apply(menu_part& self, const request::menu_fave_sticker&) {
  mux::ui::show<mux::ui::menu_facts>(*self.s_->showing, std::nullopt);
  if (self.target_.sticker)
    self.s_->emoji.flip_favourite(*self.target_.sticker);
}

void part_apply(menu_part& self, const request::menu_copy_url&) {
  mux::ui::show<mux::ui::menu_facts>(*self.s_->showing, std::nullopt);
  skiff::scene::setClipboardText(self.target_.pressed_link);
}

void part_apply(menu_part& self, const request::menu_copy_image&) {
  mux::ui::show<mux::ui::menu_facts>(*self.s_->showing, std::nullopt);
  if (self.target_.picture)
    self.pictures_->copy(*self.target_.picture);
}

void part_apply(menu_part& self, const request::menu_thread&) {
  mux::ui::show<mux::ui::menu_facts>(*self.s_->showing, std::nullopt);
  auto& screen = self.s_->root().main();
  if (!screen.chosen)
    return;
  self.s_->open_thread(self.target_.id);
  if (!self.s_->demo())
    self.s_->net->load_thread(*screen.chosen, self.target_.id);
  self.s_->refresh_due = true;
}

void part_apply(menu_part& self, const request::menu_save&) {
  mux::ui::show<mux::ui::menu_facts>(*self.s_->showing, std::nullopt);
  if (self.target_.media)
    self.pictures_->save(*self.target_.media, self.target_.media_name.empty() ? std::string("image") : self.target_.media_name);
}

void part_apply(menu_part& self, const request::menu_pin&) {
  mux::ui::show<mux::ui::menu_facts>(*self.s_->showing, std::nullopt);
  const auto& chosen = self.s_->root().main().chosen;
  if (!chosen || self.s_->demo())
    return;
  self.s_->net->pin(*chosen, self.target_.id, !self.target_.pinned);
}

void part_apply(menu_part& self, const request::menu_reactions&) {
  mux::ui::show<mux::ui::menu_facts>(*self.s_->showing, std::nullopt);
  const auto& chosen = self.s_->root().main().chosen;
  const conversation* chat = chosen ? self.s_->model->find(*chosen) : nullptr;
  if (!chat)
    return;
  const message* said = mux::ui::held_message(*chat, self.target_.id);
  if (!said)
    return;
  auto events = said->reaction_events;
  std::ranges::stable_sort(events, {}, &message::reaction_event::at);
  std::vector<mux::ui::reaction_entry> entries;
  for (const auto& one : events)
    entries.push_back(
        {one.event, one.who, mux::ui::sender_name(*chat, one.who), one.key, one.at, one.who == chat->id.account.address,
         self.target_.id});
  // Those whose reaction came without its event -- read back from the
  // history -- listed too, at the message's time.
  for (const auto& [key, who] : said->reactions)
    for (const std::string& user : who)
      if (std::ranges::none_of(events, [&](const auto& one) { return one.key == key && one.who == user; }))
        entries.push_back({std::string(), user, mux::ui::sender_name(*chat, user), key, said->at,
                           user == chat->id.account.address, self.target_.id});
  mux::ui::show(*self.s_->showing, std::optional(mux::ui::reactions_facts{chat->id, std::move(entries), &*self.s_->model}));
}

void part_apply(menu_part& self, const request::close_reactions&) { mux::ui::show<mux::ui::reactions_facts>(*self.s_->showing, std::nullopt); }

void part_apply(menu_part& self, const request::close_edit_history&) { mux::ui::show<mux::ui::history_facts>(*self.s_->showing, std::nullopt); }

void part_apply(menu_part& self, const request::menu_forward&) {
  mux::ui::show<mux::ui::menu_facts>(*self.s_->showing, std::nullopt);
  const auto& chosen = self.s_->root().main().chosen;
  if (!chosen)
    return;
  forward_from(self, *chosen, {self.target_.id});
}

void forward_from(menu_part& self, const conversation_id& chosen, std::vector<std::string> events) {
  self.forwarding_ = std::pair{chosen, std::move(events)};
  std::vector<mux::ui::forward_target> chats;
  for (const auto& [id, account] : self.s_->model->accounts())
    if (id == chosen.account)
      for (const auto& [key, one] : account.conversations)
        chats.push_back({one.id, mux::ui::display_name(one)});
  std::ranges::sort(chats, {}, &mux::ui::forward_target::name);
  mux::ui::show(*self.s_->showing, std::optional(mux::ui::forward_facts{std::move(chats)}));
}

void part_apply(menu_part& self, const request::close_forward&) { mux::ui::show<mux::ui::forward_facts>(*self.s_->showing, std::nullopt); }

void part_apply(menu_part& self, const request::menu_view_source&) {
  mux::ui::show<mux::ui::menu_facts>(*self.s_->showing, std::nullopt);
  if (const auto& chosen = self.s_->root().main().chosen; chosen && !self.s_->demo())
    self.s_->net->view_source(*chosen, self.target_.id);
}

void part_apply(menu_part& self, const request::menu_select&) {
  mux::ui::show<mux::ui::menu_facts>(*self.s_->showing, std::nullopt);
  const auto chat = self.s_->root().main().chosen;
  if (self.selected_chat_ != chat)
    self.selected_.clear();
  self.selected_chat_ = chat;
  part_apply(self, request::toggle_selected{self.target_.id});
}

void part_apply(menu_part& self, const request::toggle_selected& one) {
  if (!self.selected_chat_ || self.selected_chat_ != self.s_->root().main().chosen)
    return;
  const conversation* chat = self.s_->model->find(*self.selected_chat_);
  const message* item = chat ? mux::ui::held_message(*chat, one.id) : nullptr;
  if (!item || item->redacted || one.id.empty())
    return;
  if (!self.selected_.erase(one.id) && self.selected_.size() < 100)
    self.selected_.insert(one.id);
  show_selection(self);
}

void part_apply(menu_part& self, const request::selection_span& span) {
if (!self.selected_chat_ || self.selected_chat_ != self.s_->root().main().chosen)
  return;
const conversation* chat = self.s_->model->find(*self.selected_chat_);
if (!chat)
  return;
std::ranges::for_each(span.ids, [&](const std::string& id) {
  const message* one = mux::ui::held_message(*chat, id);
  if (!one || one->redacted || id.empty())
    return;
  if (!span.selected)
    self.selected_.erase(id);
  else if (self.selected_.size() < 100)
    self.selected_.insert(id);
});
show_selection(self);
}

void part_apply(menu_part& self, const request::selection_cancel&) {
  self.s_->root().close_text_menu();
  self.selected_.clear();
  show_selection(self);
}

void part_apply(menu_part& self, const request::selection_copy&) {
  self.s_->root().close_text_menu();
  const std::string text = std::ranges::to<std::string>(std::views::join_with(std::views::transform(selected_messages(self), [](const message* one) {
    return !one->body.plain.empty() ? one->body.plain : one->attachment ? one->attachment->name : std::string();
  }), std::string("\n\n")));
  skiff::scene::setClipboardText(text);
  self.selected_.clear();
  show_selection(self);
}

void part_apply(menu_part& self, const request::selection_delete&) {
  self.s_->root().close_text_menu();
  const conversation* chat = self.selected_chat_ ? self.s_->model->find(*self.selected_chat_) : nullptr;
  auto chosen = selected_messages(self);
  if (!chat || chosen.empty())
    return;
  const auto now = mux::ui::protocol_state_of(self.s_->ui, chat->id.account);
  if (!proto::available(now)) return;
  std::erase_if(chosen, [&](const message* one) { return !proto::may_delete(now, *chat, one->outgoing); });
  if (chosen.empty()) return;
  self.deleting_ = std::pair{chat->id, std::ranges::to<std::vector>(std::views::transform(chosen, [](const message* one) { return one->id; }))};
  mux::ui::show(*self.s_->showing, std::optional(mux::ui::notice_facts{
      "Delete messages?", std::format("Delete {} selected messages for everyone? This cannot be undone.", chosen.size()), true}));
}

void part_apply(menu_part& self, const request::selection_delete_confirm&) {
  mux::ui::show<mux::ui::notice_facts>(*self.s_->showing, std::nullopt);
  const auto pending = std::exchange(self.deleting_, std::nullopt);
  if (!pending)
    return;
  const conversation* chat = self.s_->model->find(pending->first);
  if (!chat)
    return;
  const auto now = mux::ui::protocol_state_of(self.s_->ui, chat->id.account);
  if (!proto::available(now))
    return;
  std::ranges::for_each(pending->second, [&](const std::string& id) {
    const message* one = mux::ui::held_message(*chat, id);
    if (!one || one->redacted || !proto::may_delete(now, *chat, one->outgoing))
      return;
    if (self.s_->demo())
      self.s_->box->push(change_t{change::message_redacted{chat->id, id}});
    else
      self.s_->net->remove_message(chat->id, id);
    if (self.selected_chat_ == pending->first) self.selected_.erase(id);
  });
  if (self.selected_chat_ == pending->first) show_selection(self);
}

void part_apply(menu_part& self, const request::selection_forward&) {
  self.s_->root().close_text_menu();
  if (!self.selected_chat_ || !mux::ui::ops_of(self.s_->ui, self.selected_chat_->account).forward || selected_messages(self).empty())
    return;
  forward_from(self, *self.selected_chat_, std::ranges::to<std::vector>(std::views::transform(selected_messages(self), [](const message* one) { return one->id; })));
  self.selected_.clear();
  show_selection(self);
}

void keep_selection(menu_part& self) {
  if (self.selected_.empty())
    return;
  if (self.s_->root().main().chosen != self.selected_chat_) {
    self.selected_.clear();
    show_selection(self);
    return;
  }
  const conversation* chat = self.s_->model->find(*self.selected_chat_);
  (void)std::erase_if(self.selected_, [&](const std::string& id) {
    const message* one = chat ? mux::ui::held_message(*chat, id) : nullptr;
    return !one || one->redacted;
  });
  // The room's permissions and account capabilities can change while the
  // selection stays the same. Refresh its actions too; equal facts do not
  // change the shown model's revision.
  show_selection(self);
}

void part_apply(menu_part& self, const request::menu_edit_history&) {
  mux::ui::show<mux::ui::menu_facts>(*self.s_->showing, std::nullopt);
  const auto& chosen = self.s_->root().main().chosen;
  const mux::conversation* chat = chosen ? self.s_->model->find(*chosen) : nullptr;
  if (!chat)
    return;
  const auto in_threads = std::views::join(std::views::values(chat->threads));
  const auto is_it = [&](const mux::message& one) { return one.id == self.target_.id; };
  const mux::message* found = nullptr;
  if (const auto at = std::ranges::find_if(chat->timeline, is_it); at != chat->timeline.end())
    found = &*at;
  else if (const auto there = std::ranges::find_if(in_threads, is_it); there != std::ranges::end(in_threads))
    found = &*there;
  if (!found || found->versions.empty())
    return;
  mux::ui::show(*self.s_->showing, std::optional(mux::ui::history_facts{chat->id, *found, self.s_->model}));
}

void part_apply(menu_part& self, const request::forward_to& one) {
  mux::ui::show<mux::ui::forward_facts>(*self.s_->showing, std::nullopt);
  if (!self.forwarding_ || self.s_->demo())
    return;
  const auto [from, events] = *std::exchange(self.forwarding_, std::nullopt);
  for (const std::string& event : events)
    self.s_->net->forward(from, event, one.to);
  self.s_->notice("Forward", "Forwarded to " + [&] {
    const conversation* to = self.s_->model->find(one.to);
    return to ? mux::ui::display_name(*to) : one.to.id;
  }());
}

void part_apply(menu_part& self, const request::menu_save_gif&) {
  mux::ui::show<mux::ui::menu_facts>(*self.s_->showing, std::nullopt);
  if (self.target_.media)
    self.pictures_->save_gif(*self.target_.media);
}

void part_apply(menu_part& self, const request::menu_delete&) {
  mux::ui::show<mux::ui::menu_facts>(*self.s_->showing, std::nullopt);
  const auto& chosen = self.s_->root().main().chosen;
  if (!chosen)
    return;
  if (self.s_->demo())
    self.s_->box->push(change_t{change::message_redacted{*chosen, self.target_.id}});
  else
    self.s_->net->remove_message(*chosen, self.target_.id);
}

void part_apply(menu_part& self, const request::menu_react& one) {
  mux::ui::show<mux::ui::menu_facts>(*self.s_->showing, std::nullopt);
  part_apply(self, request::react{self.target_.id, one.key});
}

void part_apply(menu_part& self, const request::react& one) {
  const auto& chosen = self.s_->root().main().chosen;
  const conversation* chat = chosen ? self.s_->model->find(*chosen) : nullptr;
  if (!chat)
    return;
  const message* said = mux::ui::held_message(*chat, one.id);
  if (!said)
    return;
  const std::string& me = chosen->account.address;
  const bool on = logic::reaction_turns_on(*said, one.key, me);
  self.s_->model->apply(change_t{change::reaction_changed{*chosen, one.id, one.key, me, on}});
  if (!self.s_->demo())
    self.s_->net->react(*chosen, one.id, one.key, on);
  self.s_->refresh_due = true;
}

[[nodiscard]] std::vector<const message*> selected_messages(const menu_part& self) {
  const conversation* chat = self.selected_chat_ ? self.s_->model->find(*self.selected_chat_) : nullptr;
  if (!chat)
    return {};
  auto all = std::views::transform(self.selected_, [&](const std::string& id) { return mux::ui::held_message(*chat, id); });
  auto chosen = std::ranges::to<std::vector>(std::views::filter(all, [](const message* one) { return one && !one->redacted; }));
  std::ranges::sort(chosen, [](const message* a, const message* b) { return std::tie(a->at, a->id) < std::tie(b->at, b->id); });
  return chosen;
}

void show_selection(menu_part& self) {
  const conversation* chat = self.selected_chat_ ? self.s_->model->find(*self.selected_chat_) : nullptr;
  const auto ops = chat ? mux::ui::ops_of(self.s_->ui, chat->id.account) : mux::proto::account_ops{};
  const auto chosen = selected_messages(self);
  const auto now = chat ? mux::ui::protocol_state_of(self.s_->ui, chat->id.account) : mux::protocol_state_t{};
  const bool available = chat && proto::available(now);
  const bool deletable = available && std::ranges::any_of(chosen, [&](const message* one) { return proto::may_delete(now, *chat, one->outgoing); });
  const mux::ui::selection_shown shown{chosen.size(), available && ops.forward, deletable};
  if (*self.s_->showing->look<mux::ui::selection_shown>() != shown)
    mux::ui::show(*self.s_->showing, shown);
  self.s_->root().main().show_selection(self.selected_);
}

}  // namespace mux::app
