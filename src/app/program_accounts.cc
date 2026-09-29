// SPDX-License-Identifier: AGPL-3.0-only
// The program: accounts, their pages, privacy and proxies.
module mux.app.program;

import std;
import knot;
import skia;
import mux.core;
import mux.logic.links;
import mux.logic.room_events;
import mux.config;
import mux.net;
import mux.xmpp;
import mux.matrix;
import mux.media;
import mux.host;
import mux.ui;
import skiff.paint;
import skiff.scene;
import mux.app.network;
import mux.app.demo;
import mux.app.store;
import mux.app.requests;
import mux.app.words;

namespace mux::app {

// A person's info: a box in the middle of the window, as tdesktop's.
void app::apply(const request::open_member_info& one) {
  const auto& chosen = root().main().chosen;
  if (!chosen)
    return;
  const mux::conversation* in = model->find(*chosen);
  root().open_person(chosen->account, one.id, mux::ui::person_of(in, *model, chosen->account, one.id));
}

void app::apply(const request::close_person_info&) { root().close_person(); }

// The input's emoji panel: opened over the chat above its button, or closed.
void app::apply(const request::toggle_emoji&) {
  if (root().emoji_open()) {
    root().close_emoji();
    return;
  }
  const auto at = root().main().line.parts.input.parts.emoji.bounds();
  const auto& chosen = root().main().chosen;
  const mux::conversation* chat = chosen ? model->find(*chosen) : nullptr;
  mux::ui::chat_emotes() = chat ? chat->emotes : std::vector<mux::emote>{};
  mux::ui::chat_stickers() = chat ? chat->stickers : std::vector<mux::emote>{};
  root().open_emoji(at.fRight, at.fTop - 6.0f);
}
void app::apply(const request::close_emoji&) { root().close_emoji(); }

// A new chat: its box; a direct chat or a group asked of the account whose
// chats are listed -- or, for a direct chat with someone it already has
// one with, that chat shown.
void app::apply(const request::open_new_chat&) { root().open_new_chat(); }
void app::apply(const request::close_new_chat&) { root().close_new_chat(); }
void app::apply(const request::start_direct& one) {
  const auto& current = root().main().current;
  if (!current || shared.demo())
    return;
  root().close_new_chat();
  for (const auto& [key, chat] : model->accounts().at(*current).conversations)
    if (!mux::ui::is_group(chat) && mux::ui::contact_of(chat) == one.user) {
      this->open_chat(chat.id, std::nullopt);
      return;
    }
  net->create_direct(*current, one.user);
  root().show_message("New chat", "Starting a chat with " + one.user + "…");
}
std::optional<mux::account_id> app::matrix_account() {
  if (const auto& current = root().main().current; current && mux::is_matrix(current->speaks))
    return current;
  for (const auto& [id, account] : model->accounts())
    if (mux::is_matrix(id.speaks))
      return id;
  return std::nullopt;
}
// Explore rooms: opened on the account's own server.
void app::apply(const request::open_explore&) {
  const auto by = this->matrix_account();
  const std::string own = by ? by->address.substr(by->address.find(':') + 1) : std::string();
  root().open_explore(own);
}
void app::apply(const request::close_explore&) { root().close_explore(); }
// A search: an address typed in is gone to, as a link to it would be --
// its card, or the room where joined; else the directory asked.
void app::apply(const request::search_rooms& one) {
  if (auto link = mux::logic::matrix_id_of(one.query)) {
    root().close_explore();
    this->follow(*link);
    return;
  }
  const auto by = this->matrix_account();
  if (!by || shared.demo())
    return;
  net->search_directory(*by, one.server, one.query);
}
// A room of the directory joined, through the server it was listed by, and
// opened when it comes.
void app::apply(const request::join_directory_room& one) {
  const auto by = this->matrix_account();
  if (!by || shared.demo())
    return;
  std::vector<std::string> via;
  if (!one.server.empty())
    via.push_back(one.server);
  joining = mux::logic::link::room{one.room, std::nullopt, via};
  net->join(*by, one.room, via);
  root().close_explore();
}
// A room made, and opened once the model has it.
void app::apply(const request::create_room& one) {
  const auto by = this->matrix_account();
  if (!by || shared.demo())
    return;
  root().close_new_chat();
  std::string alias = one.alias;
  if (alias.starts_with('#'))
    alias = alias.substr(1, alias.find(':') == std::string::npos ? std::string::npos : alias.find(':') - 1);
  net->create_room(*by, one.name, one.topic, one.open, one.open ? alias : std::string());
  root().show_message("New room", "Making " + one.name + "\u2026");
}
void app::apply(const request::start_group& one) {
  const auto& current = root().main().current;
  if (!current || shared.demo())
    return;
  root().close_new_chat();
  net->create_group(*current, one.name);
  root().show_message("New group", "Making " + one.name + "…");
}

// The room's management: made from what the model knows of it now.
void app::apply(const request::open_manage&) {
  const auto& chosen = root().main().chosen;
  const mux::conversation* chat = chosen ? model->find(*chosen) : nullptr;
  if (!chat)
    return;
  const auto level_of = [&](const std::string& user) {
    const auto found = chat->powers.find(user);
    return found == chat->powers.end() ? chat->power_default : found->second;
  };
  mux::ui::room_settings_facts facts{.id = chat->id.id,
                                     .name = chat->name,
                                     .topic = chat->topic.value_or(""),
                                     .alias = chat->alias,
                                     .other_aliases = chat->other_aliases,
                                     .encrypted = chat->encrypted,
                                     .join_rule = chat->join_rule,
                                     .history = chat->history,
                                     .version = chat->version,
                                     .notify_mode = this->notify_mode_of(chat->id),
                                     .events_all = room_events.contains(chat->id)
                                                       ? std::optional<bool>(room_events.at(chat->id))
                                                       : std::nullopt,
                                     .previews = previews_shown_in.contains(chat->id)
                                                     ? std::optional<bool>(previews_shown_in.at(chat->id))
                                                     : std::nullopt,
                                     .receipts = receipts_shown_in.contains(chat->id)
                                                     ? std::optional<bool>(receipts_shown_in.at(chat->id))
                                                     : std::nullopt,
                                     .jump_search = jump_search_in.contains(chat->id)
                                                        ? std::optional<std::int64_t>(jump_search_in.at(chat->id))
                                                        : std::nullopt,
                                     .event_kinds = room_event_kinds.contains(chat->id)
                                                        ? std::optional<mux::config::room_event_kinds>(room_event_kinds.at(chat->id))
                                                        : std::nullopt,
                                     .mine = level_of(chat->id.account.address),
                                     .needs = chat->needs};
  // Element's privileged users: those the power levels name with a level of
  // their own, the highest first.
  for (const auto& [user, level] : chat->powers) {
    if (level == chat->needs.users_default)
      continue;
    const auto member = std::ranges::find(chat->members, user, &mux::member::id);
    facts.privileged.push_back(
        {user, member != chat->members.end() && !member->name.empty() ? member->name : user, level});
  }
  std::ranges::stable_sort(facts.privileged, std::greater{}, &mux::ui::room_settings_facts::person::level);
  root().open_manage(facts);
}
void app::apply(const request::close_manage&) { root().close_manage(); }
// The developer tools, for the chat being read.
void app::apply(const request::explore_state&) {
  const auto& chosen = root().main().chosen;
  if (!chosen || shared.demo())
    return;
  root().close_manage();
  net->list_state(*chosen);
}
void app::apply(const request::open_send_custom&) {
  root().close_manage();
  root().open_send_custom();
}
void app::apply(const request::close_devtools&) { root().close_devtools(); }
void app::apply(const request::send_custom& one) {
  const auto& chosen = root().main().chosen;
  if (!chosen || shared.demo())
    return;
  net->send_custom(*chosen, one.type, one.state_key, one.json);
}
// Done to the room being read, by its account.
void app::apply(const request::room_act& one) {
  const auto& chosen = root().main().chosen;
  if (!chosen || shared.demo())
    return;
  net->manage(*chosen, one.action);
}
// An emoji picked: into what is written, where the caret is; the input keeps
// the keys.
void app::apply(const request::insert_emoji& one) {
  auto& field = root().main().line.parts.input.parts.field;
  // A custom emoji: its picture in the line, as the message will show it,
  // sent as its shortcode.
  if (one.picture.empty())
    field.insertText(one.text);
  else
    field.insertAtom("\u2003", one.picture, one.text, true);
  scene.focus(root().main().line.field);
}

void app::apply(const request::not_implemented& one) { root().show_notice(one.what); }

void app::apply(const request::close_notice&) { root().close_notice(); }

void app::apply(const request::resize_info& one) { root().main().resize_info(one.x); }

void app::apply(const request::choose_new_proxy& one) {
  if (auto* up = root().open_panel())
    std::visit(
        [&](accounts& panel) {
          if (auto* pane = panel.adding())
            pane->set_proxy(one.index);
        },
        *up);
}

void app::apply(const request::toggle_mute&) {
  auto& screen = root().main();
  if (!screen.chosen)
    return;
  if (!muted.erase(*screen.chosen))
    muted.insert(*screen.chosen);
  (void)this->write();
  this->refresh();
}

void app::apply(const request::close_account_pages&) {
  if (auto* up = root().open_panel())
    std::visit([](accounts& panel) { panel.close_pages(); }, *up);
}

void app::apply(const request::accounts_back&) {
  auto* up = root().open_panel();
  if (!up)
    return;
  std::visit(
      [this](accounts& panel) {
        if (panel.pages_open())
          panel.close_pages();
        else
          this->apply(request::pop_panel{});
      },
      *up);
}

void app::apply(const request::account_page& one) {
  this->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
    panel.show_page(one.page, account, *model, proxies);
  });
}

void app::apply(const request::flip_account_receipts&) {
  this->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
    auto& kept = mux::config::read_receipts_in(account);
    kept = !kept.value_or(true);
    if (auto* page = panel.privacy())
      page->show(*kept, mux::config::send_typing_of(account));
    (void)this->write();
  });
}

void app::apply(const request::flip_account_typing&) {
  this->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
    auto& kept = mux::config::send_typing_in(account);
    kept = !kept.value_or(true);
    if (auto* page = panel.privacy())
      page->show(mux::config::read_receipts_of(account), *kept);
    (void)this->write();
  });
}

// Notifications: the page, its switches, what shows them; an account's and
// a chat's own.
void app::apply(const request::settings_notifications&) {
  if (auto* up = root().settings_up())
    up->show_notifications(notifications);
}
void app::apply(const request::flip_notify& one) {
  bool& flag = mux::config::flag_in(notifications, one.flag);
  flag = !flag;
  (void)this->write();
  if (auto* up = root().settings_up())
    if (auto* page = up->notifications())
      page->show(notifications);
}
void app::apply(const request::set_notify_backend& one) {
  notifications.backend = mux::config::word_of(one.backend);
  (void)this->write();
  if (auto* up = root().settings_up())
    if (auto* page = up->notifications())
      page->show(notifications);
}
void app::apply(const request::flip_account_notify&) {
  this->with_chosen_account([&](accounts&, mux::config::account_t& account) {
    auto& kept = mux::config::notify_in(account);
    kept = !kept.value_or(notifications.desktop);
    (void)this->write();
  });
}
void app::apply(const request::flip_account_notify_sound&) {
  this->with_chosen_account([&](accounts&, mux::config::account_t& account) {
    auto& kept = mux::config::notify_sound_in(account);
    kept = !kept.value_or(notifications.sound);
    (void)this->write();
  });
}
void app::apply(const request::set_chat_notify& one) {
  const auto& chosen = root().main().chosen;
  if (!chosen)
    return;
  notify_modes.erase(*chosen);
  muted.erase(*chosen);
  std::visit(mux::overloaded{[&](mux::config::notify_mode::off) { muted.insert(*chosen); },
                             [&](mux::config::notify_mode::by_default) {},
                             [&](const auto& own) { notify_modes.insert_or_assign(*chosen, own); }},
             one.mode);
  (void)this->write();
  this->refresh();
}

// Which room events show, as chosen at a level: all of them, or one kind --
// none said, as the level under says.
void app::apply(const request::set_room_event_kind& one) {
  const auto set_kind = [&](std::optional<mux::config::room_event_kinds>& kinds) {
    if (!kinds)
      kinds.emplace();
    mux::logic::choice_in(*kinds, *one.kind) = one.show;
  };
  std::visit(mux::overloaded{[&](mux::choice_level::everywhere) {
                               if (one.kind)
                                 set_kind(history.room_event_kinds);
                               else
                                 history.show_room_events = one.show.value_or(true);
                             },
                             [&](mux::choice_level::account) {
                               this->with_chosen_account([&](accounts&, mux::config::account_t& account) {
                                 if (one.kind)
                                   set_kind(mux::config::room_event_kinds_in(account));
                                 else
                                   mux::config::room_events_in(account) = one.show;
                               });
                             },
                             [&](mux::choice_level::chat) {
                               const auto& chosen = root().main().chosen;
                               if (!chosen)
                                 return;
                               if (one.kind)
                                 mux::logic::choice_in(room_event_kinds[*chosen], *one.kind) = one.show;
                               else if (one.show)
                                 room_events.insert_or_assign(*chosen, *one.show);
                               else
                                 room_events.erase(*chosen);
                             }},
             one.level);
  (void)this->write();
  this->refresh();
}

// How far a jump's search pages back, at a level.
void app::apply(const request::set_jump_search& one) {
  std::visit(mux::overloaded{[&](mux::choice_level::everywhere) { history.jump_search = one.most.value_or(5000); },
                             [&](mux::choice_level::account) {
                               this->with_chosen_account([&](accounts&, mux::config::account_t& account) {
                                 mux::config::jump_search_in(account) = one.most;
                               });
                             },
                             [&](mux::choice_level::chat) {
                               const auto& chosen = root().main().chosen;
                               if (!chosen)
                                 return;
                               if (one.most)
                                 jump_search_in.insert_or_assign(*chosen, *one.most);
                               else
                                 jump_search_in.erase(*chosen);
                             }},
             one.level);
  (void)this->write();
  this->refresh();
}

// Link previews, at a level.
void app::apply(const request::set_link_previews& one) {
  std::visit(mux::overloaded{[&](mux::choice_level::everywhere) { history.link_previews = one.show.value_or(true); },
                             [&](mux::choice_level::account) {
                               this->with_chosen_account([&](accounts&, mux::config::account_t& account) {
                                 mux::config::link_previews_in(account) = one.show;
                               });
                             },
                             [&](mux::choice_level::chat) {
                               const auto& chosen = root().main().chosen;
                               if (!chosen)
                                 return;
                               if (one.show)
                                 previews_shown_in.insert_or_assign(*chosen, *one.show);
                               else
                                 previews_shown_in.erase(*chosen);
                             }},
             one.level);
  (void)this->write();
  this->refresh();
}

// Who has read up to where, as faces, at a level.
void app::apply(const request::set_receipts_shown& one) {
  std::visit(mux::overloaded{[&](mux::choice_level::everywhere) { history.show_receipts = one.show.value_or(false); },
                             [&](mux::choice_level::account) {
                               this->with_chosen_account([&](accounts&, mux::config::account_t& account) {
                                 mux::config::show_receipts_in(account) = one.show;
                               });
                             },
                             [&](mux::choice_level::chat) {
                               const auto& chosen = root().main().chosen;
                               if (!chosen)
                                 return;
                               if (one.show)
                                 receipts_shown_in.insert_or_assign(*chosen, *one.show);
                               else
                                 receipts_shown_in.erase(*chosen);
                             }},
             one.level);
  (void)this->write();
  this->refresh();
}

// Room events, for the chosen account's chats: shown or not from now on,
// whatever every account's is.
void app::apply(const request::flip_account_room_events&) {
  this->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
    auto& kept = mux::config::room_events_in(account);
    kept = !kept.value_or(history.show_room_events);

    (void)this->write();
    this->refresh();
  });
}
// Room events, for the chat being read, whatever its account's are.
void app::apply(const request::flip_chat_room_events&) {
  const auto& chosen = root().main().chosen;
  if (!chosen)
    return;
  const bool now = this->room_events_shown(*chosen);
  room_events.insert_or_assign(*chosen, !now);
  (void)this->write();
  root().show_message("Room events", !now ? "Joins, renames and other room events are shown in this chat."
                                          : "Room events are hidden in this chat.");
  this->refresh();
}

void app::apply(const request::proxy_kind& one) {
  if (auto* up = root().settings_up())
    if (auto* editor = up->editor())
      editor->set_kind(one.kind);
}

void app::apply(const request::choose_account_proxy& one) {
  this->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
    auto& kept = mux::config::proxy_in(account);
    if (one.index < 0 || static_cast<std::size_t>(one.index) >= proxies.size())
      kept.reset();
    else
      kept = proxies[static_cast<std::size_t>(one.index)].name;
    (void)this->write();
    this->reconnect(account);
    panel.show_page(2, account, *model, proxies);
  });
}

void app::reconnect(const mux::config::account_t& account) {
  if (!mux::config::enabled_of(account) || ask.demo)
    return;
  net->remove(mux::config::address_of(account));
  net->add(account, proxies);
}

void app::reconnect_through(const std::string& name) {
  for (const auto& one : saved)
    if (mux::config::proxy_of(one) == name)
      this->reconnect(one);
}

}  // namespace mux::app
