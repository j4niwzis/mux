// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.explore.app -- Explore's part of the program: opened on the
// listed account's own server, its directory asked at once; a space's rooms
// asked of its account; a search -- an address typed in gone to, as a link
// to it would be; a room joined, and Explore closed; what a directory
// listed, put in.
export module mux.plugin.explore.app;

import std;
import splice;
import mux.composition;
import mux.core;
import mux.protocols;
import mux.ui;
import mux.logic.links;
import mux.app.network;
import mux.app.services;
import mux.plugin.explore.types;
import mux.plugin.explore.ui;

export namespace mux::plugins::explore {

struct part {
  app::services* s_ = nullptr;

  [[nodiscard]] const std::optional<shown>& open_now() const { return *s_->showing->look<std::optional<shown>>(); }

  // Opened on the account's own server: what the server lists, at once, as
  // Cinny opens its explorer -- its directory with nothing searched -- said
  // as asked until it answers.
  void apply(const open&) {
    const auto by = s_->listed();
    if (!by)
      return;
    const std::string own = proto::directory_server_of(ui::protocol_state_of(s_->ui, *by), by->address);
    ui::show(*s_->showing, ui::drawer_shown{false});
    ui::show<ui::new_chat_facts>(*s_->showing, std::nullopt);
    const bool asking = !s_->demo();
    ui::show(*s_->showing, std::optional(shown{own, std::nullopt, asking, std::nullopt, by, own, {}}));
    if (asking)
      s_->net->search_directory(*by, own, std::string());
  }
  void apply(const ui::request::close_shown<shown>&) { ui::show<shown>(*s_->showing, std::nullopt); }
  // A space's rooms and spaces: asked of its account, said as the space's --
  // its name over what it holds.
  void apply(const ui::request::explore_space& one) {
    const auto& facts = this->open_now();
    const auto by = facts ? facts->by : s_->listed();
    if (!by || s_->demo())
      return;
    ui::show(*s_->showing, ui::drawer_shown{false});
    ui::show<ui::new_chat_facts>(*s_->showing, std::nullopt);
    std::string name = one.name;
    if (const auto& chats = s_->model->accounts().at(*by).conversations; chats.contains(one.room))
      name = ui::display_name(chats.at(one.room));
    ui::show(*s_->showing, std::optional(shown{by->address.substr(by->address.find(':') + 1), std::nullopt, true,
                                               space_shown{one.room, name.empty() ? one.room : name}, by, {}, {}}));
    s_->net->explore_space(*by, one.room);
  }
  // A search: an address typed in is gone to, as a link to it would be --
  // its card, or the room where joined; else the directory asked.
  template <class Asked>
  void apply(const ui::request::search_rooms& one, const Asked& asked) {
    const auto& facts = this->open_now();
    const auto by = facts ? facts->by : std::nullopt;
    if (!by || s_->demo())
      return;
    if (auto room = proto::directory_room(by->speaks, one.query)) {
      this->apply(join{std::move(*room), one.server, by}, asked);
      return;
    }
    if (auto link = logic::link_of_id(one.query); link && proto::owns_address(ui::protocol_state_of(s_->ui, *by), one.query)) {
      ui::show<shown>(*s_->showing, std::nullopt);
      s_->link_due = *link;
      return;
    }
    bool space = false;
    ui::change_shown<std::optional<shown>>(*s_->showing, [&](auto& now) {
      if (!now)
        return;
      space = now->space.has_value();
      now->server = one.server;
      now->query = one.query;
      now->loading = !space;
    });
    if (!space)
      s_->net->search_directory(*by, one.server, one.query, one.since);
  }
  // A room of what is listed joined -- by the program, as any room of a
  // directory -- and Explore closed.
  template <class Asked>
  void apply(const join& one, const Asked& asked) {
    const auto& facts = this->open_now();
    const auto by = one.by ? one.by : facts ? facts->by : std::nullopt;
    ui::show<shown>(*s_->showing, std::nullopt);
    asked(ui::request::join_directory_room{one.room, one.server, by});
  }
  // What a directory listed: put in Explore, a further page after those
  // there, else in their place -- where it is for the search open.
  void take(const change::directory_listed& one) {
    ui::change_shown<std::optional<shown>>(*s_->showing, [&](auto& now) {
      if (!now || !accepts_listing(*now, one))
        return;
      auto rooms = one.more && now->listed ? std::move(now->listed->rooms) : std::vector<directory_room>{};
      rooms.insert(rooms.end(), one.rooms.begin(), one.rooms.end());
      now->listed = listing{std::move(rooms), one.server, one.space, one.query, one.next};
      now->loading = false;
    });
  }
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::explore
