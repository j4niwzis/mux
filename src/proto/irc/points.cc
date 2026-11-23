// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.irc -- What IRC says when the client asks (mux.proto): its
// overloads, on its tag, found by ADL. What it leaves out is mux.proto's
// default.
export module mux.proto.irc;

import std;
import splice;
import mux.core;
import mux.proto;
import mux.proto.irc.changes;

export namespace mux::proto::irc {

inline chat_form direct_chat_form(const state&) {
  return {"nick", "Start a private conversation with someone on the network by their nick."};
}
// A channel is joined by its name: typed in Explore, it is joined at once.
constexpr bool offers(const state&, feature::room_creation) { return false; }
// Nothing of a room's events is shown as its own line yet.
inline std::vector<room_event_t> room_event_kinds(const state&) { return {}; }
constexpr bool has_room_events(const state&) { return false; }

// Edits: IRC has none -- what was said stays said.
struct no_edits {};
[[nodiscard]] inline bool allows(no_edits, const conversation&, const message&) { return false; }
inline no_edits edit_rule(const state&) { return {}; }
// Older lines asked of the server where it keeps them (IRCv3 chathistory).
constexpr bool can_page_back(const state& now) { return now.history; }
// One's own lines taken back where the server lets them be (REDACT); not
// known here, so offered, and refused by the server where it does not.
inline bool may_delete(const state& now, const conversation&, bool outgoing) { return now.online && outgoing; }

// A channel is left (PART); a private conversation is closed.
inline bool can_leave(const state&, const conversation&) { return true; }

// What may be done in a chat: write while connected and, in a channel,
// while in it.
inline const channel_part& channel_of(const conversation& chat) {
  static const channel_part none;
  const auto* part = chat.theirs.template get_if<channel_part>();
  return part ? *part : none;
}
inline part::chat_rights chat_rights(const state& now, const conversation& chat) {
  const bool group = spl::visit(spl::overloaded{[](conversation_kind::group) { return true; }, [](const auto&) { return false; }}, chat.kind);
  return {now.online && (!group || channel_of(chat).joined), now.online && group && channel_of(chat).joined, false};
}

// A line typed in a chat, read once into what it asks: IRC's commands.
namespace command {
struct join { std::string channels, keys; };   // /join #a,#b [keys]
struct part { std::string reason; };           // /part [reason]
struct me { std::string text; };               // /me waves
struct msg { std::string to, text; };          // /msg nick text, /query nick [text]
struct nick { std::string to; };               // /nick new
struct topic { std::string text; };            // /topic text
struct quote { std::string line; };            // /quote RAW LINE, /raw
struct notice { std::string to, text; };       // /notice target text
}  // namespace command
using command_t = spl::variant<command::join, command::part, command::me, command::msg, command::nick, command::topic,
                               command::quote, command::notice>;
namespace request {
// A command, for the chat it was typed in.
struct command {
  mux::conversation_id to;
  command_t what;
};
}  // namespace request
constexpr request_list<request::command> requests_of(const state&) { return {}; }

namespace command_word {
struct join {};
struct part {};
struct me {};
struct msg {};
struct query {};
struct nick {};
struct topic {};
struct quote {};
struct notice {};
}  // namespace command_word
using command_word_t = spl::variant<command_word::join, command_word::part, command_word::me, command_word::msg,
                                    command_word::query, command_word::nick, command_word::topic, command_word::quote,
                                    command_word::notice>;
[[nodiscard]] inline std::optional<command_word_t> command_word_of(std::string_view word) {
  static const std::array<std::pair<std::string_view, command_word_t>, 11> known{{
      {"join", command_word::join{}}, {"j", command_word::join{}}, {"part", command_word::part{}},
      {"me", command_word::me{}}, {"msg", command_word::msg{}}, {"query", command_word::query{}},
      {"nick", command_word::nick{}}, {"topic", command_word::topic{}}, {"quote", command_word::quote{}},
      {"raw", command_word::quote{}}, {"notice", command_word::notice{}}}};
  const auto lower = word | std::views::transform([](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }) |
                     std::ranges::to<std::string>();
  const auto found = std::ranges::find(known, std::string_view(lower), &decltype(known)::value_type::first);
  if (found == known.end()) return std::nullopt;
  return found->second;
}
// The first word, and the rest after the spaces that follow it.
[[nodiscard]] inline std::pair<std::string_view, std::string_view> first_word(std::string_view text) {
  const auto space = text.find(' ');
  if (space == std::string_view::npos) return {text, {}};
  const auto rest = text.substr(space);
  return {text.substr(0, space), rest.substr(std::min(rest.find_first_not_of(' '), rest.size()))};
}
[[nodiscard]] inline std::optional<command_t> command_from(command_word::join, std::string_view rest) {
  const auto [channels, keys] = first_word(rest);
  if (channels.empty()) return std::nullopt;
  return command::join{std::string(channels), std::string(keys)};
}
[[nodiscard]] inline std::optional<command_t> command_from(command_word::part, std::string_view rest) {
  return command::part{std::string(rest)};
}
[[nodiscard]] inline std::optional<command_t> command_from(command_word::me, std::string_view rest) {
  return command::me{std::string(rest)};
}
[[nodiscard]] inline std::optional<command_t> command_from(command_word::msg, std::string_view rest) {
  const auto [to, text] = first_word(rest);
  if (to.empty() || text.empty()) return std::nullopt;
  return command::msg{std::string(to), std::string(text)};
}
[[nodiscard]] inline std::optional<command_t> command_from(command_word::query, std::string_view rest) {
  const auto [to, text] = first_word(rest);
  if (to.empty()) return std::nullopt;
  return command::msg{std::string(to), std::string(text)};
}
[[nodiscard]] inline std::optional<command_t> command_from(command_word::nick, std::string_view rest) {
  const auto [to, after] = first_word(rest);
  if (to.empty()) return std::nullopt;
  return command::nick{std::string(to)};
}
[[nodiscard]] inline std::optional<command_t> command_from(command_word::topic, std::string_view rest) {
  return command::topic{std::string(rest)};
}
[[nodiscard]] inline std::optional<command_t> command_from(command_word::quote, std::string_view rest) {
  if (rest.empty()) return std::nullopt;
  return command::quote{std::string(rest)};
}
[[nodiscard]] inline std::optional<command_t> command_from(command_word::notice, std::string_view rest) {
  const auto [to, text] = first_word(rest);
  if (to.empty() || text.empty()) return std::nullopt;
  return command::notice{std::string(to), std::string(text)};
}
// A line typed in an IRC chat, as a command: what it asks done, or nothing
// -- the line, sent as it is ("//" sends a line beginning with "/").
[[nodiscard]] inline std::optional<request::command> command_of(const state&, const conversation_id& to, std::string_view text) {
  if (!text.starts_with('/') || text.starts_with("//")) return std::nullopt;
  const auto [word, rest] = first_word(text.substr(1));
  const auto known = command_word_of(word);
  if (!known) return std::nullopt;
  auto what = spl::visit([&](auto one) { return command_from(one, rest); }, *known);
  if (!what) return std::nullopt;
  return request::command{to, std::move(*what)};
}

// A nick has no local part; an address's is its nick.
inline std::string local_part(const state&, std::string_view address) {
  const auto at = address.find('@');
  const auto scheme = address.find("://");
  const auto from = scheme == std::string_view::npos ? 0 : scheme + 3;
  return std::string(address.substr(from, at == std::string_view::npos ? std::string_view::npos : at - from));
}

}  // namespace mux::proto::irc
