// SPDX-License-Identifier: AGPL-3.0-only
// The program: chats chosen, read, and their menus.
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

void app::apply(const request::choose& one) {
  // What was being written where the reader was: kept as its draft; and
  // the chat opened's own put back in the field.
  auto& screen = root().main();
  search.chat_chosen(one.which);
  if (screen.chosen && *screen.chosen != one.which) {
    drafts.keep(*screen.chosen, screen.line.text());
    screen.line.set_text(screen.draft_of(one.which));
  } else if (!screen.chosen) {
    screen.line.set_text(screen.draft_of(one.which));
  }
  model->touch(one.which);
  // What was kept of its reads, where the model has nothing newer.
  if (const mux::conversation* chat = model->find(one.which); chat && !ask.demo) {
    auto kept = message_store::read_reads(one.which);
    std::map<std::string, std::string> missing;
    for (auto& [user, event] : kept.read_by)
      if (!chat->read_by.contains(user))
        missing.emplace(user, std::move(event));
    if (!missing.empty())
      model->apply(mux::change_t{mux::change::receipts_changed{one.which, std::move(missing)}});
    if (!chat->read_up_to && kept.me)
      model->read_up_to(one.which, *kept.me);
  }
  // A group opened: all its members, once, where a sync gives only some.
  if (const mux::conversation* chat = model->find(one.which);
      chat && !ask.demo && chat->member_count > static_cast<std::int64_t>(chat->members.size()) &&
      members_fetched.insert(one.which).second)
    net->fetch_members(one.which);
  root().main().chosen = one.which;
  root().main().show(*model);
  reading.mark_read(one.which);
}

void app::apply(const request::leave_chat&) {
  const auto& chosen = root().main().chosen;
  if (!chosen)
    return;
  const mux::conversation* one = model->find(*chosen);
  const bool room = one && mux::ui::is_group(*one);
  std::visit(mux::overloaded{[&](mux::protocol::xmpp) {
                               if (!room) {
                                 root().show_notice("Leaving a direct XMPP chat");
                                 return;
                               }
                               if (!ask.demo)
                                 net->leave(*chosen);
                               root().main().info_open = false;
                             },
                             [&](mux::protocol::matrix) {
                               if (!ask.demo)
                                 net->leave(*chosen);
                               root().main().info_open = false;
                             }},
             chosen->account.speaks);
}

void app::apply(const request::back&) { this->show_conversations(); }

void app::apply(const request::open_accounts&) { (void)this->show_accounts(); }

void app::apply(const request::open_new_account&) { this->show_adding(); }

void app::apply(const request::add_xmpp&) { this->switch_form(&adding::show_xmpp); }

void app::apply(const request::add_matrix&) { this->switch_form(&adding::show_matrix); }

void app::apply(const request::select_account& one) {
  auto* up = root().open_panel();
  if (!up)
    return;
  std::visit(
      [&](accounts& panel) {
        if (const auto found = this->find(one.address); found != saved.end()) {
          pending_login.reset();
          panel.select(*found, *model);
          panel.show(saved, *model);
        }
      },
      *up);
}

void app::apply(const request::toggle_advanced&) {
  if (auto* form = this->xmpp_form_up())
    form->show_advanced(!form->advanced);
}

void app::apply(const request::toggle_plain&) {
  if (auto* form = this->xmpp_form_up())
    form->flip_plain();
}

void app::apply(const request::submit_login&) {
  auto* up = root().open_panel();
  if (!up)
    return;
  std::visit(
      [this](accounts& panel) {
        if (auto* editor = panel.editor())
          std::visit([this](auto& form) { this->edit(form); }, editor->form);
        else if (auto* pane = panel.adding()) {
          new_proxy = pane->proxy;
          std::visit([this](auto& form) { this->add(form); }, pane->form);
        }
      },
      *up);
}

void app::apply(const request::flip_enabled& one) { this->flip_enabled(one.address); }

void app::apply(const request::remove_account& one) { this->remove(one.address); }

void app::apply(const request::open_drawer&) { root().open_drawer(); }

void app::apply(const request::show_account& one) { (void)this->show_account(one.address); }

void app::apply(const request::quit&) { mux::host::request_quit(); }

void app::apply(const request::toggle_info&) { root().main().toggle_info(); }

// To the newest: where the chat is a window away from it, back to the
// newest from the disk first -- live again -- then to its end.
void app::apply(const request::jump_to_end&) {
  auto& screen = root().main();
  if (const mux::conversation* chat = screen.chosen ? model->find(*screen.chosen) : nullptr; chat && chat->detached) {
    const mux::conversation_id in = chat->id;
    model->apply(mux::change_t{mux::change::window_opened{in, std::string(), std::nullopt}});
    for (auto& one : store.older(in, message_store::time_point::max(), 80))
      model->apply(mux::change_t{mux::change::message_added{.message = std::move(one), .where = mux::placement::in_window{}}});
    this->refresh();
  }
  screen.jump_to_end();
}

void app::apply(const request::message_menu& one) {
  menu_target = one;
  root().open_menu(one);
}

void app::apply(const request::close_menu&) { root().close_menu(); }

}  // namespace mux::app
