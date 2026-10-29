// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:window -- The window.
module;
#include "skiff/named.h"
export module mux.ui:window;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.nodes.box;
import skiff.nodes.scroll;
import skiff.widgets.motion;
import skiff.widgets.wallpaper;
import mux.core;
import mux.config;
import mux.kept_root;
import skiff.model;
import skiff.bind;
import mux.plugins.types;
import mux.ui.base;
import mux.plugins.ui.top;
import mux.ui.proto;
import :header;
import :info;
import :room_settings;
import mux.ui.messages;
import mux.ui.lists;
import :conversations;
import :accounts;
import :drawer;
import :settings;
import :context_menu;
import :sending;

export namespace mux::ui {

// Text editing commands are data; the menu is composed from ordinary rows.
// Keep the editor's focus so a command still reaches its original selection.
template <class List>
struct text_menu_action_of;
template <class... Features>
struct text_menu_action_of<plugin_list<Features...>> {
  using type = spl::variant<request::favourite_emoji, request::copy_text, request::text_key, request::text_formatting,
      request::selection_copy, request::selection_delete, request::selection_cancel, request::selection_asked<Features>...>;
};
using text_menu_action = typename text_menu_action_of<composition::providers<contract::selection_actions, enabled_plugins>>::type;
struct text_menu_item {
  std::string label;
  std::string shortcut;
  text_menu_action action;
  bool enabled = true;
  bool separate = false;
};
inline float text_menu_height(const std::vector<text_menu_item>& items) {
  return 12.0f + std::ranges::fold_left(items | std::views::transform([](const auto& item) {
    return item.separate ? 39.0f : 33.0f;
  }), 0.0f, std::plus{});
}
inline auto text_menu_row(const palette& colours, const text_menu_item& item) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(text_menu_row, c::onClick(item.action,
      c::row(c::hbox(12.0f, {.fillX = true, .height = item.separate ? 39.0f : 33.0f,
                            .padding = {item.separate ? 6.0f : 0.0f, 17.0f, 0.0f, 17.0f},
                            .hoverBackground = colours.chosen, .disabled = !item.enabled}),
             c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                       nodes::Text(item.label, 13.0f, item.enabled ? colours.text : colours.dim)),
             c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(item.shortcut, 12.0f, colours.dim)),
             c::visible(item.separate, c::styled({.place = scene::anchor::kTopLeft, .fillX = true, .height = 1.0f},
                                                nodes::Box<>(colours.band)))), item.label));
}
inline auto make_text_menu(const palette& colours, const std::vector<text_menu_item>& items) {
  auto rows = skiff::compose::many(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      items | std::views::transform([&](const auto& item) { return text_menu_row(colours, item); }) | std::ranges::to<std::vector>());
  return SKIFF_NAMED(make_text_menu, skiff::compose::styled(
      {.width = 260.0f, .height = text_menu_height(items), .padding = {6.0f, 0.0f, 6.0f, 0.0f},
       .cornerRadius = 10.0f, .background = colours.popup(), .border = scene::Border{colours.band, 1.0f},
       .shadow = scene::Shadow{skia::colorSetARGB(70, 0, 0, 0), 3.0f}, .masking = true},
      nodes::ScrollContainer<decltype(rows)>(std::move(rows))));
}
inline std::vector<text_menu_item> field_menu_items(const scene::text_menu::of_field& field, bool formatting) {
  const bool selected = field.selection && !field.masked;
  if (formatting)
    return {{"‹ Back", "", request::text_formatting{false}},
            {"Bold", "Ctrl+B", request::text_key{scene::keys::kB}, selected, true},
            {"Italic", "Ctrl+I", request::text_key{scene::keys::kI}, selected},
            {"Underline", "Ctrl+U", request::text_key{scene::keys::kU}, selected},
            {"Strikethrough", "Ctrl+Shift+X", request::text_key{scene::keys::kX, true}, selected},
            {"Monospace", "Ctrl+Shift+M", request::text_key{scene::keys::kM, true}, selected},
            {"Spoiler", "Ctrl+Shift+P", request::text_key{scene::keys::kP, true}, selected},
            {"Quote", "Ctrl+Shift+.", request::text_key{scene::keys::kPeriod, true}, selected},
            {"Link…", "Ctrl+K", request::text_key{scene::keys::kK}, selected},
            {"Plain text", "Ctrl+Shift+N", request::text_key{scene::keys::kN, true}, selected, true}};
  std::vector<text_menu_item> items{
      {"Undo", "Ctrl+Z", request::text_key{scene::keys::kZ}, field.undo},
      {"Redo", "Ctrl+Shift+Z", request::text_key{scene::keys::kZ, true}, field.redo},
      {"Cut", "Ctrl+X", request::text_key{scene::keys::kX}, selected, true},
      {"Copy", "Ctrl+C", request::text_key{scene::keys::kC}, selected},
      {"Paste", "Ctrl+V", request::text_key{scene::keys::kV}},
      {"Delete", "", request::text_key{scene::keys::kDelete, false, false}, field.selection},
      {"Select All", "Ctrl+A", request::text_key{scene::keys::kA}, field.text, true}};
  if (field.formats && !field.masked)
    items.push_back({"Formatting ›", "", request::text_formatting{true}, selected, true});
  return items;
}

// A selectable text's: Copy, what it selected; where the press was on a
// link, Copy Link -- and on a custom emoji, a way to keep it or let it go.
inline std::vector<text_menu_item> text_menu_items(std::string text, std::optional<std::string> link, const emoji_kept& kept) {
  std::vector<text_menu_item> items{{"Copy", "", request::copy_text{std::move(text)}}};
  if (link) {
    if (const auto emoji = copied_emoji_at(*link))
      items.push_back({kept.emoji_is_favourite(emoji->url) ? "Remove from favorites" : "Add to favorites", "", request::favourite_emoji{*emoji}});
    items.push_back({"Copy Link", "", request::copy_text{std::move(*link)}});
  }
  return items;
}
// Messages selected: copied, forwarded, deleted, or let go.
// The plugins' actions among them (contract::selection_actions), each where
// it is offered: a bit of `offered` each, in their order.
template <class... Features>
void selection_plugin_items(std::vector<text_menu_item>& items, plugin_list<Features...>, std::uint64_t offered) {
  std::size_t at = 0;
  ((items.push_back({std::format("{} selected", label(contract::selection_actions{}, type_tag<Features>{})), "",
                     request::selection_asked<Features>{}, ((offered >> at) & 1u) != 0}),
    ++at),
   ...);
}
inline std::vector<text_menu_item> selection_menu_items(std::size_t count, std::uint64_t offered, bool deletable) {
  std::vector<text_menu_item> items{{std::format("Copy {} messages", count), "Ctrl+C", request::selection_copy{}}};
  selection_plugin_items(items, composition::providers<contract::selection_actions, enabled_plugins>{}, offered);
  items.push_back({"Delete selected\u2026", "Delete", request::selection_delete{}, deletable});
  items.push_back({"Clear selection", "Esc", request::selection_cancel{}, true, true});
  return items;
}
// A text menu, where the pointer was pressed -- a right press, a long one
// on a phone. A field's: Cut and Copy where something is selected and it is
// no password's, Paste, Select All -- each the key the field takes for it,
// given to it (it keeps the focus: a button takes none).
// A text menu up, as the program says: its items, where it is -- at the
// press that asked for it, kept in the window -- and, a field's, the field
// as it was, for its Formatting page.
struct text_menu_facts {
  std::vector<text_menu_item> items;
  float x = 0.0f, y = 0.0f, width = 260.0f, height = 0.0f;
  std::optional<scene::text_menu::of_field> field;
};
inline text_menu_facts text_menu_at(std::vector<text_menu_item> items, std::optional<scene::text_menu::of_field> field, skia::SkPoint press,
                                    skia::SkRect window) {
  const float tall = std::min(text_menu_height(items), window.height());
  return {.items = std::move(items),
          .x = std::clamp(press.x() - window.fLeft, 0.0f, std::max(0.0f, window.width() - 260.0f)),
          .y = std::clamp(press.y() - window.fTop, 0.0f, std::max(0.0f, window.height() - tall)),
          .width = std::min(260.0f, window.width()),
          .height = tall,
          .field = std::move(field)};
}
// The menu, where its facts put it, over a clear backdrop taking every
// press off it: a press there lets the menu go, as tdesktop's popup menus
// close, and goes no further. With only the tap off the layer (window body,
// tapped_off), a press off it reached what was under it: on a phone, a tap
// held a little long there was a long press -- on a message's text, its
// Copy menu again -- and the menu could not be got rid of.
inline auto text_menu_layer(const ui_needs& n, const text_menu_facts& facts) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(text_menu_layer, c::column(c::vbox(0.0f, {.fill = true}),
                   c::onClick(request::close_text_menu{},
                              c::styled({.place = scene::anchor::kTopLeft, .fill = true}, nodes::Box<>(skia::colorSetARGB(0, 0, 0, 0)))),
                   c::styled({.place = scene::anchor::kTopLeft, .x = facts.x, .y = facts.y, .width = facts.width, .height = facts.height},
                             make_text_menu(*n.colours, facts.items))));
}

// ---- the window -------------------------------------------------------------------

// The chat's background behind the whole window, as the program says: the
// chosen chat's, else every chat's.
struct behind_shown {
  config::wallpaper_t wallpaper = config::wallpaper::theme{};
  friend bool operator==(const behind_shown&, const behind_shown&) = default;
};
inline auto behind_of(const ui_needs& n) {
  return SKIFF_NAMED(behind_of, skiff::compose::applied<behind_shown>([](const behind_shown& now) { return now.wallpaper; },
      [at = &n](wallpaper_t& wall, const config::wallpaper_t& chosen) { show_wallpaper_on(wall, chosen, *at->colours, *at->looks); },
      wallpaper_t{}));
}
using behind_t = decltype(behind_of(std::declval<const ui_needs&>()));

// What the window shows that the program opens and closes, as a model:
// each dialog's facts, while it is open. The program edits it; the
// dialogs, bound to it, open and close as they read it.
// Whether the drawer is out.
struct drawer_shown {
  bool out = false;
};
// The dialogs plugins give the window (contract::window_dialogs), in their
// order: each its facts' part of what is shown -- in an aggregate, which the
// model looks into for a part by its type -- and its close.
using dialog_plugins = composition::providers<contract::window_dialogs, enabled_plugins>;
template <class Feature>
using dialog_facts_t = typename decltype(facts_type(contract::window_dialogs{}, type_tag<Feature>{}))::type;
template <class... Features>
consteval auto dialogs_shown_of(plugin_list<Features...>) {
  return type_tag<composition::aggregate_list<skiff::model::Tracked<std::optional<dialog_facts_t<Features>>>...>>{};
}
using dialogs_shown_t = typename decltype(dialogs_shown_of(dialog_plugins{}))::type;
// The dialogs protocols have of their own (dialogs(state): a list of what
// each shows -- Matrix's developer tools, for one): each its facts' part of
// what is shown, as a plugin's; its content made by its dialog_content,
// found by the facts' namespace; closed by close_shown of its facts.
template <class List>
struct tool_facts_of_list;
template <class... Ds>
struct tool_facts_of_list<proto::dialog_list<Ds...>> {
  using type = type_list<Ds...>;
};
template <class>
struct protocol_tool_facts;
template <class... Tags>
struct protocol_tool_facts<protocol_list<Tags...>> {
  using type = typename joined<type_list<>, typename tool_facts_of_list<decltype(proto::dialogs_of(::mux::state_of<Tags>{}))>::type...>::type;
};
using tool_facts_list = typename protocol_tool_facts<protocols>::type;
template <class... Facts>
consteval auto tools_shown_of(type_list<Facts...>) {
  return type_tag<composition::aggregate_list<skiff::model::Tracked<std::optional<Facts>>...>>{};
}
using tools_shown_t = typename decltype(tools_shown_of(tool_facts_list{}))::type;
// And the layers they put over the window (contract::window_layers): what is
// shown of each -- a call -- held the same way.
using layer_plugins = composition::providers<contract::window_layers, enabled_plugins>;
template <class Feature>
using layer_facts_t = typename decltype(facts_type(contract::window_layers{}, type_tag<Feature>{}))::type;
template <class... Features>
consteval auto layers_shown_of(plugin_list<Features...>) {
  return type_tag<composition::aggregate_list<skiff::model::Tracked<layer_facts_t<Features>>...>>{};
}
using layers_shown_t = typename decltype(layers_shown_of(layer_plugins{}))::type;
// And what they keep among what is shown (contract::window_state): a value
// of each's own.
using state_plugins = composition::providers<contract::window_state, enabled_plugins>;
template <class Feature>
using plugin_state_t = typename decltype(state_type(contract::window_state{}, type_tag<Feature>{}))::type;
template <class... Features>
consteval auto states_shown_of(plugin_list<Features...>) {
  return type_tag<composition::aggregate_list<skiff::model::Tracked<plugin_state_t<Features>>...>>{};
}
using states_shown_t = typename decltype(states_shown_of(state_plugins{}))::type;
// Whether something is up that Esc lets go of, as the program says.
struct overlay_shown {
  bool escapes = false;
  friend bool operator==(const overlay_shown&, const overlay_shown&) = default;
};
// How many times the window was made again from scratch.
struct window_made {
  std::uint64_t count = 0;
  friend bool operator==(const window_made&, const window_made&) = default;
};
struct shown_root {
  skiff::model::Tracked<drawer_shown> drawer;
  // The accounts it lists, as the program says.
  skiff::model::Tracked<drawer_accounts> drawer_list;
  skiff::model::Tracked<chat_shown> chat;
  // What the chat's field writes: an answer, an edit, or a plain message.
  skiff::model::Tracked<chat_compose> composing;
  // And a thread's field.
  skiff::model::Tracked<thread_compose> thread_composing;
  // Each plugin's layer's: a call, for one.
  layers_shown_t layers;
  states_shown_t plugin_states;
  skiff::model::Tracked<selection_shown> selection;
  // What is written in the chat's field and the thread's, as the program
  // keeps it.
  skiff::model::Tracked<chat_draft> chat_writing;
  skiff::model::Tracked<thread_draft> thread_writing;
  skiff::model::Tracked<std::optional<leave_space_facts>> leaving;
  skiff::model::Tracked<std::optional<link_facts>> linking;
  skiff::model::Tracked<std::optional<notice_facts>> notice;
  // Each plugin's dialog's: who reacted to a message, for one.
  dialogs_shown_t dialogs;
  // Each protocol's own dialogs'.
  tools_shown_t tools;
  skiff::model::Tracked<std::optional<person_shown>> person;
  skiff::model::Tracked<std::optional<settings_facts>> settings;
  // Where the settings page rests scrolled: put back there when it is made
  // again (a theme), told by the page as it comes to rest.
  skiff::model::Tracked<settings_scroll> settings_at;
  // Which field the program gives the keys to.
  skiff::model::Tracked<keys_asked> keys;
  skiff::model::Tracked<local_seal> seal;
  skiff::model::Tracked<std::optional<room_settings_facts>> manage;
  // The chosen account's page, as the program shows it: what its pages read.
  skiff::model::Tracked<std::optional<account_page_facts>> account_page;
  // An account's form, as the program says: what it is made from, and says.
  skiff::model::Tracked<std::optional<account_form_facts>> account_form;
  // The pane adding an account: its protocol, the proxies, the one chosen.
  skiff::model::Tracked<add_account_shown> adding;
  skiff::model::Tracked<std::optional<send_facts>> sending;
  skiff::model::Tracked<std::optional<passphrase_facts>> passphrase;
  skiff::model::Tracked<std::optional<menu_facts>> menu;
  skiff::model::Tracked<std::optional<text_menu_facts>> text_menu;
  skiff::model::Tracked<std::optional<panel_facts>> panel;
  // What the speaker plays: the voice messages' buttons and times read it.
  skiff::model::Tracked<audio_shown> listening;
  // What the chat's list and the thread's are told; the marks on messages
  // (selected, flashed); the corner's ways back.
  skiff::model::Tracked<chat_told> chat_list;
  skiff::model::Tracked<thread_told> thread_list;
  skiff::model::Tracked<message_marks> messages_marked;
  skiff::model::Tracked<chat_ways> ways;
  // The way down over the chat's list, as the list said it.
  skiff::model::Tracked<way_down_shown> way_down;
  // The chat's own bands, as the program says (app: chat_view, suggestions).
  skiff::model::Tracked<chats_shown> chats_now;
  skiff::model::Tracked<chat_head_shown> head;
  skiff::model::Tracked<connection_shown> connections;
  skiff::model::Tracked<banner_shown> banner;
  // The window's shape, from its size: a phone's, or not.
  skiff::model::Tracked<window_shape> shape;
  // The looks, as their choices show them (app: refresh_shown).
  skiff::model::Tracked<looks_picked> looks;
  skiff::model::Tracked<room_events_picked> room_events;
  // The background behind the whole window, where it is so.
  skiff::model::Tracked<behind_shown> behind;
  // The chat's info, while it is open (app: chat_view).
  skiff::model::Tracked<info_shown> info;
  // The side column and the screen's frame, as the program says (app:
  // chat_list).
  skiff::model::Tracked<list_shown> list;
  skiff::model::Tracked<side_shown> side;
  skiff::model::Tracked<found_shown> found_messages;
  skiff::model::Tracked<elsewhere_shown> elsewhere;
  skiff::model::Tracked<listing_shown> listing;
  skiff::model::Tracked<screen_shown> frame;
  skiff::model::Tracked<chat_backdrop_shown> backdrop;
  // Whether Esc lets go of something up (escape_in); how many times the
  // window was made again from scratch (a theme).
  skiff::model::Tracked<overlay_shown> overlay;
  skiff::model::Tracked<window_made> made;
};
struct shown_reactions {};
using shown_model = skiff::model::Model<shown_root, shown_reactions>;
// A part that is always there -- the drawer out or back, the chat chosen --
// set.
// Not written where it is what the part holds already: a write moves the
// part's revision, and what reads it is read again -- a dialog with no rule
// of its own for what still stands made anew, with what it held (a search
// typed, how far it was scrolled). change_shown wrote back what it had not
// changed (Explore at every directory listed, the chat list's searches
// among them), and the account's page was written at every refresh.
template <class Part>
void show(shown_model& showing, Part part) {
  if constexpr (std::equality_comparable<Part>)
    if (*showing.look<Part>() == part)
      return;
  (void)showing.apply(skiff::model::edit(skiff::model::placeOf<Part, shown_root>(), skiff::model::setTo(std::move(part))));
}
// Such a part changed from what it is: the rest of it as it was.
template <class Part, class Change>
void change_shown(shown_model& showing, Change change) {
  Part now = *showing.look<Part>();
  change(now);
  show(showing, std::move(now));
}
// The looks as their choices show them, where they moved.
inline void show_looks(shown_model& showing, const looks_shown& looks) {
  if (auto now = looks_picked_of(looks); *showing.look<looks_picked>() != now)
    show(showing, std::move(now));
}
// A dialog shown with these facts, or closed: its part of what is shown set.
template <class Facts>
void show(shown_model& showing, std::optional<Facts> facts) {
  if constexpr (std::equality_comparable<Facts>)
    if (*showing.look<std::optional<Facts>>() == facts)
      return;
  (void)showing.apply(skiff::model::edit(skiff::model::placeOf<std::optional<Facts>, shown_root>(), skiff::model::setTo(std::move(facts))));
}

// A dialog's part emptied -- a press off it, Esc: what it answers.
template <class Facts>
struct facts_emptied {
  static auto answer() { return skiff::bind::own(skiff::model::setTo(std::optional<Facts>{})); }
};
template <std::size_t N> struct rank : rank<N - 1> {};
template <> struct rank<0> {};
// What a dialog is, said by its facts' type -- distinct for every dialog,
// where its content's type may not be (outside a release build every
// content is held alike): how it looks, whether it may be dismissed, whether
// what is up still stands, how it is made.
// How it looks: as its facts say; else as its content says of itself; else
// as a dialog does.
template <class Content, class Facts>
  requires requires { content_look(std::type_identity<Facts>{}); }
[[nodiscard]] dialog_look dialog_look_of(std::type_identity<Content>, std::type_identity<Facts>, rank<2>) {
  return content_look(std::type_identity<Facts>{});
}
template <class Content, class Facts>
  requires requires { Content::look_of_dialog(); }
[[nodiscard]] dialog_look dialog_look_of(std::type_identity<Content>, std::type_identity<Facts>, rank<1>) {
  return Content::look_of_dialog();
}
template <class Content, class Facts>
[[nodiscard]] dialog_look dialog_look_of(std::type_identity<Content>, std::type_identity<Facts>, rank<0>) {
  return {};
}
// Whether a press off it or Esc dismisses it, where these facts say so (or
// what it shows does, of them); else as its look says.
template <class Content, class Facts>
  requires requires(const Facts& facts) { Content::dismissable(facts); }
[[nodiscard]] std::optional<bool> dismissable_of(std::type_identity<Content>, const Facts& facts) {
  return Content::dismissable(facts);
}
template <class Content, class Facts>
  requires requires(const Facts& facts) { content_dismissable(std::type_identity<Facts>{}, facts); }
[[nodiscard]] std::optional<bool> dismissable_of(std::type_identity<Content>, const Facts& facts) {
  return content_dismissable(std::type_identity<Facts>{}, facts);
}
template <class Content, class Facts>
[[nodiscard]] std::optional<bool> dismissable_of(std::type_identity<Content>, const Facts&) {
  return std::nullopt;
}
// Whether what is up still stands for the facts as they are now, from the
// most it says of itself down: made from facts that match these
// (content_matches: the same room); made once while it is up
// (content_persistent); a page that reads its facts itself, told them; else
// not -- made again.
template <class Content, class Facts>
  requires requires(const Facts& was, const Facts& now) { content_matches(was, now); }
[[nodiscard]] bool content_stands(Content&, const Facts& was, const Facts& now, rank<3>) {
  return content_matches(was, now);
}
template <class Content, class Facts>
  requires requires { content_persistent(std::type_identity<Facts>{}); }
[[nodiscard]] bool content_stands(Content&, const Facts&, const Facts&, rank<2>) {
  return content_persistent(std::type_identity<Facts>{});
}
template <class Content, class Facts>
  requires requires(Content& up, const Facts& facts) { up.read(facts); }
[[nodiscard]] bool content_stands(Content& up, const Facts&, const Facts& now, rank<1>) {
  up.read(now);
  return true;
}
template <class Content, class Facts>
[[nodiscard]] bool content_stands(Content&, const Facts&, const Facts&, rank<0>) {
  return false;
}
// What it is made of: as make_content makes it of these facts -- told them
// where it reads them itself -- or from what the program handed the window
// and the facts.
template <class Content, class Facts>
  requires requires(Content& up, const Facts& facts) { up.read(facts); }
void told(Content& made, const Facts& facts) {
  made.read(facts);
}
template <class Content, class Facts>
void told(Content&, const Facts&) {}
template <class Content, class Needs, class Facts>
  requires requires(const Needs& needs, const Facts& facts) { make_content(needs, facts); }
[[nodiscard]] Content content_made(std::type_identity<Content>, const Needs& needs, const Facts& facts) {
  Content made = make_content(needs, facts);
  told(made, facts);
  return made;
}
template <class Content, class Needs, class Facts>
[[nodiscard]] Content content_made(std::type_identity<Content>, const Needs& needs, const Facts& facts) {
  return Content(needs, facts);
}
// What a dialog of Content shows, made from its Facts with what the program
// handed the window: put as its content looks as it opens, and as its facts
// say it may be dismissed.
template <class Content, class Facts>
using dialog_of = widgets::Dialog<Content, facts_emptied<Facts>>;
template <class Content, class Facts>
struct dialog_made {
  const ui_needs* needs = nullptr;
  void put(dialog_of<Content, Facts>& dialog, const Facts& facts) const {
    if (!dialog.shown())
      look_as_its_content(dialog, *needs->colours, dialog_look_of(std::type_identity<Content>{}, std::type_identity<Facts>{}, rank<2>{}));
    if (const auto dismissable = dismissable_of(std::type_identity<Content>{}, facts))
      dialog.setDismissable(*dismissable);
  }
  [[nodiscard]] bool stands(Content& up, const Facts& was, const Facts& now) const { return content_stands(up, was, now, rank<3>{}); }
  [[nodiscard]] Content make(const Facts& facts) const { return content_made(std::type_identity<Content>{}, *needs, facts); }
};


// The drawer, out while what is shown says so; pushed back -- the scrim
// pressed, a swipe, Esc -- that part set so.
struct drawer_pushed {
  // Bound to drawer_shown::out: that part set false.
  static auto answer() { return skiff::bind::own(skiff::model::setTo(false)); }
};

// The pages over the chats -- the accounts panel -- up while what is shown
// holds its facts, and gone where they are not: slid in, made as the
// panel's function makes it, as it opens; what it shows it reads itself.
template <class Panels>
struct panel_made {
  const ui_needs* needs = nullptr;
  template <class Container>
  void put(Container&, const panel_facts&) const {}
  [[nodiscard]] bool stands(Panels&, const panel_facts&, const panel_facts&) const { return true; }
  [[nodiscard]] Panels make(const panel_facts&) const { return Panels(std::in_place_type<accounts_panel>, accounts_view(*needs)); }
};

struct make_menu {
  auto operator()(const ui_needs& n, const menu_facts& facts) const { return context_menu(n, facts); }
};
struct make_text_menu_layer {
  auto operator()(const ui_needs& n, const text_menu_facts& facts) const { return text_menu_layer(n, facts); }
};
template <class Content, class Facts, class Needs>
using shown_layer =
    skiff::compose::Mounted<Content, Facts, layer_arguments<Needs>>;
template <class Content, class Facts, class Needs>
auto layer_for(const Needs &needs) {
  return skiff::bind::Bound<std::optional<Facts>,
                            shown_layer<Content, Facts, Needs>>(
      std::in_place, layer_arguments<Needs>{needs}, scene::Spec{.place = scene::anchor::kTopLeft, .fill = true},
      true);
}

// The conversations; over them the panel that is open, if one is, sliding in
// from the right and back out when closed; and over both, the drawer, pulled
// out from the left. All in this one window, switched by the program between
// events.

// What the layers' keys ask for: a dialog closed, the plugins' each by its
// facts.
// What Esc on a dialog or a layer asks for, by its facts type, unless said
// otherwise below: close_shown of it.
template <class Facts> ::mux::ui::request::close_shown<Facts> closing_of(std::type_identity<Facts>) { return {}; }
// The plugins' layers Esc closes: those whose facts are a std::optional
// (a picture in the viewer), by close_shown of what is in it.
template <class Facts>
struct optional_inner {
  using type = type_list<>;
};
template <class Facts>
struct optional_inner<std::optional<Facts>> {
  using type = type_list<Facts>;
};
template <class Features>
struct plugin_layer_escapes;
template <class... Features>
struct plugin_layer_escapes<plugin_list<Features...>> {
  using type = typename joined<type_list<>, typename optional_inner<layer_facts_t<Features>>::type...>::type;
};
template <class... Features, class... Tools, class... Layers>
consteval auto window_answers_of(plugin_list<Features...>, type_list<Tools...>, type_list<Layers...>) {
  return type_tag<std::variant<::mux::ui::request::close_menu, ::mux::ui::request::close_send_box, ::mux::ui::request::close_manage, ::mux::ui::request::close_leave_space, ::mux::ui::request::close_link, ::mux::ui::request::close_person_info, ::mux::ui::request::close_notice, ::mux::ui::request::close_settings, ::mux::ui::request::settings_home, ::mux::ui::request::settings_about, ::mux::ui::request::close_drawer, ::mux::ui::request::close_text_menu,
                               ::mux::ui::request::close_shown<dialog_facts_t<Features>>...,
                               decltype(closing_of(std::type_identity<Tools>{}))...,
                               ::mux::ui::request::close_shown<Layers>...>>{};
}
// What Esc on a dialog or a layer asks for, by its facts' type: a plugin's
// its close_shown; the window's own each its request. A dialog of the
// window's own with none here asks for close_shown of its facts, which no
// handler answers: the build says so (it is not in the answers).
inline ::mux::ui::request::close_send_box closing_of(std::type_identity<send_facts>) { return {}; }
inline ::mux::ui::request::close_manage closing_of(std::type_identity<room_settings_facts>) { return {}; }
inline ::mux::ui::request::close_leave_space closing_of(std::type_identity<leave_space_facts>) { return {}; }
inline ::mux::ui::request::close_link closing_of(std::type_identity<link_facts>) { return {}; }
inline ::mux::ui::request::close_person_info closing_of(std::type_identity<person_shown>) { return {}; }
inline ::mux::ui::request::close_notice closing_of(std::type_identity<notice_facts>) { return {}; }
inline ::mux::ui::request::close_settings closing_of(std::type_identity<settings_facts>) { return {}; }
inline ::mux::ui::request::close_menu closing_of(std::type_identity<menu_facts>) { return {}; }
inline ::mux::ui::request::close_text_menu closing_of(std::type_identity<text_menu_facts>) { return {}; }


// ---- Esc, as what is shown says ------------------------------------------------
// What Esc closes of a dialog or a layer up, by its facts: as closing_of
// says; Settings a page back -- home, closed; a passphrase never -- Esc
// goes on under it. (An emoji verification's, the encryption plugin's, as its
// step lets it: its part answers its close_shown.)
using escape_answer =
    typename decltype(window_answers_of(dialog_plugins{}, tool_facts_list{}, typename plugin_layer_escapes<layer_plugins>::type{}))::type;
template <class Facts> [[nodiscard]] std::optional<escape_answer> escape_of(const Facts&) {
  return escape_answer{closing_of(std::type_identity<Facts>{})};
}
[[nodiscard]] inline std::optional<escape_answer> escape_of(const settings_facts& facts) {
  return spl::visit(spl::overloaded{[](settings_page::home) { return escape_answer{request::close_settings{}}; },
                                    [](const settings_page::library&) { return escape_answer{request::settings_about{}}; },
                                    [](const auto&) { return escape_answer{request::settings_home{}}; }},
                    facts.page);
}
[[nodiscard]] inline std::optional<escape_answer> escape_of(const passphrase_facts&) { return std::nullopt; }
// The dialogs and the layers, in the order they are drawn: Esc asks of the
// top-most up first.
template <class Features>
struct plugin_dialog_facts;
template <class... Features>
struct plugin_dialog_facts<plugin_list<Features...>> {
  using type = type_list<dialog_facts_t<Features>...>;
};
using escape_order = typename joined<
    type_list<settings_facts, notice_facts, person_shown>, typename plugin_dialog_facts<dialog_plugins>::type,
    type_list<link_facts, leave_space_facts, room_settings_facts>,
    tool_facts_list, type_list<send_facts, passphrase_facts, menu_facts, text_menu_facts>,
    typename plugin_layer_escapes<layer_plugins>::type>::type;
template <class Model, class... Facts>
[[nodiscard]] std::optional<escape_answer> escape_in(const Model& showing, type_list<Facts...>) {
  std::optional<escape_answer> found;
  const auto ask = [&]<class F>(std::type_identity<F>) {
    if (const auto& up = *showing.template look<std::optional<F>>(); up)
      found = escape_of(*up);
    return found.has_value();
  };
  // The one drawn last -- on top -- first.
  [&]<std::size_t... At>(std::index_sequence<At...>) {
    (void)(ask(std::type_identity<std::tuple_element_t<sizeof...(Facts) - 1 - At, std::tuple<Facts...>>>{}) || ...);
  }(std::index_sequence_for<Facts...>{});
  return found;
}
// ---- the window: its frame, and over it the dialogs and the layers ------------------
using panel_type = spl::variant<accounts_panel>;
using drawer_node = widgets::Drawer<chats_screen_t, drawer_view_t, drawer_pushed>;
using with_drawer = skiff::bind::Bound<skiff::model::Field<&drawer_shown::out>, drawer_node>;
// The pages slide over the drawer and the chats: Manage accounts comes in
// over them.
using frame_t = skiff::bind::Bound<std::optional<panel_facts>,
                                   widgets::Shown<widgets::SlideOver<with_drawer, panel_type>, panel_facts, panel_made<panel_type>>>;
// A dialog of Content, bound to its Facts' part of what is shown, looking
// as its content declares.
template <class Content, class Facts>
using shown_in = skiff::bind::Bound<std::optional<Facts>, widgets::Shown<dialog_of<Content, Facts>, Facts, dialog_made<Content, Facts>>>;
template <class Content, class Facts>
shown_in<Content, Facts> shown_made(const ui_needs& n) {
  // Over everything under it, out of the flow of what it is put in: the
  // dialogs are put one after another in one column, and each fills it --
  // in its flow, the second was laid out a window's height below the first,
  // the third two, and none but the first was ever seen.
  shown_in<Content, Facts> made(std::in_place, dialog_made<Content, Facts>{&n});
  made.apply({.place = scene::anchor::kTopLeft});
  return made;
}
// The plugins' dialogs, each its box -- from its extend(), found where the
// window is instantiated (mux.plugins.ui).
template <class List>
struct plugin_dialogs;
template <class... Features>
struct plugin_dialogs<plugin_list<Features...>> {
  template <class Feature>
  static auto box_of() -> decltype(extend(contract::window_dialogs{}, type_tag<Feature>{})) {
    static_assert(requires { extend(contract::window_dialogs{}, type_tag<Feature>{}); },
                  composition::broken<contract::window_dialogs, Feature>());
    return extend(contract::window_dialogs{}, type_tag<Feature>{});
  }
  template <class Feature>
  using box_t = typename decltype(box_of<Feature>())::type;
  static auto made(const ui_needs& n) {
    return skiff::compose::column(skiff::compose::vbox(0.0f, {.place = scene::anchor::kTopLeft, .fill = true}), shown_made<box_t<Features>, dialog_facts_t<Features>>(n)...);
  }
};
// The protocols' dialogs, each made by its dialog_content from its facts.
template <class Facts>
using tool_content_t = decltype(dialog_content(std::declval<const ui_needs&>(), std::declval<const Facts&>()));
template <class... Facts>
auto tool_dialogs(const ui_needs& n, type_list<Facts...>) {
  return SKIFF_NAMED(tool_dialogs, skiff::compose::column(skiff::compose::vbox(0.0f, {.place = scene::anchor::kTopLeft, .fill = true}), shown_made<tool_content_t<Facts>, Facts>(n)...));
}
// The plugins' layers, each its node from its extend(), over everything: a
// call's card -- on a phone the whole window, as Element's phone apps.
template <class List>
struct plugin_layers;
template <class... Features>
struct plugin_layers<plugin_list<Features...>> {
  template <class Feature>
  static auto made_of(const ui_needs& n) -> decltype(extend(contract::window_layers{}, type_tag<Feature>{}, n)) {
    static_assert(requires { extend(contract::window_layers{}, type_tag<Feature>{}, n); },
                  composition::broken<contract::window_layers, Feature>());
    return extend(contract::window_layers{}, type_tag<Feature>{}, n);
  }
  static auto made(const ui_needs& n) {
    return skiff::compose::column(skiff::compose::vbox(0.0f, {.place = scene::anchor::kTopLeft, .fill = true}),
                                  skiff::compose::styled({.place = scene::anchor::kTopLeft}, made_of<Features>(n))...);
  }
};
// Under: the window's background, the chat's background behind all of it
// (where it is so, at its own opacity), and the frame. Over them, bottom to
// top: the dialogs, then the layers -- the emoji, a message's menu, a
// picture, a text's menu -- then the plugins' layers.
inline auto window_body(const ui_needs& n) {
  namespace c = skiff::compose;
  const scene::Spec everywhere{.place = scene::anchor::kTopLeft, .fill = true};
  auto behind = c::styled(everywhere, behind_of(n));
  behind.setVisible(n.looks->window.behind);
  frame_t frame(std::in_place, panel_made<panel_type>{&n}, std::in_place, std::piecewise_construct,
                std::forward_as_tuple(chats_screen(n)), std::forward_as_tuple(drawer_view(n)));
  frame.apply(everywhere);
  // The pages over the chats (Accounts) on the panels' colour: as
  // see-through as the panels are.
  frame.setSheetColour(n.colours->sidebar);
  frame.base().setSheetColour(n.colours->sidebar);
  // The files to send: as wide as 440 at most, as high as they are.
  auto sending = shown_made<send_box_t, send_facts>(n);
  sending.setWidthFittingContent(440.0f);
  return SKIFF_NAMED(window_body, c::column(c::vbox(0.0f, {.fill = true}),
      c::frozen_under(c::vbox(0.0f, {.fill = true}),
      c::column(c::vbox(0.0f, everywhere), c::styled(everywhere, nodes::Box<>(n.colours->background)), std::move(behind), std::move(frame)),
      c::column(c::vbox(0.0f, everywhere),
          shown_made<settings_view_t, settings_facts>(n),
          shown_made<notice_box_t, notice_facts>(n),
          // A person's info, in the middle, as tdesktop's profile layer;
          // a room not joined, from a link: its card, as a person's.
          shown_made<person_card_t, person_shown>(n),
          plugin_dialogs<dialog_plugins>::made(n),
          shown_made<link_box_t, link_facts>(n),
          shown_made<leave_space_box_t, leave_space_facts>(n),
          shown_made<room_settings_t, room_settings_facts>(n),
          tool_dialogs(n, tool_facts_list{}),
          std::move(sending),
          // A passphrase asked for: at the start, where local data is
          // encrypted; or to turn that on or off, or change it.
          shown_made<passphrase_box_t, passphrase_facts>(n))),
      // The layers over the dialogs: what moves in them (a panel scrolled)
      // does not hold what is under the dialogs frozen.
      c::column(c::vbox(0.0f, everywhere),
          layer_made_by<menu_facts, make_menu>(n),
          c::tapped_off(sends<request::close_text_menu>{}, layer_made_by<text_menu_facts, make_text_menu_layer>(n)),
          plugin_layers<layer_plugins>::made(n))));
}
using window_body_t = decltype(window_body(std::declval<const ui_needs&>()));

// The keys the window catches: Esc, before anything under the keys takes
// it, where the program says something is up to let go of -- what, it says
// (escape_in); Ctrl+C, where nothing under the keys took it: what any text
// shows selected copied.
// What the window's own handlers answer -- Esc, Ctrl+C, a press -- as one
// type: they wrap one node, which answers as the outermost of them does.
using window_answer = std::variant<request::escape, request::copy_selected, request::pressed_at>;
struct escape_key {
  std::optional<window_answer> operator()(const overlay_shown& now, const scene::key::down& press) const {
    return now.escapes && press.key == scene::keys::kEscape ? std::optional<window_answer>(request::escape{}) : std::nullopt;
  }
};
struct copy_key {
  std::optional<window_answer> operator()(const scene::key::down& press) const {
    return press.key == scene::keys::kC && press.modifiers.template has<scene::modifier::control>()
               ? std::optional<window_answer>(request::copy_selected{})
               : std::nullopt;
  }
};
// Where the pointer is pressed: where a menu that press asks for is put.
struct pressed_where {
  template <class N> std::optional<window_answer> operator()(const N&, const scene::pointer::down& press, scene::PointerReply&) const {
    return request::pressed_at{press.x, press.y};
  }
};
// The window: its body, made again from scratch where the program says the
// look changed so much (a theme: what is made takes its colours then).
inline auto window_view(const ui_needs& n) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(window_view, c::keys_for<overlay_shown, scene::phase::capture>([](const overlay_shown& now) { return now; }, escape_key{},
      c::onKeys(copy_key{},
          c::onCapture<window_answer>(pressed_where{},
              c::projected<window_made>([](const window_made& now) { return std::optional(now.count); },
                  c::mount_now<window_body_t, std::uint64_t>([at = &n](std::uint64_t) { return std::tuple{window_body(*at)}; }, std::uint64_t{0},
                                                             {.fill = true}))))));
}
using window = decltype(window_view(std::declval<const ui_needs&>()));

// The emote pictures the window wants fetched: those its panels and menus
// drew without their pictures (drained) -- the custom emoji the list over
// the composer shows among them -- and those in the fields.
[[nodiscard]] inline std::vector<emote_picture> emoji_pictures_wanted(const chat_draft& chat, const thread_draft& thread) {
  std::vector<emote_picture> wanted = std::exchange(emotes_wanted(), {});
  // Those put in what is being written: the drafts' pictures.
  const auto inserted = [&](const composer_doc& draft) {
    wanted.append_range(draft.atoms | std::views::filter(&widgets::TextAtom::picture) |
                        std::views::transform([](const widgets::TextAtom& atom) { return emote_picture{atom.target, emote_kind::emoji}; }));
  };
  inserted(chat.now);
  inserted(thread.now);
  return wanted;
}

}  // namespace mux::ui

// Made once (src/ui/model_shown.cc): an edit's end of batch -- the model's reactions and
// its change log, a walk over the whole root's type -- was made again in
// every unit that edited this model. In a named module, members defined in a
// class are not implicitly inline: the other units call it.
// (Its end of batch alone, not the whole class: clang 23 crashed making the
// class's constexpr constructor in an explicit instantiation of it.)
extern template void skiff::model::Model<mux::ui::shown_root, mux::ui::shown_reactions>::endBatch();

