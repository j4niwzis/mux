// SPDX-License-Identifier: AGPL-3.0-only
// mux.composition -- How a plugin extends what something else owns.
//
// A plugin is a folder, src/plugins/<name>/ (cmake/plugins.cmake), known by
// its feature type, mux::plugins::<name>::feature, which says its name; the
// ones turned on are mux::enabled_plugins (mux.plugins.types, written by
// CMake). A contract is a type owned by what is extended, which says its name
// too. A plugin that extends one says so --
//   provides(contract, type_tag<feature>) -> true
// -- and gives what the contract asks, overloads on the same two found by ADL:
// what a contract's owner asks of each of providers<Contract, List>, where the
// owner is instantiated. What a plugin does not say it provides it does not;
// what it says it provides must be there, or the build stops on broken(),
// which names both.
export module mux.composition;

import std;
export import mux.proto.identity;

export namespace mux {

// Plugins, by their feature types.
template <class... Features>
struct plugin_list {};

// The contracts the client's own parts own, named low enough that a
// plugin's types can say they provide them; each what it asks is said where
// it is used.
namespace contract {
// The media panel's pages beside its emoji and stickers (mux.ui:emoji_popup):
//   page_type(c, f)  -> type_tag<its page>: in popup_page_t, what is shown
//   shown_type(c, f) -> type_tag<what is shown on it>: in emoji_facts
//   tab_name(page), shown_where(page, facts): of its page; opened(page),
//   what it asks for as it is chosen, where it asks something
//   extend(c, f, needs, shown) -> its node, or in_place<node, args...> where
//   it may not move, in the window; the emote pictures in it drawn without
//   their pictures are fetched by the program (emotes_wanted)
struct media_pages {
  static constexpr std::string_view name = "the media panel's pages";
};
// A message's menu, its rows after Save As (mux.ui:context_menu):
//   extend(c, f, palette, emoji_kept, menu_facts) -> its row: shown,
//   or not, as the facts say, and what it sends its plugin's own
struct message_menu {
  static constexpr std::string_view name = "a message's menu";
};
// A message's trailers: what goes after its text, at the foot of its bubble,
// that its time sits beside (mux.ui:message):
//   extend(c, f, trailer_facts) -> std::optional<its node>:
//   none where it has nothing to show of the message; and
//   trailing_width(c, f, node), how far its last line goes, for the time
//   beside it
struct message_trailers {
  static constexpr std::string_view name = "a message's trailers";
};
// The window's dialogs (mux.ui:window): what a plugin's dialog shows -- its
// facts, a part of what is shown -- and its box:
//   facts_type(c, f) -> type_tag<its facts>, in the plugin's types
//   extend(c, f) -> type_tag<its box>, a dialog's content
//   made from ui_needs and the facts (make_content, content_look)
// Closed by request::close_shown<its facts>, which its part takes.
struct window_dialogs {
  static constexpr std::string_view name = "the window's dialogs";
};
// A message's menu's top, over its rows (mux.ui:context_menu):
//   extend(c, f, palette, menu_facts) -> its head: shown, or not, as the
//   facts say; what answers menu_unroll<f> unrolls its own over the items,
//   and it reads message_menu_state (a Local of the menu's) to know it has
//   unrolled
//   unrolled(c, f, palette, emoji_kept) -> that, made then
struct message_menu_top {
  static constexpr std::string_view name = "a message's menu's top";
};
// What a message's menu has open, its own: which of its tops unrolled
// (its place among them, from 1; none, 0), and its list of who has seen it
// -- while its row or the list is pointed at, from the row's box.
struct message_menu_state {
  std::size_t unrolled = 0;
  bool seen_row = false, seen_list = false;
  float seen_left = 0.0f, seen_top = 0.0f, seen_right = 0.0f, seen_bottom = 0.0f;
  friend bool operator==(const message_menu_state&, const message_menu_state&) = default;
};
// A top asking to unroll its own.
template <class Feature> struct menu_unroll {};
// The window's layers, over everything, not dialogs: a call's card and its
// whole-window view (mux.ui:window):
//   facts_type(c, f) -> type_tag<what is shown of it>, in its types
//   extend(c, f, needs) -> its node, made from what is
//   shown (bound or derived from those facts)
struct window_layers {
  static constexpr std::string_view name = "the window's layers";
};
// The chat's column, under its head: a call's view in its chat
// (mux.ui:conversations_screen): extend(c, f, needs) ->
// its node, there whatever chat is shown.
struct chat_column {
  static constexpr std::string_view name = "the chat column";
};
// The chat's head, its buttons after Search (mux.ui:header):
//   extend(c, f, palette) -> its button
struct chat_header {
  static constexpr std::string_view name = "the chat's head";
};
// Settings' home page, its links after the client's own (mux.ui:proxies):
//   extend(c, f, palette) -> its row, settings_link(...) as the others are:
//   what it opens its plugin's own
struct settings_links {
  static constexpr std::string_view name = "Settings' links";
};
// An account's pages in Manage accounts, beside the client's and its
// protocol's (mux.ui:accounts), as a protocol's own are made:
//   account_pages(c, f) -> page_list<its pages...>; each page a type of
//   its own, with page_title(page), page_icon(page) and page_view(page,
//   palette, account_page_facts) -> its node, found by ADL
template <class... Pages>
struct page_list {};
struct account_pages {
  static constexpr std::string_view name = "an account's pages";
};
// Settings' pages (mux.ui:settings): a plugin's own page, opened by its
// link (contract::settings_links):
//   page_type(c, f) -> type_tag<its page>: what it shows, in settings_page_t
//   extend(c, needs, page) -> its page's node, made from it; Esc and its <-
//   go to Settings' home, its own requests its plugin's
struct settings_pages {
  static constexpr std::string_view name = "Settings' pages";
};
// An account's page in Manage accounts, its rows under its connection
// (mux.ui:accounts): extend(c, f, palette, address) -> its row, shown or
// not as the account's protocol is -- what it opens its plugin's own
struct account_rows {
  static constexpr std::string_view name = "an account's rows";
};
// What can be done with the messages selected, beside Delete in the selection
// bar (mux.ui:header) and in their text menu (mux.ui:window):
//   label(c, f) -> what it says: "<label> 3" on the bar, "<label> selected"
//   in the menu; allowed(c, f, ui_shared, chat) -> whether it is offered
//   there. Pressed, request::selection_asked<f>: the selection's owner hands
//   the messages over as request::selection_taken<f>, which its part takes.
struct selection_actions {
  static constexpr std::string_view name = "the selection's actions";
};
// The program's parts (mux.app.program): extend(c, f, services&) -> a part,
// offered the requests as the program's own are.
// A part may also take the model's changes (take(change), as they come)
// and do what it does between events (tick()), where it says so.
struct program_parts {
  static constexpr std::string_view name = "the program's parts";
};
}  // namespace contract

}  // namespace mux

export namespace mux::composition {

namespace defaults {
consteval bool provides(const auto&, const auto&) { return false; }
}  // namespace defaults
template <class Contract, class Feature>
consteval bool provides() {
  using defaults::provides;
  return provides(Contract{}, type_tag<Feature>{});
}

// Lists joined, in order.
template <class... Lists>
struct joined {
  using type = plugin_list<>;
};
template <class... A>
struct joined<plugin_list<A...>> {
  using type = plugin_list<A...>;
};
template <class... A, class... B, class... Rest>
struct joined<plugin_list<A...>, plugin_list<B...>, Rest...> : joined<plugin_list<A..., B...>, Rest...> {};

// The plugins of a list that say they provide a contract, in the list's order.
template <class Contract, class List>
struct providers_of;
template <class Contract, class... Features>
struct providers_of<Contract, plugin_list<Features...>> {
  using type = typename joined<std::conditional_t<provides<Contract, Features>(), plugin_list<Features>, plugin_list<>>...>::type;
};
template <class Contract, class List>
using providers = typename providers_of<Contract, List>::type;

// What a contribution makes, put where it is kept -- a std::optional slot,
// which the walks go into: a node that may move, as it is; one that may not
// (it points at its own parts), as its constructor's arguments, in_place,
// built at its slot. Overloads, not a test of the type.
template <class Node, class... Args>
struct in_place {
  std::tuple<Args...> args;
};
template <class Made>
struct node_of {
  using type = Made;
};
template <class Node, class... Args>
struct node_of<in_place<Node, Args...>> {
  using type = Node;
};
template <class Made>
using node_t = typename node_of<Made>::type;
template <class Node>
Node& put(std::optional<Node>& slot, Node&& made) {
  return slot.emplace(std::move(made));
}
// Or nothing, where it has nothing to show: the slot left empty.
template <class Made>
struct node_of<std::optional<Made>> {
  using type = Made;
};
template <class Node>
void put(std::optional<Node>& slot, std::optional<Node>&& made) {
  if (made)
    slot.emplace(std::move(*made));
}
template <class Node, class... Args>
Node& put(std::optional<Node>& slot, in_place<Node, Args...>&& made) {
  return std::apply([&](auto&&... arg) -> Node& { return slot.emplace(std::forward<decltype(arg)>(arg)...); }, std::move(made.args));
}

// Parts of a list of types, as an aggregate: what skiff's model looks into
// for a part by its type -- a std::tuple it does not. Each its head, the
// rest its tail; none, an empty one.
template <class... Parts>
struct aggregate_list {};
template <class Head, class... Rest>
struct aggregate_list<Head, Rest...> {
  Head head;
  aggregate_list<Rest...> tail;
};

// What the build says where a plugin says it provides a contract and what it
// gives does not compile: a static_assert's message, made while compiling.
struct message {
  std::array<char, 256> chars{};
  std::size_t length = 0;
  [[nodiscard]] constexpr std::size_t size() const { return length; }
  [[nodiscard]] constexpr const char* data() const { return chars.data(); }
};
template <class Contract, class Feature>
consteval message broken() {
  const std::array<std::string_view, 5> said{"the ", Feature::name, " plugin says it provides ", Contract::name,
                                             ", but what it gives for it does not compile"};
  message out;
  const auto end = std::ranges::copy(said | std::views::join | std::views::take(out.chars.size()), out.chars.begin()).out;
  out.length = static_cast<std::size_t>(end - out.chars.begin());
  return out;
}

}  // namespace mux::composition
