// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:settings -- The settings box.
export module mux.ui:settings;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
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
  struct parts_t {
    page_t page;
  } parts;
  // What is up coming in from the side, fading in, when the page changes.
  skiff::paint::Tween swap{1.0f, 200.0f, skiff::paint::movement::subtle{}};
  float swap_from = 1.0f;
  void begin_swap(float side) {
    swap_from = side;
    swap.jump(0.0f);
    swap.setTarget(1.0f);
    this->invalidateLayout();
  }
  [[nodiscard]] bool settling() const { return swap.moving(); }
  void update(double now_ms) {
    if (swap.step(now_ms))
      this->invalidateLayout();
  }

  settings_dialog(Actions* a, std::string level)
      : actions(a), motion(std::move(level)), parts{.page = page_t(std::in_place_index<0>, a)} {
    fState.apply({.fill = true});
  }

  // Home comes back from the left, the pages come in from the right.
  void show_home() {
    parts.page.template emplace<0>(actions);
    this->begin_swap(-1.0f);
  }
  void show_animations() {
    this->begin_swap(1.0f);
    parts.page.template emplace<1>(actions);
    this->show_motion(motion);
  }
  void show_appearance(const config::theme_t& theme, const config::accent_t& accent) {
    parts.page.template emplace<4>(actions, theme, accent);
    this->begin_swap(1.0f);
  }
  void show_rendering(const config::renderer_t& renderer, bool partial, bool flash) {
    parts.page.template emplace<5>(actions, renderer, partial, flash);
    this->begin_swap(1.0f);
  }
  void show_notifications(const config::notification_settings& now) {
    parts.page.template emplace<8>(actions, now);
    this->begin_swap(1.0f);
  }
  [[nodiscard]] notifications_page<Actions>* notifications() {
    return splice::visit(splice::overloaded{[](notifications_page<Actions>& one) { return &one; },
                                 [](auto&) -> notifications_page<Actions>* { return nullptr; }},
                      parts.page);
  }
  void show_files(const config::sending_settings& now) {
    parts.page.template emplace<7>(actions, now);
    this->begin_swap(1.0f);
  }
  void show_storage(const config::cache_limits& limits, const config::history_settings& history) {
    parts.page.template emplace<6>(actions, limits, history);
    this->begin_swap(1.0f);
  }
  [[nodiscard]] storage_page<Actions>* storage() {
    return splice::visit(splice::overloaded{[](storage_page<Actions>& one) { return &one; },
                                 [](auto&) -> storage_page<Actions>* { return nullptr; }},
                      parts.page);
  }
  [[nodiscard]] rendering_page<Actions>* rendering() {
    return splice::visit(splice::overloaded{[](rendering_page<Actions>& one) { return &one; },
                                 [](auto&) -> rendering_page<Actions>* { return nullptr; }},
                      parts.page);
  }
  [[nodiscard]] appearance_page<Actions>* appearance() {
    return splice::visit(splice::overloaded{[](appearance_page<Actions>& one) { return &one; },
                                 [](auto&) -> appearance_page<Actions>* { return nullptr; }},
                      parts.page);
  }
  void show_proxies(const std::vector<config::proxy_settings>& all, bool with_back = true) {
    parts.page.template emplace<2>(actions, all, with_back);
    this->begin_swap(1.0f);
  }
  void show_proxy(const std::optional<config::proxy_settings>& from, int index) {
    parts.page.template emplace<3>(actions, from, index);
    this->begin_swap(1.0f);
  }
  [[nodiscard]] proxy_editor<Actions>* editor() {
    return splice::visit(splice::overloaded{[](proxy_editor<Actions>& one) { return &one; },
                                 [](auto&) -> proxy_editor<Actions>* { return nullptr; }},
                      parts.page);
  }
  void show_motion(std::string level) {
    motion = std::move(level);
    splice::visit([this](auto& one) { one.show_motion(motion); }, parts.page);
  }


  void layoutChildren() {
    splice::visit(
        [this](auto& one) {
          one.fState.arrange(0.0f, 0.0f);
          const float value = swap.value();
          const skia::SkRect box = fState.contentBox();
          one.fState.setAlpha(value);
          scene::layout(one, skia::SkRect::MakeXYWH(box.fLeft + (1.0f - value) * 32.0f * swap_from, box.fTop,
                                                    box.width(), box.height()));
        },
        parts.page);
  }
};

}  // namespace mux::ui
