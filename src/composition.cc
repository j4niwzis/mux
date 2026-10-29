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
// What a plugin asks a passphrase (or the account's password) for, in the
// client's passphrase dialog (mux.ui.base:forms), beside the client's and
// the protocols':
//   passphrases(c, f) -> proto::passphrase_list<its purposes...>; each with
//   passphrase_text(purpose) -> proto::passphrase_words, and done by
//   passphrase_given(program, purpose, given) -- found by ADL
struct passphrases {
  static constexpr std::string_view name = "the passphrases asked";
};
// The Matrix account, made by a plugin over the client's own
// (mux.proto.matrix.client, account_of): end-to-end encryption's.
//   matrix_account_made(c, f, type_tag<Sink>) -> std::type_identity<its
//   account>, a class over client::account<Sink> that hides its hooks
struct matrix_account {
  static constexpr std::string_view name = "the Matrix account";
};
// The media panel's pages beside its emoji (the emoji picker's:
// mux.plugin.emoji_picker.ui):
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
// A message's cards: what goes in its bubble after its text and its blocks,
// before its trailers -- a link's preview (mux.ui.messages:message):
//   extend(c, f, card_facts) -> std::optional<its node>: none where it has
//   nothing to show of the message; the time goes under a card, not
//   beside it; and pressed_link(c, f, node, x, y), the link a press there
//   is on, for the message's menu, where it has one
struct message_cards {
  static constexpr std::string_view name = "a message's cards";
};
// Under a message's row, at its right (mux.ui.messages:message): who has
// read up to it, for one:
//   extend(c, f, footer_facts) -> std::optional<its node>, placed in the
//   room under the row; footer_height(c, f), how much room it takes
struct message_footers {
  static constexpr std::string_view name = "a message's footers";
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
// The buttons over the chat list's bottom right corner, over the client's
// own (mux.ui.messages:timeline): extend(c, f, palette) -> its node, shown
// or not as its part keeps in the model
struct list_corner {
  static constexpr std::string_view name = "the list's corner";
};
// A panel beside the chat, in the info's place (mux.ui:conversations_screen):
//   extend(c, f, ui_needs) -> its panel, shown while the screen says a
//   plugin's panel is up (screen_shown::panel_up) -- the threads', for one
struct beside_panels {
  static constexpr std::string_view name = "the panels beside the chat";
};
// What a chat's head says of it under its name, over what its protocol
// says (mux.ui:header): extend(c, f, conversation) ->
// std::optional<std::string>, none where it has nothing to say now -- who
// is typing, for one; the first that says something says it
struct chat_status {
  static constexpr std::string_view name = "the chat's status";
};
// The side's head, its buttons after the space bar (mux.ui:conversations_side):
//   extend(c, f, palette) -> its button: what it opens its plugin's own
struct side_header {
  static constexpr std::string_view name = "the side's head";
};
// A space's menu in the side, its rows after the space's name
// (mux.ui:conversations_side), each shown for a space:
//   label(c, f) -> what it says; act(c, f, room) -> what a press asks of
//   its plugin's own (a callable with its Answer)
struct space_menu {
  static constexpr std::string_view name = "a space's menu";
};
// Over the chat's composer, under its messages (mux.ui:conversations_screen):
//   extend(c, f, palette) -> its node -- the list of what an @ or a ':'
//   asks, for one; its part says the screen is to catch the list's keys
//   (services::composer_list_up) while it is up
struct composer_top {
  static constexpr std::string_view name = "over the composer";
};
// The drawer's rows, after the client's own new chat (mux.ui:drawer):
//   extend(c, f, palette) -> its row, row_item(...) as the others are: what
//   it opens its plugin's own
struct drawer_rows {
  static constexpr std::string_view name = "the drawer's rows";
};
// On each page a chat's looks are chosen on, for its level -- Appearance
// (every chat's), an account's page (its own), a room's Appearance tab
// (the room's) (mux.ui:info): extend(c, f, palette, choice_level_t) -> its
// section, the same node whatever the level
struct level_sections {
  static constexpr std::string_view name = "a level's sections";
};
// What a folder of the chat list is beside the client's own (All, Direct
// messages, a roster group) -- a space's (mux.app.chat_list), each asked
// with the folder_context the list is worked out in:
//   is_folder(c, f, ctx, chat) -> bool: a chat that is a folder, not listed
//   folder_rooms(c, f, ctx, folder) -> std::optional<std::set<std::string>>:
//   the rooms a folder it knows shows; none for the others
//   out_of_home(c, f, ctx) -> std::set<std::string>: rooms Home leaves out
//   folder_tabs(c, f, ctx, tabs&): its folders as tabs over the list
//   keeps_folder(c, f, ctx, folder) -> bool: a folder it shows elsewhere
//   (a bar), kept chosen though no tab lists it
//   space_bars(c, f, ctx, list, side&, ui_shared&): the bars' icons
struct chat_folders {
  static constexpr std::string_view name = "the chat list's folders";
};
// Bars beside the chat list, of folders a plugin knows -- the spaces' --
// in the side column (mux.ui:conversations_side): side_lane(c, f, palette),
// its lane down the side; top_bar(c, f, palette, big), its bar in the head
// (or under the search, big); wrap_side(c, f, palette, column), the column
// wrapped -- its menus over it, its icons dragged between its bars. One
// plugin at most
struct side_bars {
  static constexpr std::string_view name = "the side's bars";
};
// Settings' home page, its links after the client's own (mux.ui:settings_pages):
//   extend(c, f, palette) -> its row, settings_link(...) as the others are:
//   what it opens its plugin's own
struct settings_links {
  static constexpr std::string_view name = "Settings' links";
};
// What a plugin keeps among what the window shows, beside its dialogs and
// layers (mux.ui:window): state_type(c, f) -> type_tag<a value of its own>,
// one of it held in the shown model -- set by its part (ui::show), read by
// its nodes (shown_for, text_for); a proxy editor's error line, for one
struct window_state {
  static constexpr std::string_view name = "the window's state";
};
// A room's settings, their tabs after the client's and its protocol's
// (mux.ui:room_settings), as a protocol's own are made:
//   room_tabs(c, f) -> page_list<its tabs...>; each tab a type of its own,
//   with tab_title(tab), tab_icon(tab), make_manage_page(tab, palette,
//   room_settings_facts) and, where not every room has it,
//   tab_visible(tab, facts) -- found by ADL
struct room_settings_tabs {
  static constexpr std::string_view name = "a room's settings' tabs";
};
// An account's pages in Manage accounts, beside the client's and its
// protocol's (mux.ui:accounts), as a protocol's own are made:
//   account_pages(c, f) -> page_list<its pages...>; each page a type of
//   its own, with page_title(page), page_icon(page) and page_view(page,
//   palette, account_page_facts) -> its node, found by ADL; and, where it
//   is for some protocols' accounts only, page_shown(page, their state)
//   -> whether it is listed for that one
template <class... Pages>
struct page_list {};
struct account_pages {
  static constexpr std::string_view name = "an account's pages";
};
// Settings' pages (mux.ui:settings): a plugin's own page, opened by its
// link (contract::settings_links):
//   settings_pages(c, f) -> page_list<its pages...>: each what it shows, in
//   settings_page_t; extend(c, needs, page) -> that page's node, made from
//   it; Esc and its <- go to Settings' home, its own requests its plugin's
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
