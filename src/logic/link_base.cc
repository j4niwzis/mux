// SPDX-License-Identifier: AGPL-3.0-only
// mux.logic.link_base: what reading a link and following it share, whatever
// the protocol: %xx decoded, a path and its query apart, where a link may
// be opened outside, what following one comes to (link_step), and a chat
// found by a name of it. Each protocol's links -- their kinds, how they are
// read, where each leads -- are its own (src/proto/<p>/links.cc).
export module mux.logic.link_base;

import std;
import splice.bytes;
import splice;
import mux.core;

export namespace mux::logic {

// The kinds of link a protocol reads, as a list (its links_type(state)).
template <class... Kinds>
struct link_list {};

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
// A path and its query, apart.
[[nodiscard]] inline std::pair<std::string_view, std::string_view> split_query(std::string_view path) {
  const auto mark = path.find('?');
  return mark == std::string_view::npos ? std::pair(path, std::string_view())
                                        : std::pair(path.substr(0, mark), path.substr(mark + 1));
}

// Whether a link is one the system's opener may be given: a web page or a
// mail address, by its scheme, in any case (RFC 3986: schemes are
// case-insensitive) -- nothing that names a file, a share or a program.
[[nodiscard]] inline bool opens_outside(std::string_view url) {
  static constexpr std::array<std::string_view, 3> allowed{"https:", "http:", "mailto:"};
  const auto colon = url.find(':');
  if (colon == std::string_view::npos)
    return false;
  const std::string scheme = spl::bytes::lower_text(url.substr(0, colon + 1));
  return std::ranges::contains(allowed, std::string_view(scheme));
}

// What a link is in a message, whatever its protocol: a person -- a pill by
// their name in the chat; a place -- a room: a pill where it is known, a card
// where the link is given as its URL or to a message in it -- with the link
// its pill opens; or kept as the link it is. Each kind says its own
// (mention_of(kind), by ADL); one that says none is kept.
namespace mention {
struct person {
  std::string id;
};
struct place {
  std::string id;
  std::optional<std::string> event;  // a message in it
  std::string link;                  // what a pill to it opens
};
struct kept {};
}  // namespace mention
using mention_t = spl::variant<mention::person, mention::place, mention::kept>;

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
using link_step_t = spl::variant<link_step::open_chat, link_step::member_page, link_step::say, link_step::join>;

// A chat of an account of the protocol, named by its id, its main address
// or any other it publishes.
[[nodiscard]] inline std::optional<conversation_id> chat_named(const model& now, const protocol_t& speaks, std::string_view id) {
  for (const auto& [account, one] : now.accounts())
    for (const auto& [key, chat] : one.conversations)
      if (account.speaks == speaks &&
          (chat.id.id == id || (chat.alias && *chat.alias == id) || std::ranges::contains(chat.other_aliases, id)))
        return chat.id;
  return std::nullopt;
}
// An account of the protocol: the one in view where it is, else the first.
[[nodiscard]] inline std::optional<account_id> account_speaking(const model& now, const std::optional<account_id>& current,
                                                               const protocol_t& speaks) {
  if (current && current->speaks == speaks)
    return current;
  for (const auto& [account, kept] : now.accounts())
    if (account.speaks == speaks)
      return account;
  return std::nullopt;
}

}  // namespace mux::logic
