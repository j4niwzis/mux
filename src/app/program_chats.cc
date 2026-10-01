// SPDX-License-Identifier: AGPL-3.0-only
// The program: chats chosen, read, and their menus.
module mux.app.program;

import std;
import splice;
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
  // Its account told which room is read: a sliding sync follows it.
  if (!shared.demo())
    net->follow_room(one.which.account, one.which.id);
  // An invite: its card -- who asked, Accept, Decline -- not a chat.
  if (const mux::conversation* chat = model->find(one.which); chat && chat->invite) {
    const std::string who = chat->invite->from_name.empty() ? chat->invite->from : chat->invite->from_name;
    previewing = room_looked_up{mux::logic::link_step::join{one.which.account, one.which.id, {}}, std::nullopt};
    root().open_room_card(one.which.id, mux::room_preview{.id = one.which.id,
                                                          .name = mux::ui::display_name(*chat),
                                                          .alias = chat->alias.value_or(""),
                                                          .topic = chat->topic.value_or(""),
                                                          .avatar = chat->avatar,
                                                          .note = std::format("Invited by {}", who),
                                                          .invite = true});
    return;
  }
  // Chosen, by a press or a key: no forum left lit as gone to.
  root().main().pointed.reset();
  // A space shown as a forum: its rooms listed in it, as tdesktop opens a
  // forum's topics -- no chat opened.
  if (const mux::conversation* chat = model->find(one.which); chat && chat->space && forums.contains(one.which)) {
    root().main().open_forum(one.which.id);
    return;
  }
  // What was being written where the reader was: kept as its draft; and
  // the chat opened's own put back in the field.
  auto& screen = root().main();
  search.chat_chosen(one.which);
  if (screen.chosen && *screen.chosen != one.which) {
    drafts.keep(*screen.chosen, screen.line.plain());
    screen.line.set_text(screen.draft_of(one.which));
  } else if (!screen.chosen) {
    screen.line.set_text(screen.draft_of(one.which));
  }
  model->touch(one.which);
  // What was kept of its reads, where the model has nothing newer.
  if (model->find(one.which) && !ask.demo)
    work.run([this, which = one.which]() -> workers::done_t {
      auto kept = message_store::read_reads(which);
      return [this, which, kept = std::move(kept)]() mutable {
        const mux::conversation* chat = model->find(which);
        if (!chat)
          return;
        std::map<std::string, std::string> missing;
        for (auto& [user, event] : kept.read_by)
          if (!chat->read_by.contains(user))
            missing.emplace(user, std::move(event));
        if (!missing.empty())
          model->apply(mux::change_t{mux::change::receipts_changed{which, std::move(missing)}});
        if (!chat->read_up_to && kept.me)
          model->read_up_to(which, *kept.me);
        this->refresh();
      };
    });
  // A group opened: all its members, once, where a sync gives only some.
  if (const mux::conversation* chat = model->find(one.which);
      chat && !ask.demo && chat->member_count > static_cast<std::int64_t>(chat->members.size()) &&
      members_fetched.insert(one.which).second)
    net->fetch_members(one.which);
  root().main().chosen = one.which;
  root().main().show(*model);
}

void app::apply(const request::leave_chat&) {
  const auto& chosen = root().main().chosen;
  if (!chosen)
    return;
  const mux::conversation* one = model->find(*chosen);
  const bool room = one && mux::ui::is_group(*one);
  splice::visit(splice::overloaded{[&](mux::protocol::xmpp) {
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
  splice::visit(
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
  splice::visit(
      [this](accounts& panel) {
        if (auto* editor = panel.editor())
          splice::visit([this](auto& form) { this->edit(form); }, editor->parts.form);
        else if (auto* pane = panel.adding()) {
          new_proxy = pane->proxy;
          splice::visit([this](auto& form) { this->add(form); }, pane->parts.form);
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
  // The chat live again first -- its newest from the disk, what was shown
  // before the jump among them -- then back where a jump in it came from,
  // where there is one: found there, not fetched as a window of its own,
  // which had only what the server put around it.
  if (screen.chosen)
    this->go_live(*screen.chosen);
  if (screen.go_back())
    return;
  screen.jump_to_end();
}

// Back to the chat a jump to another left: as it was left there.
void app::apply(const request::return_to_chat&) {
  if (const auto back = root().main().take_chat_return())
    this->apply(request::choose{*back});
}

// A chat that is a window away from its newest: its newest from the disk,
// where all that came meanwhile is kept, and live again.
void app::go_live(const mux::conversation_id& in) {
  shared.go_live(in);
  if (std::exchange(shared.refresh_due, false))
    this->refresh();
}

}  // namespace mux::app
