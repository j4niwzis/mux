// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:settings -- The settings box.
module;
#include "../inline_in_release.h"
#include "skiff/named.h"
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
import mux.composition;
import mux.plugins.ui;
import :settings_pages;
import :appearance;

export namespace mux::ui {

// Settings' pages, each with what it shows.
namespace settings_page {
struct home {};
struct appearance {
  config::theme_t theme;
  config::accent_t accent;
};
}  // namespace settings_page
// The plugins' pages among them (contract::settings_pages): each its own
// type of what it shows, and its node made from that.
template <class Page>
using plugin_settings_node = decltype(extend(contract::settings_pages{}, std::declval<const ui_needs&>(), std::declval<const Page&>()));
template <class List>
struct settings_page_types;
template <class... Pages>
struct settings_page_types<composition::page_list<Pages...>> {
  using type = type_list<Pages...>;
};
template <class List>
struct plugin_settings_pages;
template <class... Features>
struct plugin_settings_pages<plugin_list<Features...>> {
  using type = typename joined<type_list<>,
      typename settings_page_types<decltype(settings_pages(contract::settings_pages{}, type_tag<Features>{}))>::type...>::type;
};
template <class List>
struct settings_pages_of;
template <class... Pages>
struct settings_pages_of<type_list<Pages...>> {
  using pages = spl::variant<settings_page::home, settings_page::appearance,
                             Pages...>;
  using nodes = spl::variant<settings_home_t,
                             appearance_page_t,
                             plugin_settings_node<Pages>...>;
};
using settings_plugins = typename plugin_settings_pages<composition::providers<contract::settings_pages, enabled_plugins>>::type;
using settings_page_t = typename settings_pages_of<settings_plugins>::pages;
// Settings open, on a page.
struct settings_facts {
  settings_page_t page = settings_page::home{};
};
// Where the settings page rests scrolled, as the program keeps it.
struct settings_scroll {
  float at = 0.0f;
  friend bool operator==(const settings_scroll&, const settings_scroll&) = default;
};
// What the page says as it comes to rest scrolled.
struct settings_scrolled_to {
  request::settings_scrolled operator()(float at) const { return {at}; }
};

// The settings box: its pages, one up at a time in a view that scrolls (a
// skiff page view), made from what is shown (settings_facts). Home comes back
// from the left, the pages come in from the right; a page made again where
// it is up -- a choice on it changed -- is made where it was scrolled to. A
// page's header -- its first part -- stays at the top while the rest
// scrolls, raised over it; home has none. Esc on a page goes where its ←
// goes, on home it closes the box (escape_of, by the facts).
using settings_page_node = typename settings_pages_of<settings_plugins>::nodes;
MUX_INLINE_IN_RELEASE auto settings_view(const ui_needs& needs) {
  const palette* colours = needs.colours;
  // The page the facts name, made in place of the one up.
  auto show = [n = needs](settings_page_node& page, const settings_facts& facts) {
    const palette& c = *n.colours;
    spl::visit(spl::overloaded{
                   [&](settings_page::home) { page.template emplace<settings_home_t>(settings_home(c)); },
                   [&](const settings_page::appearance& one) { page.template emplace<appearance_page_t>(appearance_page(n, one.theme, one.accent)); },
                   // A plugin's page: its node, made from what it shows.
                   [&]<class Page>(const Page& one) {
                     page.template emplace<plugin_settings_node<Page>>(extend(contract::settings_pages{}, n, one));
                   }},
               facts.page);
  };
  // The same page asked for again: made again where it is.
  auto same = [](const settings_facts& before, const settings_facts& now) { return before.page.index() == now.page.index(); };
  auto side = [](const settings_facts& facts) {
    return spl::visit(spl::overloaded{[](settings_page::home) { return -1.0f; }, [](const auto&) { return 1.0f; }}, facts.page);
  };
  // Every page but home has a header, raised over what scrolls under it.
  auto pin = [colours](const settings_facts& facts) {
    return spl::visit(spl::overloaded{[](settings_page::home) { return std::optional<scene::Spec>(); },
                                      [colours](const auto&) { return std::optional(scene::Spec{.depth = 1.0f, .background = colours->sidebar}); }},
                      facts.page);
  };
  // Where it is scrolled, kept by the program: a page made again (another
  // theme) goes back where it rested.
  return SKIFF_NAMED(settings_view, skiff::compose::kept_scroll<skiff::model::Field<&settings_scroll::at>>(settings_scrolled_to{},
      skiff::compose::page_view<settings_facts, settings_page_node>(
          std::move(show), same, side, pin, settings_page_node(std::in_place_index<0>, settings_home(*colours)), {.fill = true})));
}
using settings_view_t = decltype(settings_view(std::declval<const ui_needs&>()));
// The window's dialog of it: made from what is shown, then told it as it changes.
inline settings_view_t make_content(const ui_needs& needs, const settings_facts&) {
  return settings_view(needs);
}
[[nodiscard]] inline dialog_look content_look(std::type_identity<settings_facts>) {
  return {.size = dialog_size::fixed{440.0f, 520.0f}};
}

}  // namespace mux::ui

