// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:window -- The window.
export module mux.ui:window;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.box;
import skiff.widgets.motion;
import mux.core;
import mux.config;
import :base;
import :header;
import :info;
import :room_settings;
import :timeline;
import :conversations;
import :accounts;
import :drawer;
import :settings;
import :context_menu;
import :sending;
import :viewer;

export namespace mux::ui {

// ---- the window -------------------------------------------------------------------

// The conversations; over them the panel that is open, if one is, sliding in
// from the right and back out when closed; and over both, the drawer, pulled
// out from the left. All in this one window, switched by the program between
// events.
template <class Actions>
struct window : scene::Node {
  using panel_type = std::variant<accounts_panel<Actions>>;
  using with_drawer = widgets::Drawer<conversations_screen<Actions>, drawer_panel<Actions>>;

  // What the window holds, made anew when the theme changes: what is made
  // takes its colours then. Its layers, bottom to top.
  struct layers : scene::Node {
    using frame_t = widgets::SlideOver<with_drawer, panel_type>;
    struct parts_t {
      nodes::Box<> backdrop{background};
      // The pages slide over the drawer too: Manage accounts comes in over it.
      frame_t frame;
      widgets::Dialog<settings_dialog<Actions>> settings;
      widgets::Dialog<notice_box<Actions>> notice;
      // A person's info, in the middle, as tdesktop's profile layer.
      widgets::Dialog<person_card<Actions>> person;
      // A room not joined, from a link: its card, as a person's.
      widgets::Dialog<room_card<Actions>> room;
      // A message's reactions as events.
      widgets::Dialog<reactions_box<Actions>> reactions;
      // The mentions or the reactions not yet seen, listed.
      widgets::Dialog<marks_box<Actions>> marks;
      // A room's management.
      widgets::Dialog<room_settings<Actions>> manage;
      // Where a message is forwarded to.
      widgets::Dialog<forward_box<Actions>> forwarding;
      // A new chat: someone to message, or a group to make.
      widgets::Dialog<new_chat_box<Actions>> new_chat;
      // A server's public rooms, searched.
      widgets::Dialog<explore_box<Actions>> explore;
      // The developer tools.
      widgets::Dialog<devtools_box<Actions>> devtools;
      widgets::Dialog<send_box<Actions>> sending;
      std::optional<emoji_popup<Actions>> emoji;
      std::optional<context_menu<Actions>> menu;
      std::optional<picture_viewer<Actions>> viewer;
    } parts;

    Actions* actions_of = nullptr;
    // Esc closes the emoji popup first, whatever has the keys: the input
    // keeps them while the popup is open, so the press comes down to it
    // through here -- caught on its way, before the chat reads Esc as
    // letting an answer go.
    using Node::onKey;
    void onKey(scene::phase::capture, const scene::key::down& press, scene::Reply& reply) {
      if (press.key == scene::keys::kEscape && parts.emoji) {
        actions_of->close_emoji();
        reply.handle();
      }
    }

    explicit layers(Actions* a)
        : parts{.frame = frame_t(std::piecewise_construct, std::forward_as_tuple(a), std::forward_as_tuple(a))},
          actions_of(a) {
      auto& [backdrop, frame, settings, notice, person, room, reactions, marks, manage, forwarding, new_chat, explore, devtools, sending,
             emoji, menu, viewer] = parts;
      fState.apply({.fill = true});
      backdrop.apply({.fill = true});
      frame.setSheetColour(background);
      frame.base().setSheetColour(sidebar_colour);
      settings.setSheetColour(sidebar_colour);
      sending.setSheetColour(sidebar_colour);
      settings.setSize(440.0f, 520.0f);
      notice.setSheetColour(sidebar_colour);
      notice.setWidthFittingContent(440.0f);
      person.setSheetColour(sidebar_colour);
      // tdesktop's profile layer: 392 wide (infoDesiredWidth), as high as
      // what it shows, a 24th of the window down within 20 and 40.
      person.setWidthFittingContent(392.0f);
      person.setPlace(widgets::dialog_place::near_top{});
      room.setSheetColour(sidebar_colour);
      room.setWidthFittingContent(392.0f);
      room.setPlace(widgets::dialog_place::near_top{});
      reactions.setSheetColour(chat_colour);  // its bubbles, as in the chat
      marks.setSheetColour(chat_colour);
      marks.setSize(460.0f, 520.0f);
      reactions.setSize(392.0f, 420.0f);
      manage.setSheetColour(sidebar_colour);
      manage.setSize(860.0f, 620.0f);
      forwarding.setSheetColour(sidebar_colour);
      forwarding.setSize(400.0f, 520.0f);
      new_chat.setSheetColour(sidebar_colour);
      new_chat.setWidthFittingContent(420.0f);
      explore.setSheetColour(sidebar_colour);
      explore.setSize(640.0f, 560.0f);
      devtools.setSheetColour(sidebar_colour);
      devtools.setSize(560.0f, 560.0f);
    }
  };

  Actions* actions = nullptr;
  struct parts_t {
    std::optional<layers> now;
  } parts;
  // The layers as they are.
  [[nodiscard]] typename layers::parts_t& layer() { return parts.now->parts; }

  explicit window(Actions* a) : actions(a) {
    fState.apply({.fill = true});
    parts.now.emplace(a);
  }
  // Everything made again, in the colours of the theme now in place.
  void rebuild() {
    parts.now.reset();
    parts.now.emplace(actions);
    this->invalidateLayout();
    this->markDamaged();
  }

  [[nodiscard]] conversations_screen<Actions>& main() { return layer().frame.base().base(); }
  // The panel that is up, not on its way out.
  [[nodiscard]] panel_type* open_panel() { return layer().frame.shown(); }

  // A panel opened, in place of the one up if there is one.
  // A panel up: the one on top if it is one of these, else a new one sliding
  // in over it.
  template <class Panel>
  Panel& open() {
    if (panel_type* up = layer().frame.shown())
      if (Panel* same = std::visit(overloaded{[](Panel& one) -> Panel* { return &one; },
                                              [](auto&) -> Panel* { return nullptr; }},
                                   *up))
        return *same;
    return std::get<Panel>(layer().frame.open(std::in_place_type<Panel>, actions));
  }
  // The top panel goes, and the one under it is up again.
  void back_panel() { layer().frame.back(); }
  void close() { layer().frame.close(); }
  // From the program, between events.
  void drop_closed() {
    layer().frame.dropClosed();
    layer().settings.dropClosed();
    layer().notice.dropClosed();
    layer().person.dropClosed();
    layer().room.dropClosed();
    layer().reactions.dropClosed();
    layer().marks.dropClosed();
    layer().manage.dropClosed();
    layer().forwarding.dropClosed();
    layer().new_chat.dropClosed();
    layer().explore.dropClosed();
    layer().devtools.dropClosed();
    layer().sending.dropClosed();
  }

  void open_settings(std::string motion) { layer().settings.open(actions, std::move(motion)); }
  void open_picture(std::string source, std::string sender, std::string name, std::string when) {
    layer().viewer.emplace(actions, std::move(source), std::move(sender), std::move(name), std::move(when));
  }
  void open_send_box(const std::vector<pending_file>& files) {
    layer().sending.setWidthFittingContent(440.0f);
    layer().sending.open(actions, files);
  }
  void close_send_box() { layer().sending.close(); }
  [[nodiscard]] send_box<Actions>* send_box_up() { return layer().sending.shown(); }
  void close_picture() { layer().viewer.reset(); }
  void close_settings() { layer().settings.close(); }
  [[nodiscard]] settings_dialog<Actions>* settings_up() { return layer().settings.shown(); }

  void open_drawer() { layer().frame.base().open(); }
  void close_drawer() { layer().frame.base().close(); }
  void close_drawer_now() { layer().frame.base().closeNow(); }
  [[nodiscard]] bool drawer_open() { return layer().frame.base().isOpen(); }
  // Whether the pages are still moving.
  [[nodiscard]] bool pages_moving() { return layer().frame.settling(); }

  void open_menu(const menu_facts& facts) { layer().menu.emplace(actions, facts); }
  void close_menu() { layer().menu.reset(); }
  // The input's emoji panel, over the chat above its button.
  void open_emoji(float right, float bottom) { layer().emoji.emplace(actions, right, bottom); }
  void close_emoji() { layer().emoji.reset(); }
  // The GIFs saved, for the popup's GIF tab, where it is open.
  void show_gifs(const std::vector<std::string>& paths) {
    if (layer().emoji)
      layer().emoji->parts.card.parts.gifs.show(paths);
  }
  [[nodiscard]] bool emoji_open() { return layer().emoji.has_value(); }
  // The menu's card, where one is up: what takes the keys while it is.
  [[nodiscard]] scene::Node* menu_card() { return layer().menu ? &layer().menu->parts.menu : nullptr; }

  void show_notice(std::string what) {
    layer().notice.open(actions, "Not implemented yet", std::format("{} isn't implemented yet.", what));
  }
  void show_message(std::string heading, std::string text) {
    layer().notice.open(actions, std::move(heading), std::move(text));
  }
  void close_notice() { layer().notice.close(); }

  void open_person(const account_id& account, const std::string& key, const person_facts& facts) {
    layer().person.open(actions, account, key, facts);
  }
  void close_person() { layer().person.close(); }
  // Opened again while up, it takes what is known now in place.
  void open_room_card(const std::string& asked, const room_preview& known) {
    layer().room.open(actions, asked, known);
  }
  void close_room_card() { layer().room.close(); }
  [[nodiscard]] bool room_card_up() { return layer().room.shown() != nullptr; }
  void open_reactions(const conversation& in, const std::vector<reaction_entry>& entries, const model* now) {
    layer().reactions.open(actions, in, entries, now);
  }
  void close_reactions() { layer().reactions.close(); }
  void open_marks(mark_kind_t kind, const conversation& in, const std::vector<mark_entry>& entries, const model* now) {
    layer().marks.open(actions, kind, in, entries, now);
  }
  void close_marks() { layer().marks.close(); }
  void open_manage(const room_settings_facts& facts) { layer().manage.open(actions, facts); }
  void close_manage() { layer().manage.close(); }
  void open_forward(const std::vector<forward_target>& chats) { layer().forwarding.open(actions, chats); }
  void close_forward() { layer().forwarding.close(); }
  void open_new_chat() {
    close_drawer();
    layer().new_chat.open(actions);
  }
  void close_new_chat() { layer().new_chat.close(); }
  void open_explore(const std::string& own_server) {
    layer().new_chat.close();
    layer().explore.open(actions, own_server);
  }
  void close_explore() { layer().explore.close(); }
  void show_directory(const std::vector<directory_room>& rooms, const std::string& server) {
    if (auto* up = layer().explore.shown())
      up->show(rooms, server);
  }
  void show_devtools_text(std::string title, std::string text) {
    layer().devtools.open(actions, std::move(title), std::move(text));
  }
  void show_room_state(std::vector<change::state_entry> entries) { layer().devtools.open(actions, std::move(entries)); }
  void open_send_custom() { layer().devtools.open(actions, typename devtools_box<Actions>::send_form_t{}); }
  void close_devtools() { layer().devtools.close(); }

  void show(const std::vector<config::account_t>& saved, const model& now) {
    const auto& current = layer().frame.base().base().current;
    layer().frame.base().content().show(actions, saved, now, current ? std::string_view(current->address) : std::string_view());
  }
  void show_motion(std::string_view level) {
    if (auto* up = layer().settings.shown())
      up->show_motion(std::string(level));
  }

  // Its layers, each filling the window, as the default layout places them:
  // nothing placed by hand.
};

}  // namespace mux::ui
