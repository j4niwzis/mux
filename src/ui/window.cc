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
struct shown_root {
  skiff::model::Tracked<drawer_shown> drawer;
  skiff::model::Tracked<chat_shown> chat;
  // What the chat's field writes: an answer, an edit, or a plain message.
  skiff::model::Tracked<chat_compose> composing;
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
  // The chat's info, while it is open (app: chat_view).
  skiff::model::Tracked<info_shown> info;
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

// The chat screen, its chat the one what is shown says is chosen.
template <class Screen>
struct shown_screen : Screen {
  using Screen::Screen;
  using Screen::onKey;
  using Screen::onPointer;
  using Screen::onText;
  using Screen::onSemantic;
  void read(const chat_shown& now) {
    this->chosen = now.chosen;
    // The account the program asked for; where it asked for none, the one
    // the screen falls back on as it shows the chats (the first) is kept.
    if (now.current)
      this->current = now.current;
    if (this->info_open != now.info_open)
      this->set_info_open(now.info_open);
    if (this->threads_open != now.threads_open || this->thread_root != now.thread)
      this->set_threads(now.threads_open, now.thread);
    this->thread_answering = now.thread_answer.has_value();
    // A forum opened or left as the program says it, where that changed:
    // one the screen let go itself -- its space gone -- stays gone.
    if (std::exchange(forum_read, now.forum) != now.forum && this->forum_open != now.forum) {
      if (now.forum)
        this->open_forum(*now.forum);
      else
        this->close_forum();
    }
  }
  std::optional<std::string> forum_read;
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
            std::forward_as_tuple(std::in_place, needs),
            std::forward_as_tuple(needs)) {}
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
template <class... Features, class... Tools>
consteval auto window_answers_of(plugin_list<Features...>, type_list<Tools...>) {
  return type_tag<std::variant<::mux::ui::request::close_emoji, ::mux::ui::request::close_menu, ::mux::ui::request::close_picture, ::mux::ui::request::close_verification, ::mux::ui::request::verify_cancel_now, ::mux::ui::request::close_send_box, ::mux::ui::request::close_explore, ::mux::ui::request::close_wallpaper, ::mux::ui::request::close_packs, ::mux::ui::request::close_new_room, ::mux::ui::request::close_new_chat, ::mux::ui::request::close_forward, ::mux::ui::request::close_manage, ::mux::ui::request::close_marks, ::mux::ui::request::close_leave_space, ::mux::ui::request::close_link, ::mux::ui::request::close_edit_history, ::mux::ui::request::close_room_card, ::mux::ui::request::close_person_info, ::mux::ui::request::close_notice, ::mux::ui::request::close_settings, ::mux::ui::request::close_drawer, ::mux::ui::request::close_text_menu,
                               ::mux::ui::request::close_shown<dialog_facts_t<Features>>...,
                               ::mux::ui::request::close_shown<Tools>...>>{};
}
// What Esc on a dialog or a layer asks for, by its facts' type: a plugin's
// its close_shown; the window's own each its request. A dialog of the
// window's own with none here asks for close_shown of its facts, which no
// handler answers: the build says so (it is not in the answers).
template <class Facts> ::mux::ui::request::close_shown<Facts> closing_of(std::type_identity<Facts>) { return {}; }
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
// A box over the window (widgets::Dialog, whatever it shows): drawn while it
// fades out too, and its content dropped once it is shut.
template <class Content, class Dismiss> std::true_type dialog_kind(const widgets::Dialog<Content, Dismiss>*);
std::false_type dialog_kind(const void*);
template <class Part> concept a_dialog = decltype(dialog_kind(static_cast<Part*>(nullptr)))::value;
// A layer that is there only while it holds something: a message's menu.
template <class Part> concept up_while_shown = !a_dialog<Part> && requires(Part& part) { part.shown(); };

struct window : skiff::compose::Specced {
  // Child references and handlers require a fixed address.
  window(const window&) = delete;
  window& operator=(const window&) = delete;
  window(window&&) = delete;
  window& operator=(window&&) = delete;

  using panel_type = spl::variant<accounts_panel>;
  using screen_node = skiff::bind::Bound<chat_shown, shown_screen<conversations_screen>>;
  using drawer_node = shown_drawer<screen_node, drawer_panel>;
  using with_drawer = skiff::bind::Bound<drawer_shown, drawer_node>;

  // What the window holds, made anew when the theme changes: what is made
  // takes its colours then. Its layers, bottom to top.
  struct layers : skiff::compose::Specced {
    // What its handlers ask for, returned.
    using Answer = typename decltype(window_answers_of(dialog_plugins{}, tool_facts_list{}))::type;    using frame_t = skiff::bind::Bound<std::optional<panel_facts>,
                                       shown_frame<widgets::SlideOver<with_drawer, panel_type>, accounts_panel, ui_needs>>;
    // A dialog of Content, bound to its Facts' part of what is shown.
    template <class Content, class Facts>
    using shown_in = skiff::bind::Bound<std::optional<Facts>, shown_dialog<Content, Facts, ui_needs>>;
    template <class Content, class Facts>
    static shown_in<Content, Facts> shown_made(const ui_needs& n) {
      return shown_in<Content, Facts>(std::in_place, &n);
    }
    // What its parts do, each by its type, folded over them in the order
    // they are drawn (over_chats): no list of them to keep, nor to forget a
    // new dialog in.
    //
    // Esc on one: taken -- with what it asks for, where it asks for
    // something -- or not, and the one under it is asked.
    struct escaped {
      std::optional<Answer> asked;
    };
    template <class Part> static std::optional<escaped> escape(Part&) { return std::nullopt; }
    template <class... Each> static std::optional<escaped> escape(std::tuple<Each...>& all) {
      return std::apply([](auto&... one) { return topmost_first(one...); }, all);
    }
    template <class Held> static std::optional<escaped> escape(std::optional<Held>& held) {
      return held ? escape(*held) : std::nullopt;
    }
    // A dialog up: closed, as its facts ask.
    template <class Content, class Facts> static std::optional<escaped> escape(shown_in<Content, Facts>& dialog) {
      return dialog.shown() ? std::optional<escaped>(escaped{closing_of(std::type_identity<Facts>{})}) : std::nullopt;
    }
    // Settings: a page back to where its ← goes; home, closed.
    static std::optional<escaped> escape(shown_in<settings_view_t, settings_facts>& dialog) {
      auto* box = dialog.shown();
      if (box == nullptr)
        return std::nullopt;
      return box->stepBack() ? escaped{} : escaped{closing_of(std::type_identity<settings_facts>{})};
    }
    // A verification: OK where it is over, Decline or Cancel where it waits.
    // Not while the emoji are compared: an answer is asked there.
    static std::optional<escaped> escape(shown_in<verification_box, verification_view>& dialog) {
      auto* box = dialog.shown();
      if (box != nullptr && step_over(box->step))
        return escaped{::mux::ui::request::close_verification{}};
      if (box != nullptr && step_waits(box->step))
        return escaped{::mux::ui::request::verify_cancel_now{}};
      return std::nullopt;
    }
    // A passphrase is answered, never let go of.
    static std::optional<escaped> escape(shown_in<passphrase_box_t, passphrase_facts>&) { return std::nullopt; }
    // A layer up -- the emoji, a message's menu, a picture: closed, as its
    // facts ask.
    template <class Content, class Facts>
    static std::optional<escaped> escape(skiff::bind::Bound<std::optional<Facts>, shown_layer<Content, Facts, ui_needs>>& layer) {
      return layer.shown() ? std::optional<escaped>(escaped{closing_of(std::type_identity<Facts>{})}) : std::nullopt;
    }
    template <class Facts, class Make>
    static std::optional<escaped> escape(skiff::bind::Bound<std::optional<Facts>, made_layer<Facts, ui_needs, Make>>& layer) {
      return layer.shown() ? std::optional<escaped>(escaped{closing_of(std::type_identity<Facts>{})}) : std::nullopt;
    }
    // The one drawn last -- on top -- asked first.
    template <class... Part> static std::optional<escaped> topmost_first(Part&... part) {
      std::optional<escaped> found;
      auto all = std::tie(part...);
      [&]<std::size_t... At>(std::index_sequence<At...>) {
        (void)((found = escape(std::get<sizeof...(Part) - 1 - At>(all))) || ...);
      }(std::index_sequence_for<Part...>{});
      return found;
    }
    // Whether it fades in or out.
    template <class Part> static bool settling_of(Part&) { return false; }
    template <a_dialog Part> static bool settling_of(Part& dialog) { return dialog.settling(); }
    template <class... Each> static bool settling_of(std::tuple<Each...>& all) {
      return std::apply([](auto&... one) { return (settling_of(one) || ...); }, all);
    }
    template <class Held> static bool settling_of(std::optional<Held>& held) { return held && settling_of(*held); }
    // What it shows dropped, where it has shut.
    template <class Part> static void drop_of(Part&) {}
    template <a_dialog Part> static void drop_of(Part& dialog) { dialog.dropClosed(); }
    template <class... Each> static void drop_of(std::tuple<Each...>& all) {
      std::apply([](auto&... one) { (drop_of(one), ...); }, all);
    }
    template <class Held> static void drop_of(std::optional<Held>& held) {
      if (held)
        drop_of(*held);
    }
    // Drawn over what is under it, frozen: a dialog as it is, up or fading
    // out; a layer where it holds something; anything else as it is.
    template <class Part> static void draw_over(Part& part, skiff::scene::Painting& painting, skia::SkCanvas* canvas, float alpha) {
      scene::draw(part, painting, canvas, alpha);
    }
    template <up_while_shown Part>
    static void draw_over(Part& part, skiff::scene::Painting& painting, skia::SkCanvas* canvas, float alpha) {
      if (part.shown())
        scene::draw(part, painting, canvas, alpha);
    }
    template <class... Each>
    static void draw_over(std::tuple<Each...>& all, skiff::scene::Painting& painting, skia::SkCanvas* canvas, float alpha) {
      std::apply([&](auto&... one) { (draw_over(one, painting, canvas, alpha), ...); }, all);
    }
    template <class Held>
    static void draw_over(std::optional<Held>& held, skiff::scene::Painting& painting, skia::SkCanvas* canvas, float alpha) {
      if (held)
        draw_over(*held, painting, canvas, alpha);
    }
    // The plugins' dialogs, each its box -- from its extend(), found where
    // the window is instantiated (mux.plugins.ui) -- in a slot of its own,
    // made there.
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
      using nodes_t = std::tuple<std::optional<shown_in<box_t<Features>, dialog_facts_t<Features>>>...>;
      static void build(nodes_t& slots, const ui_needs& n) {
        std::apply([&](auto&... slot) { (slot.emplace(std::in_place, &n), ...); }, slots);
      }
    };
    using dialogs_t = plugin_dialogs<dialog_plugins>;
    // The protocols' dialogs, each made by its dialog_content from its facts.
    template <class Facts>
    using tool_content_t = decltype(dialog_content(std::declval<const ui_needs&>(), std::declval<const Facts&>()));
    template <class... Facts>
    static auto tool_nodes_of(type_list<Facts...>) -> std::tuple<std::optional<shown_in<tool_content_t<Facts>, Facts>>...>;
    using tool_nodes_t = decltype(tool_nodes_of(tool_facts_list{}));
    // The plugins' layers, each its node from its extend().
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
      using nodes_t = std::tuple<decltype(made_of<Features>(std::declval<const ui_needs&>()))...>;
      static nodes_t made(const ui_needs& n) { return nodes_t{made_of<Features>(n)...}; }
    };
    using plugin_layers_t = plugin_layers<layer_plugins>;
    struct parts_t {
      nodes::Box<> backdrop;
      // The chat's background behind all of the window, where it is so:
      // drawn at its own opacity -- the desktop through it only where the
      // picture itself lets it be seen.
      wallpaper_t behind;
      // The pages slide over the drawer too: Manage accounts comes in over it.
      frame_t frame;
      shown_in<settings_view_t, settings_facts> settings;
      shown_in<notice_box_t, notice_facts> notice;
      // A person's info, in the middle, as tdesktop's profile layer.
      shown_in<person_card, person_shown> person;
      // A room not joined, from a link: its card, as a person's.
      shown_in<room_card, room_card_facts> room;
      // The plugins' dialogs: a message's reactions as events, for one.
      typename dialogs_t::nodes_t dialogs;
      // A message's earlier versions, as AyuGram's edit history.
      shown_in<edit_history_box, history_facts> history;
      // A link put on what is selected in the message field: Ctrl+K's.
      shown_in<link_box_t, link_facts> linking;
      // Leaving a space, and which of its rooms with it.
      shown_in<leave_space_box_t, leave_space_facts> leaving;
      // The mentions or the reactions not yet seen, listed.
      shown_in<marks_box, marks_facts> marks;
      // A room's management.
      shown_in<room_settings, room_settings_facts> manage;
      // Where a message is forwarded to.
      shown_in<forward_box, forward_facts> forwarding;
      // Element's Start chat, and its Create a room.
      shown_in<start_chat_box, new_chat_facts> new_chat;
      shown_in<create_room_box, new_room_facts> new_room;
      // Emojis & Stickers: a room's packs, or one's own.
      shown_in<packs_box_t, packs_facts> packs;
      // A chat background chosen, at a level.
      shown_in<wallpaper_box_t, wallpaper_facts> wallpaper;
      // A server's public rooms, searched.
      shown_in<explore_box, explore_facts> explore;
      // The protocols' own dialogs: Matrix's developer tools, for one.
      tool_nodes_t tools;
      shown_in<send_box_t, send_facts> sending;
      // A passphrase asked for: at the start, where local data is encrypted;
      // or to turn that on or off, or change it. Over everything.
      shown_in<passphrase_box_t, passphrase_facts> passphrase;
      // An emoji verification, as it goes.
      shown_in<verification_box, verification_view> verifying;
      skiff::bind::Bound<std::optional<emoji_facts>, made_layer<emoji_facts, ui_needs, make_emoji_popup>> emoji;
      skiff::bind::Bound<std::optional<menu_facts>, made_layer<menu_facts, ui_needs, make_menu>> menu;
      skiff::bind::Bound<std::optional<viewer_facts>, made_layer<viewer_facts, ui_needs, make_viewer>> viewer;
      // A selectable text's menu, where it was pressed with the right button.
      skiff::bind::Bound<std::optional<text_menu_facts>, made_layer<text_menu_facts, ui_needs, make_text_menu_layer>> text_menu;
      // The plugins' layers, over everything: a call's card, and on a phone
      // the whole window, as Element's phone apps.
      typename plugin_layers_t::nodes_t over_all;
    } parts;
    // Its parts over the chats -- the dialogs and the layers -- in the order
    // they are drawn, bottom to top: given to `with`. (Defined before what
    // calls it in this class: its return type is deduced.)
    template <class With> decltype(auto) over_chats(With&& with) {
      auto& [backdrop, behind, frame, ...over] = parts;
      return std::forward<With>(with)(over...);
    }
    // Where the pointer was last pressed, in the window: where a menu asked
    // by that press is put. A press off the text menu closes it, at once --
    // nothing of it is pressed.
    skia::SkPoint last_press{};
    // A press off the emoji popup, where it was: let go there -- a tap, not
    // a drag to scroll the chat under it -- it closes the popup.
    std::optional<skia::SkPoint> press_off_emoji;
    using Node::onPointer;
    // A press off the text menu: it closes as the press is let go.
    bool press_off_text_menu = false;
    std::optional<Answer> onPointer(scene::phase::capture, const scene::pointer::up& lift, scene::PointerReply&) {
      std::optional<Answer> answer;
      const auto off = std::exchange(press_off_emoji, std::nullopt);
      if (off && parts.emoji.shown() && std::hypot(lift.x - off->fX, lift.y - off->fY) < 8.0f)
        answer = ::mux::ui::request::close_emoji{};
      if (std::exchange(press_off_text_menu, false))
        answer = ::mux::ui::request::close_text_menu{};
      return answer;
    }
    void onPointer(scene::phase::capture, const scene::pointer::down& press, scene::PointerReply&) {
      last_press = {press.x, press.y};
      press_off_emoji = parts.emoji.shown() && !popup_card(*parts.emoji.shown()).bounds().contains(press.x, press.y)
                            ? std::optional<skia::SkPoint>(skia::SkPoint::Make(press.x, press.y))
                            : std::nullopt;
      auto* menu = parts.text_menu.shown();
      press_off_text_menu = menu != nullptr && !menu->bounds().contains(press.x, press.y);
    }
    // Esc closes the emoji popup first, whatever has the keys: the input
    // keeps them while the popup is open, so the press comes down to it
    // through here -- caught on its way, before the chat reads Esc as
    // letting an answer go.
    using Node::onKey;
    // Esc closes what is on top, whatever has the keys -- a menu, the emoji
    // popup, a picture, a dialog -- one at a time, the topmost first.
    // Ctrl+C, where nothing under the keys took it: what any text shows
    // selected -- a dialog's, a notice's -- copied. The chat's own Ctrl+C
    // comes first, for its messages.
    void onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply) {
      if (press.key != scene::keys::kC || !press.modifiers.template has<scene::modifier::control>())
        return;
      if (const std::string selected = scene::selectedText(); !selected.empty()) {
        skiff::scene::setClipboardText(selected);
        reply.handle();
      }
    }
    std::optional<Answer> onKey(scene::phase::capture, const scene::key::down& press, scene::Reply& reply) {
      if (press.key != scene::keys::kEscape)
        return std::nullopt;
      // Taken: with what it asks for, where it asks for something.
      const auto closed = [&](std::optional<Answer> asked = std::nullopt) {
        reply.handle();
        return asked;
      };
      // The text menu first, then the rest, the one drawn last -- on top -- first.
      if (auto taken = over_chats([](auto&... over) { return topmost_first(over...); }))
        return closed(std::move(taken->asked));
      // The drawer, under every dialog.
      if (parts.frame.base().isOpen())
        return closed(::mux::ui::request::close_drawer{});
      return std::nullopt;
    }

    // While a dialog fades in or out, what is under it -- the window's
    // background, its wallpaper, the chats and the messages -- drawn once,
    // as the fade sets off, into pixels kept: each frame of the fade puts
    // those down and draws only the dialogs and what floats over them, its
    // one blur taken from them (the user's, #13520). Every frame of a fade
    // repainted all of the window under its scrim, and blurred it again.
    // Done, it is let go of: the frame the fade ends on draws all of it as
    // it is, whatever changed under it meanwhile.
    skia::Sp<skia::SkImage> frozen;
    skia::SkRect frozen_at = skia::SkRect::MakeEmpty();  // where it is on the device

    [[nodiscard]] bool dialog_fading() {
      return over_chats([](auto&... over) { return (settling_of(over) || ...); });
    }
    void draw(skiff::scene::Painting& painting, skia::SkCanvas* canvas, float alpha) {
      auto [backdrop, behind, frame] = std::tie(parts.backdrop, parts.behind, parts.frame);
      skia::SkMatrix inverse;
      if (!this->dialog_fading() || !canvas->getTotalMatrix().invert(&inverse)) {
        frozen = nullptr;
        scene::drawDefault(*this, painting, canvas, alpha);
        return;
      }
      if (!frozen) {
        const skia::SkRect device = canvas->getTotalMatrix().mapRect(fState.fBounds);
        const int width = std::max(1, static_cast<int>(std::ceil(device.width())));
        const int height = std::max(1, static_cast<int>(std::ceil(device.height())));
        skia::SkBitmap pixels;
        if (pixels.tryAllocN32Pixels(width, height)) {
          pixels.eraseColor(0);
          skia::SkCanvas into(pixels);
          into.translate(-device.fLeft, -device.fTop);
          into.concat(canvas->getTotalMatrix());
          scene::draw(backdrop, painting, &into, alpha);
          scene::draw(behind, painting, &into, alpha);
          scene::draw(frame, painting, &into, alpha);
          frozen = pixels.asImage();
          frozen_at = skia::SkRect::MakeXYWH(device.fLeft, device.fTop, static_cast<float>(width), static_cast<float>(height));
        }
      }
      if (!frozen) {
        scene::drawDefault(*this, painting, canvas, alpha);
        return;
      }
      canvas->drawImageRect(frozen, inverse.mapRect(frozen_at), skia::SkSamplingOptions(skia::SkFilterMode::kNearest));
      over_chats([&](auto&... over) { (draw_over(over, painting, canvas, alpha), ...); });
    }

    layers(const ui_needs &n)
        : Specced({.fill = true}),
          parts{.backdrop = nodes::Box<>(n.colours->background),
                .frame = frame_t(std::in_place, &n,
                                 std::in_place, n),
                .settings =
                    shown_made<settings_view_t, settings_facts>(n),
                .notice = shown_made<notice_box_t, notice_facts>(n),
                .person = shown_made<person_card, person_shown>(n),
                .room = shown_made<room_card, room_card_facts>(n),
                .history =
                    shown_made<edit_history_box, history_facts>(n),
                .linking = shown_made<link_box_t, link_facts>(n),
                .leaving =
                    shown_made<leave_space_box_t, leave_space_facts>(n),
                .marks = shown_made<marks_box, marks_facts>(n),
                .manage =
                    shown_made<room_settings, room_settings_facts>(n),
                .forwarding =
                    shown_made<forward_box, forward_facts>(n),
                .new_chat =
                    shown_made<start_chat_box, new_chat_facts>(n),
                .new_room =
                    shown_made<create_room_box, new_room_facts>(n),
                .packs = shown_made<packs_box_t, packs_facts>(n),
                .wallpaper =
                    shown_made<wallpaper_box_t, wallpaper_facts>(n),
                .explore = shown_made<explore_box, explore_facts>(n),
                .sending = shown_made<send_box_t, send_facts>(n),
                .passphrase =
                    shown_made<passphrase_box_t, passphrase_facts>(n),
                .verifying =
                    shown_made<verification_box, verification_view>(n),
                .emoji = layer_made_by<emoji_facts, make_emoji_popup>(n),
                .menu = layer_made_by<menu_facts, make_menu>(n),
                .viewer = layer_made_by<viewer_facts, make_viewer>(n),
                .text_menu = layer_made_by<text_menu_facts, make_text_menu_layer>(n),
                .over_all = plugin_layers_t::made(n)} {
      // (A pack of the parts: bound in a generic lambda, the template a pack
      // declaration needs.)
      [&](auto& all) {
      auto& [backdrop, behind, frame, ...over] = all;
      backdrop.apply({.fill = true});
      behind.apply({.fill = true});
      behind.setVisible(n.looks->window.behind);
      // The pages over the chats (Accounts) on the panels' colour: as
      // see-through as the panels are.
      frame.setSheetColour(n.colours->sidebar);
      frame.base().setSheetColour(n.colours->sidebar);
      // The plugins' dialogs, made in their slots.
      dialogs_t::build(parts.dialogs, n);
      std::apply([&](auto&... slot) { (slot.emplace(std::in_place, &n), ...); }, parts.tools);
      // Each dialog as what it shows declares it.
      (look_as_its_content(over, *n.colours), ...);
      }(parts);
      std::apply([&](auto&... slot) { (look_as_its_content(*slot, *n.colours), ...); }, parts.dialogs);
      // The files to send: as wide as 440 at most, as high as they are.
      parts.sending.setWidthFittingContent(440.0f);
    }
  };
  struct parts_t {
    std::optional<layers> now;
  } parts;
  // The layers as they are.
  [[nodiscard]] typename layers::parts_t& layer() { return parts.now->parts; }

  // What it was handed: the program's own objects, for the layers it makes.
  ui_needs needs_;
  explicit window(const ui_needs &n)
      : Specced({.fill = true}), needs_(n) {
    // From its own copy, which the layers' dialogs point at.
    parts.now.emplace(needs_);
  }
  // Everything made again, in the colours of the theme now in place.
  void rebuild() {
    parts.now.reset();
    parts.now.emplace(needs_);
    this->invalidateLayout();
    this->markDamaged();
  }

  [[nodiscard]] conversations_screen& main() { return layer().frame.base().base(); }
  // What is kept, as the window's binding last read it.
  const kept_root* kept_ = nullptr;
  // Read by the window's binding as what is kept moves, and by the chats
  // binding as the chats -- or the chat chosen -- do: the looks in effect.
  template <class Reactions>
  void refresh(const skiff::model::Model<kept_root, Reactions>& kept) {
    this->refresh_kept(kept.root());
  }
  // What is kept read: by its root, whatever the model's reactions.
  void refresh_kept(const kept_root& root) {
    kept_ = &root;
    this->read_looks();
  }
  void refresh(const chats_model&) { this->read_looks(); }
  // What the runtime binding walks call it by (outside a release build):
  // its refresh is overloaded, which they cannot pick by themselves -- the
  // window was never read there, and the drawer, the panels' look and the
  // background behind the window stayed as they were made.
  skiff::bind::Refreshes<2> fRefreshes = skiff::bind::refreshes(
      skiff::bind::refreshFrom<window, chats_model>(),
      skiff::bind::refreshFromRoot<window, kept_root, &window::refresh_kept>());
  // The looks in effect, as what is kept says for the chat chosen: what each
  // level holds of the looks and of room events, for the choices to show
  // what is in effect; the panels' look, the window repainted where it
  // changes; and the background behind the window -- the chat's, else
  // every chat's.
  void read_looks() {
    auto& screen = this->main();
    if (kept_ == nullptr || screen.last_model == nullptr)
      return;
    const auto& looks = kept_->looks.fValue;
    const auto& history = kept_->history.fValue;
    const auto space = space_above_of(screen.last_model->accounts());
    const chat_settings reads{kept_, &space};
    const std::optional<conversation_id>& chosen = screen.chosen;
    looks_shown& shown = *needs_.looks;
    looks_held account_held, chat_held;
    room_events_held account_events, chat_events;
    if (chosen) {
      chat_held = {reads.own_of<&config::chat_choices::wallpaper>(*chosen), reads.own_of<&config::chat_choices::bubbles>(*chosen),
                   reads.own_of<&config::chat_choices::panels>(*chosen)};
      chat_events = {reads.own_of<&config::chat_choices::room_events>(*chosen), reads.own_of<&config::chat_choices::room_event_kinds>(*chosen)};
      if (const config::account_t* account = reads.settings_of(chosen->account.address)) {
        if (const auto& word = config::wallpaper_of(*account))
          account_held.wallpaper = config::wallpaper_of(std::string_view(*word));
        if (const auto& word = config::bubbles_of(*account))
          account_held.bubbles = config::bubble_look_of(*word);
        if (const auto& word = config::panels_of(*account))
          account_held.panels = config::bubble_look_of(*word);
        account_events = {config::room_events_of(*account), config::room_event_kinds_of(*account)};
      }
    }
    shown.at(choice_level::everywhere{}) = {looks.wallpaper, looks.bubbles, looks.panels};
    shown.at(choice_level::account{}) = std::move(account_held);
    shown.at(choice_level::chat{}) = std::move(chat_held);
    room_events_at(choice_level::everywhere{}) = {history.show_room_events, history.room_event_kinds};
    room_events_at(choice_level::account{}) = std::move(account_events);
    room_events_at(choice_level::chat{}) = std::move(chat_events);
    shown.panels = chosen ? reads.panels_of(*chosen) : looks.panels.value_or(config::bubble_look{});
    if (show_panels(*needs_.paint, shown.panels, shown.window, *needs_.colours)) {
      this->markDamaged();
      scene::work::mark(screen.fState.fId);  // an ease ticked by the screen
    }
    this->show_behind(chosen ? reads.wallpaper_of(*chosen) : looks.wallpaper.value_or(config::wallpaper_t{config::wallpaper::theme{}}));
    // The drawer: the accounts saved, with what the chats say of each.
    const auto& current = screen.current;
    layer().frame.base().content().show(kept_->accounts.values(), *screen.last_model,
                                        current ? std::string_view(current->address) : std::string_view());
  }
  // The background behind the whole window, where it is so.
  void show_behind(const config::wallpaper_t& chosen) {
    if (needs_.looks->window.behind)
      show_wallpaper_on(layer().behind, chosen, *needs_.colours, *needs_.looks);
  }
  // The panel that is up, not on its way out.
  [[nodiscard]] panel_type* open_panel() { return layer().frame.shown(); }

  // A panel opened, in place of the one up if there is one.
  // A panel up: the one on top if it is one of these, else a new one sliding
  // in over it.
  template <class Panel>
  Panel& open() {
    if (panel_type* up = layer().frame.shown())
      if (Panel* same = up->visit(spl::overloaded{[](Panel& one) -> Panel* { return &one; },
                                             [](auto&) -> Panel* { return nullptr; }}))
        return *same;
    return spl::get<Panel>(layer().frame.open(std::in_place_type<Panel>, needs_));
  }
  // The top panel goes, and the one under it is up again.
  void back_panel() { layer().frame.back(); }
  void close() { layer().frame.close(); }
  // From the program, between events.
  void drop_closed() {
    layer().frame.dropClosed();
    parts.now->over_chats([](auto&... over) { (layers::drop_of(over), ...); });
  }

  [[nodiscard]] settings_view_t* settings_up() { return layer().settings.shown(); }
  // Where the pointer was last pressed: where a text menu asked by that
  // press goes.
  [[nodiscard]] skia::SkPoint last_press() const { return parts.now->last_press; }
  // A message's menu up: the right press was its.
  [[nodiscard]] bool context_menu_up() { return layer().menu.shown() != nullptr; }
  [[nodiscard]] room_settings* manage_up() { return layer().manage.shown(); }

  void close_drawer_now() { layer().frame.base().closeNow(); }
  [[nodiscard]] bool drawer_open() { return layer().frame.base().isOpen(); }
  // Whether the pages are still moving.
  [[nodiscard]] bool pages_moving() { return layer().frame.settling(); }

  // The input's emoji panel, over the chat above its button.
  // The emoji panel gone: the field back where it was.
  void emoji_closed() {
    needs_.shared->set_docked_panel_height(0.0f);  // the field back at the bottom
    // Told here, not only by its id: the screen made at the start was moved
    // into the window since, and the panel gone left an empty space under
    // the field where nothing ticked it.
    main().follow_docked();
  }
  [[nodiscard]] bool emoji_open() { return layer().emoji.shown() != nullptr; }
  // The sticker pictures the panel shows, for the program to ask for.
  [[nodiscard]] std::vector<emote_picture> emoji_pictures_shown(const suggestions_shown& listed) {
    auto* up = layer().emoji.shown();
    std::vector<emote_picture> shown;
    // Those the panel's pages show, in view: the emoji's, every plugin's.
    if (up) {
      const skia::SkRect view = popup_card(*up).bounds();
      emote_pictures_in(popup_card(*up), view, shown);
    }
    // And those of what a message's menu unrolled: every emoji.
    if (auto* menu = layer().menu.shown())
      emote_pictures_in(card_of(*menu), card_of(*menu).bounds(), shown);
    auto& screen = main();
    // The custom emoji the list over the composer shows.
    for (const suggestion& emoji : listed.items)
      if (!emoji.picture.empty())
        shown.push_back({emoji.picture, emote_kind::emoji});
    const auto inserted = [&](const auto& field) {
      for (const auto& atom : field.atoms())
        if (atom.picture)
          shown.push_back({atom.target, emote_kind::emoji});
    };
    inserted(composer_field(screen.line));
    if (screen.thread_open())
      inserted(composer_field(screen.thread_line()));
    return shown;
  }
  // The menu's card, where one is up: what takes the keys while it is.
  [[nodiscard]] scene::Node* menu_card() {
    auto* up = layer().menu.shown();
    return up ? &card_of(*up) : nullptr;
  }


  // Its layers, each filling the window, as the default layout places them:
  // nothing placed by hand.
};

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
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::window::layers> = true;  // walks_window.cc
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::window::layers>() noexcept;
