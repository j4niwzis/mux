// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:window -- The window.
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
import :conversations;
import :accounts;
import :drawer;
import :settings;
import :context_menu;
import :sending;
import :viewer;

export namespace mux::ui {

// Text editing commands are data; the menu is composed from ordinary rows.
// Keep the editor's focus so a command still reaches its original selection.
using text_menu_action = spl::variant<request::favourite_emoji, request::copy_text, request::text_key, request::text_formatting,
    request::selection_copy, request::selection_forward, request::selection_delete, request::selection_cancel>;
struct text_menu_item {
  std::string label;
  std::string shortcut;
  text_menu_action action;
  bool enabled = true;
  bool separate = false;
};
inline float text_menu_height(const std::vector<text_menu_item>& items) {
  return 12.0f + std::ranges::fold_left(std::views::transform(items, [](const auto& item) {
    return item.separate ? 39.0f : 33.0f;
  }), 0.0f, std::plus{});
}
inline auto text_menu_row(const palette& colours, const text_menu_item& item) {
  namespace c = skiff::compose;
  return c::onClick(item.action,
      c::row(c::hbox(12.0f, {.fillX = true, .height = item.separate ? 39.0f : 33.0f,
                            .padding = {item.separate ? 6.0f : 0.0f, 17.0f, 0.0f, 17.0f},
                            .hoverBackground = colours.chosen, .disabled = !item.enabled}),
             c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                       nodes::Text(item.label, 13.0f, item.enabled ? colours.text : colours.dim)),
             c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(item.shortcut, 12.0f, colours.dim)),
             c::visible(item.separate, c::styled({.place = scene::anchor::kTopLeft, .fillX = true, .height = 1.0f},
                                                nodes::Box<>(colours.band)))), item.label);
}
inline auto make_text_menu(const palette& colours, const std::vector<text_menu_item>& items) {
  auto rows = skiff::compose::many(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      std::ranges::to<std::vector>(std::views::transform(items, [&](const auto& item) { return text_menu_row(colours, item); })));
  return skiff::compose::styled(
      {.width = 260.0f, .height = text_menu_height(items), .padding = {6.0f, 0.0f, 6.0f, 0.0f},
       .cornerRadius = 10.0f, .background = colours.popup(), .border = scene::Border{colours.band, 1.0f},
       .shadow = scene::Shadow{skia::colorSetARGB(70, 0, 0, 0), 3.0f}, .masking = true},
      nodes::ScrollContainer<decltype(rows)>(std::move(rows)));
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
inline std::vector<text_menu_item> selection_menu_items(std::size_t count, bool forwardable, bool deletable) {
  return {{std::format("Copy {} messages", count), "Ctrl+C", request::selection_copy{}},
          {"Forward selected", "", request::selection_forward{}, forwardable},
          {"Delete selected\u2026", "Delete", request::selection_delete{}, deletable},
          {"Clear selection", "Esc", request::selection_cancel{}, true, true}};
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
// The menu, where its facts put it.
inline auto text_menu_layer(const ui_needs& n, const text_menu_facts& facts) {
  return skiff::compose::styled({.place = scene::anchor::kTopLeft, .x = facts.x, .y = facts.y, .width = facts.width, .height = facts.height},
                                make_text_menu(*n.colours, facts.items));
}

// ---- the window -------------------------------------------------------------------

// The chat's background behind the whole window, as the program says: the
// chosen chat's, else every chat's.
struct behind_shown {
  config::wallpaper_t wallpaper = config::wallpaper::theme{};
  friend bool operator==(const behind_shown&, const behind_shown&) = default;
};
inline auto behind_of(const ui_needs& n) {
  return skiff::compose::applied<behind_shown>([](const behind_shown& now) { return now.wallpaper; },
      [at = &n](wallpaper_t& wall, const config::wallpaper_t& chosen) { show_wallpaper_on(wall, chosen, *at->colours, *at->looks); },
      wallpaper_t{});
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
  skiff::model::Tracked<selection_shown> selection;
  skiff::model::Tracked<search_found> found;
  skiff::model::Tracked<std::optional<marks_facts>> marks;
  skiff::model::Tracked<std::optional<leave_space_facts>> leaving;
  skiff::model::Tracked<std::optional<link_facts>> linking;
  skiff::model::Tracked<std::optional<notice_facts>> notice;
  // Each plugin's dialog's: who reacted to a message, for one.
  dialogs_shown_t dialogs;
  // Each protocol's own dialogs'.
  tools_shown_t tools;
  skiff::model::Tracked<std::optional<history_facts>> history;
  skiff::model::Tracked<std::optional<forward_facts>> forwarding;
  skiff::model::Tracked<std::optional<wallpaper_facts>> wallpaper;
  skiff::model::Tracked<std::optional<person_shown>> person;
  skiff::model::Tracked<std::optional<room_card_facts>> room;
  skiff::model::Tracked<std::optional<new_chat_facts>> new_chat;
  skiff::model::Tracked<std::optional<new_room_facts>> new_room;
  skiff::model::Tracked<std::optional<packs_facts>> packs;
  skiff::model::Tracked<std::optional<explore_facts>> explore;
  skiff::model::Tracked<std::optional<settings_facts>> settings;
  skiff::model::Tracked<proxy_notice> proxy_error;
  skiff::model::Tracked<local_seal> seal;
  skiff::model::Tracked<std::optional<room_settings_facts>> manage;
  // The chosen account's page, as the program shows it: what its pages read.
  skiff::model::Tracked<std::optional<account_page_facts>> account_page;
  skiff::model::Tracked<std::optional<send_facts>> sending;
  skiff::model::Tracked<std::optional<verification_view>> verifying;
  skiff::model::Tracked<std::optional<passphrase_facts>> passphrase;
  skiff::model::Tracked<std::optional<menu_facts>> menu;
  skiff::model::Tracked<std::optional<viewer_facts>> viewer;
  skiff::model::Tracked<std::optional<text_menu_facts>> text_menu;
  // What the viewer has coming, and the video it plays: the program's.
  skiff::model::Tracked<viewer_load> viewer_coming;
  skiff::model::Tracked<video_shown> video;
  skiff::model::Tracked<std::optional<emoji_facts>> emoji;
  skiff::model::Tracked<std::optional<panel_facts>> panel;
  // What the speaker plays: the voice messages' buttons and times read it.
  skiff::model::Tracked<audio_shown> listening;
  // What the chat's list and the thread's are told; the marks on messages
  // (selected, flashed); the corner's ways back.
  skiff::model::Tracked<chat_told> chat_list;
  skiff::model::Tracked<thread_told> thread_list;
  skiff::model::Tracked<message_marks> messages_marked;
  skiff::model::Tracked<chat_ways> ways;
  // The chat's own bands, as the program says (app: chat_view, suggestions).
  skiff::model::Tracked<chats_shown> chats_now;
  skiff::model::Tracked<chat_head_shown> head;
  skiff::model::Tracked<connection_shown> connections;
  skiff::model::Tracked<pinned_shown> pinned;
  skiff::model::Tracked<banner_shown> banner;
  skiff::model::Tracked<suggestions_shown> suggested;
  skiff::model::Tracked<threads_shown> threads_list;
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
template <class Part>
void show(shown_model& showing, Part part) {
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
  (void)showing.apply(skiff::model::edit(skiff::model::placeOf<std::optional<Facts>, shown_root>(), skiff::model::setTo(std::move(facts))));
}

// A dialog open while the part it is bound to holds its facts -- made from
// them, with what the program handed the window (Needs) -- and closed where
// they are gone. Pressed off, or Esc: that part emptied.
template <class Content, class Facts, class Needs>
struct shown_dialog : widgets::Dialog<Content, widgets::dismiss::pressed> {
  using dialog_content = Content;
  // The expression-specific overloads are imported here. Looking them up
  // in base.cc only sees the generic class-member fallback.
  [[nodiscard]] dialog_look look_of_content() const {
    if constexpr (requires { content_look(std::type_identity<Content>{}, std::type_identity<Facts>{}); })
      return content_look(std::type_identity<Content>{}, std::type_identity<Facts>{});
    else
      return content_look(std::type_identity<Content>{});
  }
  const Needs* needs = nullptr;
  explicit shown_dialog(const Needs* handed) : needs(handed) {}
  void read(const std::optional<Facts>& now) {
    if (now) {
      this->dismissable_for(*now);
      this->show_with(*now);
    } else {
      this->close();
    }
  }
  // Facts changed while it is up: shown in place where what it shows says
  // how (show_page); else made again from them.
  void show_with(const Facts& facts)
    requires requires(Content& up) { up.show_page(facts); }
  {
    if (auto* up = this->shown())
      up->show_page(facts);
    else
      (void)this->open(*needs, facts);
  }
  void show_with(const Facts& facts)
    requires requires { make_content(std::type_identity<Content>{}, *needs, facts); }
  {
    if constexpr (requires { content_persistent(std::type_identity<Content>{}); }) {
      if (content_persistent(std::type_identity<Content>{}) && this->shown()) {
        if constexpr (requires { content_matches(*this->shown(), facts); }) {
          if (content_matches(*this->shown(), facts)) return;
        } else {
          return;
        }
      }
    }
    // Content that reads its facts (a page view): told them as they change,
    // made once.
    if constexpr (requires(Content& up) { up.read(facts); }) {
      if (auto* up = this->shown()) {
        up->read(facts);
        return;
      }
      this->open(make_content(std::type_identity<Content>{}, *needs, facts)).read(facts);
      return;
    }
    (void)this->open(make_content(std::type_identity<Content>{}, *needs, facts));
  }
  void show_with(const Facts& facts) { (void)this->open(*needs, facts); }
  // Whether a press off it or Esc dismisses it, where what it shows says so
  // of these facts; else as its look says.
  void dismissable_for(const Facts& facts)
    requires requires { Content::dismissable(facts); }
  {
    this->setDismissable(Content::dismissable(facts));
  }
  void dismissable_for(const Facts& facts)
    requires requires { content_dismissable(std::type_identity<Content>{}, facts); }
  {
    this->setDismissable(content_dismissable(std::type_identity<Content>{}, facts));
  }
  void dismissable_for(const Facts&) {}
  auto onPress() { return skiff::bind::own(skiff::model::setTo(std::optional<Facts>{})); }
};


// The drawer, out while what is shown says so; pushed back -- the scrim
// pressed, a swipe, Esc -- that part set so.
template <class Base, class Content>
struct shown_drawer : widgets::Drawer<Base, Content, widgets::dismiss::pressed> {
  // Keep the piecewise construction inside the drawer: the surrounding
  // binding and slide-over forward only the screen's needs.
  explicit shown_drawer(const ui_needs& needs)
      : widgets::Drawer<Base, Content, widgets::dismiss::pressed>(
            std::piecewise_construct,
            std::forward_as_tuple(chats_screen(needs)),
            std::forward_as_tuple(drawer_view(needs))) {}
  void read(const drawer_shown& now) {
    if (now.out != this->isOpen())
      this->setOpen(now.out);
  }
  auto onPress() { return skiff::bind::own(skiff::model::setTo(drawer_shown{false})); }
};

// The pages over the chats -- the accounts panel -- up while what is shown
// holds its facts, and gone where they are not; what is beside its list
// left due for the program to show as it brings the panel up to date.
template <class Frame, class Panel, class Needs>
struct shown_frame : Frame {
  const Needs* needs = nullptr;
  template <class... Args>
  explicit shown_frame(const Needs* handed, Args&&... args) : Frame(std::forward<Args>(args)...), needs(handed) {}
  void read(const std::optional<panel_facts>& now) {
    if (!now) {
      if (this->shown())
        this->close();
      return;
    }
    Panel* panel = this->shown() ? this->shown()->visit(spl::overloaded{[](Panel& one) -> Panel* { return &one; }, [](auto&) -> Panel* { return nullptr; }})
                                 : nullptr;
    if (panel == nullptr) {
      panel = &spl::get<Panel>(this->open(std::in_place_type<Panel>, *needs));
      if (now->note)
        panel->say(*now->note);
    }
    panel->detail_due = now->detail;
  }
};

// A layer holding Node while its part of what is shown holds its facts --
// made from them, with what the program handed the window -- and nothing
// where they are gone: a message's menu.
template <class Needs> struct layer_arguments {
  Needs needs;
  template <class Facts> auto operator()(const Facts &facts) const {
    return std::tie(needs, facts);
  }
};
// A layer made by a function of what it shows: its node made anew as that
// changes (Make: the function, as a type).
template <class Needs, class Make> struct layer_made {
  Needs needs;
  template <class Facts> auto operator()(const Facts& facts) const { return std::tuple{Make{}(needs, facts)}; }
};
template <class Facts, class Needs, class Make>
using made_layer = skiff::compose::Mounted<std::invoke_result_t<Make, const Needs&, const Facts&>, Facts, layer_made<Needs, Make>>;
template <class Facts, class Make, class Needs>
auto layer_made_by(const Needs& needs) {
  return skiff::bind::Bound<std::optional<Facts>, made_layer<Facts, Needs, Make>>(std::in_place, layer_made<Needs, Make>{needs},
                                                                              scene::Spec{.fill = true}, true);
}
struct make_menu {
  auto operator()(const ui_needs& n, const menu_facts& facts) const { return context_menu(n, facts); }
};
struct make_emoji_popup {
  auto operator()(const ui_needs& n, const emoji_facts& facts) const { return emoji_popup(n, facts); }
};
struct make_text_menu_layer {
  auto operator()(const ui_needs& n, const text_menu_facts& facts) const { return text_menu_layer(n, facts); }
};
struct make_viewer {
  auto operator()(const ui_needs& n, const viewer_facts& facts) const { return picture_viewer(n, facts); }
};
template <class Content, class Facts, class Needs>
using shown_layer =
    skiff::compose::Mounted<Content, Facts, layer_arguments<Needs>>;
template <class Content, class Facts, class Needs>
auto layer_for(const Needs &needs) {
  return skiff::bind::Bound<std::optional<Facts>,
                            shown_layer<Content, Facts, Needs>>(
      std::in_place, layer_arguments<Needs>{needs}, scene::Spec{.fill = true},
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
template <class... Features, class... Tools>
consteval auto window_answers_of(plugin_list<Features...>, type_list<Tools...>) {
  return type_tag<std::variant<::mux::ui::request::close_emoji, ::mux::ui::request::close_menu, ::mux::ui::request::close_picture, ::mux::ui::request::close_verification, ::mux::ui::request::verify_cancel_now, ::mux::ui::request::close_send_box, ::mux::ui::request::close_explore, ::mux::ui::request::close_wallpaper, ::mux::ui::request::close_packs, ::mux::ui::request::close_new_room, ::mux::ui::request::close_new_chat, ::mux::ui::request::close_forward, ::mux::ui::request::close_manage, ::mux::ui::request::close_marks, ::mux::ui::request::close_leave_space, ::mux::ui::request::close_link, ::mux::ui::request::close_edit_history, ::mux::ui::request::close_room_card, ::mux::ui::request::close_person_info, ::mux::ui::request::close_notice, ::mux::ui::request::close_settings, ::mux::ui::request::close_drawer, ::mux::ui::request::close_text_menu,
                               ::mux::ui::request::close_shown<dialog_facts_t<Features>>...,
                               decltype(closing_of(std::type_identity<Tools>{}))...>>{};
}
// What Esc on a dialog or a layer asks for, by its facts' type: a plugin's
// its close_shown; the window's own each its request. A dialog of the
// window's own with none here asks for close_shown of its facts, which no
// handler answers: the build says so (it is not in the answers).
inline ::mux::ui::request::close_send_box closing_of(std::type_identity<send_facts>) { return {}; }
inline ::mux::ui::request::close_explore closing_of(std::type_identity<explore_facts>) { return {}; }
inline ::mux::ui::request::close_wallpaper closing_of(std::type_identity<wallpaper_facts>) { return {}; }
inline ::mux::ui::request::close_packs closing_of(std::type_identity<packs_facts>) { return {}; }
inline ::mux::ui::request::close_new_room closing_of(std::type_identity<new_room_facts>) { return {}; }
inline ::mux::ui::request::close_new_chat closing_of(std::type_identity<new_chat_facts>) { return {}; }
inline ::mux::ui::request::close_forward closing_of(std::type_identity<forward_facts>) { return {}; }
inline ::mux::ui::request::close_manage closing_of(std::type_identity<room_settings_facts>) { return {}; }
inline ::mux::ui::request::close_marks closing_of(std::type_identity<marks_facts>) { return {}; }
inline ::mux::ui::request::close_leave_space closing_of(std::type_identity<leave_space_facts>) { return {}; }
inline ::mux::ui::request::close_link closing_of(std::type_identity<link_facts>) { return {}; }
inline ::mux::ui::request::close_edit_history closing_of(std::type_identity<history_facts>) { return {}; }
inline ::mux::ui::request::close_room_card closing_of(std::type_identity<room_card_facts>) { return {}; }
inline ::mux::ui::request::close_person_info closing_of(std::type_identity<person_shown>) { return {}; }
inline ::mux::ui::request::close_notice closing_of(std::type_identity<notice_facts>) { return {}; }
inline ::mux::ui::request::close_settings closing_of(std::type_identity<settings_facts>) { return {}; }
inline ::mux::ui::request::close_emoji closing_of(std::type_identity<emoji_facts>) { return {}; }
inline ::mux::ui::request::close_menu closing_of(std::type_identity<menu_facts>) { return {}; }
inline ::mux::ui::request::close_picture closing_of(std::type_identity<viewer_facts>) { return {}; }
inline ::mux::ui::request::close_text_menu closing_of(std::type_identity<text_menu_facts>) { return {}; }

// ---- Esc, as what is shown says ------------------------------------------------
// What Esc closes of a dialog or a layer up, by its facts: as closing_of
// says; Settings a page back -- home, closed; a verification as its step
// lets it (OK over, Cancel while it waits; not while the emoji are
// compared); a passphrase never -- Esc goes on under it.
using escape_answer = typename decltype(window_answers_of(dialog_plugins{}, tool_facts_list{}))::type;
template <class Facts> [[nodiscard]] std::optional<escape_answer> escape_of(const Facts&) {
  return escape_answer{closing_of(std::type_identity<Facts>{})};
}
[[nodiscard]] inline std::optional<escape_answer> escape_of(const settings_facts& facts) {
  return spl::visit(spl::overloaded{[](settings_page::home) { return escape_answer{request::close_settings{}}; },
                                    [](const settings_page::proxy&) { return escape_answer{request::settings_proxies{}}; },
                                    [](const settings_page::library&) { return escape_answer{request::settings_about{}}; },
                                    [](const settings_page::proxies& one) {
                                      return one.with_back ? escape_answer{request::settings_home{}} : escape_answer{request::close_settings{}};
                                    },
                                    [](const auto&) { return escape_answer{request::settings_home{}}; }},
                    facts.page);
}
[[nodiscard]] inline std::optional<escape_answer> escape_of(const verification_view& now) {
  if (step_over(now.step))
    return escape_answer{request::close_verification{}};
  if (step_waits(now.step))
    return escape_answer{request::verify_cancel_now{}};
  return std::nullopt;
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
    type_list<settings_facts, notice_facts, person_shown, room_card_facts>, typename plugin_dialog_facts<dialog_plugins>::type,
    type_list<history_facts, link_facts, leave_space_facts, marks_facts, room_settings_facts, forward_facts, new_chat_facts, new_room_facts,
              packs_facts, wallpaper_facts, explore_facts>,
    tool_facts_list, type_list<send_facts, passphrase_facts, verification_view, emoji_facts, menu_facts, viewer_facts, text_menu_facts>>::type;
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
// A box over the window (widgets::Dialog, whatever it shows): drawn while it
// fades out too, and its content dropped once it is shut.
template <class Content, class Dismiss> std::true_type dialog_kind(const widgets::Dialog<Content, Dismiss>*);
std::false_type dialog_kind(const void*);
template <class Part> concept a_dialog = decltype(dialog_kind(static_cast<Part*>(nullptr)))::value;

// Whether a dialog fades in or out: what under it is drawn frozen then.
template <class Part> bool fading(Part&) { return false; }
template <a_dialog Part> bool fading(Part& dialog) { return dialog.settling(); }
template <class... Parts> bool fading(skiff::compose::Box<Parts...>& box) {
  return std::apply([](auto&... part) { return (fading(part) || ...); }, box.fParts);
}
struct dialogs_fading {
  template <class Over> bool operator()(Over& over) const { return fading(over); }
};

// ---- the window: its frame, and over it the dialogs and the layers ------------------
using panel_type = spl::variant<accounts_panel>;
using drawer_node = shown_drawer<chats_screen_t, drawer_view_t>;
using with_drawer = skiff::bind::Bound<drawer_shown, drawer_node>;
// The pages slide over the drawer and the chats: Manage accounts comes in
// over them.
using frame_t = skiff::bind::Bound<std::optional<panel_facts>, shown_frame<widgets::SlideOver<with_drawer, panel_type>, accounts_panel, ui_needs>>;
// A dialog of Content, bound to its Facts' part of what is shown, looking
// as its content declares.
template <class Content, class Facts>
using shown_in = skiff::bind::Bound<std::optional<Facts>, shown_dialog<Content, Facts, ui_needs>>;
template <class Content, class Facts>
shown_in<Content, Facts> shown_made(const ui_needs& n) {
  shown_in<Content, Facts> made(std::in_place, &n);
  look_as_its_content(made, *n.colours);
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
    return skiff::compose::column(skiff::compose::vbox(0.0f, {.fill = true}), shown_made<box_t<Features>, dialog_facts_t<Features>>(n)...);
  }
};
// The protocols' dialogs, each made by its dialog_content from its facts.
template <class Facts>
using tool_content_t = decltype(dialog_content(std::declval<const ui_needs&>(), std::declval<const Facts&>()));
template <class... Facts>
auto tool_dialogs(const ui_needs& n, type_list<Facts...>) {
  return skiff::compose::column(skiff::compose::vbox(0.0f, {.fill = true}), shown_made<tool_content_t<Facts>, Facts>(n)...);
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
    return skiff::compose::column(skiff::compose::vbox(0.0f, {.fill = true}),
                                  skiff::compose::styled({.place = scene::anchor::kTopLeft}, made_of<Features>(n))...);
  }
};
// A popup closed by a tap off it: what of it is the popup -- its card.
struct emoji_card {
  template <class Layer> std::optional<skia::SkRect> operator()(Layer& layer) const {
    auto* up = layer.shown();
    return up ? std::optional(popup_card(*up).bounds()) : std::nullopt;
  }
};
struct text_menu_card {
  template <class Layer> std::optional<skia::SkRect> operator()(Layer& layer) const {
    auto* up = layer.shown();
    return up ? std::optional(up->bounds()) : std::nullopt;
  }
};
// Under: the window's background, the chat's background behind all of it
// (where it is so, at its own opacity), and the frame. Over them, bottom to
// top: the dialogs, then the layers -- the emoji, a message's menu, a
// picture, a text's menu -- then the plugins' layers.
namespace layer {
inline constexpr std::size_t settings = 0, person = 2, dialogs = 4, manage = 9, tools = 16, emoji = 20, menu = 21, viewer = 22,
                             text_menu = 23;
}  // namespace layer
inline auto window_body(const ui_needs& n) {
  namespace c = skiff::compose;
  const scene::Spec everywhere{.place = scene::anchor::kTopLeft, .fill = true};
  auto behind = c::styled(everywhere, behind_of(n));
  behind.setVisible(n.looks->window.behind);
  frame_t frame(std::in_place, &n, std::in_place, n);
  frame.apply(everywhere);
  // The pages over the chats (Accounts) on the panels' colour: as
  // see-through as the panels are.
  frame.setSheetColour(n.colours->sidebar);
  frame.base().setSheetColour(n.colours->sidebar);
  // The files to send: as wide as 440 at most, as high as they are.
  auto sending = shown_made<send_box_t, send_facts>(n);
  sending.setWidthFittingContent(440.0f);
  return c::frozen_under<dialogs_fading>(c::vbox(0.0f, {.fill = true}),
      c::column(c::vbox(0.0f, everywhere), c::styled(everywhere, nodes::Box<>(n.colours->background)), std::move(behind), std::move(frame)),
      c::column(c::vbox(0.0f, everywhere),
          shown_made<settings_view_t, settings_facts>(n),
          shown_made<notice_box_t, notice_facts>(n),
          // A person's info, in the middle, as tdesktop's profile layer;
          // a room not joined, from a link: its card, as a person's.
          shown_made<person_card_t, person_shown>(n),
          shown_made<room_card_t, room_card_facts>(n),
          plugin_dialogs<dialog_plugins>::made(n),
          shown_made<edit_history_box, history_facts>(n),
          shown_made<link_box_t, link_facts>(n),
          shown_made<leave_space_box_t, leave_space_facts>(n),
          shown_made<marks_box, marks_facts>(n),
          shown_made<room_settings_t, room_settings_facts>(n),
          shown_made<forward_box, forward_facts>(n),
          shown_made<start_chat_box, new_chat_facts>(n),
          shown_made<create_room_box, new_room_facts>(n),
          shown_made<packs_box_t, packs_facts>(n),
          shown_made<wallpaper_box_t, wallpaper_facts>(n),
          shown_made<explore_box, explore_facts>(n),
          tool_dialogs(n, tool_facts_list{}),
          std::move(sending),
          // A passphrase asked for: at the start, where local data is
          // encrypted; or to turn that on or off, or change it.
          shown_made<passphrase_box_t, passphrase_facts>(n),
          shown_made<verification_box, verification_view>(n),
          c::tapped_off(emoji_card{}, sends<request::close_emoji>{}, layer_made_by<emoji_facts, make_emoji_popup>(n)),
          layer_made_by<menu_facts, make_menu>(n),
          layer_made_by<viewer_facts, make_viewer>(n),
          c::tapped_off(text_menu_card{}, sends<request::close_text_menu>{}, layer_made_by<text_menu_facts, make_text_menu_layer>(n)),
          plugin_layers<layer_plugins>::made(n)));
}
using window_body_t = decltype(window_body(std::declval<const ui_needs&>()));

// The keys the window catches: Esc, before anything under the keys takes
// it, where the program says something is up to let go of -- what, it says
// (escape_in); Ctrl+C, where nothing under the keys took it: what any text
// shows selected copied.
struct escape_key {
  std::optional<request::escape> operator()(const overlay_shown& now, const scene::key::down& press) const {
    return now.escapes && press.key == scene::keys::kEscape ? std::optional(request::escape{}) : std::nullopt;
  }
};
struct copy_key {
  std::optional<request::copy_selected> operator()(const scene::key::down& press) const {
    return press.key == scene::keys::kC && press.modifiers.template has<scene::modifier::control>() ? std::optional(request::copy_selected{})
                                                                                                     : std::nullopt;
  }
};
// Where the pointer is pressed: where a menu that press asks for is put.
struct pressed_where {
  template <class N> std::optional<request::pressed_at> operator()(const N&, const scene::pointer::down& press, scene::PointerReply&) const {
    return request::pressed_at{press.x, press.y};
  }
};
// The window: its body, made again from scratch where the program says the
// look changed so much (a theme: what is made takes its colours then).
inline auto window_view(const ui_needs& n) {
  namespace c = skiff::compose;
  return c::keys_for<overlay_shown, scene::phase::capture>([](const overlay_shown& now) { return now; }, escape_key{},
      c::onKeys(copy_key{},
          c::onCapture<request::pressed_at>(pressed_where{},
              c::projected<window_made>([](const window_made& now) { return std::optional(now.count); },
                  c::mount_now<window_body_t, std::uint64_t>([at = &n](std::uint64_t) { return std::tuple{window_body(*at)}; }, std::uint64_t{0},
                                                             {.fill = true})))));
}
using window = decltype(window_view(std::declval<const ui_needs&>()));

// ---- what the program reaches of it ------------------------------------------------
[[nodiscard]] inline window_body_t& body_of(window& root) { return *root.shown(); }
[[nodiscard]] inline auto& frame_of(window& root) { return std::get<2>(std::get<0>(body_of(root).fParts).fParts); }
[[nodiscard]] inline auto& over_of(window& root) { return std::get<1>(body_of(root).fParts); }
template <std::size_t At> [[nodiscard]] inline auto& layer_of(window& root) { return std::get<At>(over_of(root).fParts); }
[[nodiscard]] inline chats_screen_t& main_of(window& root) { return frame_of(root).base().base(); }
[[nodiscard]] inline auto& chat_line_of(window& root) { return chat_line_of(main_of(root)); }
[[nodiscard]] inline auto& thread_line_of(window& root) { return thread_line_of(main_of(root)); }
[[nodiscard]] inline auto& search_field_of(window& root) { return search_field_of(main_of(root)); }
// The panel that is up, not on its way out; one opened, in place of the
// one up if it is of the kind (a new one sliding in over it, else); the top
// one gone, the one under it up again.
[[nodiscard]] inline panel_type* open_panel(window& root) { return frame_of(root).shown(); }
template <class Panel>
Panel& open_panel_of(window& root, const ui_needs& n) {
  if (panel_type* up = frame_of(root).shown())
    if (Panel* same = up->visit(spl::overloaded{[](Panel& one) -> Panel* { return &one; }, [](auto&) -> Panel* { return nullptr; }}))
      return *same;
  return spl::get<Panel>(frame_of(root).open(std::in_place_type<Panel>, n));
}
inline void back_panel(window& root) { frame_of(root).back(); }
inline void close_panel(window& root) { frame_of(root).close(); }
[[nodiscard]] inline bool pages_moving(window& root) { return frame_of(root).settling(); }
inline void close_drawer_now(window& root) { frame_of(root).base().closeNow(); }
[[nodiscard]] inline bool drawer_open(window& root) { return frame_of(root).base().isOpen(); }
// What each dialog shut shows, dropped: from the program, between events.
template <class Part> void drop_of(Part&) {}
template <a_dialog Part> void drop_of(Part& dialog) { dialog.dropClosed(); }
template <class... Parts> void drop_of(skiff::compose::Box<Parts...>& box) {
  std::apply([](auto&... part) { (drop_of(part), ...); }, box.fParts);
}
inline void drop_closed(window& root) {
  frame_of(root).dropClosed();
  drop_of(over_of(root));
}
[[nodiscard]] inline settings_view_t* settings_up(window& root) { return layer_of<layer::settings>(root).shown(); }
[[nodiscard]] inline room_settings_t* manage_up(window& root) { return layer_of<layer::manage>(root).shown(); }
[[nodiscard]] inline bool context_menu_up(window& root) { return layer_of<layer::menu>(root).shown() != nullptr; }
[[nodiscard]] inline bool emoji_open(window& root) { return layer_of<layer::emoji>(root).shown() != nullptr; }
// The menu's card, where one is up: what takes the keys while it is.
[[nodiscard]] inline scene::Node* menu_card(window& root) {
  auto* up = layer_of<layer::menu>(root).shown();
  return up ? &card_of(*up) : nullptr;
}
// The panels' paint changed: repainted, the screen's ease ticked.
inline void panels_changed(window& root) {
  root.markDamaged();
  scene::work::mark(main_of(root).fState.fId);
}
// The sticker pictures the window shows, for the program to ask for: the
// emoji panel's pages in view, what a message's menu unrolled, the custom
// emoji the list over the composer shows and those in the fields.
[[nodiscard]] inline std::vector<emote_picture> emoji_pictures_shown(window& root, const suggestions_shown& listed) {
  std::vector<emote_picture> shown;
  if (auto* up = layer_of<layer::emoji>(root).shown())
    emote_pictures_in(popup_card(*up), popup_card(*up).bounds(), shown);
  if (auto* menu = layer_of<layer::menu>(root).shown())
    emote_pictures_in(card_of(*menu), card_of(*menu).bounds(), shown);
  shown.append_range(listed.items | std::views::filter([](const suggestion& one) { return !one.picture.empty(); }) |
                     std::views::transform([](const suggestion& one) { return emote_picture{one.picture, emote_kind::emoji}; }));
  const auto inserted = [&](const auto& field) {
    shown.append_range(field.atoms() | std::views::filter([](const auto& atom) { return atom.picture.has_value(); }) |
                       std::views::transform([](const auto& atom) { return emote_picture{atom.target, emote_kind::emoji}; }));
  };
  inserted(composer_field(chat_line_of(root)));
  inserted(composer_field(thread_line_of(root)));
  return shown;
}

}  // namespace mux::ui

// Made once (src/ui/model_shown.cc): an edit's end of batch -- the model's reactions and
// its change log, a walk over the whole root's type -- was made again in
// every unit that edited this model. In a named module, members defined in a
// class are not implicitly inline: the other units call it.
// (Its end of batch alone, not the whole class: clang 23 crashed making the
// class's constexpr constructor in an explicit instantiation of it.)
extern template void skiff::model::Model<mux::ui::shown_root, mux::ui::shown_reactions>::endBatch();

// Outside a release build each child is walked through its table, and a
// stack lays itself out through its own (Stack::layoutChildren): so a unit
// that lays out or walks one of these would make its table, and the walks
// of everything under it, there -- the chats screen's took this unit past
// ten minutes. Their tables are made once, each in a walks unit of this
// module (src/ui/walks_*.cc); here, and wherever they are walked, a call.
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::window_body_t> = true;  // walks_window.cc
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::window_body_t>() noexcept;
