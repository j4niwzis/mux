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
  // takes its colours then.
  struct parts {
    nodes::Box<> backdrop{background};
    // The pages slide over the drawer too: Manage accounts comes in over it.
    widgets::SlideOver<with_drawer, panel_type> frame;
    widgets::Dialog<settings_dialog<Actions>> settings;
    widgets::Dialog<notice_box<Actions>> notice;
    // A person's info, in the middle, as tdesktop's profile layer.
    widgets::Dialog<person_card<Actions>> person;
    std::optional<context_menu<Actions>> menu;
    std::optional<picture_viewer<Actions>> viewer;
    widgets::Dialog<send_box<Actions>> sending;

    explicit parts(Actions* a) : frame(std::piecewise_construct, std::forward_as_tuple(a), std::forward_as_tuple(a)) {
      backdrop.apply({.fill = true});
      frame.setSheetColour(background);
      frame.base().setSheetColour(sidebar_colour);
      settings.setSheetColour(sidebar_colour);
      sending.setSheetColour(sidebar_colour);
      settings.setSize(440.0f, 520.0f);
      notice.setSheetColour(sidebar_colour);
      notice.setSize(440.0f, 240.0f);
      person.setSheetColour(sidebar_colour);
      person.setSize(392.0f, 300.0f);
    }
  };

  Actions* actions = nullptr;
  std::optional<parts> p;

  explicit window(Actions* a) : actions(a) {
    fState.apply({.fill = true});
    p.emplace(a);
  }
  // Everything made again, in the colours of the theme now in place.
  void rebuild() {
    p.reset();
    p.emplace(actions);
    this->invalidateLayout();
    this->markDamaged();
  }
  void forEachChild(auto&& f) {
    if (!p)
      return;
    f(p->backdrop);
    f(p->frame);
    f(p->settings);
    f(p->notice);
    f(p->person);
    f(p->sending);
    f(p->menu);
    f(p->viewer);
  }

  [[nodiscard]] conversations_screen<Actions>& main() { return p->frame.base().base(); }
  // The panel that is up, not on its way out.
  [[nodiscard]] panel_type* open_panel() { return p->frame.shown(); }

  // A panel opened, in place of the one up if there is one.
  // A panel up: the one on top if it is one of these, else a new one sliding
  // in over it.
  template <class Panel>
  Panel& open() {
    if (panel_type* up = p->frame.shown())
      if (Panel* same = std::visit(overloaded{[](Panel& one) -> Panel* { return &one; },
                                              [](auto&) -> Panel* { return nullptr; }},
                                   *up))
        return *same;
    return std::get<Panel>(p->frame.open(std::in_place_type<Panel>, actions));
  }
  // The top panel goes, and the one under it is up again.
  void back_panel() { p->frame.back(); }
  void close() { p->frame.close(); }
  // From the program, between events.
  void drop_closed() {
    p->frame.dropClosed();
    p->settings.dropClosed();
    p->notice.dropClosed();
    p->person.dropClosed();
    p->sending.dropClosed();
  }

  void open_settings(std::string motion) { p->settings.open(actions, std::move(motion)); }
  void open_picture(std::string source, std::string sender, std::string name, std::string when) {
    p->viewer.emplace(actions, std::move(source), std::move(sender), std::move(name), std::move(when));
  }
  void open_send_box(const std::vector<pending_file>& files) {
    p->sending.setSize(440.0f, 560.0f);
    p->sending.open(actions, files);
  }
  void close_send_box() { p->sending.close(); }
  [[nodiscard]] send_box<Actions>* send_box_up() { return p->sending.shown(); }
  void close_picture() { p->viewer.reset(); }
  void close_settings() { p->settings.close(); }
  [[nodiscard]] settings_dialog<Actions>* settings_up() { return p->settings.shown(); }

  void open_drawer() { p->frame.base().open(); }
  void close_drawer() { p->frame.base().close(); }
  void close_drawer_now() { p->frame.base().closeNow(); }
  [[nodiscard]] bool drawer_open() { return p->frame.base().isOpen(); }
  // Whether the pages are still moving.
  [[nodiscard]] bool pages_moving() { return p->frame.settling(); }

  void open_menu(const menu_facts& facts) { p->menu.emplace(actions, facts); }
  void close_menu() { p->menu.reset(); }

  void show_notice(std::string what) {
    p->notice.open(actions, "Not implemented yet", std::format("{} isn't implemented yet.", what));
  }
  void show_message(std::string heading, std::string text) {
    p->notice.open(actions, std::move(heading), std::move(text));
  }
  void close_notice() { p->notice.close(); }

  void open_person(const account_id& account, const std::string& key, const person_facts& facts) {
    p->person.open(actions, account, key, facts);
  }
  void close_person() { p->person.close(); }

  void show(const std::vector<config::account_t>& saved, const model& now) {
    const auto& current = p->frame.base().base().current;
    p->frame.base().content().show(actions, saved, now, current ? std::string_view(current->address) : std::string_view());
  }
  void show_motion(std::string_view level) {
    if (auto* up = p->settings.shown())
      up->show_motion(std::string(level));
  }

  // Its layers, each filling the window, as the default layout places them:
  // nothing placed by hand.
};

}  // namespace mux::ui
