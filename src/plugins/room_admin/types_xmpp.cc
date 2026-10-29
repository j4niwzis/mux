// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.room_admin.types:xmpp -- An XMPP group chat's settings' tabs:
// Room, Configuration, Roles & Permissions -- shown for a group chat of
// XMPP's (a MUC) alone. Built where XMPP is.
export module mux.plugin.room_admin.types:xmpp;

import std;
import splice;
import mux.core;
import mux.composition;
import mux.proto;
import mux.proto.xmpp;

export namespace mux::proto::xmpp::manage {

struct room {};
struct configuration {};
struct roles {};
constexpr std::string_view tab_title(room) { return "Room"; }
constexpr std::string_view tab_title(configuration) { return "Configuration"; }
constexpr std::string_view tab_title(roles) { return "Roles & Permissions"; }
// Its tabs, an XMPP group chat's alone.
template <class Facts>
[[nodiscard]] bool muc_room_of(const Facts& facts) {
  return facts.group &&
         spl::visit(spl::overloaded{[](const ::mux::protocol::xmpp&) { return true; }, [](const auto&) { return false; }}, facts.speaks);
}
template <class Facts> bool tab_visible(room, const Facts& facts) { return muc_room_of(facts); }
template <class Facts> bool tab_visible(configuration, const Facts& facts) { return muc_room_of(facts); }
template <class Facts> bool tab_visible(roles, const Facts& facts) { return muc_room_of(facts); }

}  // namespace mux::proto::xmpp::manage

export namespace mux::plugins::room_admin {

// XMPP's realization: its tabs; it keeps nothing of its own beside the
// room's part the facts hold (muc_room), and opens on the client's General.
struct xmpp_admin {
  using tabs = composition::page_list<proto::xmpp::manage::room, proto::xmpp::manage::configuration, proto::xmpp::manage::roles>;
  using kept = composition::page_list<>;
  template <class Facts>
  static void opened(Facts&, const conversation&) {}
};

}  // namespace mux::plugins::room_admin
