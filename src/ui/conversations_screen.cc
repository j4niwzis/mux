// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:conversations_screen -- The conversations screen: the list, the chat, its info.
export module mux.ui:conversations_screen;

import std;
import mux.logic.text;
import mux.logic.emoji;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.bind;
import skiff.compose;
import skiff.model;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.button;
import skiff.widgets.pill;
import skiff.widgets.textarea;
import mux.core;
import mux.protocols;
import mux.config;
import mux.kept_root;
import skiff.model;
import mux.plugins.types;
import mux.ui.base;
import mux.plugins.ui.top;
import mux.ui.proto;
import :chat_list;
import mux.ui.messages;
import :header;
import :info;

export import :conversations_side;
import :list_shown;

export namespace mux::ui {

namespace head_view_defaults {
constexpr proto::sticker_view_list<> head_views(const auto&) {
  return {};
}
constexpr std::nullopt_t make_head_view(const auto&, const conversation&) {
  return std::nullopt;
}
}  // namespace head_view_defaults
template <class State>
constexpr auto head_views_for(const State& state) {
  using head_view_defaults::head_views;
  return head_views(state);
}
namespace composer_view_defaults {
constexpr proto::sticker_view_list<> composer_views(const auto&) {
  return {};
}
constexpr std::nullopt_t make_composer_view(const auto&, const conversation&) {
  return std::nullopt;
}
}  // namespace composer_view_defaults
template <class State>
constexpr auto composer_views_for(const State& state) {
  using composer_view_defaults::composer_views;
  return composer_views(state);
}

// A row of a list over the field -- the @ list's, the emoji list's: what
// it shows at its start, its text beside it; pressed to pick.
template <class Pick, class Face>
auto suggestion_row(const palette& colours, Pick pick, Face face, std::string first, std::string second) {
  namespace c = skiff::compose;
  auto label = first;
  return c::onPress(std::move(pick), c::row(
      c::hbox(10.0f, {.fillX = true, .height = 44.0f, .padding = {0.0f, 14.0f, 0.0f, 14.0f},
                     .hoverBackground = colours.chosen, .selectedBackground = colours.chosen}),
      std::move(face), two_lines(colours, std::move(first), std::move(second), 14.0f, 1.0f)), std::move(label));
}
// One of the list over the composer, as the program has it: a member (their
// avatar, their name, their id) or an emoji (its picture or character, its
// name, its code), where it is in the list, and whether it is lit.
struct suggestion {
  std::size_t at = 0;
  bool person = false;
  std::string id;       // whose avatar, a member's
  std::string glyph;    // the emoji's character
  std::string picture;  // a custom emoji's picture
  std::string title, sub;
  bool lit = false;
  friend bool operator==(const suggestion&, const suggestion&) = default;
};
struct suggestions_shown {
  std::vector<suggestion> items;
  friend bool operator==(const suggestions_shown&, const suggestions_shown&) = default;
};
using suggestion_face = spl::variant<nodes::Text, nodes::Image<from_emotes>, avatar_mark_t<avatar_of::person>>;
[[nodiscard]] inline suggestion_face face_of(const palette& colours, const suggestion& one) {
  if (one.person)
    return suggestion_face(std::in_place_index<2>, avatar_mark(avatar_of::person{}, one.id, one.title, 28.0f));
  if (one.picture.empty())
    return suggestion_face(std::in_place_index<0>, nodes::Text(one.glyph, 22.0f, colours.text));
  suggestion_face face(std::in_place_index<1>, from_emotes{one.picture});
  spl::get<1>(face).apply({.width = 28.0f, .height = 28.0f, .alignSelf = scene::align::kMiddle});
  return face;
}
// Over the field: the members matching what follows an @ at the end of
// what is written, or the emoji matching what follows a ':' -- six at
// most, the lit one marked; pressed, picked. As the program says.
inline auto suggestions_for(const palette& colours) {
  namespace c = skiff::compose;
  return c::shown_for<suggestions_shown>([](const suggestions_shown& now) { return !now.items.empty(); },
      c::projected<suggestions_shown>([](const suggestions_shown& now) { return now.items; },
          c::listed<suggestion>(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .background = colours.sidebar}),
              [&colours](const suggestion& one) {
                auto row = suggestion_row(colours, sends<request::suggestion_pick>{{one.at}}, face_of(colours, one), one.title, one.sub);
                row.apply({.selected = one.lit});
                return row;
              })));
}
using suggestions_t = decltype(suggestions_for(std::declval<const palette&>()));
// No account at all: what to do about it.
inline auto no_accounts_yet(const palette& colours) {
  using add_button = button_for<sends<::mux::ui::request::open_new_account>>;
  return skiff::compose::column(
      skiff::compose::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {120.0f, 48.0f, 0.0f, 48.0f}}),
      nodes::Text("No accounts yet", 22.0f, colours.text, true),
      skiff::compose::styled({.fillX = true},
                             wrapped(nodes::Text("Add an XMPP or a Matrix account, and its chats will be here.", 14.0f, colours.dim))),
      skiff::compose::styled({.width = 140.0f, .height = 36.0f}, primary(add_button(colours.widgets, "Add account", {}))));
}
// No chat chosen: the wallpaper, and in its middle a small pill saying what
// to do, as tdesktop's (its service message look).
inline auto select_a_chat() {
  return skiff::compose::column(
      skiff::compose::justified(skiff::compose::vbox(0.0f, {.fillX = true, .grow = scene::axes::kY}), nodes::justify::middle{}),
      skiff::compose::styled({.alignSelf = scene::align::kMiddle},
                             widgets::Pill("Select a chat to start messaging",
                                           {.plate = skia::colorSetARGB(0x66, 0, 0, 0), .size = 13.0f, .height = 26.0f, .padX = 12.0f})));
}

// A node of the chat's protocol's own over the composer (a Telegram bot's
// keyboard): listed by composer_views(state), made for
// a chat by make_composer_view, found by ADL; none by default.
template <class List>
struct protocol_view_nodes;
template <class... Vs>
struct protocol_view_nodes<proto::sticker_view_list<Vs...>> {
  using type = type_list<Vs...>;
};
template <class>
struct protocol_composer_nodes;
template <class... Tags>
struct protocol_composer_nodes<protocol_list<Tags...>> {
  using type = typename joined<
      type_list<>, typename protocol_view_nodes<decltype(composer_views_for(::mux::state_of<Tags>{}))>::type...>::type;
};
using composer_view_t = typename variant_of_types<
    typename joined<type_list<nodes::Text>, typename protocol_composer_nodes<protocols>::type>::type>::type;
// And under the chat's header, over its messages (an IRC channel's topic
// and modes, a Telegram channel's join button): head_views(state,
// type_tag), made by make_head_view.
template <class>
struct protocol_head_nodes;
template <class... Tags>
struct protocol_head_nodes<protocol_list<Tags...>> {
  using type = typename joined<
      type_list<>, typename protocol_view_nodes<decltype(head_views_for(::mux::state_of<Tags>{}))>::type...>::type;
};
using head_view_t = typename variant_of_types<
    typename joined<type_list<nodes::Text>, typename protocol_head_nodes<protocols>::type>::type>::type;
using head_view_holder = skiff::compose::Holding<head_view_t>;
using composer_view_holder = skiff::compose::Holding<composer_view_t>;

// The chat chosen, as the chats hold it now: what its protocol's own nodes
// are made from -- made again as the chats move.
struct chosen_chat_key {
  conversation_id id;
  skiff::model::Revision revision = 0;
  const chats_model* chats = nullptr;
  const ui_shared* shared = nullptr;
  friend bool operator==(const chosen_chat_key& a, const chosen_chat_key& b) { return a.id == b.id && a.revision == b.revision; }
};
[[nodiscard]] inline const conversation* chat_of(const chosen_chat_key& key) {
  const account* in = key.chats->root().accounts.find(key.id.account);
  return in == nullptr ? nullptr : in->conversations.find(key.id.id);
}
// A protocol's own node for the chat chosen, where it makes one (Make: its
// make_head_view or make_composer_view, found by ADL); none by default.
template <class View, class Holder> [[nodiscard]] std::optional<Holder> held_view(std::optional<View> made) {
  return made ? std::optional<Holder>(Holder(std::move(*made))) : std::nullopt;
}
template <class Holder> [[nodiscard]] std::optional<Holder> held_view(std::nullopt_t) { return std::nullopt; }
struct make_head_of {
  template <class State> auto operator()(const State& state, const conversation& one) const {
    using head_view_defaults::make_head_view;
    return make_head_view(state, one);
  }
};
struct make_composer_of {
  template <class State> auto operator()(const State& state, const conversation& one) const {
    using composer_view_defaults::make_composer_view;
    return make_composer_view(state, one);
  }
};
template <class Holder, class Make>
[[nodiscard]] auto protocol_view_made(const chosen_chat_key& key) {
  namespace c = skiff::compose;
  std::optional<Holder> held;
  if (const conversation* one = chat_of(key))
    held = spl::visit([&](const auto& state) { return held_view<Holder>(Make{}(state, *one)); }, protocol_state_of(*key.shared, key.id.account));
  return c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(held));
}
template <class Holder, class Make>
auto protocol_view_for(const ui_needs& n) {
  namespace c = skiff::compose;
  using made_t = decltype(protocol_view_made<Holder, Make>(std::declval<const chosen_chat_key&>()));
  return c::bound<chat_shown>(c::with_model<chats_model, chat_shown>(
      [shared = n.shared](const chats_model& chats, const chat_shown& now) {
        return now.chosen ? std::optional(chosen_chat_key{*now.chosen, chats.revision(), &chats, shared}) : std::nullopt;
      },
      c::mount<made_t, chosen_chat_key>([](const chosen_chat_key& key) { return std::tuple{protocol_view_made<Holder, Make>(key)}; },
                                        {.fillX = true, .autoSize = scene::axes::kY})));
}

// What plugins put in the chat's column under its head
// (contract::chat_column), in their order.
template <class List>
struct column_parts;
template <class... Features>
struct column_parts<plugin_list<Features...>> {
  template <class Feature>
  static auto made_of(const ui_needs& n) -> decltype(extend(contract::chat_column{}, type_tag<Feature>{}, n)) {
    static_assert(requires { extend(contract::chat_column{}, type_tag<Feature>{}, n); },
                  composition::broken<contract::chat_column, Feature>());
    return extend(contract::chat_column{}, type_tag<Feature>{}, n);
  }
  // One under another, in the plugins' order.
  static auto column_of(const ui_needs& n) {
    return skiff::compose::column(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), made_of<Features>(n)...);
  }
};
using column_parts_t = column_parts<composition::providers<contract::chat_column, enabled_plugins>>;
// The chat: its head (or its search, or the bar of messages selected),
// the connection band, the pin, the plugins' parts under the head (a
// call), its protocol's own node there, the messages, the list over the
// composer, the banner over it, its protocol's node there (a bot's
// keyboard), and where one writes -- while a chat is open; else the hint
// to choose one, or, with no account at all, what to do about it. What
// shows is what the program says.
inline auto chat_column(const ui_needs& n) {
  namespace c = skiff::compose;
  const auto open = [](const chat_shown& now) { return now.chosen.has_value(); };
  // On a phone's window, standing over the panel docked under its field.
  return c::spec_for<screen_shown>([](const screen_shown& now) { return scene::Spec{.padding = {0.0f, 0.0f, now.docked, 0.0f}}; },
      c::column(c::vbox(0.0f, {.fillY = true, .grow = scene::axes::kX,
                                  .background = n.looks->window.behind ? skia::SkColor{0} : n.colours->chat}),
      chat_header_for(*n.colours),
      connection_bar_for<connection_shown>(*n.colours, &connection_shown::chat),
      search_bar_for(*n.colours),
      selection_bar(n),
      pinned_bar_for(*n.colours),
      column_parts_t::column_of(n),
      protocol_view_for<head_view_holder, make_head_of>(n),
      c::shown_for<chat_shown>(open, c::styled({.fillX = true, .grow = scene::axes::kY}, chat_area(n))),
      suggestions_for(*n.colours),
      composer_banner_for(*n.colours),
      protocol_view_for<composer_view_holder, make_composer_of>(n),
      c::shown_for<chat_shown>(open, composer_bar<>(n)),
      c::shown_for<chats_shown>([](const chats_shown& now) { return now.none; }, no_accounts_yet(*n.colours)),
      c::shown_for<chats_shown>([](const chats_shown& now) { return !now.none && !now.chosen; }, select_a_chat())));
}
using chat_column_t = decltype(chat_column(std::declval<const ui_needs&>()));
// Its parts, by where they are in it.
struct chat_part {
  static constexpr std::size_t header = 0, connection = 1, search = 2, selection = 3, pinned = 4, plugins = 5,
                               their_head = 6, area = 7, suggestions = 8, banner = 9, their_view = 10, line = 11;
};

// The chats screen: the side (its chats), the chat, and its info or its
// threads beside it -- or, on a phone's window (the user's, #13548), one at a
// time, all of it across, sliding in as the one up changes (#13662). What is
// up, how wide, and what its keys and swipes do, as the program says
// (screen_shown); it keeps nothing.
//
// The keys caught before the field takes them, where the program says they
// are live: Ctrl+C and Delete on the messages selected; Up, Down, Enter,
// Tab and Esc on the list over the field; Alt+Right into the forum gone to,
// Alt+Left out of the one open.
// One answer type for the keys the screen takes at both phases: the node
// keys_for wraps answers as the outer one does, and the screen_keys under it
// (on the bubbling phase) answer through it.
using capture_answer = std::variant<request::selection_copy, request::selection_delete, request::suggestion_step, request::suggestion_pick,
                                    request::suggestion_close, request::open_pointed, request::leave_forum, request::screen_key>;
struct capture_keys {
  std::optional<capture_answer> operator()(const screen_shown& now, const scene::key::down& press) const {
    namespace keys = scene::keys;
    const bool control = press.modifiers.template has<scene::modifier::control>();
    const bool shift = press.modifiers.template has<scene::modifier::shift>();
    const bool alt = press.modifiers.template has<scene::modifier::alt>();
    if (now.selecting && press.key == keys::kC && control && !shift && !alt)
      return request::selection_copy{};
    if (now.selecting && press.key == keys::kDelete && !control && !shift && !alt)
      return request::selection_delete{};
    if (alt && press.key == keys::kRight && now.pointing)
      return request::open_pointed{};
    if (alt && press.key == keys::kLeft && now.in_forum)
      return request::leave_forum{};
    if (!now.suggesting || control || alt)
      return std::nullopt;
    if (press.key == keys::kUp || press.key == keys::kDown)
      return request::suggestion_step{press.key == keys::kDown};
    if (press.key == keys::kEnter || press.key == keys::kTab)
      return request::suggestion_pick{};
    if (press.key == keys::kEscape)
      return request::suggestion_close{};
    return std::nullopt;
  }
};
// The rest, on their way up, nothing under the keys having taken them:
// those the screen has a use for, asked of the program (screen_key).
struct screen_keys {
  std::optional<request::screen_key> operator()(const scene::key::down& press) const {
    namespace keys = scene::keys;
    const bool control = press.modifiers.template has<scene::modifier::control>();
    static constexpr std::array kTheirs{keys::kEscape, keys::kUp, keys::kDown, keys::kTab, keys::kEnd, keys::kF, keys::kK, keys::kC, keys::kDelete,
                                        keys::kPageUp, keys::kPageDown, keys::k1, keys::k2, keys::k3, keys::k4, keys::k5, keys::k6, keys::k7,
                                        keys::k8, keys::k9};
    static constexpr std::array kBare{keys::kEscape, keys::kUp, keys::kDown, keys::kDelete, keys::kPageUp, keys::kPageDown};
    if (!std::ranges::contains(kTheirs, press.key) || (!control && !std::ranges::contains(kBare, press.key)))
      return std::nullopt;
    return request::screen_key{press.key, control, press.modifiers.template has<scene::modifier::shift>(),
                               press.modifiers.template has<scene::modifier::alt>()};
  }
};
// A swipe across it, on a phone's window: right, a step back (or the
// drawer out, from the chats); the program's to say.
struct screen_swiped {
  std::optional<request::screen_swipe> operator()(bool towards_left) const { return request::screen_swipe{!towards_left}; }
};
// The info's or the threads' column: as wide as the program says, all of
// the window across on a phone's.
[[nodiscard]] inline scene::Spec beside_width(const screen_shown& now) {
  return now.single ? scene::Spec{.width = 1.0f, .relativeSize = scene::axes::kBoth}
                    : scene::Spec{.width = now.info_width, .relativeSize = scene::axes::kY};
}
inline auto chats_screen(const ui_needs& n) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  const auto up = [](bool screen_shown::*flag) { return [flag](const screen_shown& now) { return now.*flag; }; };
  const scene::Spec edge{.fillY = true, .width = 7.0f, .margin = {0.0f, -3.0f, 0.0f, -3.0f}};
  return c::keys_for<screen_shown, scene::phase::capture>([](const screen_shown& now) { return now; }, capture_keys{},
      c::onKeys(screen_keys{},
          c::flicked(90.0f, screen_swiped{},
              c::slid_for<screen_shown>([](const screen_shown& now) { return c::Paned{now.single, static_cast<int>(now.pane.index())}; },
                  c::row(c::hbox(0.0f, {.fill = true}),
                      c::shown_for<screen_shown>(up(&screen_shown::side_up),
                          c::column(c::vbox(0.0f, {.fillY = true, .autoSize = scene::axes::kX}), side_column(n))),
                      c::shown_for<screen_shown>(up(&screen_shown::side_edge), c::styled(edge, drag_edge(colours, resize_sidebar_to{}))),
                      c::shown_for<screen_shown>(up(&screen_shown::chat_up),
                          c::column(c::vbox(0.0f, {.fillY = true, .grow = scene::axes::kX}), chat_column(n))),
                      c::shown_for<screen_shown>(up(&screen_shown::beside_edge), c::styled(edge, drag_edge(colours, resize_info_to{}, false))),
                      c::shown_for<screen_shown>(up(&screen_shown::info_up),
                          c::spec_for<screen_shown>(beside_width, c::column(c::vbox(0.0f, {.fillY = true}), info_panel(n)))),
                      c::shown_for<screen_shown>(up(&screen_shown::threads_up),
                          c::spec_for<screen_shown>(beside_width, c::column(c::vbox(0.0f, {.fillY = true}), threads_panel(n)))))))));
}
using chats_screen_t = decltype(chats_screen(std::declval<const ui_needs&>()));
// Its parts, where the program reaches them: the chat's column, its field,
// the threads' field, the chat's search field.
// (keys_for, onKeys, flicked and slid_for each derive from the row under
// them: its parts are the screen's.)
[[nodiscard]] inline auto& chat_column_of(chats_screen_t& screen) { return std::get<0>(std::get<2>(screen.fParts).fParts); }
[[nodiscard]] inline auto& chat_line_of(chats_screen_t& screen) { return std::get<chat_part::line>(chat_column_of(screen).fParts); }
[[nodiscard]] inline auto& thread_line_of(chats_screen_t& screen) {
  return std::get<kThreadLine>(std::get<0>(std::get<5>(screen.fParts).fParts).fParts);
}
[[nodiscard]] inline auto& search_field_of(chats_screen_t& screen) {
  return std::get<0>(std::get<chat_part::search>(chat_column_of(screen).fParts).fParts);
}

}  // namespace mux::ui

// Outside a release build each child is walked through its table, and a
// stack lays itself out through its own (Stack::layoutChildren): so a unit
// that lays out or walks one of these would make its table, and the walks
// of everything under it, there -- the chats screen's took this unit past
// ten minutes. Their tables are made once, each in a walks unit of this
// module (src/ui/walks_*.cc); here, and wherever they are walked, a call.
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::chats_screen_t> = true;  // walks_chats.cc
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::chats_screen_t>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::chat_column_t> = true;  // walks_column.cc
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::chat_column_t>() noexcept;
