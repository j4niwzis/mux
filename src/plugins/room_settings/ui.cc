// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.room_settings.ui -- A room's settings, Element's and Cinny's:
// the tabs down the left -- the plugins' (contract::tabs: room_admin's for
// its protocol, packs', devtools'), then the client's (General,
// Notifications, Appearance) -- and the tab up beside them, under its head.
// As the program says: the tab, the tab list up in its place on a narrow
// window, and what a plugin keeps while they are open (room_settings_of,
// one type for each kind: a Matrix room's, an XMPP group chat's, a plain chat's).
// Every page is made once as they open and shown while its tab is up: what
// is typed in one stays as tabs are switched. A press asks the program.
//
// Made where the window is (templates on its needs): the plugins' tabs and
// pages are found there, by ADL -- this plugin imports none of them.
module;
#include "skiff/named.h"
export module mux.plugin.room_settings.ui;

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
import mux.composition;
import mux.protocols;
import mux.plugins.types;
import mux.ui.base;
import mux.plugin.room_settings.types;

export namespace mux::plugins::room_settings {

using namespace ::mux::ui;

// ---- its tabs, and what is kept while it is open ------------------------------------
// The client's tabs, every room's.
namespace settings_tab {
struct general {};        // the room's events, receipts, previews, typing, Home, leaving
struct notifications {};
struct looks {};
constexpr std::string_view tab_title(general) { return "General"; }
constexpr std::string_view tab_title(notifications) { return "Notifications"; }
constexpr std::string_view tab_title(looks) { return "Appearance"; }
inline icon_t tab_icon(general) { return icon::gear{}; }
inline icon_t tab_icon(notifications) { return icon::bell{}; }
inline icon_t tab_icon(looks) { return icon::eye{}; }
}  // namespace settings_tab
// Nothing kept: what the settings hold where no plugin keeps anything.
struct nothing_kept {
  friend bool operator==(nothing_kept, nothing_kept) = default;
};
// What a plugin says of a kind of room's settings where it says nothing:
// no tabs, nothing kept -- each plugin put straight on the kinds it has
// tabs for (contract::tabs, kept), no list filtered.
namespace tab_defaults {
constexpr composition::page_list<> room_tabs(contract::tabs, const auto&, const auto&) { return {}; }
constexpr composition::page_list<> kept_parts(contract::kept, const auto&, const auto&) { return {}; }
constexpr bool hosts_general(const auto&) { return false; }
inline bool manage_close_editor(const auto&, auto&) { return false; }
inline bool manage_editor_header(const auto&, const auto&) { return false; }
constexpr void avatar_sending(auto&) {}
}  // namespace tab_defaults
template <class Kind, class Feature>
consteval auto tabs_said(type_tag<Feature>) {
  using tab_defaults::room_tabs;
  return room_tabs(contract::tabs{}, type_tag<Feature>{}, type_tag<Kind>{});
}
template <class Kind, class Feature>
consteval auto kept_said(type_tag<Feature>) {
  using tab_defaults::kept_parts;
  return kept_parts(contract::kept{}, type_tag<Feature>{}, type_tag<Kind>{});
}
// The plugins' tabs for a kind (contract::tabs), in their order; what they
// keep for it (contract::kept).
template <class List>
struct listed_types;
template <class... Ts>
struct listed_types<composition::page_list<Ts...>> {
  using type = ui::type_list<Ts...>;
};
template <class Kind, class>
struct plugin_tabs_of;
template <class Kind, class... Features>
struct plugin_tabs_of<Kind, plugin_list<Features...>> {
  using type = typename ui::joined<ui::type_list<>, typename listed_types<decltype(tabs_said<Kind>(type_tag<Features>{}))>::type...>::type;
};
template <class Kind>
using plugin_tabs_t = typename plugin_tabs_of<Kind, composition::providers<contract::tabs, enabled_plugins>>::type;
template <class Kind, class>
struct plugin_kept_of;
template <class Kind, class... Features>
struct plugin_kept_of<Kind, plugin_list<Features...>> {
  using type = typename ui::joined<ui::type_list<nothing_kept>,
                                   typename listed_types<decltype(kept_said<Kind>(type_tag<Features>{}))>::type...>::type;
};
// A tab of a kind's settings, and what a plugin keeps while they are open.
template <class Kind>
using manage_tab_t = typename ui::variant_of_types<typename ui::joined<
    ui::type_list<settings_tab::general, settings_tab::notifications, settings_tab::looks>, plugin_tabs_t<Kind>>::type>::type;
template <class Kind>
using manage_part_t = typename ui::variant_of_types<
    typename plugin_kept_of<Kind, composition::providers<contract::kept, enabled_plugins>>::type>::type;
// The protocol's part of a room, as a kind's settings hold it: its
// protocol's parts alone (Matrix: its rules and levels; XMPP: a MUC's
// configuration); a plain chat's, none.
template <class List>
struct their_parts_of;
template <class... Parts>
struct their_parts_of<proto::room_part_list<Parts...>> {
  using type = spl::variant<proto::no_room_part, Parts...>;
};
template <class Kind>
struct their_part;
template <class Protocol>
struct their_part<room_kind::room_of<Protocol>> {
  using type = typename their_parts_of<decltype(::mux::room_parts_of(::mux::state_of<Protocol>{}))>::type;
};
template <class Protocol>
struct their_part<room_kind::chat_of<Protocol>> {
  using type = spl::variant<proto::no_room_part>;
};
template <class Kind>
using their_part_t = typename their_part<Kind>::type;

// The kinds, one dialog each: a room and a chat of each protocol turned on.
template <class>
struct kinds_of;
template <class... Protocols>
struct kinds_of<protocol_list<Protocols...>> {
  using type = ui::type_list<room_kind::room_of<Protocols>..., room_kind::chat_of<Protocols>...>;
};
using room_kinds = typename kinds_of<protocols>::type;

// A room's settings open, of one kind -- a Matrix room's and an XMPP group
// chat's two types, each with its own tabs and its protocol's part alone:
// what they show of it, as the program made them as they opened -- the
// room, its choices, what its protocol's part says, what the plugins filled
// and keep.
template <class Kind>
struct room_settings_of {
  using kind = Kind;
  std::string id;  // the room's ID
  std::string name;
  std::string topic;
  std::optional<std::string> avatar;
  std::optional<std::string> alias;
  std::vector<std::string> other_aliases;
  bool encrypted = false;
  // Its protocol's part, for its pages.
  their_part_t<Kind> theirs;
  // Its notifications, as chosen for it (muted: off).
  config::notify_choices notify;
  // Which of its room events it shows, as chosen for it: none chosen is as
  // its account's.
  std::optional<bool> events_all;
  // How far a jump's search pages back in it, as chosen for it.
  std::optional<std::int64_t> jump_search;
  std::optional<config::room_event_kinds> event_kinds;
  // The user's own level, as a plugin fills it as they open (contract::opened).
  std::int64_t mine = 0;
  // A room by its ID and name.
  using named_room = ::mux::ui::named_room;
  // The spaces it is in, as theirs list it: what a rule for their members
  // names.
  std::vector<named_room> parents;
  // A space's: the rooms and spaces it holds, and the account's others --
  // those that may be added to it.
  std::vector<named_room> children;
  std::vector<named_room> addable;
  std::vector<member> members;
  // A space: whether it holds spaces, and whether it is shown as a forum.
  bool space = false;
  bool holds_spaces = false;
  // Those whose level is not the default: Element's privileged users.
  struct person {
    std::string id;
    std::string name;
    std::int64_t level = 0;
  };
  std::vector<person> privileged;
  // The chat itself, its own choices kept by it in the model.
  conversation_id chat{};
  // What the dialog shows, as the program says: the tab up; on a narrow
  // window, whether the tab list is up in place of the page; and what the
  // plugin keeps while it is open.
  manage_tab_t<Kind> tab = settings_tab::general{};
  bool narrow = false, navigation = false;
  manage_part_t<Kind> manage;
};
// A protocol's rooms' settings: room_admin's pages are made for these.
template <class Protocol>
using protocol_settings = room_settings_of<room_kind::room_of<Protocol>>;

// What its dialog asks of the program: a tab picked; ← (an editor open
// closed first, then the page, to the tab list on a narrow window). Closed
// by request::close_shown<room_settings_of<Kind>>.
template <class Kind>
struct manage_tab {
  manage_tab_t<Kind> tab;
};
struct manage_back {};

// ---- what the plugins' tabs say of themselves (contract::tabs) ----------------------
template <class Tab>
[[nodiscard]] constexpr bool tab_hosts_general() {
  using tab_defaults::hosts_general;
  return hosts_general(Tab{});
}
// Whether a tab of a plugin's this kind has holds the client's General
// under its own page (Matrix's: the room's name, topic and address): one
// General, not two -- known from the kind's tabs, while compiling.
template <class... Tabs>
[[nodiscard]] constexpr bool general_hosted(ui::type_list<Tabs...>) {
  return (tab_hosts_general<Tabs>() || ...);
}
template <class Kind>
inline constexpr bool general_on_its_own = !general_hosted(plugin_tabs_t<Kind>{});
// What the client's General is called: General where it is a tab of its
// own; Preferences where another's page holds it.
template <class Kind>
[[nodiscard]] std::string general_title() {
  return general_on_its_own<Kind> ? std::string("General") : std::string("Preferences");
}
// A room avatar being sent: what a plugin keeps shows it pending.
template <class Kind>
void manage_avatar_sending(room_settings_of<Kind>& facts) {
  spl::visit([](auto& part) {
    using tab_defaults::avatar_sending;
    avatar_sending(part);
  }, facts.manage);
}
// What the tab up closes first, by its plugin: an editor open on it.
// Whether it did.
template <class Kind>
bool close_manage_editor(room_settings_of<Kind>& facts) {
  return spl::visit([&](const auto& tab) {
    return spl::visit([&](auto& part) {
      using tab_defaults::manage_close_editor;
      return manage_close_editor(tab, part);
    }, facts.manage);
  }, facts.tab);
}
// Whether the tab's page shows an editor, with a way back over it.
template <class Kind>
[[nodiscard]] bool manage_editor_up(const room_settings_of<Kind>& facts) {
  return spl::visit([&](const auto& tab) {
    return spl::visit([&](const auto& part) {
      using tab_defaults::manage_editor_header;
      return manage_editor_header(tab, part);
    }, facts.manage);
  }, facts.tab);
}

// ---- how it is drawn (contract::presentation) ---------------------------------------
struct room_manage_style {
  palette colours;
  bool page_header = false;
  float navigation_width = 220.0f;
};
template <class Kind, class... Features>
[[nodiscard]] room_manage_style presented(plugin_list<Features...>, const room_settings_of<Kind>& facts, const auto& colours) {
  std::optional<room_manage_style> found;
  ((found = found ? found : room_presented(contract::presentation{}, type_tag<Features>{}, facts, colours)), ...);
  return found.value_or(room_manage_style{colours});
}

// The client's General: the room's events, receipts, previews, typing, Home,
// leaving -- in a tab of its own, or under a plugin's page that hosts it.
// The plugins' sections of it (contract::general_sections), after its own.
template <class Kind, class... Features>
auto general_sections_of(plugin_list<Features...>, const auto& colours, const room_settings_of<Kind>& facts) {
  return skiff::compose::column(skiff::compose::vbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                                extend(contract::general_sections{}, type_tag<Features>{}, colours, facts)...);
}
// What the General has by its kind alone, known while compiling: the note
// over the room events where its protocol has them; Leave where a room of
// its is left. Where not, nothing in its place.
inline auto nothing_here() {
  return skiff::compose::column(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}));
}
template <class Kind>
auto room_events_note(const auto&) {
  return nothing_here();
}
template <class Kind>
  requires(proto::shows_room_events<state_of<typename Kind::protocol>>())
auto room_events_note(const auto& colours) {
  return explained(colours, "Room events shown in this room, for you: As above follows the inherited settings.");
}
template <class Kind>
auto leave_part(const auto&) {
  return nothing_here();
}
template <class Kind>
  requires(Kind::leavable)
auto leave_part(const auto& colours) {
  namespace c = skiff::compose;
  return c::column(c::vbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY}), part_heading(colours, "Leave room"),
                   c::styled({.width = 130.0f, .height = 32.0f},
                             button_for<sends<::mux::ui::request::leave_chat>>(colours.widgets, "Leave room", {})));
}
template <class Kind>
auto room_general_page(const auto& colours, const room_settings_of<Kind>& facts, bool has_page_header = false) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(room_general_page, c::column(c::vbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY,
                                .padding = {0.0f, 28.0f, 24.0f, 12.0f}}),
      c::visible(!has_page_header, tab_heading(colours, general_title<Kind>())),
      room_events_note<Kind>(colours),
      chat_settings_view(colours, facts.chat),
      general_sections_of(composition::providers<contract::general_sections, enabled_plugins>{}, colours, facts),
      leave_part<Kind>(colours)));
}

template <class Kind>
using room_facts_t = std::optional<room_settings_of<Kind>>;

// Whether a tab is the one up.
template <class Kind, class Tab>
struct tab_up {
  bool operator()(const room_facts_t<Kind>& now) const {
    return now && !now->navigation &&
           spl::visit(spl::overloaded{[](const Tab&) { return true; }, [](const auto&) { return false; }}, now->tab);
  }
};
// Whether the client's General is up: its own tab, or a tab hosting it.
template <class Kind, class... Tabs>
[[nodiscard]] bool hosting_up(ui::type_list<Tabs...>, const room_facts_t<Kind>& now) {
  return ((tab_hosts_general<Tabs>() && tab_up<Kind, Tabs>{}(now)) || ...);
}
template <class Kind>
struct general_up {
  bool operator()(const room_facts_t<Kind>& now) const {
    return now && (tab_up<Kind, settings_tab::general>{}(now) || hosting_up<Kind>(plugin_tabs_t<Kind>{}, now));
  }
};
// A tab's row: its icon and title, marked while it is up; pressed, it is.
// Every row made is listed: the kind's tabs are its own, known while
// compiling.
template <class Kind, class Tab>
auto manage_tab_row(const auto& colours, Tab tab) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(manage_tab_row, c::spec_for<room_facts_t<Kind>>([](const room_facts_t<Kind>& now) { return scene::Spec{.selected = tab_up<Kind, Tab>{}(now)}; },
      c::onPress(sends<manage_tab<Kind>>{{manage_tab_t<Kind>{tab}}},
          c::row(c::hbox(8.0f, {.fillX = true, .height = 40.0f, .padding = {0.0f, 12.0f, 0.0f, 12.0f}, .cornerRadius = colours.widgets.fCorner,
                                .hoverBackground = colours.background, .selectedBackground = colours.tile}),
                 c::styled({.width = 16.0f, .height = 16.0f, .alignSelf = scene::align::kMiddle}, icon_mark(colours, tab_icon(tab))),
                 c::applied<room_facts_t<Kind>>([](const room_facts_t<Kind>& now) { return tab_up<Kind, Tab>{}(now); },
                                          [](nodes::Text& title, bool up) { title.setBold(up); },
                                          c::styled({.shrink = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                                                    elided(nodes::Text(std::string(tab_title(tab)), 14.0f, colours.text))))))));
}
// The client's tabs: General only where no tab of the kind's hosts it.
template <class Kind>
auto client_tab_rows(const auto& colours) {
  namespace c = skiff::compose;
  if constexpr (general_on_its_own<Kind>)
    return c::column(c::vbox(2.0f, {.fillX = true, .autoSize = scene::axes::kY}), manage_tab_row<Kind>(colours, settings_tab::general{}),
                     manage_tab_row<Kind>(colours, settings_tab::notifications{}), manage_tab_row<Kind>(colours, settings_tab::looks{}));
  else
    return c::column(c::vbox(2.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                     manage_tab_row<Kind>(colours, settings_tab::notifications{}), manage_tab_row<Kind>(colours, settings_tab::looks{}));
}

// The plugins' rows, those of this kind; and each one's page, shown while
// its tab is up: made by its plugin (make_manage_page, found by ADL with its
// tab where the window makes the dialog), from the facts as the settings
// opened; it reads them as they move.
template <class Kind, class... Tabs>
auto plugin_tab_rows(ui::type_list<Tabs...>, const auto& colours) {
  return SKIFF_NAMED(plugin_tab_rows, skiff::compose::column(skiff::compose::vbox(2.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                                manage_tab_row<Kind>(colours, Tabs{})...));
}
template <class Kind, class Tab>
auto plugin_page(const auto& colours, const room_settings_of<Kind>& facts) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(plugin_page, c::shown_for<room_facts_t<Kind>>(tab_up<Kind, Tab>{},
      c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), make_manage_page(Tab{}, colours, facts))));
}
template <class Kind, class... Tabs>
auto plugin_pages(ui::type_list<Tabs...>, const auto& colours, const room_settings_of<Kind>& facts) {
  return SKIFF_NAMED(plugin_pages, skiff::compose::column(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                                plugin_page<Kind, Tabs>(colours, facts)...));
}

// The client's pages: Notifications and Appearance (General above).
template <class Kind>
auto notifications_page(const palette& colours, const room_settings_of<Kind>& facts) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(notifications_page, c::column(c::vbox(10.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 28.0f, 24.0f, 12.0f}}),
      tab_heading(colours, "Notifications"), chat_notify_view(colours, facts.chat),
      c::styled({.fillX = true}, wrapped(nodes::Text(facts.space ? "For every chat in this space, unless the chat chooses again; Default is "
                                                                    "as the space above it, or the account, says."
                                                                  : "Default is as the space it is in, or the account, says.",
                                                     12.0f, colours.dim)))));
}
// (The looks plugins' sections under it, found where the window makes it.)
template <class Kind>
auto looks_page(const auto& colours, const ui_shared& shared, const room_settings_of<Kind>& facts) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(looks_page, c::column(c::vbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 28.0f, 24.0f, 12.0f}}),
      tab_heading(colours, "Appearance"),
      c::scoped<config::chat_choices>(c::handlers(), style_controls(colours), facts.chat),
      level_section_plugins::made(colours, choice_level::chat{}, shared)));
}
template <class Kind, class Up, class Page>
auto client_page(Up up, Page page) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(client_page, c::shown_for<room_facts_t<Kind>>(up, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(page))));
}

// The tab up's title: Preferences for the client's General where another's
// page holds it, else the tab's. (Asked where the window makes the dialog:
// a plugin's tab_title is found there.)
template <class Needs, class Kind>
[[nodiscard]] std::string manage_title(const room_settings_of<Kind>& facts) {
  return spl::visit(spl::overloaded{[](settings_tab::general) { return general_title<Kind>(); },
                                    [](const auto& one) { return std::string(tab_title(one)); }},
                    facts.tab);
}

// The settings, for a room of a kind: made once as they open
// (content_persistent), again only for another room (content_matches).
template <class Needs, class Kind>
auto room_settings_view(const Needs& n, const room_settings_of<Kind>& facts) {
  namespace c = skiff::compose;
  using facts_t = room_facts_t<Kind>;
  using closed = ::mux::ui::request::close_shown<room_settings_of<Kind>>;
  // Its needs' palette, as they are: what the plugins' tabs, pages and
  // looks are asked of with, found where the window makes this.
  const auto& colours = *n.colours;
  const room_manage_style presentation = presented(composition::providers<contract::presentation, enabled_plugins>{}, facts, colours);
  const auto narrow_page = [](const facts_t& now) { return now && now->narrow && !now->navigation; };
  const auto tabs_up = [](const facts_t& now) { return !now || !now->narrow || now->navigation; };
  const auto page_up = [](const facts_t& now) { return !now || !now->narrow || !now->navigation; };
  const float across = presentation.navigation_width;
  return SKIFF_NAMED(room_settings_view, c::column(c::vbox(0.0f, {.fill = true, .cornerRadius = std::max(10.0f, colours.widgets.fCorner), .background = colours.background,
                                  .masking = true}),
      // Without a head over each page: the dialog's own, its ← on a narrow
      // window's page.
      c::visible(!presentation.page_header,
          c::row(c::hbox(12.0f, {.fillX = true, .height = kPageHeaderHeight, .padding = {0.0f, 10.0f, 0.0f, 10.0f}}),
                 c::shown_for<facts_t>(narrow_page, icon_button(colours, icon::back{}, sends<manage_back>{})),
                 c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                           elided(nodes::Text("Room Settings - " + facts.name, 18.0f, colours.text, true))),
                 icon_button(colours, icon::close{}, sends<closed>{}))),
      c::row(c::hbox(0.0f, {.fillX = true, .grow = scene::axes::kY}),
          // The tabs: across the window where it is narrow.
          c::shown_for<facts_t>(tabs_up,
              c::spec_for<facts_t>([across](const facts_t& now) {
                return now && now->narrow ? scene::Spec{.width = 1.0f, .relativeSize = scene::axes::kBoth}
                                          : scene::Spec{.width = across, .relativeSize = scene::axes::kY};
              },
                  c::column(c::vbox(0.0f, {.fillY = true, .width = across, .padding = {0.0f, 8.0f, 32.0f, 8.0f}, .background = colours.sidebar}),
                      c::row(c::hbox(8.0f, {.fillX = true, .height = 54.0f}), avatar_mark(avatar_of::chat{}, facts.id, facts.name, 24.0f),
                             c::styled({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX, .shrink = scene::axes::kX,
                                        .alignSelf = scene::align::kMiddle},
                                       elided(nodes::Text(facts.name, 18.0f, colours.text, true))),
                             c::shown_for<facts_t>([](const facts_t& now) { return now && now->narrow && now->navigation; },
                                                        icon_button(colours, icon::close{}, sends<closed>{}))),
                      plugin_tab_rows<Kind>(plugin_tabs_t<Kind>{}, colours),
                      client_tab_rows<Kind>(colours)))),
          // The tab up, under its head where the look has one.
          c::shown_for<facts_t>(page_up,
              c::column(c::vbox(0.0f, {.fillY = true, .relativeSize = scene::axes::kY, .grow = scene::axes::kX, .background = colours.background}),
                  c::visible(presentation.page_header,
                      c::row(c::hbox(12.0f, {.fillX = true, .height = kPageHeaderHeight, .padding = {0.0f, 10.0f, 0.0f, 10.0f}}),
                             c::shown_for<facts_t>([](const facts_t& now) { return now && (manage_editor_up(*now) || now->narrow); },
                                                        icon_button(colours, icon::back{}, sends<manage_back>{})),
                             c::text_for<facts_t>([](const facts_t& now) { return now ? manage_title<Needs>(*now) : std::string(); },
                                 c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(nodes::Text("", 20.0f, colours.text, true)))),
                             icon_button(colours, icon::close{}, sends<closed>{}))),
                  // Another tab: shown from its top.
                  c::styled({.fillX = true, .relativeSize = scene::axes::kX, .grow = scene::axes::kY},
                      c::projected<facts_t>([](const facts_t& now) { return now ? now->tab.index() : std::size_t{0}; },
                          c::restarted_scroll<std::size_t>(nodes::ScrollContainer(
                              c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                                        client_page<Kind>(tab_up<Kind, settings_tab::notifications>{}, notifications_page(colours, facts)),
                                        client_page<Kind>(tab_up<Kind, settings_tab::looks>{}, looks_page(colours, *n.shared, facts)),
                                        plugin_pages(plugin_tabs_t<Kind>{}, colours, facts),
                                        // Under a plugin's page, where it hosts the General.
                                        client_page<Kind>(general_up<Kind>{}, room_general_page(colours, facts, presentation.page_header && general_on_its_own<Kind>))))))))))));
}
template <class Kind>
dialog_look content_look(std::type_identity<room_settings_of<Kind>>) { return {.size = dialog_size::fixed{802.0f, 684.0f}}; }
template <class Needs, class Kind>
auto make_content(const Needs& needs, const room_settings_of<Kind>& facts) {
  return room_settings_view(needs, facts);
}
// The same room's settings stand while they are up.
template <class Kind>
bool content_matches(const room_settings_of<Kind>& was, const room_settings_of<Kind>& now) { return was.chat == now.chat; }

// Its dialogs, among the window's (contract::window_dialogs): one for each
// kind, its box made where the window asks for it, with the window's needs.
template <class... Kinds>
constexpr composition::page_list<room_settings_of<Kinds>...> settings_facts_of(ui::type_list<Kinds...>) { return {}; }
constexpr auto facts_types(::mux::contract::window_dialogs, type_tag<feature>) { return settings_facts_of(room_kinds{}); }
template <class Facts, class Needs = ui_needs>
constexpr auto extend(::mux::contract::window_dialogs, type_tag<feature>, type_tag<Facts>) {
  return type_tag<decltype(room_settings_view(std::declval<const Needs&>(), std::declval<const Facts&>()))>{};
}

// Each kind's settings, closed where any is open: what opens another dialog
// over them closes them first (devtools').
template <class Showing, class... Kinds>
void close_room_settings(Showing& showing, ui::type_list<Kinds...>) {
  // show() is the window's (mux.ui), found by ADL on the model where this
  // is instantiated: this module does not import the window.
  (show(showing, std::optional<room_settings_of<Kinds>>{}), ...);
}
template <class Showing>
void close_room_settings(Showing& showing) {
  close_room_settings(showing, room_kinds{});
}

}  // namespace mux::plugins::room_settings
