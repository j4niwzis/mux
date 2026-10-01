// SPDX-License-Identifier: AGPL-3.0-only
// mux.logic.links: a link as the program reads it -- to a person, a room
// (and a message in it), or an XMPP address -- and what following it comes
// to, given what the model has.
export module mux.logic.links;

import std;
import splice;
import mux.core;

export namespace mux::logic {

// What a link points at: read once, from matrix.to, matrix: or xmpp: links.
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
struct xmpp_address {  // a JID
  std::string jid;
  friend bool operator==(const xmpp_address&, const xmpp_address&) = default;
};
}  // namespace link
using link_t = splice::variant<link::person, link::room, link::xmpp_address>;

// %xx decoded.
[[nodiscard]] inline std::string percent_decoded(std::string_view text) {
  std::string out;
  for (std::size_t at = 0; at < text.size(); ++at) {
    if (text[at] == '%' && at + 2 < text.size()) {
      unsigned value = 0;
      if (std::from_chars(text.data() + at + 1, text.data() + at + 3, value, 16).ec == std::errc{}) {
        out += static_cast<char>(value);
        at += 2;
        continue;
      }
    }
    out += text[at];
  }
  return out;
}

// A Matrix ID read by its sigil into what it names; none for another.
[[nodiscard]] inline std::optional<link_t> matrix_id_of(std::string id, std::optional<std::string> event = std::nullopt,
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
      out.push_back(percent_decoded(pair.substr(4)));
    at = end + 1;
  }
  return out;
}
// A path and its query, apart.
inline std::pair<std::string_view, std::string_view> split_query(std::string_view path) {
  const auto mark = path.find('?');
  return mark == std::string_view::npos ? std::pair(path, std::string_view())
                                        : std::pair(path.substr(0, mark), path.substr(mark + 1));
}

// https://matrix.to/#/<id>[/<event>][?via=...]
inline std::optional<link_t> from_matrix_to(std::string_view rest) {
  const auto [path, query] = split_query(rest);
  const auto slash = path.find('/');
  std::optional<std::string> event;
  if (slash != std::string_view::npos)
    event = percent_decoded(path.substr(slash + 1));
  // What follows the room is an event, and an event id begins with $: a
  // second address there is not one, and the link is no link to a message.
  if (event && !event->starts_with('$'))
    return std::nullopt;
  return matrix_id_of(percent_decoded(path.substr(0, slash)), std::move(event), via_of(query));
}

// matrix:u/<user>, matrix:r/<alias>, matrix:roomid/<id>[/e/<event>] (MSC2312),
// each part's name read into the sigil of the ID it names.
inline std::optional<link_t> from_matrix_uri(std::string_view rest) {
  auto [path, query] = split_query(rest);
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
  const std::string name = percent_decoded(part());
  std::optional<std::string> event;
  if (part() == "e")
    event = "$" + percent_decoded(part());
  return matrix_id_of(kind->second + name, std::move(event), via_of(query));
}

// xmpp:<jid>[?...]
inline std::optional<link_t> from_xmpp(std::string_view rest) {
  const auto [path, query] = split_query(rest);
  if (path.empty())
    return std::nullopt;
  return link::xmpp_address{percent_decoded(path)};
}
}  // namespace detail

// Whether a link is one the system's opener may be given: a web page or a
// mail address, by its scheme, in any case (RFC 3986: schemes are
// case-insensitive) -- nothing that names a file, a share or a program.
[[nodiscard]] inline bool opens_outside(std::string_view url) {
  static constexpr std::array<std::string_view, 3> allowed{"https:", "http:", "mailto:"};
  const auto colon = url.find(':');
  if (colon == std::string_view::npos)
    return false;
  const std::string scheme = url.substr(0, colon + 1) | std::views::transform([](char c) {
                               return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                             }) |
                             std::ranges::to<std::string>();
  return std::ranges::contains(allowed, std::string_view(scheme));
}

// A link read into what it points at, by its scheme; none for one that
// points outside the program -- the browser's.
[[nodiscard]] inline std::optional<link_t> link_of(std::string_view url) {
  using reader = std::optional<link_t> (*)(std::string_view);
  static constexpr std::array<std::pair<std::string_view, reader>, 3> schemes{{
      {"https://matrix.to/#/", &detail::from_matrix_to},
      {"matrix:", &detail::from_matrix_uri},
      {"xmpp:", &detail::from_xmpp},
  }};
  for (const auto& [prefix, read] : schemes)
    if (url.starts_with(prefix))
      return read(url.substr(prefix.size()));
  return std::nullopt;
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
namespace detail {
inline std::string with_via(std::string link, const std::vector<std::string>& via) {
  for (std::size_t i = 0; i < via.size(); ++i)
    link += (i == 0 ? "?via=" : "&via=") + via[i];
  return link;
}
}  // namespace detail
// A link to a room: by its alias where it has one, which needs no servers;
// by its ID with the servers to join through where not.
[[nodiscard]] inline std::string room_link(const conversation& chat) {
  if (chat.alias)
    return "https://matrix.to/#/" + *chat.alias;
  return detail::with_via("https://matrix.to/#/" + chat.id.id, via_servers(chat));
}
// A link to a message in it: by the room's ID, with its servers.
[[nodiscard]] inline std::string message_link(const conversation& chat, std::string_view event) {
  return detail::with_via("https://matrix.to/#/" + chat.id.id + "/" + std::string(event), via_servers(chat));
}

// What following a link comes to.
namespace link_step {
struct open_chat {  // a chat opened, and a message in it jumped to
  conversation_id chat;
  std::optional<std::string> event;
  friend bool operator==(const open_chat&, const open_chat&) = default;
};
struct member_page {  // a person of the chat being read: their page
  std::string user;
  friend bool operator==(const member_page&, const member_page&) = default;
};
struct say {  // nothing to open: said why
  std::string title, text;
  friend bool operator==(const say&, const say&) = default;
};
struct join {  // a room not joined: joined through an account, then opened
  account_id by;
  std::string room;
  std::vector<std::string> via;
  friend bool operator==(const join&, const join&) = default;
};
}  // namespace link_step
using link_step_t = splice::variant<link_step::open_chat, link_step::member_page, link_step::say, link_step::join>;

// The chat a link names, where the model has it: a room by its id or alias,
// an XMPP address by its JID -- in an account of the protocol it is of.
[[nodiscard]] inline std::optional<conversation_id> chat_of(const model& now, const link_t& where) {
  const auto named = [&](bool matrix, std::string_view id) -> std::optional<conversation_id> {
    for (const auto& [account, one] : now.accounts())
      for (const auto& [key, chat] : one.conversations)
        // By its id, its main address, or any other it publishes.
        if (is_matrix(account.speaks) == matrix &&
            (chat.id.id == id || (chat.alias && *chat.alias == id) || std::ranges::contains(chat.other_aliases, id)))
          return chat.id;
    return std::nullopt;
  };
  return splice::visit(splice::overloaded{[](const link::person&) { return std::optional<conversation_id>(); },
                               [&](const link::room& one) { return named(true, one.id); },
                               [&](const link::xmpp_address& one) { return named(false, one.jid); }},
                    where);
}

// Where a link leads, from the chat being read and the account in view.
[[nodiscard]] inline link_step_t where_to(const model& now, const link_t& where,
                                          const std::optional<account_id>& current) {
  return splice::visit(
      splice::overloaded{
          // A person: their card, as Telegram opens a mention's profile --
          // a member of the chat being read or not; a message to them is
          // one press from there.
          [&](const link::person& one) -> link_step_t { return link_step::member_page{one.id}; },
          // Joined: opened. Not: joined through the account in view, or the
          // first Matrix one.
          [&](const link::room& one) -> link_step_t {
            if (const auto found = chat_of(now, where))
              return link_step::open_chat{*found, one.event};
            std::optional<account_id> by;
            if (current && is_matrix(current->speaks))
              by = current;
            for (const auto& [account, kept] : now.accounts())
              if (!by && is_matrix(account.speaks))
                by = account;
            if (!by)
              return link_step::say{"No Matrix account", "A Matrix account is needed to open that room."};
            return link_step::join{*by, one.id, one.via};
          },
          [&](const link::xmpp_address& one) -> link_step_t {
            if (const auto found = chat_of(now, where))
              return link_step::open_chat{*found, std::nullopt};
            return link_step::say{"Not joined", std::format("{} is not in your list.", one.jid)};
          }},
      where);
}

}  // namespace mux::logic
