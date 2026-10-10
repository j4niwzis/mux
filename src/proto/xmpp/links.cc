// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.xmpp.links: XMPP's link -- an xmpp: URI (RFC 5122), to an
// address -- and where it leads.
export module mux.proto.xmpp.links;

import std;
import splice;
import tern.jid;
import mux.core;
import mux.logic.link_base;

export namespace mux::proto::xmpp {

namespace link {
struct address {  // a JID
  std::string jid;
  friend bool operator==(const address&, const address&) = default;
};
struct room {
  std::string jid;
  std::optional<account_id> by;
  friend bool operator==(const room&, const room&) = default;
};
}  // namespace link
using any_link = spl::variant<link::address, link::room>;
constexpr type_tag<logic::link_list<link::address, link::room>> links_type(const state&) { return {}; }

// A bare room address, or the join URI of XEP-0045. A JID without a
// join action remains a person/address link outside the room explorer.
[[nodiscard]] inline std::optional<link::room> room_address(std::string_view input) {
  std::string jid;
  if (input.starts_with("xmpp:")) {
    const auto [path, query] = logic::split_query(input.substr(5));
    if (query != "join") return std::nullopt;
    jid = logic::percent_decoded(path);
  } else {
    jid = input;
  }
  const auto parsed = tern::jid::parse(jid);
  if (!parsed || parsed->local().empty() || !parsed->resource().empty()) return std::nullopt;
  return link::room{parsed->str(), std::nullopt};
}

// xmpp:<jid>[?...]
[[nodiscard]] inline std::optional<any_link> read_link(const state&, std::string_view url) {
  constexpr std::string_view scheme = "xmpp:";
  if (!url.starts_with(scheme))
    return std::nullopt;
  const auto [path, query] = logic::split_query(url.substr(scheme.size()));
  if (path.empty())
    return std::nullopt;
  if (query == "join") {
    if (auto room = room_address(url)) return *room;
    return std::nullopt;
  }
  return link::address{logic::percent_decoded(path)};
}

// Where a link of it leads, and the chat it names: in the kinds' own
// namespace, where ADL looks for them (a link's associated namespace is
// the innermost one enclosing it).
namespace link {

[[nodiscard]] inline std::optional<conversation_id> chat_for(const link::address& one, const model& now) {
  return logic::chat_named(now, protocol::xmpp{}, one.jid);
}
[[nodiscard]] inline logic::link_step_t step_for(const link::address& one, const model& now, const std::optional<account_id>&) {
  if (const auto found = chat_for(one, now))
    return logic::link_step::open_chat{*found, std::nullopt};
  return logic::link_step::say{"Not joined", std::format("{} is not in your list.", one.jid)};
}

[[nodiscard]] inline std::optional<conversation_id> chat_for(const link::room& one, const model& now) {
  if (one.by) {
    const conversation_id id{*one.by, one.jid};
    return now.find(id) ? std::optional(id) : std::nullopt;
  }
  return logic::chat_named(now, protocol::xmpp{}, one.jid);
}
[[nodiscard]] inline logic::link_step_t step_for(const link::room& one, const model& now,
                                                const std::optional<account_id>& current) {
  const auto by = one.by ? one.by : logic::account_speaking(now, current, protocol::xmpp{});
  if (!by || by->speaks != protocol_t(protocol::xmpp{}))
    return logic::link_step::say{"No XMPP account", "An XMPP account is needed to join that room."};
  const conversation_id id{*by, one.jid};
  if (now.find(id)) return logic::link_step::open_chat{id, std::nullopt};
  return logic::link_step::join{*by, one.jid, {}};
}

inline room on_account(room one, const account_id& by) {
  one.by = by;
  return one;
}

}  // namespace link

}  // namespace mux::proto::xmpp
