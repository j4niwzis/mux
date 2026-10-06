// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix.links: Matrix's links -- to a person, to a room and a
// message in it -- read from matrix.to and matrix: links and from an ID
// typed or written in a message; where each leads; and the links made to a
// room and a message of it.
export module mux.proto.matrix.links;

import std;
import splice;
import mux.core;
import mux.logic.link_base;

export namespace mux::proto::matrix {

namespace link {
struct person {  // @user:server
  std::string id;
  friend bool operator==(const person&, const person&) = default;
};
struct room {  // !room:server or #alias:server, and a message in it
  std::string id;
  std::optional<std::string> event;
  std::vector<std::string> via;  // the servers to join through
  friend bool operator==(const room&, const room&) = default;
};
}  // namespace link
using any_link = spl::variant<link::person, link::room>;
constexpr type_tag<logic::link_list<link::person, link::room>> links_type(const state&) { return {}; }

// A Matrix ID read by its sigil into what it names; none for another.
[[nodiscard]] inline std::optional<any_link> matrix_id_of(std::string id, std::optional<std::string> event = std::nullopt,
                                                         std::vector<std::string> via = {}) {
  if (id.size() < 2)
    return std::nullopt;
  static const std::map<char, bool> sigils = {{'@', true}, {'#', false}, {'!', false}};  // a person's, or a room's
  const auto found = sigils.find(id.front());
  if (found == sigils.end())
    return std::nullopt;
  if (found->second)
    return link::person{std::move(id)};
  return link::room{std::move(id), std::move(event), std::move(via)};
}

// A word shaped as a Matrix ID: a sigil, a name, a colon and a server; or
// a room's ID of room version 12 and on (MSC4291), which has no server: '!'
// and the 43 characters of its create event's hash, unpadded base64url.
[[nodiscard]] inline bool id_shaped(std::string_view word) {
  if (word.size() < 4)
    return false;
  if (const auto colon = word.find(':'); colon != std::string_view::npos)
    return colon >= 2 && colon + 1 < word.size();
  const auto base64url = [](char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
  };
  return word.size() == 44 && word.front() == '!' && std::ranges::all_of(word.substr(1), base64url);
}

namespace detail {
// What follows a '?': the servers of its via=.
inline std::vector<std::string> via_of(std::string_view query) {
  std::vector<std::string> out;
  for (std::size_t at = 0; at < query.size();) {
    auto end = query.find('&', at);
    if (end == std::string_view::npos)
      end = query.size();
    const std::string_view pair = query.substr(at, end - at);
    if (pair.starts_with("via="))
      out.push_back(logic::percent_decoded(pair.substr(4)));
    at = end + 1;
  }
  return out;
}
// https://matrix.to/#/<id>[/<event>][?via=...]
inline std::optional<any_link> from_matrix_to(std::string_view rest) {
  const auto [path, query] = logic::split_query(rest);
  const auto slash = path.find('/');
  std::optional<std::string> event;
  if (slash != std::string_view::npos)
    event = logic::percent_decoded(path.substr(slash + 1));
  // What follows the room is an event, and an event id begins with $: a
  // second address there is not one, and the link is no link to a message.
  if (event && !event->starts_with('$'))
    return std::nullopt;
  return matrix_id_of(logic::percent_decoded(path.substr(0, slash)), std::move(event), via_of(query));
}
// matrix:u/<user>, matrix:r/<alias>, matrix:roomid/<id>[/e/<event>] (MSC2312),
// each part's name read into the sigil of the ID it names.
inline std::optional<any_link> from_matrix_uri(std::string_view rest) {
  auto [path, query] = logic::split_query(rest);
  const auto part = [&path]() {
    const auto slash = path.find('/');
    const std::string_view one = path.substr(0, slash);
    path = slash == std::string_view::npos ? std::string_view() : path.substr(slash + 1);
    return one;
  };
  static const std::map<std::string_view, char> sigils = {{"u", '@'}, {"r", '#'}, {"roomid", '!'}};
  const auto kind = sigils.find(part());
  if (kind == sigils.end())
    return std::nullopt;
  const std::string name = logic::percent_decoded(part());
  std::optional<std::string> event;
  if (part() == "e")
    event = "$" + logic::percent_decoded(part());
  return matrix_id_of(kind->second + name, std::move(event), via_of(query));
}
inline std::string with_via(std::string link, const std::vector<std::string>& via) {
  for (std::size_t i = 0; i < via.size(); ++i)
    link += (i == 0 ? "?via=" : "&via=") + via[i];
  return link;
}
}  // namespace detail

// A link read, by its scheme: matrix.to's or matrix:'s.
[[nodiscard]] inline std::optional<any_link> read_link(const state&, std::string_view url) {
  if (constexpr std::string_view to = "https://matrix.to/#/"; url.starts_with(to))
    return detail::from_matrix_to(url.substr(to.size()));
  if (constexpr std::string_view uri = "matrix:"; url.starts_with(uri))
    return detail::from_matrix_uri(url.substr(uri.size()));
  return std::nullopt;
}
// An ID as it is written -- typed in a search, said in a message.
[[nodiscard]] inline std::optional<any_link> read_id(const state&, std::string_view word) {
  if (!id_shaped(word))
    return std::nullopt;
  return matrix_id_of(std::string(word));
}

// The servers a room is joined through, for a link to it: as Element picks
// them, those with the most of its members, at most three -- a room's ID
// from version 12 on names no server, and one that did may be gone.
[[nodiscard]] inline std::vector<std::string> via_servers(const conversation& chat, std::size_t most = 3) {
  std::map<std::string, std::size_t> counted;
  for (const member& each : chat.members)
    if (const auto colon = each.id.find(':'); colon != std::string::npos)
      ++counted[each.id.substr(colon + 1)];
  std::vector<std::pair<std::string, std::size_t>> ranked(counted.begin(), counted.end());
  std::ranges::stable_sort(ranked, std::ranges::greater{}, &std::pair<std::string, std::size_t>::second);
  std::vector<std::string> out;
  for (auto& [server, count] : ranked) {
    if (out.size() == most)
      break;
    out.push_back(std::move(server));
  }
  return out;
}
// A link to a room: by its alias where it has one, which needs no servers;
// by its ID with the servers to join through where not.
[[nodiscard]] inline std::string room_link_to(const conversation& chat) {
  if (chat.alias)
    return "https://matrix.to/#/" + *chat.alias;
  return detail::with_via("https://matrix.to/#/" + chat.id.id, via_servers(chat));
}
// A link to a message in it: by the room's ID, with its servers.
[[nodiscard]] inline std::string message_link_to(const conversation& chat, std::string_view event) {
  return detail::with_via("https://matrix.to/#/" + chat.id.id + "/" + std::string(event), via_servers(chat));
}

// Where a link of it leads, and the chat it names: in the kinds' own
// namespace, where ADL looks for them (a link's associated namespace is
// the innermost one enclosing it).
namespace link {

// The chat a link names, where the model has it; and where it leads.
[[nodiscard]] inline std::optional<conversation_id> chat_for(const link::room& one, const model& now) {
  return logic::chat_named(now, protocol::matrix{}, one.id);
}
// In a message: a person, by their name; a room, a place -- its pill
// opening matrix.to.
[[nodiscard]] inline logic::mention_t mention_of(const person& one) { return logic::mention::person{one.id}; }
[[nodiscard]] inline logic::mention_t mention_of(const room& one) {
  return logic::mention::place{one.id, one.event, "https://matrix.to/#/" + one.id};
}
// A person: their card, as Telegram opens a mention's profile -- a member of
// the chat being read or not; a message to them is one press from there.
[[nodiscard]] inline logic::link_step_t step_for(const link::person& one, const model&, const std::optional<account_id>&) {
  return logic::link_step::member_page{one.id};
}
// A room joined: opened. Not: joined through the account in view, or the
// first Matrix one.
[[nodiscard]] inline logic::link_step_t step_for(const link::room& one, const model& now, const std::optional<account_id>& current) {
  if (const auto found = chat_for(one, now))
    return logic::link_step::open_chat{*found, one.event};
  const auto by = logic::account_speaking(now, current, protocol::matrix{});
  if (!by)
    return logic::link_step::say{"No Matrix account", "A Matrix account is needed to open that room."};
  return logic::link_step::join{*by, one.id, one.via};
}

}  // namespace link

}  // namespace mux::proto::matrix
