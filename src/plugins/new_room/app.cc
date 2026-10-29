// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.new_room.app -- The new room's part of the program: Create a
// room opened -- by the first account that can make rooms, from the drawer;
// in a space, from its menu -- and the room made, opened once the model has
// it (the program's).
export module mux.plugin.new_room.app;

import std;
import mux.composition;
import mux.core;
import mux.protocols;
import mux.ui;
import mux.app.network;
import mux.app.services;
import mux.plugin.new_room.types;
import mux.plugin.new_room.ui;

export namespace mux::plugins::new_room {

struct part {
  app::services* s_ = nullptr;
  // Its closes, templates on the form: listed for the runtime walks.
  using requests_taken = typename forms::requests;

  // With no account that can make rooms, said so: left silent, a press on
  // the drawer's row did nothing at all.
  void apply(const ui::request::open_new_room&) {
    const auto by = s_->account_offering(proto::feature::room_creation{});
    ui::show(*s_->showing, ui::drawer_shown{false});
    if (!by) {
      s_->notice("New room", "No account signed in can make rooms: sign in to one in Manage accounts first.");
      return;
    }
    const std::string server = proto::directory_server_of(ui::protocol_state_of(s_->ui, *by), by->address);
    forms::close(*s_->showing);
    spl::visit([&]<class Protocol>(Protocol) { ui::show(*s_->showing, std::optional(room_form_of<Protocol>{server, *by})); }, by->speaks);
  }
  void apply(const ui::request::open_new_room_in& one) {
    const std::string& address = one.space.account.address;
    ui::show(*s_->showing, ui::drawer_shown{false});
    forms::close(*s_->showing);
    spl::visit([&]<class Protocol>(Protocol) {
      ui::show(*s_->showing, std::optional(space_room_form_of<Protocol>{address.substr(address.find(':') + 1), one.space.account,
                                                                        place{one.space, one.name, one.make_space}}));
    }, one.space.account.speaks);
  }
  template <class Protocol>
  void apply(const ui::request::close_shown<room_form_of<Protocol>>&) { forms::close(*s_->showing); }
  template <class Protocol>
  void apply(const ui::request::close_shown<space_room_form_of<Protocol>>&) { forms::close(*s_->showing); }
  // The room made: in a space, by its account; its alias's local part as
  // the protocol has it (#name:server, name).
  void apply(const ui::request::create_room& one) {
    const auto by = one.space ? std::optional<account_id>(one.space->account) : this->open_by(protocols{});
    if (!by || s_->demo())
      return;
    forms::close(*s_->showing);
    const auto state = ui::protocol_state_of(s_->ui, *by);
    const auto form = proto::room_creation_form_of(state, std::string());
    const std::string alias = form.address_required ? one.alias : one.open ? proto::local_part_of(state, one.alias) : std::string();
    s_->net->create_room(*by, one.name, one.topic, one.open, alias, one.federate, one.encrypted,
                         room_place{one.space ? std::optional<std::string>(one.space->id) : std::nullopt, one.space_members, one.make_space});
    s_->notice("New room", "Making " + one.name + "…");
  }

 private:
  // The account of the drawer's form open, whichever protocol's it is.
  template <class... Protocols>
  std::optional<account_id> open_by(protocol_list<Protocols...>) const {
    std::optional<account_id> found;
    ((found = found ? found : this->open_by_of<Protocols>()), ...);
    return found;
  }
  template <class Protocol>
  std::optional<account_id> open_by_of() const {
    const auto& open = *s_->showing->look<std::optional<room_form_of<Protocol>>>();
    return open ? std::optional(open->by) : std::nullopt;
  }
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::new_room
