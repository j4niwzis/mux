// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.rooms: chats and rooms begun or found -- Start chat and its
// people searched, a direct chat or a group made; Create room; Explore, a
// server's directory or a space's rooms, searched and joined. A part of the
// program: it reaches the rest through the services it is given, and
// leaves what the program does next -- a chat opened, a link followed, a
// room opened once joined -- there, as data.
export module mux.app.rooms;

import std;
import splice;
import mux.core;
import mux.protocols;
import mux.ui;
import mux.ui.proto;
import mux.logic.links;
import mux.app.network;
import mux.app.services;
import mux.app.requests;

export namespace mux::app {

class rooms_part {
 public:
  explicit rooms_part(services& shared) : s_(&shared) {}
  rooms_part(const rooms_part&) = delete;
  rooms_part& operator=(const rooms_part&) = delete;

  // A new chat: its box; a direct chat or a group asked of the account whose
  // chats are listed -- or, for a direct chat with someone it already has
  // one with, that chat shown.
  // Element's Start chat: those one has direct chats with to begin with, and
  // one's own link to send.
  void apply(const request::open_new_chat&) {
    const auto& current = s_->root().main().current;
    std::vector<mux::found_person> known;
    std::string link;
    if (current) {
      if (const auto found = s_->model->accounts().find(*current); found != s_->model->accounts().end())
        for (const auto& [key, chat] : found->second.conversations)
          if (!mux::ui::is_group(chat))
            known.push_back({.id = mux::ui::contact_of(s_->ui, chat), .name = mux::ui::display_name(chat), .avatar = chat.avatar});
      link = mux::proto::share_link(mux::ui::protocol_state_of(s_->ui, *current), current->address).value_or(std::string());
    }
    std::ranges::sort(known, {}, &mux::found_person::name);
    s_->root().open_new_chat(std::move(known), std::move(link));
  }
  void apply(const request::find_people& one) {
    const auto by = s_->account_offering(mux::proto::feature::people_directory{});
    if (!by || s_->demo())
      return;
    s_->net->search_people(*by, one.query);
  }
  void apply(const request::search_elsewhere& one) {
    const auto by = s_->account_offering(mux::proto::feature::people_directory{});
    if (!by || s_->demo())
      return;
    s_->net->search_directory(*by, std::string(), one.query);
    s_->net->search_people(*by, one.query);
  }
  void apply(const request::open_new_room&) {
    const auto by = s_->account_offering(mux::proto::feature::room_creation{});
    s_->root().open_new_room(by ? by->address.substr(by->address.find(':') + 1) : std::string());
  }
  void apply(const request::open_new_room_in& one) {
    const std::string& address = one.space.account.address;
    s_->root().open_new_room(address.substr(address.find(':') + 1),
                             mux::ui::new_room_place{one.space, one.name, one.make_space});
  }
  void apply(const request::close_new_room&) { s_->root().close_new_room(); }
  void apply(const request::close_new_chat&) { s_->root().close_new_chat(); }
  void apply(const request::start_direct& one) {
    const auto& current = s_->root().main().current;
    if (!current || s_->demo())
      return;
    s_->root().close_new_chat();
    for (const auto& [key, chat] : s_->model->accounts().at(*current).conversations)
      if (!mux::ui::is_group(chat) && mux::ui::contact_of(s_->ui, chat) == one.user) {
        s_->chat_due = chat.id;
        return;
      }
    s_->net->create_direct(*current, one.user);
    s_->root().show_message("New chat", "Starting a chat with " + one.user + "…");
  }
  // Explore rooms: opened on the account's own server.
  void apply(const request::open_explore&) {
    const auto by = s_->account_offering(mux::proto::feature::room_directory{});
    const std::string own = by ? by->address.substr(by->address.find(':') + 1) : std::string();
    s_->root().open_explore(own);
    // What the server lists, at once, as Cinny opens its explorer: its
    // directory with nothing searched.
    if (by && !s_->demo()) {
      s_->root().explore_loading();
      s_->net->search_directory(*by, own, std::string());
    }
  }
  void apply(const request::close_explore&) { s_->root().close_explore(); }
  // A space's rooms and spaces, in Explore: asked of its account.
  void apply(const request::explore_space& one) {
    (void)s_->root().main().close_space_menu();
    const auto by = s_->root().main().current;
    if (!by || s_->demo())
      return;
    s_->root().open_explore(by->address.substr(by->address.find(':') + 1));
    // Said as the space's: its name and picture over what it holds.
    std::string name = one.name;
    if (const auto& chats = s_->model->accounts().at(*by).conversations; chats.contains(one.room))
      name = mux::ui::display_name(chats.at(one.room));
    s_->root().explore_as_space(one.room, name.empty() ? one.room : name);
    s_->root().explore_loading();
    s_->net->explore_space(*by, one.room);
  }
  // A search: an address typed in is gone to, as a link to it would be --
  // its card, or the room where joined; else the directory asked.
  void apply(const request::search_rooms& one) {
    if (auto link = mux::logic::link_of_id(one.query)) {
      s_->root().close_explore();
      s_->link_due = *link;
      return;
    }
    const auto by = s_->account_offering(mux::proto::feature::room_directory{});
    if (!by || s_->demo())
      return;
    s_->net->search_directory(*by, one.server, one.query, one.since);
  }
  // A room of the directory joined, through the server it was listed by, and
  // opened when it comes.
  void apply(const request::join_directory_room& one) {
    const auto by = s_->account_offering(mux::proto::feature::room_directory{});
    if (!by || s_->demo())
      return;
    std::vector<std::string> via;
    if (!one.server.empty())
      via.push_back(one.server);
    s_->joining = mux::logic::link_of_id(one.room);
    s_->net->join(*by, one.room, via);
    s_->root().close_explore();
  }
  // A room made, and opened once the model has it.
  void apply(const request::create_room& one) {
    // In a space: by its account.
    const auto by = one.space ? std::optional<mux::account_id>(one.space->account)
                              : s_->account_offering(mux::proto::feature::room_creation{});
    if (!by || s_->demo())
      return;
    s_->root().close_new_room();
    // Its alias's local part, as the protocol has it (#name:server, name).
    const std::string alias = mux::proto::local_part_of(mux::ui::protocol_state_of(s_->ui, *by), one.alias);
    s_->net->create_room(*by, one.name, one.topic, one.open, one.open ? alias : std::string(), one.federate, one.encrypted,
                         mux::room_place{one.space ? std::optional<std::string>(one.space->id) : std::nullopt,
                                         one.space_members, one.make_space});
    s_->root().show_message("New room", "Making " + one.name + "\u2026");
  }
  void apply(const request::start_group& one) {
    const auto& current = s_->root().main().current;
    if (!current || s_->demo())
      return;
    s_->root().close_new_chat();
    s_->net->create_group(*current, one.name);
    s_->root().show_message("New group", "Making " + one.name + "…");
  }

 private:
  services* s_;
};

}  // namespace mux::app
