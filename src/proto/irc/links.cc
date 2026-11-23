// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.irc.links: IRC's link -- an irc:// or ircs:// URL to a channel
// on a server (ircs://irc.libera.chat/#mux, the channel's '#' written as is
// or as %23) -- and where it leads.
export module mux.proto.irc.links;

import std;
import splice;
import mux.core;
import mux.logic.link_base;

export namespace mux::proto::irc {

namespace link {
struct channel {
  std::string server;  // the host, as the URL says it
  std::string name;    // #channel
  std::optional<account_id> by;
  friend bool operator==(const channel&, const channel&) = default;
};
}  // namespace link
using any_link = spl::variant<link::channel>;
constexpr type_tag<logic::link_list<link::channel>> links_type(const state&) { return {}; }

// Whether a name is a channel's: what begins it, as the usual CHANTYPES.
[[nodiscard]] constexpr bool channel_name(std::string_view name) {
  return !name.empty() && std::string_view("#&+!").find(name.front()) != std::string_view::npos;
}

// Explore: a channel's name typed in is joined at once, not looked up.
[[nodiscard]] constexpr std::string_view directory_hint(const state&) { return "#channel"; }
[[nodiscard]] inline std::optional<std::string> directory_room(const state&, std::string_view query) {
  if (!channel_name(query) || query.find_first_of(" ,") != std::string_view::npos) return std::nullopt;
  return std::string(query);
}
[[nodiscard]] inline std::optional<any_link> directory_link(const state&, std::string_view room, const account_id& by) {
  return any_link{link::channel{{}, std::string(room), by}};
}

// irc[s]://host[:port]/[#]channel[,...]
[[nodiscard]] inline std::optional<any_link> read_link(const state&, std::string_view url) {
  std::string_view rest;
  if (url.starts_with("ircs://")) rest = url.substr(7);
  else if (url.starts_with("irc://")) rest = url.substr(6);
  else return std::nullopt;
  const auto slash = rest.find('/');
  if (slash == std::string_view::npos) return std::nullopt;
  std::string_view host = rest.substr(0, slash);
  host = host.substr(host.find('@') == std::string_view::npos ? 0 : host.find('@') + 1);
  host = host.substr(0, host.rfind(':'));
  std::string name = logic::percent_decoded(logic::split_query(rest.substr(slash + 1)).first);
  name = name.substr(0, name.find(','));
  if (name.empty()) return std::nullopt;
  if (!channel_name(name)) name.insert(0, 1, '#');
  return link::channel{std::string(host), std::move(name), std::nullopt};
}

// Where a link of it leads, and the chat it names: in the kinds' own
// namespace, where ADL looks for them.
namespace link {

[[nodiscard]] inline std::optional<conversation_id> chat_for(const link::channel& one, const model& now) {
  if (one.by) {
    const conversation_id id{*one.by, one.name};
    return now.find(id) ? std::optional(id) : std::nullopt;
  }
  return logic::chat_named(now, protocol::irc{}, one.name);
}
[[nodiscard]] inline logic::link_step_t step_for(const link::channel& one, const model& now, const std::optional<account_id>& current) {
  const auto by = one.by ? one.by : logic::account_speaking(now, current, protocol::irc{});
  if (!by || by->speaks != protocol_t(protocol::irc{}))
    return logic::link_step::say{"No IRC account", std::format("An IRC account on {} is needed to join {}.", one.server, one.name)};
  const conversation_id id{*by, one.name};
  if (now.find(id)) return logic::link_step::open_chat{id, std::nullopt};
  return logic::link_step::join{*by, one.name, {}};
}

inline channel on_account(channel one, const account_id& by) {
  one.by = by;
  return one;
}

}  // namespace link

}  // namespace mux::proto::irc
