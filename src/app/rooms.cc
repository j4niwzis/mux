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

  void apply(const request::search_elsewhere& one) {
    const auto by = one.by ? one.by : s_->listed();
    if (!by || by != s_->listed() || s_->demo())
      return;
    const auto state = mux::ui::protocol_state_of(s_->ui, *by);
    if (mux::proto::offers(state, mux::proto::feature::room_directory{}))
      s_->net->search_directory(*by, std::string(), one.query);
    if (mux::proto::offers(state, mux::proto::feature::people_directory{}))
      s_->net->search_people(*by, one.query);
  }
  void apply(const request::start_direct& one) {
    const auto current = one.by ? one.by : s_->listed();
    if (!current || s_->demo())
      return;
    for (const auto& [key, chat] : s_->model->accounts().at(*current).conversations)
      if (!mux::ui::is_group(chat) && mux::ui::contact_of(s_->ui, chat) == one.user) {
        s_->chat_due = chat.id;
        return;
      }
    s_->net->create_direct(*current, one.user);
    s_->notice("New chat", "Starting a chat with " + one.user + "…");
  }
  // A room of the directory joined, through the server it was listed by, and
  // opened when it comes.
  void apply(const request::join_directory_room& one) {
    const auto by = one.by ? one.by : s_->listed();
    if (!by || s_->demo())
      return;
    std::vector<std::string> via;
    if (!one.server.empty())
      via.push_back(one.server);
    // The protocol's own link to it, where it has one; else the room's.
    const auto own = spl::visit([&]<class Tag>(Tag) {
      using mux::proto::directory_defaults::directory_link;
      return mux::logic::as_some_link(directory_link(mux::state_of<Tag>{}, one.room, *by));
    }, by->speaks);
    if (own)
      s_->joining = own;
    else if (const auto link = mux::logic::link_of_id(one.room))
      s_->joining = mux::logic::on_account(*link, *by);
    else
      s_->joining.reset();
    s_->net->join(*by, one.room, via);
  }
  // A room made, and opened once the model has it.
  void apply(const request::start_group& one) {
    const auto& current = s_->listed();
    if (!current || s_->demo())
      return;
    s_->net->create_group(*current, one.name);
    s_->notice("New group", "Making " + one.name + "…");
  }

 private:
  services* s_;
};

}  // namespace mux::app
