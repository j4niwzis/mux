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
import mux.platform.events;
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
    room_card.invited(*chat);
    return;
  }
  // Chosen, by a press or a key: no forum left lit as gone to.
  root().main().pointed.reset();
  // A space shown as a forum: its rooms listed in it, as tdesktop opens a
  // forum's topics -- no chat opened.
  if (root().main().is_forum(one.which)) {
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
  // What was kept of its reads, where the model has nothing newer: read
  // now, before the chat is shown -- a small file -- so that it opens at
  // the first unread as read here, not by the server's count, which it did
  // when this came a moment after.
  if (const mux::conversation* chat = model->find(one.which); chat && !ask.demo) {
    auto kept = store.read_reads(one.which);
    if (!chat->read_up_to && kept.me)
      model->read_up_to(one.which, *kept.me);
    std::map<std::string, std::string> missing;
    for (auto& [user, event] : kept.read_by)
      if (!chat->read_by.contains(user))
        missing.emplace(user, std::move(event));
    if (!missing.empty())
      model->apply(mux::change_t{mux::change::receipts_changed{one.which, std::move(missing)}});
  }
  // A group opened: all its members, once, where a sync gives only some.
  if (const mux::conversation* chat = model->find(one.which);
      chat && !ask.demo && chat->member_count > static_cast<std::int64_t>(chat->members.size()) &&
      members_fetched.insert(one.which).second)
    net->fetch_members(one.which);
  root().main().chosen = one.which;
  root().main().show(*model);
}

void app::apply(const request::close_chat&) {
  auto& screen = root().main();
  if (!screen.chosen)
    return;
  drafts.keep(*screen.chosen, screen.line.plain());
  screen.line.set_text({});
  screen.info_open = false;
  screen.chosen.reset();
  screen.show(*model);
}

void app::apply(const request::leave_chat&) {
  const auto& chosen = root().main().chosen;
  if (!chosen)
    return;
  const mux::conversation* one = model->find(*chosen);
  // As its protocol says: where it may not be left, there is no Leave.
  if (!one || !mux::proto::can_leave(mux::ui::protocol_state_of(shared.ui, chosen->account), *one)) {
    root().show_notice("This chat cannot be left");
    return;
  }
  // A space: Element's box first, for which of its rooms to leave with it.
  if (one->space) {
    this->apply(request::open_leave_space{*chosen});
    return;
  }
  if (!ask.demo)
    net->leave(*chosen);
  root().main().info_open = false;
}

void app::apply(const request::open_leave_space& one) {
  const mux::conversation* space = model->find(one.space);
  if (!space)
    return;
  mux::ui::leave_space_facts facts{.space = one.space, .name = space->name.empty() ? one.space.id : space->name};
  for (const std::string& child : space->children)
    if (const mux::conversation* room = model->find(mux::conversation_id{one.space.account, child}))
      facts.rooms.push_back({child, room->name.empty() ? child : room->name});
  root().open_leave_space(std::move(facts));
}
void app::apply(const request::leave_space& one) {
  root().close_leave_space();
  if (ask.demo)
    return;
  for (const std::string& room : one.rooms)
    net->leave(mux::conversation_id{one.space.account, room});
  net->leave(one.space);
  root().main().info_open = false;
}
void app::apply(const request::close_leave_space&) { root().close_leave_space(); }

void app::apply(const request::back&) { this->show_conversations(); }

void app::apply(const request::open_drawer&) { root().open_drawer(); }

void app::apply(const request::quit&) { mux::platform::events::request_quit(); }

void app::apply(const request::toggle_info&) {
  root().main().toggle_info();
  // An encrypted room's members, what is known of each one's identity asked
  // for, for their rows -- up to two hundred: the account answers from what
  // it holds, and no more than a page of rows is looked at.
  if (const auto& chosen = root().main().chosen; chosen && !shared.demo())
    if (const mux::conversation* chat = model->find(*chosen); chat && chat->encrypted)
      for (const mux::member& each : std::views::take(chat->members, 200))
        net->ask_trust(chosen->account, each.id);
}

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
