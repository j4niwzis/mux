// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.xmpp.links: XMPP's link -- an xmpp: URI (RFC 5122), to an
// address -- and where it leads.
export module mux.proto.xmpp.links;

import std;
import splice;
import mux.core;
import mux.logic.link_base;

export namespace mux::proto::xmpp {

namespace link {
struct address {  // a JID
  std::string jid;
  friend bool operator==(const address&, const address&) = default;
};
}  // namespace link
using any_link = spl::variant<link::address>;
constexpr type_tag<logic::link_list<link::address>> links_type(const state&) { return {}; }

// xmpp:<jid>[?...]
[[nodiscard]] inline std::optional<any_link> read_link(const state&, std::string_view url) {
  constexpr std::string_view scheme = "xmpp:";
  if (!url.starts_with(scheme))
    return std::nullopt;
  const auto [path, query] = logic::split_query(url.substr(scheme.size()));
  if (path.empty())
    return std::nullopt;
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

}  // namespace link

}  // namespace mux::proto::xmpp
