// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:settings -- The settings box.
export module mux.ui:settings;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.nodes.scroll;
import mux.core;
import mux.config;
import mux.ui.base;
import :proxies;
import :appearance;
import :storage;
import :about;

export namespace mux::ui {

// Settings' pages, each with what it shows.
namespace settings_page {
struct home {};
struct about {};
struct library { std::string name; };
struct animations {};
struct appearance {
  config::theme_t theme;
  config::accent_t accent;
};
struct rendering {
  config::renderer_t renderer;
};
struct notifications {
  config::notification_settings now;
};
struct files {
  config::sending_settings now;
};
struct storage {
  config::cache_limits limits;
  config::history_settings history;
  bool sealed = false;
};
struct proxies {
  std::vector<config::proxy_settings> all;
  bool with_back = true;
};
struct proxy {
  std::optional<config::proxy_settings> from;
  int index = -1;
};
}  // namespace settings_page
using settings_page_t = spl::variant<settings_page::home, settings_page::animations, settings_page::appearance, settings_page::rendering,
                                     settings_page::notifications, settings_page::files, settings_page::storage, settings_page::proxies,
                                     settings_page::proxy, settings_page::about, settings_page::library>;
// Settings open, on a page.
struct settings_facts {
  settings_page_t page = settings_page::home{};
};
// A page's header is the first child of its expression. Older pages keep
// named parts until their own conversion reaches them.
template <class Page> auto& settings_header(Page& page)
  requires requires { page.parts.header; }
{ return page.parts.header; }
template <class Page> auto& settings_header(Page& page)
  requires requires { page.fParts; }
{ return std::get<0>(page.fParts); }

// The settings box: its pages, one up at a time in a view that scrolls (a
// skiff page view), made from what is shown (settings_facts). Home comes back
// from the left, the pages come in from the right; a page made again where
// it is up -- a choice on it changed -- is made where it was scrolled to. A
// page's header stays at the top while the rest scrolls; Esc on a page goes
// where its ← goes, on home it closes the box.
using settings_page_node = spl::variant<settings_home_t, animations_page_t, proxies_page_t, proxy_editor_t,
                                        appearance_page_t, rendering_page_t, storage_page_t, files_page_t,
                                        notifications_page_t, about_page_t, library_page_t>;
inline auto settings_view(const ui_needs& needs) {
  const palette* colours = needs.colours;
  // The page the facts name, made in place of the one up, its header raised
  // over what scrolls under it.
  auto show = [n = needs](settings_page_node& page, const settings_facts& facts) {
    const palette& c = *n.colours;
    spl::visit(spl::overloaded{
                   [&](settings_page::home) { page.template emplace<0>(settings_home(c)); },
                   [&](settings_page::animations) { page.template emplace<1>(animations_page(c)); },
                   [&](const settings_page::proxies& one) { page.template emplace<2>(proxies_page(c, one.all, one.with_back)); },
                   [&](const settings_page::proxy& one) { page.template emplace<3>(proxy_editor(c, one.from, one.index)); },
                   [&](const settings_page::appearance& one) { page.template emplace<4>(appearance_page(n, one.theme, one.accent)); },
                   [&](settings_page::rendering) { page.template emplace<5>(rendering_page(c)); },
                   [&](settings_page::storage) { page.template emplace<6>(storage_page(c)); },
                   [&](settings_page::files) { page.template emplace<7>(files_page(c)); },
                   [&](settings_page::notifications) { page.template emplace<8>(notifications_page(c)); },
                   [&](settings_page::about) { page.template emplace<9>(about_page(c)); },
                   [&](const settings_page::library& one) { page.template emplace<10>(library_page(c, one.name)); }},
               facts.page);
    spl::visit(spl::overloaded{[](settings_home_t&) {},
                               [&](auto& one) { settings_header(one).apply({.depth = 1.0f, .background = c.sidebar}); }},
               page);
  };
  // The same page asked for again: made again where it is.
  auto same = [](const settings_facts& before, const settings_facts& now) { return before.page.index() == now.page.index(); };
  auto side = [](const settings_facts& facts) {
    return spl::visit(spl::overloaded{[](settings_page::home) { return -1.0f; }, [](const auto&) { return 1.0f; }}, facts.page);
  };
  auto pin = [](settings_page_node& page) {
    return spl::visit(spl::overloaded{[](settings_home_t&) -> scene::Node* { return nullptr; },
                                      [](auto& one) -> scene::Node* { return &settings_header(one); }},
                      page);
  };
  auto back = [](settings_page_node& page) {
    return spl::visit(spl::overloaded{[](settings_home_t&) { return false; },
                                      [](auto& one) { return page_step_back(settings_header(one)); }},
                      page);
  };
  return skiff::compose::page_view<settings_facts, settings_page_node>(
      std::move(show), same, side, pin, back, settings_page_node(std::in_place_index<0>, settings_home(*colours)), {.fill = true});
}
using settings_view_t = decltype(settings_view(std::declval<const ui_needs&>()));
// The window's dialog of it: made from what is shown, then told it as it changes.
inline settings_view_t make_content(std::type_identity<settings_view_t>, const ui_needs& needs, const settings_facts&) {
  return settings_view(needs);
}
[[nodiscard]] inline dialog_look content_look(std::type_identity<settings_view_t>) {
  return {.size = dialog_size::fixed{440.0f, 520.0f}};
}

}  // namespace mux::ui

// Outside a release build each child is walked through its table, and a
// stack lays itself out through its own (Stack::layoutChildren): so a unit
// that lays out or walks one of these would make its table, and the walks
// of everything under it, there -- the chats screen's took this unit past
// ten minutes. Their tables are made once, each in a walks unit of this
// module (src/ui/walks_*.cc); here, and wherever they are walked, a call.
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::settings_view_t> = true;  // walks_settings.cc
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::settings_view_t>() noexcept;
