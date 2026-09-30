// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:settings -- The settings box.
export module mux.ui:settings;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.scroll;
import mux.core;
import mux.config;
import :base;
import :proxies;
import :appearance;
import :storage;

export namespace mux::ui {

template <class Actions>
struct settings_dialog : scene::Node {
  Actions* actions = nullptr;
  std::string motion;
  using page_t = splice::variant<settings_home<Actions>, animations_page<Actions>, proxies_page<Actions>, proxy_editor<Actions>,
                              appearance_page<Actions>, rendering_page<Actions>, storage_page<Actions>, files_page<Actions>,
                              notifications_page<Actions>>;
  // The page up: home, or one of its pages.
  // The page up, in a scroll view of the dialog's size: sized to what it
  // holds, it scrolls where it is taller -- never past the dialog's edges,
  // however few or many its rows.
  struct parts_t {
    nodes::ScrollContainer<page_t> scroll;
  } parts;
  [[nodiscard]] page_t& page() { return std::get<0>(parts.scroll.fChildren); }
  // A page fills the dialog across, and is as tall as what it holds.
  void fit_page() {
    splice::visit(
        [](auto& one) {
          one.fState.apply({.relativeSize = scene::axes::kX});
          one.fState.apply({.autoSize = scene::axes::kY});
          raise_header(one);
        },
        this->page());
    parts.scroll.scrollToStart();
  }
  // A page's header pinned at the top while what is under it scrolls: drawn
  // over the rest, on the dialog's colour, moved down by the offset. The
  // view is repainted at each step of a scroll, not copied, so it is not
  // carried along with what scrolls.
  template <class P>
    requires requires(P& page) { page.parts.header; }
  static void raise_header(P& page) {
    page.parts.header.apply({.depth = 1.0f, .background = sidebar_colour});
  }
  static void raise_header(auto&) {}
  template <class P>
    requires requires(P& page) { page.parts.header; }
  static void pin_header(P& page, float offset) {
    page.parts.header.fState.setShift(0.0f, offset);
  }
  static void pin_header(auto&, float) {}
  void draw(skia::SkCanvas* canvas, float alpha) {
    const float offset = -parts.scroll.contentsShift();
    splice::visit([&](auto& one) { pin_header(one, offset); }, this->page());
    skiff::scene::drawDefault(*this, canvas, alpha);
  }
  // Where the page is scrolled to, to be kept as it is made again.
  [[nodiscard]] float offset() const { return parts.scroll.current(); }
  void keep_offset(float at) { parts.scroll.setCurrent(at); }
  // What is up coming in from the side, fading in, when the page changes.
  skiff::paint::Tween swap{1.0f, 200.0f, skiff::paint::movement::subtle{}};
  float swap_from = 1.0f;
  void begin_swap(float side) {
    swap_from = side;
    this->fit_page();
    swap.jump(0.0f);
    swap.setTarget(1.0f);
    this->invalidateLayout();
  }
  [[nodiscard]] bool settling() const { return swap.moving(); }
  [[nodiscard]] bool wantsTick() const { return swap.moving(); }
  void update(double now_ms) {
    if (swap.step(now_ms))
      this->invalidateLayout();
  }

  settings_dialog(Actions* a, std::string level)
      : actions(a), motion(std::move(level)),
        parts{.scroll = nodes::ScrollContainer<page_t>(page_t(std::in_place_index<0>, a))} {
    fState.apply({.fill = true});
    parts.scroll.apply({.fill = true});
    parts.scroll.setCopiesOnScroll(false);
    this->fit_page();
  }

  // Home comes back from the left, the pages come in from the right.
  void show_home() {
    this->page().template emplace<0>(actions);
    this->begin_swap(-1.0f);
  }
  void show_animations() {
    this->begin_swap(1.0f);
    this->page().template emplace<1>(actions);
    this->show_motion(motion);
  }
  // Made again where it is up -- a choice on it changed -- where it was
  // scrolled to, not slid in again from its top.
  void show_appearance(const config::theme_t& theme, const config::accent_t& accent) {
    const bool again = this->appearance() != nullptr;
    const float at = parts.scroll.current();
    this->page().template emplace<4>(actions, theme, accent);
    if (again) {
      this->fit_page();
      parts.scroll.setCurrent(at);
      this->invalidateLayout();
    } else {
      this->begin_swap(1.0f);
    }
  }
  void show_rendering(const config::renderer_t& renderer, bool partial, bool flash, bool vsync, bool fps) {
    this->page().template emplace<5>(actions, renderer, partial, flash, vsync, fps);
    this->begin_swap(1.0f);
  }
  void show_notifications(const config::notification_settings& now) {
    this->page().template emplace<8>(actions, now);
    this->begin_swap(1.0f);
  }
  [[nodiscard]] notifications_page<Actions>* notifications() {
    return splice::visit(splice::overloaded{[](notifications_page<Actions>& one) { return &one; },
                                 [](auto&) -> notifications_page<Actions>* { return nullptr; }},
                      this->page());
  }
  void show_files(const config::sending_settings& now) {
    this->page().template emplace<7>(actions, now);
    this->begin_swap(1.0f);
  }
  void show_storage(const config::cache_limits& limits, const config::history_settings& history) {
    this->page().template emplace<6>(actions, limits, history);
    this->begin_swap(1.0f);
  }
  [[nodiscard]] storage_page<Actions>* storage() {
    return splice::visit(splice::overloaded{[](storage_page<Actions>& one) { return &one; },
                                 [](auto&) -> storage_page<Actions>* { return nullptr; }},
                      this->page());
  }
  [[nodiscard]] rendering_page<Actions>* rendering() {
    return splice::visit(splice::overloaded{[](rendering_page<Actions>& one) { return &one; },
                                 [](auto&) -> rendering_page<Actions>* { return nullptr; }},
                      this->page());
  }
  [[nodiscard]] appearance_page<Actions>* appearance() {
    return splice::visit(splice::overloaded{[](appearance_page<Actions>& one) { return &one; },
                                 [](auto&) -> appearance_page<Actions>* { return nullptr; }},
                      this->page());
  }
  void show_proxies(const std::vector<config::proxy_settings>& all, bool with_back = true) {
    this->page().template emplace<2>(actions, all, with_back);
    this->begin_swap(1.0f);
  }
  void show_proxy(const std::optional<config::proxy_settings>& from, int index) {
    this->page().template emplace<3>(actions, from, index);
    this->begin_swap(1.0f);
  }
  [[nodiscard]] proxy_editor<Actions>* editor() {
    return splice::visit(splice::overloaded{[](proxy_editor<Actions>& one) { return &one; },
                                 [](auto&) -> proxy_editor<Actions>* { return nullptr; }},
                      this->page());
  }
  void show_motion(std::string level) {
    motion = std::move(level);
    splice::visit([this](auto& one) { one.show_motion(motion); }, this->page());
  }


  void layoutChildren() {
    auto& scroll = parts.scroll;
    scroll.fState.arrange(0.0f, 0.0f);
    const float value = swap.value();
    const skia::SkRect box = fState.contentBox();
    scroll.fState.setAlpha(value);
    scene::layout(scroll, skia::SkRect::MakeXYWH(box.fLeft + (1.0f - value) * 32.0f * swap_from, box.fTop,
                                                 box.width(), box.height()));
  }
};

}  // namespace mux::ui
