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
  // The dialog it is shown in.
  [[nodiscard]] static dialog_look look_of_dialog() { return {.size = dialog_size::fixed{440.0f, 520.0f}}; }
  Actions* actions = nullptr;
  // What it was handed, for the pages it makes.
  ui_needs<Actions> needs_;
  std::string motion;
  using page_t = spl::variant<settings_home<Actions>, animations_page<Actions>, proxies_page<Actions>, proxy_editor<Actions>,
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
  // Esc: the page's ← -- home from a page, the proxies from one being
  // edited. False on home, which Esc closes.
  bool step_back() {
    return spl::visit(spl::overloaded{[](settings_home<Actions>&) { return false; },
                                      [](auto& one) { return one.parts.header.step_back(); }},
                      this->page());
  }
  // A page fills the dialog across, and is as tall as what it holds.
  void fit_page() {
    spl::visit(
        [this](auto& one) {
          one.fState.apply({.relativeSize = scene::axes::kX});
          one.fState.apply({.autoSize = scene::axes::kY});
          raise_header(one, *needs_.colours);
        },
        this->page());
    this->keep_offset(0.0f);
  }
  // A page's header pinned at the top while what is under it scrolls: drawn
  // over the rest, on the dialog's colour, moved down by the offset. The
  // view is repainted at each step of a scroll, not copied, so it is not
  // carried along with what scrolls.
  template <class P>
    requires requires(P& page) { page.parts.header; }
  static void raise_header(P& page, const palette& colours) {
    page.parts.header.apply({.depth = 1.0f, .background = colours.sidebar});
  }
  static void raise_header(auto&, const palette&) {}
  template <class P>
    requires requires(P& page) { page.parts.header; }
  static void pin_header(P& page, float offset) {
    page.parts.header.fState.setShift(0.0f, offset);
  }
  static void pin_header(auto&, float) {}
  void draw(skiff::scene::Painting& painting, skia::SkCanvas* canvas, float alpha) {
    const float offset = -parts.scroll.contentsShift();
    spl::visit([&](auto& one) { pin_header(one, offset); }, this->page());
    skiff::scene::drawDefault(*this, painting, canvas, alpha);
  }
  // Where the page is scrolled to, to be kept as it is made again.
  [[nodiscard]] float offset() const { return parts.scroll.current(); }
  std::optional<float> offset_due;
  void keep_offset(float at) {
    offset_due = at;
    this->invalidateLayout();
  }
  // What is up coming in from the side, fading in, when the page changes.
  skiff::paint::Tween swap{1.0f, 200.0f, skiff::paint::movement::subtle{}};
  float swap_from = 1.0f;
  std::optional<std::pair<config::theme_t, config::accent_t>> appearance_due;
  void begin_swap(float side) {
    swap_from = side;
    this->fit_page();
    swap.jump(0.0f);
    swap.setTarget(1.0f);
    this->invalidateLayout();
  }
  [[nodiscard]] bool settling() const { return swap.moving() || appearance_due.has_value(); }
  [[nodiscard]] bool wantsTick() const { return this->settling(); }
  void update(double now_ms) {
    if (auto due = std::exchange(appearance_due, std::nullopt); due && this->appearance()) {
      const float at = parts.scroll.current();
      this->page().template emplace<4>(needs_, due->first, due->second);
      this->fit_page();
      this->keep_offset(at);
      this->invalidateLayout();
    }
    if (swap.step(now_ms))
      this->invalidateLayout();
  }

  settings_dialog(const ui_needs<Actions>& n, std::string level) : settings_dialog(n, n.actions, std::move(level)) {}
  settings_dialog(const ui_needs<Actions>& n, Actions* a, std::string level)
      : actions(a), needs_(n), motion(std::move(level)),
        parts{.scroll = nodes::ScrollContainer<page_t>(page_t(std::in_place_index<0>, n))} {
    fState.apply({.fill = true});
    parts.scroll.apply({.fill = true});
    parts.scroll.setCopiesOnScroll(false);
    this->fit_page();
  }

  // Home comes back from the left, the pages come in from the right.
  void show_home() {
    this->page().template emplace<0>(needs_);
    this->begin_swap(-1.0f);
  }
  void show_animations() {
    this->page().template emplace<1>(needs_);
    this->begin_swap(1.0f);
    this->show_motion(motion);
  }
  // Made again where it is up -- a choice on it changed -- where it was
  // scrolled to, not slid in again from its top. The request handler asks
  // before the program refreshes looks_shown; rebuild on the next update
  // so the selected option and sliders read the new values.
  void show_appearance(const config::theme_t& theme, const config::accent_t& accent) {
    if (this->appearance()) {
      appearance_due = std::pair{theme, accent};
      scene::work::mark(fState.fId);
      return;
    }
    appearance_due.reset();
    this->page().template emplace<4>(needs_, theme, accent);
    this->begin_swap(1.0f);
  }
  void show_rendering(const config::renderer_t& renderer, bool partial, bool flash, bool vsync, bool fps) {
    this->page().template emplace<5>(needs_, renderer, partial, flash, vsync, fps);
    this->begin_swap(1.0f);
  }
  void show_notifications(const config::notification_settings& now) {
    this->page().template emplace<8>(needs_, now);
    this->begin_swap(1.0f);
  }
  [[nodiscard]] notifications_page<Actions>* notifications() {
    return spl::visit(spl::overloaded{[](notifications_page<Actions>& one) { return &one; },
                                 [](auto&) -> notifications_page<Actions>* { return nullptr; }},
                      this->page());
  }
  void show_files(const config::sending_settings& now) {
    this->page().template emplace<7>(needs_, now);
    this->begin_swap(1.0f);
  }
  void show_storage(const config::cache_limits& limits, const config::history_settings& history, bool sealed) {
    this->page().template emplace<6>(needs_, limits, history, sealed);
    this->begin_swap(1.0f);
  }
  [[nodiscard]] storage_page<Actions>* storage() {
    return spl::visit(spl::overloaded{[](storage_page<Actions>& one) { return &one; },
                                 [](auto&) -> storage_page<Actions>* { return nullptr; }},
                      this->page());
  }
  [[nodiscard]] rendering_page<Actions>* rendering() {
    return spl::visit(spl::overloaded{[](rendering_page<Actions>& one) { return &one; },
                                 [](auto&) -> rendering_page<Actions>* { return nullptr; }},
                      this->page());
  }
  [[nodiscard]] appearance_page<Actions>* appearance() {
    return spl::visit(spl::overloaded{[](appearance_page<Actions>& one) { return &one; },
                                 [](auto&) -> appearance_page<Actions>* { return nullptr; }},
                      this->page());
  }
  void show_proxies(const std::vector<config::proxy_settings>& all, bool with_back = true) {
    this->page().template emplace<2>(needs_, all, with_back);
    this->begin_swap(1.0f);
  }
  void show_proxy(const std::optional<config::proxy_settings>& from, int index) {
    this->page().template emplace<3>(needs_, from, index);
    this->begin_swap(1.0f);
  }
  [[nodiscard]] proxy_editor<Actions>* editor() {
    return spl::visit(spl::overloaded{[](proxy_editor<Actions>& one) { return &one; },
                                 [](auto&) -> proxy_editor<Actions>* { return nullptr; }},
                      this->page());
  }
  void show_motion(std::string level) {
    motion = std::move(level);
    spl::visit([this](auto& one) { one.show_motion(motion); }, this->page());
  }


  void layoutChildren() {
    auto& scroll = parts.scroll;
    const skia::SkRect box = fState.contentBox();
    // Settings keep their offset through measurement and dialog resizing.
    // A scroll view otherwise follows its end when an initially short page
    // becomes scrollable, opening the menu at its bottom.
    if (auto at = std::exchange(offset_due, std::nullopt);
        at || scroll.bounds().width() != box.width() || scroll.bounds().height() != box.height())
      scroll.setCurrent(at.value_or(scroll.current()));
    scroll.fState.arrange(0.0f, 0.0f);
    const float value = swap.value();
    scroll.fState.setAlpha(value);
    scene::layout(scroll, skia::SkRect::MakeXYWH(box.fLeft + (1.0f - value) * 32.0f * swap_from, box.fTop,
                                                 box.width(), box.height()));
  }
};

}  // namespace mux::ui
