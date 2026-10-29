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
import mux.composition;
import mux.plugins.types;

export namespace mux::app {

struct menu_part {
  services* s_;
  outbox_part* outbox_;
  pictures_part* pictures_;
  request::message_menu target_;
  std::set<std::string> selected_;
  std::optional<conversation_id> selected_chat_;
  std::optional<std::pair<conversation_id, std::vector<std::string>>> deleting_;

  // A plugin's action on the messages selected (contract::selection_actions):
  // they are handed to its part, and the selection let go.
  template <class Feature, class Asked>
  void apply(const request::selection_asked<Feature>&, const Asked& asked);
};

void part_apply(menu_part& self, const request::message_menu& one);
void part_apply(menu_part& self, const request::close_menu&);
void part_apply(menu_part& self, const request::menu_pressed_off&);
void part_apply(menu_part& self, const request::reply_to& one);
void part_apply(menu_part& self, const request::menu_quote_reply&);
void part_apply(menu_part& self, const request::menu_reply&);
void part_apply(menu_part& self, const request::menu_edit&);
void part_apply(menu_part& self, const request::menu_copy&);
void part_apply(menu_part& self, const request::menu_copy_link&);
void part_apply(menu_part& self, const request::menu_copy_url&);
void part_apply(menu_part& self, const request::menu_copy_image&);
void part_apply(menu_part& self, const request::menu_thread&);
void part_apply(menu_part& self, const request::menu_save&);
void part_apply(menu_part& self, const request::menu_select&);
void part_apply(menu_part& self, const request::toggle_selected& one);
void part_apply(menu_part& self, const request::selection_cancel&);
void part_apply(menu_part& self, const request::selection_span&);
void part_apply(menu_part& self, const request::selection_copy&);
void part_apply(menu_part& self, const request::selection_delete&);
void part_apply(menu_part& self, const request::selection_delete_confirm&);
void keep_selection(menu_part& self);
void part_apply(menu_part& self, const request::menu_delete&);
void part_apply(menu_part& self, const request::menu_react& one);
void part_apply(menu_part& self, const request::react& one);
[[nodiscard]] std::vector<const message*> selected_messages(const menu_part& self);
void show_selection(menu_part& self);
[[nodiscard]] std::uint64_t offered_on(const menu_part& self, const conversation_id& chat);

void part_apply(menu_part& self, const request::message_menu& one) {
  self.target_ = one;
  if (self.selected_chat_ == self.s_->chosen() && self.selected_.contains(one.id)) {
    skiff::scene::textMenusAsked().clear();
    const auto chosen = selected_messages(self);
    const conversation* chat = self.selected_chat_ ? self.s_->model->find(*self.selected_chat_) : nullptr;
    if (chat && !chosen.empty()) {
      const auto now = mux::ui::protocol_state_of(self.s_->ui, chat->id.account);
      self.s_->show_text_menu(mux::ui::selection_menu_items(chosen.size(), offered_on(self, chat->id),
          std::ranges::any_of(chosen, [&](const message* item) { return proto::may_delete(now, *chat, item->outgoing); })));
      return;
    }
  }
  const auto& chosen = self.s_->chosen();
  const conversation* chat = chosen ? self.s_->model->find(*chosen) : nullptr;
  // History and reaction rows may refer to events retained only as typed
  // metadata. Hold the displayed event before acting on its own ID.
  if (chat && one.event && one.event->id == one.id && !mux::ui::held_message(*chat, one.id)) {
    self.s_->model->apply(change::message_added{*one.event, placement::aside{}});
    chat = self.s_->model->find(*chosen);
  }
  self.s_->emoji.pack_account = chosen ? std::optional(chosen->account) : std::nullopt;
  self.s_->emoji.chat_emotes = chat ? chat->emotes : emote_list{};
  // At the press, in the window: the row said where it was pressed in its
  // own space -- the list's, scrolled -- and the menu stood off by as far as
  // the list was scrolled. The window's own note of the press is where it is.
  request::message_menu shown = one;
  shown.x = self.s_->last_press.fX;
  shown.y = self.s_->last_press.fY;
  mux::ui::show(*self.s_->showing, std::optional(std::move(shown)));
  // The menu takes the keys, as tdesktop's: the arrows go through it,
  // Enter does what is lit, Esc closes it. Nothing lit until an arrow.
  // Asked for as it is made: the card takes them as it reads that.
  self.s_->ask_keys(mux::ui::keys::menu{});
}

void part_apply(menu_part& self, const request::close_menu&) { mux::ui::show<mux::ui::menu_facts>(*self.s_->showing, std::nullopt); }
void part_apply(menu_part& self, const request::menu_pressed_off&) {
  part_apply(self, request::close_menu{});
  self.s_->press_again = self.s_->last_press;
}

void part_apply(menu_part& self, const request::reply_to& one) {
  self.target_.id = one.id;
  self.target_.text = one.text;
  part_apply(self, request::menu_reply{});
}

void part_apply(menu_part& self, const request::menu_quote_reply&) {
  const auto selected = self.target_.selection ? self.target_.copied_fragment : std::nullopt;
  const auto plain = self.target_.selection ? self.target_.copied : std::string();
  part_apply(self, request::menu_reply{});
  if (plain.empty()) return;
  const auto fragment = selected.value_or(skiff::scene::clipboardFragment(plain, {}));
  // Into what answers it: the thread's draft, where it is answered there.
  const auto quote = [&](mux::ui::composer_doc& now) { mux::ui::prepend_quote(now, fragment); };
  if (self.s_->showing->look<mux::ui::chat_shown>()->thread_answer == self.target_.id) {
    self.s_->edit_thread_writing(quote);
    return;
  }
  self.s_->edit_chat_writing(quote);
}

void part_apply(menu_part& self, const request::menu_reply&) {
  mux::ui::show<mux::ui::menu_facts>(*self.s_->showing, std::nullopt);
  std::string title = "Reply";
  const message* said = nullptr;
  std::string line;
  if (const auto& chosen = self.s_->chosen())
    if (const conversation* chat = self.s_->model->find(*chosen))
      if (const message* it = mux::ui::held_message(*chat, self.target_.id)) {
        // Deleted, or not on the server: nothing to answer -- a swipe on
        // one asks all the same.
        if (!mux::ui::answerable(*it))
          return;
        said = &*it;
        title = "Reply to " + mux::ui::sender_name(*chat, it->sender);
        // Its mentions by name, as the quote in the bubble shows them.
        if (!it->body.plain.empty())
          line = mux::ui::quote_line_of(*it, *chat, self.s_->model);
      }
  // A thread's root or answer, its thread open: answered there.
  if (said) {
    const std::string root = said->thread ? *said->thread : said->id;
    if (self.s_->thread_open() == root) {
      self.s_->answer_in_thread(self.target_.id, mux::ui::compose_context{mux::ui::icon::reply{}, title,
                                                                          line.empty() ? logic::reply_line(said, self.target_.text) : line});
      return;
    }
  }
  self.outbox_->answer(self.target_.id, std::move(title), line.empty() ? logic::reply_line(said, self.target_.text) : line);
}

void part_apply(menu_part& self, const request::menu_edit&) {
  mux::ui::show<mux::ui::menu_facts>(*self.s_->showing, std::nullopt);
  // A picture with no caption says its file's name: nothing to edit then.
  const std::string text = self.target_.captioned && self.target_.text == self.target_.media_name ? std::string() : self.target_.text;
  // A message of the thread open -- an answer in it, or its root: edited in
  // the thread's field, where it is written.
  if (const auto& chosen = self.s_->chosen(); chosen && self.s_->thread_open())
    if (const mux::conversation* chat = self.s_->model->find(*chosen))
      if (const mux::message* said = mux::ui::held_message(*chat, self.target_.id);
          said && self.s_->thread_open() == (said->thread ? *said->thread : said->id)) {
        std::string line = text;
        std::ranges::replace(line, '\n', ' ');
        self.s_->edit_in_thread(self.target_.id, mux::ui::compose_context{mux::ui::icon::pencil{}, "Edit message", std::move(line)});
        self.s_->write_thread(mux::ui::plain_draft(text));
        return;
      }
  self.outbox_->edit(self.target_.id, text);
}

void part_apply(menu_part& self, const request::menu_copy&) {
  mux::ui::show<mux::ui::menu_facts>(*self.s_->showing, std::nullopt);
  skiff::scene::setClipboardText(self.target_.copied.empty() ? self.target_.text : self.target_.copied);
}

void part_apply(menu_part& self, const request::menu_copy_link&) {
  mux::ui::show<mux::ui::menu_facts>(*self.s_->showing, std::nullopt);
  skiff::scene::setClipboardText(self.target_.link);
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
  if (!self.s_->chosen())
    return;
  self.s_->open_thread(self.target_.id);
  if (!self.s_->demo())
    self.s_->net->load_thread(*self.s_->chosen(), self.target_.id);
  self.s_->refresh_due = true;
}

void part_apply(menu_part& self, const request::menu_save&) {
  mux::ui::show<mux::ui::menu_facts>(*self.s_->showing, std::nullopt);
  if (self.target_.media)
    self.pictures_->save(*self.target_.media, self.target_.media_name.empty() ? std::string("image") : self.target_.media_name);
}


void part_apply(menu_part& self, const request::menu_select&) {
  mux::ui::show<mux::ui::menu_facts>(*self.s_->showing, std::nullopt);
  const auto chat = self.s_->chosen();
  if (self.selected_chat_ != chat)
    self.selected_.clear();
  self.selected_chat_ = chat;
  part_apply(self, request::toggle_selected{self.target_.id});
}

void part_apply(menu_part& self, const request::toggle_selected& one) {
  if (!self.selected_chat_ || self.selected_chat_ != self.s_->chosen())
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
if (!self.selected_chat_ || self.selected_chat_ != self.s_->chosen())
  return;
const conversation* chat = self.s_->model->find(*self.selected_chat_);
if (!chat)
  return;
for (const std::string& id : span.ids) {
  const message* one = mux::ui::held_message(*chat, id);
  if (!one || one->redacted || id.empty())
    continue;
  if (!span.selected)
    self.selected_.erase(id);
  else if (self.selected_.size() < 100)
    self.selected_.insert(id);
}
show_selection(self);
}

void part_apply(menu_part& self, const request::selection_cancel&) {
  self.s_->close_text_menu();
  self.selected_.clear();
  show_selection(self);
}

void part_apply(menu_part& self, const request::selection_copy&) {
  self.s_->close_text_menu();
  const std::string text = selected_messages(self) | std::views::transform([](const message* one) {
    return !one->body.plain.empty() ? one->body.plain : one->attachment ? one->attachment->name : std::string();
  }) | std::views::join_with(std::string("\n\n")) | std::ranges::to<std::string>();
  skiff::scene::setClipboardText(text);
  self.selected_.clear();
  show_selection(self);
}

void part_apply(menu_part& self, const request::selection_delete&) {
  self.s_->close_text_menu();
  const conversation* chat = self.selected_chat_ ? self.s_->model->find(*self.selected_chat_) : nullptr;
  auto chosen = selected_messages(self);
  if (!chat || chosen.empty())
    return;
  const auto now = mux::ui::protocol_state_of(self.s_->ui, chat->id.account);
  if (!proto::available(now)) return;
  std::erase_if(chosen, [&](const message* one) { return !proto::may_delete(now, *chat, one->outgoing); });
  if (chosen.empty()) return;
  self.deleting_ = std::pair{chat->id, chosen | std::views::transform([](const message* one) { return one->id; }) | std::ranges::to<std::vector>()};
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
  for (const std::string& id : pending->second) {
    const message* one = mux::ui::held_message(*chat, id);
    if (!one || one->redacted || !proto::may_delete(now, *chat, one->outgoing))
      continue;
    if (self.s_->demo())
      self.s_->box->push(change_t{change::message_redacted{chat->id, id}});
    else
      self.s_->net->remove_message(chat->id, id);
    if (self.selected_chat_ == pending->first) self.selected_.erase(id);
  }
  if (self.selected_chat_ == pending->first) show_selection(self);
}

void keep_selection(menu_part& self) {
  if (self.selected_.empty())
    return;
  if (self.s_->chosen() != self.selected_chat_) {
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

void part_apply(menu_part& self, const request::menu_delete&) {
  mux::ui::show<mux::ui::menu_facts>(*self.s_->showing, std::nullopt);
  const auto& chosen = self.s_->chosen();
  if (!chosen)
    return;
  if (self.s_->demo())
    self.s_->box->push(change_t{change::message_redacted{*chosen, self.target_.id}});
  else
    self.s_->net->remove_message(*chosen, self.target_.id);
}

void part_apply(menu_part& self, const request::menu_react& one) {
  mux::ui::show<mux::ui::menu_facts>(*self.s_->showing, std::nullopt);
  part_apply(self, request::react{self.target_.id, one.key, one.shortcode});
}

void part_apply(menu_part& self, const request::react& one) {
  const auto& chosen = self.s_->chosen();
  const conversation* chat = chosen ? self.s_->model->find(*chosen) : nullptr;
  if (!chat)
    return;
  const message* said = mux::ui::held_message(*chat, one.id);
  if (!said || !mux::ui::ops_of(self.s_->ui, chosen->account).react ||
      (!self.s_->demo() && !proto::may_react(mux::ui::protocol_state_of(self.s_->ui, chosen->account), *chat, *said)))
    return;
  const std::string& me = chosen->account.address;
  const bool on = logic::reaction_turns_on(*said, one.key, me);
  change::reaction_changed changed{*chosen, one.id, one.key, me, on};
  changed.shortcode = one.shortcode;
  self.s_->model->apply(change_t{std::move(changed)});
  if (!self.s_->demo())
    self.s_->net->react(*chosen, one.id, one.key, on, one.shortcode);
  self.s_->refresh_due = true;
}

[[nodiscard]] std::vector<const message*> selected_messages(const menu_part& self) {
  const conversation* chat = self.selected_chat_ ? self.s_->model->find(*self.selected_chat_) : nullptr;
  if (!chat)
    return {};
  return mux::logic::selected_messages(*chat, self.selected_);
}

void show_selection(menu_part& self) {
  const conversation* chat = self.selected_chat_ ? self.s_->model->find(*self.selected_chat_) : nullptr;
  const auto chosen = selected_messages(self);
  const auto now = chat ? mux::ui::protocol_state_of(self.s_->ui, chat->id.account) : mux::protocol_state_t{};
  const bool available = chat && proto::available(now);
  const bool deletable = available && std::ranges::any_of(chosen, [&](const message* one) { return proto::may_delete(now, *chat, one->outgoing); });
  const mux::ui::selection_shown shown{chosen.size(), available ? offered_on(self, chat->id) : 0u, deletable};
  if (*self.s_->showing->look<mux::ui::selection_shown>() != shown)
    mux::ui::show(*self.s_->showing, shown);
  // The messages marked as selected: their rows read it, and while any are,
  // a press on one selects it or lets it go.
  if (self.s_->showing->look<mux::ui::message_marks>()->selected != self.selected_)
    mux::ui::change_shown<mux::ui::message_marks>(*self.s_->showing, [&](mux::ui::message_marks& marks) { marks.selected = self.selected_; });
}

// The plugins' actions offered on the messages selected in a chat: a bit
// each, in their order (contract::selection_actions).
template <class... Features>
std::uint64_t offered_of(plugin_list<Features...>, const mux::ui::ui_shared& shared, const conversation_id& chat) {
  std::uint64_t out = 0;
  std::size_t at = 0;
  ((out |= allowed(contract::selection_actions{}, type_tag<Features>{}, shared, chat) ? std::uint64_t{1} << at : 0u, ++at), ...);
  return out;
}
std::uint64_t offered_on(const menu_part& self, const conversation_id& chat) {
  return offered_of(composition::providers<contract::selection_actions, enabled_plugins>{}, self.s_->ui, chat);
}

template <class Feature, class Asked>
void menu_part::apply(const request::selection_asked<Feature>&, const Asked& asked) {
  s_->close_text_menu();
  if (!selected_chat_)
    return;
  auto events = selected_messages(*this) | std::views::transform([](const message* one) { return one->id; }) | std::ranges::to<std::vector>();
  if (events.empty())
    return;
  const conversation_id in = *selected_chat_;
  selected_.clear();
  show_selection(*this);
  asked(request::selection_taken<Feature>{in, std::move(events)});
}

}  // namespace mux::app
