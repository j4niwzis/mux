// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:room_settings -- A room's settings: the client's pages and its
// protocol's, as a function over what the program says of them.
module;
#include "skiff/named.h"
export module mux.ui:room_settings;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.icon;
import skiff.nodes.image;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.button;
import skiff.widgets.model;
import skiff.widgets.sliderbar;
import skiff.widgets.textarea;
import skiff.model;
import skiff.compose;
import mux.core;
import mux.config;
import mux.protocols;
import mux.ui.base;
import mux.plugins.ui.top;
import mux.ui.proto;
import :info;

export namespace mux::ui {

// A room's settings, Element's and Cinny's: the tabs down the left -- the
// protocol's, then the client's (General, Notifications, Appearance) -- and
// the tab up beside them, under its head. As the program says: the tab, the
// tab list up in its place on a narrow window, and what the protocol keeps
// while they are open (room_settings_facts). Every page is made once as
// they open and shown while its tab is up: what is typed in one stays as
// tabs are switched. A press asks the program.
using room_facts_t = std::optional<room_settings_facts>;

// Whether a tab is the one up.
template <class Tab>
struct tab_up {
  bool operator()(const room_facts_t& now) const {
    return now && !now->navigation &&
           spl::visit(spl::overloaded{[](const Tab&) { return true; }, [](const auto&) { return false; }}, now->tab);
  }
};
// The client's General -- the room's events, receipts, previews, typing,
// Home, leaving -- in a tab of its own, or under the protocol's General
// where it has one (Matrix's: the room's name, topic and address): one
// General, not two.
constexpr bool own_general_tab(settings_tab::general) { return true; }
template <class Tab> constexpr bool own_general_tab(Tab) { return false; }
[[nodiscard]] inline bool general_on_its_own(const room_settings_facts& facts) {
  return spl::visit([](auto tag) { return own_general_tab(general_host_of(state_of<decltype(tag)>{})); }, facts.speaks);
}
// Whether the client's General is up: its own tab, or the tab hosting it.
struct general_up {
  bool operator()(const room_facts_t& now) const {
    return now && (tab_up<settings_tab::general>{}(now) ||
                   spl::visit([&](auto tag) { return tab_up<decltype(general_host_of(state_of<decltype(tag)>{}))>{}(now); }, now->speaks));
  }
};
// Whether a tab's row is listed: the client's General only where no tab of
// the protocol's hosts it; every other tab, always.
constexpr bool tab_listed(const auto&, const room_facts_t&) { return true; }
inline bool tab_listed(settings_tab::general, const room_facts_t& now) { return !now || general_on_its_own(*now); }
// A tab's row: its icon and title, marked while it is up; pressed, it is.
template <class Tab>
auto manage_tab_row(const palette& colours, Tab tab) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(manage_tab_row, c::spec_for<room_facts_t>([](const room_facts_t& now) { return scene::Spec{.visible = tab_listed(Tab{}, now), .selected = tab_up<Tab>{}(now)}; },
      c::onPress(sends<request::manage_tab>{{manage_tab_t{tab}}},
          c::row(c::hbox(8.0f, {.fillX = true, .height = 40.0f, .padding = {0.0f, 12.0f, 0.0f, 12.0f}, .cornerRadius = colours.widgets.fCorner,
                                .hoverBackground = colours.background, .selectedBackground = colours.tile}),
                 c::styled({.width = 16.0f, .height = 16.0f, .alignSelf = scene::align::kMiddle}, icon_mark(colours, tab_icon(tab))),
                 c::applied<room_facts_t>([](const room_facts_t& now) { return tab_up<Tab>{}(now); },
                                          [](nodes::Text& title, bool up) { title.setBold(up); },
                                          c::styled({.shrink = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                                                    elided(nodes::Text(std::string(tab_title(tab)), 14.0f, colours.text))))))));
}
using client_tab_row_t = decltype(manage_tab_row(std::declval<const palette&>(), settings_tab::general{}));


// The protocol's rows, those its room has; a row of each protocol's kind.
template <class Tab>
auto protocol_tab_row(const palette& colours, const room_settings_facts& facts) {
  using manage_defaults::tab_visible;
  return skiff::compose::visible(tab_visible(Tab{}, facts), manage_tab_row(colours, Tab{}));
}
template <class... Tabs>
auto protocol_tab_rows(manage_tab_list<Tabs...>, const palette& colours, const room_settings_facts& facts) {
  return SKIFF_NAMED(protocol_tab_rows, skiff::compose::column(skiff::compose::vbox(2.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                                protocol_tab_row<Tabs>(colours, facts)...));
}
// A protocol's page for a tab, shown while it is up: made by the protocol
// (make_manage_page, found by ADL with its tab), from the facts as the
// settings opened; it reads them as they move.
template <class Tab>
auto protocol_page(const palette& colours, const room_settings_facts& facts) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(protocol_page, c::shown_for<room_facts_t>(tab_up<Tab>{},
      c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), make_manage_page(Tab{}, colours, facts))));
}
template <class... Tabs>
auto protocol_pages(manage_tab_list<Tabs...>, const palette& colours, const room_settings_facts& facts) {
  return SKIFF_NAMED(protocol_pages, skiff::compose::column(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                                protocol_page<Tabs>(colours, facts)...));
}
// The plugins' rows and pages (contract::room_settings_tabs): made as a
// protocol's are, a row each listed where its tab_visible says.
template <class... Tabs>
auto plugin_tab_rows(type_list<Tabs...>, const palette& colours, const room_settings_facts& facts) {
  return protocol_tab_rows(manage_tab_list<Tabs...>{}, colours, facts);
}
template <class... Tabs>
auto plugin_pages(type_list<Tabs...>, const palette& colours, const room_settings_facts& facts) {
  return protocol_pages(manage_tab_list<Tabs...>{}, colours, facts);
}
// Each protocol's, one of them for a room: by its index in the protocols'
// list (two protocols may make the same types).
template <class Tag, class... Tags>
consteval std::size_t protocol_index(protocol_list<Tags...>) {
  constexpr std::array<bool, sizeof...(Tags)> is{std::derived_from<Tag, Tags>...};
  return static_cast<std::size_t>(std::ranges::find(is, true) - is.begin());
}
template <class>
struct protocol_parts_of;
template <class... Tags>
struct protocol_parts_of<protocol_list<Tags...>> {
  using rows = spl::variant<decltype(protocol_tab_rows(manage_tabs_of(state_of<Tags>{}), std::declval<const palette&>(),
                                                       std::declval<const room_settings_facts&>()))...>;
  using pages = spl::variant<decltype(protocol_pages(manage_tabs_of(state_of<Tags>{}), std::declval<const palette&>(),
                                                     std::declval<const room_settings_facts&>()))...>;
};
using protocol_rows_t = typename protocol_parts_of<protocols>::rows;
using protocol_pages_t = typename protocol_parts_of<protocols>::pages;
inline protocol_rows_t protocol_rows_of(const palette& colours, const room_settings_facts& facts) {
  return spl::visit([&](auto tag) {
    return protocol_rows_t(std::in_place_index<protocol_index<decltype(tag)>(protocols{})>,
                           protocol_tab_rows(manage_tabs_of(state_of<decltype(tag)>{}), colours, facts));
  }, facts.speaks);
}
inline protocol_pages_t protocol_pages_of(const palette& colours, const room_settings_facts& facts) {
  return spl::visit([&](auto tag) {
    return protocol_pages_t(std::in_place_index<protocol_index<decltype(tag)>(protocols{})>,
                            protocol_pages(manage_tabs_of(state_of<decltype(tag)>{}), colours, facts));
  }, facts.speaks);
}

// The client's pages: Notifications and Appearance (General is pages.cc's).
inline auto notifications_page(const palette& colours, const room_settings_facts& facts) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(notifications_page, c::column(c::vbox(10.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 28.0f, 24.0f, 12.0f}}),
      tab_heading(colours, "Notifications"), chat_notify_view(colours, facts.chat),
      c::styled({.fillX = true}, wrapped(nodes::Text(facts.space ? "For every chat in this space, unless the chat chooses again; Default is "
                                                                    "as the space above it, or the account, says."
                                                                  : "Default is as the space it is in, or the account, says.",
                                                     12.0f, colours.dim)))));
}
inline auto looks_page(const palette& colours, const room_settings_facts& facts) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(looks_page, c::column(c::vbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 28.0f, 24.0f, 12.0f}}),
      tab_heading(colours, "Appearance"),
      c::scoped<config::chat_choices>(c::handlers(), style_controls(colours), facts.chat),
      look_choices(colours, choice_level::chat{})));
}
template <class Up, class Page>
auto client_page(Up up, Page page) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(client_page, c::shown_for<room_facts_t>(up, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(page))));
}

// The tab up's title: the protocol's name for General, else the tab's.
[[nodiscard]] inline std::string manage_title(const room_settings_facts& facts) {
  return spl::visit(spl::overloaded{[&](settings_tab::general) {
                                      return spl::visit([](auto tag) { return std::string(preferences_title(state_of<decltype(tag)>{})); }, facts.speaks);
                                    },
                                    [](const auto& one) { return std::string(tab_title(one)); }},
                    facts.tab);
}

// The settings, for a room: made once as they open (content_persistent),
// again only for another room (content_matches).
inline auto room_settings_view(const ui_needs& n, const room_settings_facts& facts) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  const room_manage_style presentation = spl::visit([&](auto tag) { return manage_style_of(state_of<decltype(tag)>{}, colours); }, facts.speaks);
  const auto narrow_page = [](const room_facts_t& now) { return now && now->narrow && !now->navigation; };
  const auto tabs_up = [](const room_facts_t& now) { return !now || !now->narrow || now->navigation; };
  const auto page_up = [](const room_facts_t& now) { return !now || !now->narrow || !now->navigation; };
  const float across = presentation.navigation_width;
  return SKIFF_NAMED(room_settings_view, c::column(c::vbox(0.0f, {.fill = true, .cornerRadius = std::max(10.0f, colours.widgets.fCorner), .background = colours.background,
                                  .masking = true}),
      // Without a head over each page: the dialog's own, its ← on a narrow
      // window's page.
      c::visible(!presentation.page_header,
          c::row(c::hbox(12.0f, {.fillX = true, .height = kPageHeaderHeight, .padding = {0.0f, 10.0f, 0.0f, 10.0f}}),
                 c::shown_for<room_facts_t>(narrow_page, icon_button(colours, icon::back{}, sends<request::manage_back>{})),
                 c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                           elided(nodes::Text("Room Settings - " + facts.name, 18.0f, colours.text, true))),
                 icon_button(colours, icon::close{}, sends<request::close_manage>{}))),
      c::row(c::hbox(0.0f, {.fillX = true, .grow = scene::axes::kY}),
          // The tabs: across the window where it is narrow.
          c::shown_for<room_facts_t>(tabs_up,
              c::spec_for<room_facts_t>([across](const room_facts_t& now) {
                return now && now->narrow ? scene::Spec{.width = 1.0f, .relativeSize = scene::axes::kBoth}
                                          : scene::Spec{.width = across, .relativeSize = scene::axes::kY};
              },
                  c::column(c::vbox(0.0f, {.fillY = true, .width = across, .padding = {0.0f, 8.0f, 32.0f, 8.0f}, .background = colours.sidebar}),
                      c::row(c::hbox(8.0f, {.fillX = true, .height = 54.0f}), avatar_mark(avatar_of::chat{}, facts.id, facts.name, 24.0f),
                             c::styled({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX, .shrink = scene::axes::kX,
                                        .alignSelf = scene::align::kMiddle},
                                       elided(nodes::Text(facts.name, 18.0f, colours.text, true))),
                             c::shown_for<room_facts_t>([](const room_facts_t& now) { return now && now->narrow && now->navigation; },
                                                        icon_button(colours, icon::close{}, sends<request::close_manage>{}))),
                      c::column(c::vbox(2.0f, {.fillX = true, .autoSize = scene::axes::kY}), protocol_rows_of(colours, facts)),
                      plugin_tab_rows(plugin_room_tabs_t{}, colours, facts),
                      manage_tab_row(colours, settings_tab::general{}),
                      manage_tab_row(colours, settings_tab::notifications{}),
                      manage_tab_row(colours, settings_tab::looks{})))),
          // The tab up, under its head where the protocol has one.
          c::shown_for<room_facts_t>(page_up,
              c::column(c::vbox(0.0f, {.fillY = true, .relativeSize = scene::axes::kY, .grow = scene::axes::kX, .background = colours.background}),
                  c::visible(presentation.page_header,
                      c::row(c::hbox(12.0f, {.fillX = true, .height = kPageHeaderHeight, .padding = {0.0f, 10.0f, 0.0f, 10.0f}}),
                             c::shown_for<room_facts_t>([](const room_facts_t& now) { return now && (manage_editor_up(*now) || now->narrow); },
                                                        icon_button(colours, icon::back{}, sends<request::manage_back>{})),
                             c::text_for<room_facts_t>([](const room_facts_t& now) { return now ? manage_title(*now) : std::string(); },
                                 c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(nodes::Text("", 20.0f, colours.text, true)))),
                             icon_button(colours, icon::close{}, sends<request::close_manage>{}))),
                  // Another tab: shown from its top.
                  c::styled({.fillX = true, .relativeSize = scene::axes::kX, .grow = scene::axes::kY},
                      c::projected<room_facts_t>([](const room_facts_t& now) { return now ? now->tab.index() : std::size_t{0}; },
                          c::restarted_scroll<std::size_t>(nodes::ScrollContainer(
                              c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                                        client_page(tab_up<settings_tab::notifications>{}, notifications_page(colours, facts)),
                                        client_page(tab_up<settings_tab::looks>{}, looks_page(colours, facts)),
                                        c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), protocol_pages_of(colours, facts)),
                                        plugin_pages(plugin_room_tabs_t{}, colours, facts),
                                        // Under the protocol's page, where its General hosts it.
                                        client_page(general_up{}, room_general_page(colours, facts, presentation.page_header && general_on_its_own(facts)))))))))))));
}
using room_settings_view_t = decltype(room_settings_view(std::declval<const ui_needs&>(), std::declval<const room_settings_facts&>()));
using room_settings_t = room_settings_view_t;
inline dialog_look content_look(std::type_identity<room_settings_facts>) { return {.size = dialog_size::fixed{802.0f, 684.0f}}; }
template <class Needs>
auto make_content(const Needs& needs, const room_settings_facts& facts) {
  return room_settings_view(needs, facts);
}
// The same room's settings stand while they are up.
inline bool content_matches(const room_settings_facts& was, const room_settings_facts& now) { return was.chat == now.chat; }

}  // namespace mux::ui

