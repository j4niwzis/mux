// SPDX-License-Identifier: AGPL-3.0-only
// The program: accounts, their pages, privacy and proxies.
module mux.app.program;

import std;
import knot;
import skia;
import mux.core;
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
                                     .muted = muted.contains(chat->id),
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
  root().main().line.parts.input.parts.field.insertText(one.text);
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

// Room events, for the chosen account's chats: shown or not from now on,
// whatever every account's is.
void app::apply(const request::flip_account_room_events&) {
  this->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
    auto& kept = mux::config::room_events_in(account);
    kept = !kept.value_or(history.show_room_events);
    if (auto* page = panel.privacy())
      page->show_events(*kept);
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
