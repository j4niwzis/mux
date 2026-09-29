// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:window -- The window.
export module mux.ui:window;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.widgets;
import mux.core;
import mux.config;
export import :viewer;

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
      widgets::Dialog<send_box<Actions>> sending;
      std::optional<context_menu<Actions>> menu;
      std::optional<picture_viewer<Actions>> viewer;
    } parts;

    explicit layers(Actions* a)
        : parts{.frame = frame_t(std::piecewise_construct, std::forward_as_tuple(a), std::forward_as_tuple(a))} {
      auto& [backdrop, frame, settings, notice, person, sending, menu, viewer] = parts;
      fState.apply({.fill = true});
      backdrop.apply({.fill = true});
      frame.setSheetColour(background);
      frame.base().setSheetColour(sidebar_colour);
      settings.setSheetColour(sidebar_colour);
      sending.setSheetColour(sidebar_colour);
      settings.setSize(440.0f, 520.0f);
      notice.setSheetColour(sidebar_colour);
      notice.setSize(440.0f, 240.0f);
      person.setSheetColour(sidebar_colour);
      // tdesktop's profile layer: 392 wide (infoDesiredWidth), as high as
      // what it shows, a 24th of the window down within 20 and 40.
      person.setWidthFittingContent(392.0f);
      person.setPlace(widgets::dialog_place::near_top{});
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
    layer().sending.dropClosed();
  }

  void open_settings(std::string motion) { layer().settings.open(actions, std::move(motion)); }
  void open_picture(std::string source, std::string sender, std::string name, std::string when) {
    layer().viewer.emplace(actions, std::move(source), std::move(sender), std::move(name), std::move(when));
  }
  void open_send_box(const std::vector<pending_file>& files) {
    layer().sending.setSize(440.0f, 560.0f);
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
