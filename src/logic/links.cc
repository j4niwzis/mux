// SPDX-License-Identifier: AGPL-3.0-only
// mux.logic.links: a link as the program reads it, and what following it
// comes to -- each protocol's kinds of link (links_type(state)), read by the
// protocol's read_link(state, url), and led by step_for(link, ...): all by
// ADL, in its folder; one variant of every kind, made from the lists.
export module mux.logic.links;

import std;
import splice;
import mux.core;
export import mux.logic.link_base;
export import mux.proto.links;

export namespace mux::logic {

namespace links_defaults {
constexpr type_tag<link_list<>> links_type(const auto&) { return {}; }
inline std::optional<std::monostate> read_link(const auto&, std::string_view) { return std::nullopt; }
inline std::optional<std::monostate> read_id(const auto&, std::string_view) { return std::nullopt; }
inline std::optional<conversation_id> chat_for(const auto&, const model&) { return std::nullopt; }
inline mention_t mention_of(const auto&) { return mention::kept{}; }
}  // namespace links_defaults
template <class Tag>
constexpr auto links_type_of() {
  using links_defaults::links_type;
  return links_type(state_of<Tag>{});
}
template <class Kinds, class... More>
struct links_joined {
  using type = Kinds;
};
template <class... Have, class... Theirs, class... More>
struct links_joined<link_list<Have...>, link_list<Theirs...>, More...> : links_joined<link_list<Have..., Theirs...>, More...> {};
template <class>
struct all_links;
template <class... Tags>
struct all_links<protocol_list<Tags...>> {
  using list = typename links_joined<link_list<>, typename decltype(links_type_of<Tags>())::type...>::type;
};
template <class>
struct variant_of_links;
template <class... Kinds>
struct variant_of_links<link_list<Kinds...>> {
  using type = spl::variant<Kinds...>;
};
// What a link points at, of every protocol's kinds.
using link_t = typename variant_of_links<typename all_links<protocols>::list>::type;

// A protocol's own, as any link.
template <class Theirs>
[[nodiscard]] link_t as_link(const Theirs& one) {
  return spl::visit([](const auto& kind) { return link_t{kind}; }, one);
}
// What a protocol read, as any link: none where it reads none of the kind.
inline std::optional<link_t> as_some_link(const std::optional<std::monostate>&) { return std::nullopt; }
template <class Theirs>
std::optional<link_t> as_some_link(const std::optional<Theirs>& read) {
  if (read)
    return as_link(*read);
  return std::nullopt;
}
template <class Tag>
std::optional<link_t> read_link_of(std::string_view url) {
  using links_defaults::read_link;
  return as_some_link(read_link(state_of<Tag>{}, url));
}
template <class Tag>
std::optional<link_t> read_id_of(std::string_view word) {
  using links_defaults::read_id;
  return as_some_link(read_id(state_of<Tag>{}, word));
}
template <class... Tags>
std::optional<link_t> first_link(protocol_list<Tags...>, std::string_view url) {
  std::optional<link_t> found;
  (void)((found = read_link_of<Tags>(url)).has_value() || ...);
  return found;
}
template <class... Tags>
std::optional<link_t> first_id(protocol_list<Tags...>, std::string_view word) {
  std::optional<link_t> found;
  (void)((found = read_id_of<Tags>(word)).has_value() || ...);
  return found;
}
// A link read into what it points at, by the protocol whose it is; none for
// one that points outside the program -- the browser's.
[[nodiscard]] inline std::optional<link_t> link_of(std::string_view url) { return first_link(protocols{}, url); }
// An ID as it is written -- typed, or said in a message -- read the same way.
[[nodiscard]] inline std::optional<link_t> link_of_id(std::string_view word) { return first_id(protocols{}, word); }

// The chat a link names, where the model has it.
[[nodiscard]] inline std::optional<conversation_id> chat_of(const model& now, const link_t& where) {
  return spl::visit([&](const auto& kind) {
    using links_defaults::chat_for;
    return chat_for(kind, now);
  }, where);
}
// What a link is in a message: as its kind says.
[[nodiscard]] inline mention_t mention_in(const link_t& where) {
  return spl::visit([](const auto& kind) {
    using links_defaults::mention_of;
    return mention_of(kind);
  }, where);
}
// Where a link leads, from the chat being read and the account in view: as
// its protocol says (step_for).
[[nodiscard]] inline link_step_t where_to(const model& now, const link_t& where, const std::optional<account_id>& current) {
  return spl::visit([&](const auto& kind) { return step_for(kind, now, current); }, where);
}

}  // namespace mux::logic
