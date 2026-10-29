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
import mux.media;
import skiff.platform.events;
import mux.ui;
import mux.protocols;
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
  // A direct chat that is encrypted: what is known of the other's identity,
  // asked for, for its header.
  if (const mux::conversation* chat = model->find(one.which); chat && !shared.demo() && chat->encrypted && !mux::ui::is_group(*chat))
    net->ask_trust(one.which.account, mux::ui::contact_of(shared.ui, *chat));
  // An invite: its card -- who asked, Accept, Decline -- not a chat.
  if (const mux::conversation* chat = model->find(one.which); chat && chat->invite) {
    this->take(request::show_invite{chat->id});
    return;
  }
  // Chosen, by a press or a key: no forum left lit as gone to.
  chat_list.point(std::nullopt);
  // A space shown as a forum: its rooms listed in it, as tdesktop opens a
  // forum's topics -- no chat opened.
  if (chat_list.is_forum(one.which)) {
    mux::ui::change_shown<mux::ui::chat_shown>(showing, [&](mux::ui::chat_shown& now) { now.forum = one.which.id; });
    return;
  }
  // What was being written where the reader was: kept as its draft; and
  // the chat opened's own put back in the field.
  if (shared.chosen() && *shared.chosen() != one.which) {
    drafts.keep(*shared.chosen(), mux::ui::draft_text(shared.chat_writing()));
    shared.write_chat(mux::ui::plain_draft(shared.draft_of(one.which)));
  } else if (!shared.chosen()) {
    shared.write_chat(mux::ui::plain_draft(shared.draft_of(one.which)));
  }
  model->touch(one.which);
  paging.restore_reads(one.which);
  // A group opened: all its members, once, where a sync gives only some.
  if (const mux::conversation* chat = model->find(one.which);
      chat && !ask.demo && chat->member_count > static_cast<std::int64_t>(chat->members.size()) &&
      members_fetched.insert(one.which).second)
    net->fetch_members(one.which);
  const bool another = shared.chosen() != one.which;
  // Another chat: the thread open in the one left is not one of its. Kept,
  // a link to a message in another room, followed from a thread, showed
  // that thread in the room gone to -- empty (a tester's); the panel stays
  // as it was, listing the new chat's threads.
  mux::ui::change_shown<mux::ui::chat_shown>(showing, [&](mux::ui::chat_shown& now) {
    if (now.chosen != one.which)
      now.thread.reset();
    now.chosen = one.which;
  });
  // Shown anew: opened at its first unread, quietly, where it has unread;
  // the ways back from jumps in the one left gone.
  if (another)
    jumps.opened(taker{this});
  this->show_chats_now();
}

void app::apply(const request::close_chat&) {
  if (!shared.chosen())
    return;
  drafts.keep(*shared.chosen(), mux::ui::draft_text(shared.chat_writing()));
  shared.write_chat({});
  mux::ui::change_shown<mux::ui::chat_shown>(showing, [](mux::ui::chat_shown& now) {
    now.chosen.reset();
    now.info_open = false;
  });
  jumps.opened(taker{this});
  this->show_chats_now();
}

void app::apply(const request::leave_chat&) {
  const auto& chosen = shared.chosen();
  if (!chosen)
    return;
  const mux::conversation* one = model->find(*chosen);
  // As its protocol says: where it may not be left, there is no Leave.
  if (!one || !mux::proto::can_leave(mux::ui::protocol_state_of(shared.ui, chosen->account), *one)) {
    shared.not_implemented("This chat cannot be left");
    return;
  }
  // A space: Element's box first, for which of its rooms to leave with it.
  if (one->space) {
    this->route(request::open_leave_space{*chosen});
    return;
  }
  if (!ask.demo)
    net->leave(*chosen);
  mux::ui::change_shown<mux::ui::chat_shown>(showing, [](mux::ui::chat_shown& now) { now.info_open = false; });
}

// A space left where no plugin asks which of its rooms go with it (the
// spaces plugin's box): it alone.
void app::apply(const request::open_leave_space& one) {
  if (!ask.demo)
    net->leave(one.space);
  mux::ui::change_shown<mux::ui::chat_shown>(showing, [](mux::ui::chat_shown& now) { now.info_open = false; });
}

void app::apply(const request::back&) { this->show_conversations(); }

void app::apply(const request::open_drawer&) { mux::ui::show(showing, mux::ui::drawer_shown{true}); }
void app::apply(const request::close_drawer&) { mux::ui::show(showing, mux::ui::drawer_shown{false}); }

// A banner's button, or a card's button of its protocol's own: what it asks
// got again in the visit of its protocol's state, and done in that type.
void app::apply(const request::banner_pressed& one) {
  if (const mux::conversation* chat = model->find(one.in))
    mux::proto::banner_asked(mux::ui::protocol_state_of(shared.ui, one.in.account), *chat, *model, taker{this});
}
// The chat's head pressed, off its buttons: its info, where the chat is
// shown one thing at a time -- as Telegram's phones.
void app::apply(const request::head_pressed&) {
  if (shared.single() && shared.chosen())
    this->apply(request::toggle_info{});
}
void app::apply(const request::card_action& one) {
  mux::proto::person_action_asked(mux::ui::protocol_state_of(shared.ui, one.by), one.by, one.who, one.index, taker{this});
}

void app::apply(const request::quit&) { skiff::platform::events::request_quit(); }

void app::apply(const request::toggle_info&) {
  mux::ui::change_shown<mux::ui::chat_shown>(showing, [](mux::ui::chat_shown& now) { now.info_open = !now.info_open; });
  this->refresh_shown();
  // An encrypted room's members, what is known of each one's identity asked
  // for, for their rows -- up to two hundred: the account answers from what
  // it holds, and no more than a page of rows is looked at.
  if (const auto& chosen = shared.chosen(); chosen && !shared.demo())
    if (const mux::conversation* chat = model->find(*chosen); chat && chat->encrypted)
      for (const mux::member& each : chat->members | std::views::take(200))
        net->ask_trust(chosen->account, each.id);
}

// To the newest: where the chat is a window away from it, back to the
// newest from the disk first -- live again -- then to its end.
void app::apply(const request::jump_to_end&) {
  auto& screen = root();
  // The chat live again first -- its newest from the disk, what was shown
  // before the jump among them -- then back where a jump in it came from,
  // where there is one: found there, not fetched as a window of its own,
  // which had only what the server put around it.
  if (shared.chosen())
    this->go_live(*shared.chosen());
  jumps.to_end(taker{this});
}

// Back to the chat a jump to another left: as it was left there.
void app::apply(const request::return_to_chat&) {
  if (const auto back = jumps.take_chat_return())
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
