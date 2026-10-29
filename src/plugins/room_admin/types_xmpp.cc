// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.room_admin.types:xmpp -- An XMPP group chat's settings' tabs:
// Room, Configuration, Roles & Permissions -- shown for a group chat of
// XMPP's (a MUC) alone. Built where XMPP is.
export module mux.plugin.room_admin.types:xmpp;

import std;
import splice;
import mux.core;
import mux.composition;
import mux.plugin.room_settings.types;
import :feature;
import mux.proto;
import mux.proto.xmpp;

export namespace mux::proto::xmpp::manage {

struct room {};
struct configuration {};
struct roles {};
constexpr std::string_view tab_title(room) { return "Room"; }
constexpr std::string_view tab_title(configuration) { return "Configuration"; }
constexpr std::string_view tab_title(roles) { return "Roles & Permissions"; }

}  // namespace mux::proto::xmpp::manage

export namespace mux::plugins::room_admin {

// XMPP's realization: its tabs; it keeps nothing of its own beside the
// room's part the facts hold (muc_room), and opens on the client's General.
struct xmpp_admin {
  using tabs = composition::page_list<proto::xmpp::manage::room, proto::xmpp::manage::configuration, proto::xmpp::manage::roles>;
};
// An XMPP group chat's settings: its tabs, on that kind alone (a one-to-one
// chat's settings are a plain chat's). It keeps nothing of its own beside
// the room's part the facts hold (muc_room), and opens on the client's
// General.
using xmpp_kind = room_settings::room_kind::room_of<::mux::protocol::xmpp>;
constexpr xmpp_admin::tabs room_tabs(room_settings::contract::tabs, type_tag<feature>, type_tag<xmpp_kind>) { return {}; }

}  // namespace mux::plugins::room_admin
