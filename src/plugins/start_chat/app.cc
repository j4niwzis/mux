// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.start_chat.app -- Start chat's part of the program: opened from
// the drawer -- those one has direct chats with to begin with, and one's own
// link to send -- or from a room's info, to invite someone there; the
// directory asked as what is typed changes; someone picked: the chat with
// them started (the program's), or them invited.
export module mux.plugin.start_chat.app;

import std;
import splice;
import mux.composition;
import mux.core;
import mux.protocols;
import mux.ui;
import mux.app.network;
import mux.app.services;
import mux.plugin.start_chat.types;
import mux.plugin.start_chat.ui;

export namespace mux::plugins::start_chat {

struct part {
  app::services* s_ = nullptr;
  // Its closes, templates on the dialog: listed for the runtime walks.
  using requests_taken = typename dialogs::requests;

  // The people one has direct chats with, by name.
  [[nodiscard]] std::vector<found_person> known_of(const account_id& by) const {
    std::vector<found_person> known;
    if (const account* found = s_->model->accounts().find(by))
      known = found->conversations.values() | std::views::filter([](const auto& in) { return !ui::is_group(in); }) |
              std::views::transform([&](const auto& in) {
                return found_person{.id = ui::contact_of(s_->ui, in), .name = ui::display_name(in), .avatar = in.avatar};
              }) |
              std::ranges::to<std::vector>();
    std::ranges::sort(known, {}, &found_person::name);
    return known;
  }

  // Element's Start chat. With no account to ask, said so: left silent, a
  // press on the drawer's row did nothing at all -- a tester signed in to
  // none pressed it again and again.
  void apply(const ui::request::open_new_chat&) {
    const auto& current = s_->listed();
    ui::show(*s_->showing, ui::drawer_shown{false});
    if (!current) {
      s_->notice("Start new chat", "No account is signed in: sign in to one in Manage accounts first.");
      return;
    }
    dialogs::close(*s_->showing);
    spl::visit([&]<class Protocol>(Protocol) {
      start_of<Protocol> facts{.people = this->known_of(*current), .by = *current,
                               .own_link = proto::share_link(ui::protocol_state_of(s_->ui, *current), current->address).value_or(std::string())};
      facts.rows = people_rows(facts);
      ui::show(*s_->showing, std::optional(std::move(facts)));
    }, current->speaks);
  }
  // Someone invited to a room, from its info: the same dialog, for it.
  void apply(const ui::request::open_room_invite& one) {
    const conversation* chat = s_->model->find(one.room);
    if (!chat || !ui::is_group(*chat) || chat->invite || !proto::chat_rights(ui::protocol_state_of(s_->ui, one.room.account), *chat).invite)
      return;
    dialogs::close(*s_->showing);
    spl::visit([&]<class Protocol>(Protocol) {
      invite_of<Protocol> facts{.people = this->known_of(one.room.account), .by = one.room.account, .inviting = one.room,
                                .room_name = ui::display_name(*chat)};
      facts.rows = people_rows(facts);
      ui::show(*s_->showing, std::optional(std::move(facts)));
    }, one.room.account.speaks);
  }
  void apply(const ui::request::invite_to_room& one) {
    const conversation* chat = s_->model->find(one.room);
    if (!chat || !ui::is_group(*chat) || chat->invite || !direct_address(one.room.account, one.user) ||
        !proto::chat_rights(ui::protocol_state_of(s_->ui, one.room.account), *chat).invite || s_->demo())
      return;
    s_->net->manage(one.room, room_action::invite{one.user});
    dialogs::close(*s_->showing);
  }
  // What is typed: the rows made again from it, and the account's directory
  // asked where its protocol has one.
  void apply(const ui::request::find_people& one) {
    std::optional<account_id> by;
    dialogs::each_open(*s_->showing, [&]<class Facts>(const Facts& open) {
      by = open.by;
      ui::change_shown<std::optional<Facts>>(*s_->showing, [&](auto& now) {
        now->query = one.query;
        now->found.clear();
        now->rows = people_rows(*now);
      });
    });
    s_->refresh_due = true;
    if (!by || s_->demo() || !proto::offers(ui::protocol_state_of(s_->ui, *by), proto::feature::people_directory{}))
      return;
    s_->net->search_people(*by, one.query);
  }
  // Someone picked: invited to the room the dialog is for, else the chat
  // with them -- by the program, as a link to them does.
  template <class Asked>
  void apply(const pick& one, const Asked& asked) {
    std::optional<ui::request::invite_to_room> invite;
    std::optional<ui::request::start_direct> start;
    dialogs::each_open(*s_->showing, [&](const auto& open) { this->picked(open, one, invite, start); });
    if (invite)
      asked(*invite);
    if (start) {
      dialogs::close(*s_->showing);
      asked(*start);
    }
  }
  template <class Protocol>
  void apply(const ui::request::close_shown<start_of<Protocol>>&) { dialogs::close(*s_->showing); }
  template <class Protocol>
  void apply(const ui::request::close_shown<invite_of<Protocol>>&) { dialogs::close(*s_->showing); }
  // The directory's answer: for the search open, by the same account.
  void take(const change::people_found& found) {
    dialogs::each_open(*s_->showing, [&]<class Facts>(const Facts&) {
      ui::change_shown<std::optional<Facts>>(*s_->showing, [&](auto& now) {
        if (now->by != found.by || now->query != found.query)
          return;
        now->found = found.people;
        now->rows = people_rows(*now);
        for (const auto& person : found.people)
          if (person.avatar && !person.avatar->empty())
            ui::listed_avatars().emplace_back(person.id, *person.avatar);
      });
    });
  }

 private:
  // Who was picked, by what the dialog is for.
  template <class Protocol>
  static void picked(const invite_of<Protocol>& open, const pick& one, std::optional<ui::request::invite_to_room>& invite,
                     std::optional<ui::request::start_direct>&) {
    invite = ui::request::invite_to_room{open.inviting, one.user};
  }
  template <class Protocol>
  static void picked(const start_of<Protocol>& open, const pick& one, std::optional<ui::request::invite_to_room>&,
                     std::optional<ui::request::start_direct>& start) {
    start = ui::request::start_direct{one.user, open.by};
  }
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::start_chat
