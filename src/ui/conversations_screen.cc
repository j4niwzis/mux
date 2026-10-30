// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:conversations_screen -- The conversations screen: the list, the chat, its info.
module;
#include "skiff/named.h"
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
// The program's rules, what the pieces under it are built under.
import mux.ui.shaping;
import mux.plugins.ui.top;
import mux.ui.proto;
import mux.ui.lists;
import mux.ui.messages;
import :header;
import :info;

export import :conversations_side;

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

// What plugins put over the composer (contract::composer_top), in their
// order -- the list of what an @ or a ':' asks, for one.
template <class List>
struct composer_top_list;
template <class... Features>
struct composer_top_list<plugin_list<Features...>> {
  template <class Feature>
  // A builder where it holds pieces (the suggestions' faces): built with
  // the window.
  static auto node_of(const palette& colours) -> decltype(extend(contract::composer_top{}, type_tag<Feature>{}, colours)) {
    static_assert(requires { extend(contract::composer_top{}, type_tag<Feature>{}, colours); },
                  composition::broken<contract::composer_top, Feature>());
    return extend(contract::composer_top{}, type_tag<Feature>{}, colours);
  }
  static auto made(const palette& colours) {
    return skiff::compose::column(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), node_of<Features>(colours)...);
  }
};
using composer_top_plugins = composer_top_list<composition::providers<contract::composer_top, enabled_plugins>>;
// No account at all: what to do about it.
inline auto no_accounts_yet(const palette& colours) {
  using add_button = button_for<sends<::mux::ui::request::open_new_account>>;
  return SKIFF_NAMED(no_accounts_yet, skiff::compose::column(
      skiff::compose::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {120.0f, 48.0f, 0.0f, 48.0f}}),
      nodes::Text("No accounts yet", 22.0f, colours.text, true),
      skiff::compose::styled({.fillX = true},
                             wrapped(nodes::Text("Add an XMPP or a Matrix account, and its chats will be here.", 14.0f, colours.dim))),
      // As wide as the column lets it be, 140 at most: a fixed 140 stuck out
      // of a phone's narrow column.
      skiff::compose::styled({.fillX = true, .height = 36.0f, .maxWidth = 140.0f}, primary(add_button(colours.widgets, "Add account", {})))));
}
// No chat chosen: the wallpaper, and in its middle a small pill saying what
// to do, as tdesktop's (its service message look).
inline auto select_a_chat() {
  return SKIFF_NAMED(select_a_chat, skiff::compose::column(
      skiff::compose::justified(skiff::compose::vbox(0.0f, {.fillX = true, .grow = scene::axes::kY}), nodes::justify::middle{}),
      skiff::compose::styled({.alignSelf = scene::align::kMiddle},
                             widgets::Pill("Select a chat to start messaging",
                                           {.plate = skia::colorSetARGB(0x66, 0, 0, 0), .size = 13.0f, .height = 26.0f, .padX = 12.0f}))));
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
  return SKIFF_NAMED(protocol_view_for, c::bound<chat_shown>(c::with_model<chats_model, chat_shown>(
      [shared = n.shared](const chats_model& chats, const chat_shown& now) {
        return now.chosen ? std::optional(chosen_chat_key{*now.chosen, chats.revision(), &chats, shared}) : std::nullopt;
      },
      c::mount<made_t, chosen_chat_key>([](const chosen_chat_key& key) { return std::tuple{protocol_view_made<Holder, Make>(key)}; },
                                        {.fillX = true, .autoSize = scene::axes::kY}))));
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
  return SKIFF_NAMED(chat_column, c::spec_for<screen_shown>([](const screen_shown& now) { return scene::Spec{.padding = {0.0f, 0.0f, now.docked, 0.0f}}; },
      // Filling the column the screen grows it in: a grow said here, across
      // that column, meant nothing -- it was 0 wide, and what it showed
      // (No accounts yet, its button) stood outside it, taking no press.
      c::column(c::vbox(0.0f, {.fill = true, .background = n.looks->window.behind ? skia::SkColor{0} : n.colours->chat}),
      chat_header_for(*n.colours),
      connection_bar_for<connection_shown>(*n.colours, &connection_shown::chat),
      selection_bar(n),
      column_parts_t::column_of(n),
      protocol_view_for<head_view_holder, make_head_of>(n),
      // The messages: a builder (their bubbles pieces), built with the window.
      c::shown_for<chat_shown>(open, c::styled({.fillX = true, .grow = scene::axes::kY}, chat_area(n))),
      composer_top_plugins::made(*n.colours),
      composer_banner_for(*n.colours),
      protocol_view_for<composer_view_holder, make_composer_of>(n),
      c::shown_for<chat_shown>(open, composer_bar<>(n)),
      c::shown_for<chats_shown>([](const chats_shown& now) { return now.none; }, no_accounts_yet(*n.colours)),
      c::shown_for<chats_shown>([](const chats_shown& now) { return !now.none && !now.chosen; }, select_a_chat()))));
}
using chat_column_t = decltype(chat_column(std::declval<const ui_needs&>()));
// Its parts, by where they are in it.
struct chat_part {
  static constexpr std::size_t header = 0, connection = 1, selection = 2, plugins = 3,
                               their_head = 4, area = 5, suggestions = 6, banner = 7, their_view = 8, line = 9;
};

// The chats screen: the side (its chats), the chat, and its info or its
// threads beside it -- or, on a phone's window (the user's, #13548), one at a
// time, all of it across, sliding in as the one up changes (#13662). What is
// up, how wide, and what its keys and swipes do, as the program says
// (screen_shown); it keeps nothing.
//
// The keys caught before the field takes them, where the program says they
// are live: Ctrl+C and Delete on the messages selected; Up, Down, Enter,
// Tab and Esc on the list over the field; Alt+Right into the list of chats
// gone to, Alt+Left out of the one open.
// One answer type for all the screen's own handlers -- the keys it takes at
// both phases and a swipe across it: they wrap one node, which (outside a
// release build) answers as its outermost wrapper says, so each inner one's
// answer is one of the outer one's.
using capture_answer = std::variant<request::selection_copy, request::selection_delete, request::suggestion_step, request::suggestion_pick,
                                    request::suggestion_close, request::open_pointed, request::leave_scope, request::screen_key,
                                    request::screen_swipe>;
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
    if (alt && press.key == keys::kLeft && now.in_scope)
      return request::leave_scope{};
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
                               press.modifiers.template has<scene::modifier::alt>(), press.repeat};
  }
};
// A swipe across it, on a phone's window: right, a step back (or the
// drawer out, from the chats); the program's to say.
struct screen_swiped {
  std::optional<request::screen_swipe> operator()(bool towards_left) const { return request::screen_swipe{!towards_left}; }
};
// The plugins' panels beside the chat (contract::beside_panels), in their
// order, in the info's place -- the threads', for one.
template <class List>
struct beside_panel_list;
template <class... Features>
struct beside_panel_list<plugin_list<Features...>> {
  // A builder where it holds pieces (the threads', its messages): built
  // with the window.
  template <class Feature>
  static auto panel_of(const ui_needs& n) -> decltype(extend(contract::beside_panels{}, type_tag<Feature>{}, n)) {
    static_assert(requires { extend(contract::beside_panels{}, type_tag<Feature>{}, n); },
                  composition::broken<contract::beside_panels, Feature>());
    return extend(contract::beside_panels{}, type_tag<Feature>{}, n);
  }
  static auto made(const ui_needs& n) {
    return skiff::compose::column(skiff::compose::vbox(0.0f, {.fillY = true}), panel_of<Features>(n)...);
  }
};
using beside_plugins = beside_panel_list<composition::providers<contract::beside_panels, enabled_plugins>>;
// The info's or a plugin's panel's column: as wide as the program says, all
// of the window across on a phone's.
[[nodiscard]] inline scene::Spec beside_width(const screen_shown& now) {
  return now.single ? scene::Spec{.width = 1.0f, .relativeSize = scene::axes::kBoth}
                    : scene::Spec{.width = now.info_width, .relativeSize = scene::axes::kY};
}
inline auto chats_screen(const ui_needs& n) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  const auto up = [](bool screen_shown::*flag) { return [flag](const screen_shown& now) { return now.*flag; }; };
  const scene::Spec edge{.fillY = true, .width = 7.0f, .margin = {0.0f, -3.0f, 0.0f, -3.0f}};
  return SKIFF_NAMED(chats_screen, c::keys_for<screen_shown, scene::phase::capture>([](const screen_shown& now) { return now; }, capture_keys{},
      c::onKeys(screen_keys{},
          c::flicked(90.0f, screen_swiped{},
              c::slid_for<screen_shown>([](const screen_shown& now) { return c::Paned{now.single, static_cast<int>(now.pane.index())}; },
                  c::row(c::hbox(0.0f, {.fill = true}),
                      c::shown_for<screen_shown>(up(&screen_shown::side_up),
                          c::column(c::vbox(0.0f, {.fillY = true, .autoSize = scene::axes::kX}), side_column_of(n))),
                      c::shown_for<screen_shown>(up(&screen_shown::side_edge), c::styled(edge, drag_edge(colours, resize_sidebar_to{}))),
                      c::shown_for<screen_shown>(up(&screen_shown::chat_up),
                          c::column(c::vbox(0.0f, {.fillY = true, .grow = scene::axes::kX}), chat_column(n))),
                      c::shown_for<screen_shown>(up(&screen_shown::beside_edge), c::styled(edge, drag_edge(colours, resize_info_to{}, false))),
                      c::shown_for<screen_shown>(up(&screen_shown::info_up),
                          c::spec_for<screen_shown>(beside_width, c::column(c::vbox(0.0f, {.fillY = true}), info_panel(n)))),
                      c::shown_for<screen_shown>(up(&screen_shown::panel_up),
                          c::spec_for<screen_shown>(beside_width, beside_plugins::made(n)))))))));
}
using chats_screen_t = decltype(chats_screen(std::declval<const ui_needs&>()));
}  // namespace mux::ui

