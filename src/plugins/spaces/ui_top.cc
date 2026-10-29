// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.spaces.ui_top -- The space bars, as Element's and Telegram's:
// the side one, a lane of icons down the side of the chat list, and the top
// one, in the side's head -- or under its search, made big for a finger --
// Home, Direct messages and the account's spaces, each icon pressed its
// folder, pressed with the other button its menu (Explore its rooms,
// settings, a room or a space made in it, Leave, which bars it is in); an
// icon dragged from one bar to the other, or along one. In the side column
// through contract::side_bars.
module;
#include "skiff/named.h"
export module mux.plugin.spaces.ui_top;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.bind;
import skiff.model;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.button;
import mux.composition;
import mux.core;
import mux.config;
import mux.protocols;
import mux.ui.base;
import mux.ui.messages;
import mux.ui.lists;
import mux.plugins.ui;
import mux.plugin.spaces.types;

// Bars are keyed by their items, ordered for memo reconciliation.
export namespace mux::config::space_item {
inline bool operator<(const space_item_t& a, const space_item_t& b) {
  if (a.index() != b.index()) return a.index() < b.index();
  return spl::visit(spl::overloaded{
      [&](const space& left) { return left.room < spl::get<space>(b).room; },
      [](const auto&) { return false; }}, a);
}
}

export namespace mux::plugins::spaces {
using namespace ::mux::ui;

// What a bar's icon is, as its row is made of it.
struct side_space_view {
  folder_t folder;
  std::string account, key, name;
  bool chosen = false;
  float size = 40.0f;
  friend bool operator==(const side_space_view&, const side_space_view&) = default;
};
// An icon's menu open: whose, for which item, named, and where.
struct side_space_menu_facts {
  std::string account;
  config::space_item_t item;
  std::string name;
  skia::SkPoint at{};
  friend bool operator==(const side_space_menu_facts&, const side_space_menu_facts&) = default;
};
// The menu's state, the bars' own: the one open, or none.
struct space_menu_state {
  std::optional<side_space_menu_facts> open;
};
struct close_space_menu {};
struct space_menu_events {
  auto on(const side_space_menu_facts& facts, const space_menu_state&) const {
    return skiff::model::over<space_menu_state>(skiff::model::setTo(space_menu_state{.open = facts}));
  }
  auto on(close_space_menu, const space_menu_state&) const {
    return skiff::model::over<space_menu_state>(skiff::model::setTo(space_menu_state{}));
  }
};
// A row of the menu pressed: the menu closed, and what it asks.
template <class Act> struct space_menu_pick {
  using Answer = std::tuple<close_space_menu, typename Act::Answer>;
  Act act;
  Answer operator()() { return {close_space_menu{}, act()}; }
};
auto side_popup_look(const auto& colours, float width) {
  return skiff::compose::vbox(4.0f,
      {.width = width, .autoSize = scene::axes::kY, .padding = {8.0f, 8.0f, 8.0f, 8.0f},
       .cornerRadius = 10.0f, .background = colours.popup(),
       .border = scene::Border{colours.band, 1.0f},
       .shadow = scene::Shadow{skia::colorSetARGB(70, 0, 0, 0), 3.0f}});
}
template <class Act>
auto side_menu_button(const auto& colours, std::string label, Act act) {
  return SKIFF_NAMED(space_menu_button, skiff::compose::styled({.fillX = true, .height = 30.0f},
      button_for<space_menu_pick<Act>>(colours.widgets, std::move(label), space_menu_pick<Act>{std::move(act)})));
}
// The rows plugins put in a space's menu (contract::space_menu), in their
// order -- Explore's, for one.
template <class List>
struct space_menu_rows;
template <class... Features>
struct space_menu_rows<plugin_list<Features...>> {
  static auto made(const auto& colours, const std::string& room) {
    return skiff::compose::column(skiff::compose::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
        skiff::compose::visible(!room.empty(), side_menu_button(colours, std::string(label(contract::space_menu{}, type_tag<Features>{})),
                                                                act(contract::space_menu{}, type_tag<Features>{}, room)))...);
  }
};
using space_menu_plugins = space_menu_rows<composition::providers<contract::space_menu, enabled_plugins>>;

template <class Pick>
auto side_space_icon(const auto& colours, const config::space_item_t& item,
                     folder_t which, std::string key, std::string name,
                     bool chosen, float size, Pick pick) {
  namespace c = skiff::compose;
  const auto [home, direct, space] = spl::visit(spl::overloaded{
      [](config::space_item::home) { return std::tuple{true, false, false}; },
      [](config::space_item::direct) { return std::tuple{false, true, false}; },
      [](const config::space_item::space&) { return std::tuple{false, false, true}; }}, item);
  return c::with_cursor(scene::cursor::arrow{}, c::said(scene::SemanticRole{scene::semantic_role::tab{}}, chosen,
      c::onPress(pick_side_folder<Pick>{std::move(pick), std::move(which)},
          c::column(c::justified(c::hbox(0.0f,
              {.width = size, .height = size, .cornerRadius = size * 0.5f,
               .background = colours.tile, .hoverBackground = colours.chosen,
               .border = scene::Border{chosen ? colours.accent : skia::SkColor{0}, chosen ? 2.0f : 0.0f}}),
              nodes::justify::middle{}),
              c::visible(home, c::styled({.alignSelf = scene::align::kMiddle},
                  nodes::Text("\u2302", size * 0.5f, colours.text))),
              c::visible(direct, c::styled({.alignSelf = scene::align::kMiddle},
                  nodes::Text("@", size * 0.45f, colours.text, true))),
              c::visible(space, c::styled({.alignSelf = scene::align::kMiddle},
                  avatar_mark(avatar_of::chat{}, std::move(key), name, size - 6.0f)))),
          std::move(name))));
}


template <class Pick>
struct pick_side_folder {
  using Answer = std::invoke_result_t<Pick&, const folder_t&>;
  Pick pick;
  folder_t which;
  Answer operator()() { return std::invoke(pick, which); }
};


struct set_bars_act {
  using Answer = ::mux::ui::request::set_space_bars;
  std::string account;
  config::space_item_t item;
  bool side = true, top = false;
  ::mux::ui::request::set_space_bars operator()() { return ::mux::ui::request::set_space_bars{account, item, side, top}; }
};
struct manage_act {
  using Answer = ::mux::ui::request::manage_space;
  std::string room;
  ::mux::ui::request::manage_space operator()() { return ::mux::ui::request::manage_space{room}; }
};
struct leave_act {
  using Answer = ::mux::ui::request::open_leave_space;
  std::string account;
  std::string room;
  ::mux::ui::request::open_leave_space operator()() { return ::mux::ui::request::open_leave_space{conversation_id{account_id{protocol_of(account), account}, room}}; }
};
// A room, or a space, made in it -- Element's Add room and Add space.
struct create_in_act {
  using Answer = std::optional<::mux::ui::request::open_new_room_in>;
  std::string account;
  std::string room;
  std::string name;
  bool make_space = false;
  std::optional<::mux::ui::request::open_new_room_in> operator()() {
    return ::mux::ui::request::open_new_room_in{conversation_id{account_id{protocol_of(account), account}, room}, name, make_space};
  }
};

[[nodiscard]] inline std::string side_space_room(const config::space_item_t& item) {
  return spl::visit(spl::overloaded{
      [](const config::space_item::space& space) { return space.room; },
      [](const auto&) { return std::string(); }}, item);
}

auto side_space_menu(const auto& colours, const std::string& account,
                            const config::space_item_t& item, std::string name) {
  namespace c = skiff::compose;
  const auto room = side_space_room(item);
  return SKIFF_NAMED(side_space_menu, c::column(side_popup_look(colours, 190.0f),
      c::styled({.fillX = true}, elided(nodes::Text(name, 13.0f, colours.dim, true))),
      space_menu_plugins::made(colours, room),
      c::visible(!room.empty(), side_menu_button(colours, "Space settings\u2026", manage_act{room})),
      c::visible(!room.empty(), side_menu_button(colours, "Create a room in it\u2026", create_in_act{account, room, name, false})),
      c::visible(!room.empty(), side_menu_button(colours, "Create a space in it\u2026", create_in_act{account, room, name, true})),
      c::visible(!room.empty(), side_menu_button(colours, "Leave space\u2026", leave_act{account, room})),
      side_menu_button(colours, "Side bar only", set_bars_act{account, item, true, false}),
      side_menu_button(colours, "Top bar only", set_bars_act{account, item, false, true}),
      side_menu_button(colours, "Both bars", set_bars_act{account, item, true, true}),
      side_menu_button(colours, "Hide", set_bars_act{account, item, false, false})));
}


struct side_space_menu_at {
  std::string account;
  config::space_item_t item;
  std::string name;
  // Pressed where it is in its list, scrolled: in the window, its menu's
  // place -- as far off as the list was scrolled, where taken as it is.
  std::optional<side_space_menu_facts> operator()(auto& row, float x, float y) const {
    const auto shift = skiff::scene::work::drawnOffset(skiff::compose::state_of(row).fId);
    return side_space_menu_facts{account, item, name, {x + shift.x, y + shift.y}};
  }
};

template <class Pick> struct side_space_row_make {
  const palette* colours;
  Pick pick;
  auto operator()(const config::space_item_t& key, const side_space_view& view, bool ghost) const {
    namespace c = skiff::compose;
    return c::onRightPressAt(side_space_menu_at{view.account, key, view.name},
        c::styled({.alignSelf = scene::align::kMiddle},
            side_space_icon(*colours, key, view.folder, view.key, view.name,
                            !ghost && view.chosen, view.size, pick)));
  }
  auto operator()(const std::pair<config::space_item_t, side_space_view>& entry) const {
    return (*this)(entry.first, entry.second, false);
  }
};

// The lanes of the drag between the bars, by their marks: the folder bar's
// rows and the view they scroll in (in the head, or under the search where
// the bars are big -- the one shown), and the side bar's.
struct space_bar_rows {};
struct space_bar_view {};
struct space_list_rows {};
struct space_list_view {};
template <class Pick>
using side_space_rows_raw =
    nodes::MemoRows<config::space_item_t, side_space_view,
                    std::invoke_result_t<side_space_row_make<Pick>, const config::space_item_t&, const side_space_view&, bool>,
                    side_space_row_make<Pick>, skiff::compose::Box<>>;
template <class Lane, class Pick>
auto side_spaces_for(const auto& colours, Pick pick, bool horizontal) {
  namespace c = skiff::compose;
  using make = side_space_row_make<Pick>;
  using rows = side_space_rows_raw<Pick>;
  auto line = rows(make{&colours, std::move(pick)}, c::Box<>(horizontal
      ? c::hbox(4.0f, {.fillY = true, .autoSize = scene::axes::kX})
      : c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 6.0f, 8.0f, 6.0f}})),
      {.autoSize = scene::axes::kBoth, .alignSelf = scene::align::kMiddle});
  return c::projected<side_shown>([horizontal](const side_shown& now) {
    return ((horizontal ? now.top_bar : now.side_bar)) | std::views::transform([&](const auto& entry) {
      return std::pair{entry.item, side_space_view{entry.folder, now.account, entry.key, entry.name, entry.chosen, entry.size}};
    }) | std::ranges::to<std::vector>();
  }, c::LaneMark<Lane, rows>(std::move(line)));
}

auto side_space_popup(const auto& colours) {
  namespace c = skiff::compose;
  using facts = side_space_menu_facts;
  using field = skiff::model::Field<&space_menu_state::open>;
  using menu = decltype(side_space_menu(colours, std::string{}, config::space_item_t{}, std::string{}));
  using content = c::Box<menu>;
  auto box = c::projected<field>([](const std::optional<facts>& now) { return now; },
      c::mount<content, facts>([&colours](const facts& now) {
        return std::tuple{c::vbox(0.0f, {.autoSize = scene::axes::kBoth}),
            side_space_menu(colours, now.account, now.item, now.name)};
      }, {.autoSize = scene::axes::kBoth}, true));
  return SKIFF_NAMED(side_space_popup, c::placed_at<field>([](const std::optional<facts>& now) {
    return now ? std::optional(now->at) : std::nullopt;
  }, std::move(box)));
}

struct side_pick_folder {
  using Answer = request::pick_folder;
  Answer operator()(const folder_t& folder) const { return {folder}; }
};
// The rows of a lane -- the bar's or the list's, each marked as its own.
template <class P, class Lane = space_bar_rows>
using side_space_rows_t = decltype(side_spaces_for<Lane>(std::declval<P>(), side_pick_folder{}, true));
// The views the lanes scroll in: the list's around its own rows -- made
// around the bar's, its type was not the one the list is made of.
template <class P> using space_bar_view_t = decltype(skiff::compose::SideScroll(std::declval<side_space_rows_t<P>>(), true));
template <class P> using space_list_view_t = decltype(nodes::ScrollContainer(std::declval<side_space_rows_t<P, space_list_rows>>()));
// The folder bar, where it is shown: in the head, or under the search field
// where the bars are big.
auto side_space_bar(const auto& colours, bool big) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(side_space_bar, c::shown_for<side_shown>([big](const side_shown& now) { return (now.top_on || now.dragging) && now.big == big; },
      c::styled(big ? scene::Spec{.fillX = true, .height = 58.0f, .margin = {0.0f, 8.0f, 8.0f, 8.0f}, .cornerRadius = 8.0f, .masking = true}
                    : scene::Spec{.height = 34.0f, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle, .cornerRadius = 8.0f,
                                  .masking = true},
          c::LaneMark<space_bar_view, space_bar_view_t<decltype(colours)>>(c::SideScroll(side_spaces_for<space_bar_rows>(colours, side_pick_folder{}, true), true)))));
}

// The lanes of the drag between the bars, as the reorder finds them shown.
template <class P> struct side_lanes {
  template <class Found> auto operator()(const Found& found) const {
    namespace c = skiff::compose;
    using bar_view = c::LaneMark<space_bar_view, space_bar_view_t<P>>;
    using bar_rows = c::LaneMark<space_bar_rows, side_space_rows_raw<side_pick_folder>>;
    using list_view = c::LaneMark<space_list_view, space_list_view_t<P>>;
    using list_rows = c::LaneMark<space_list_rows, side_space_rows_raw<side_pick_folder>>;
    using bar_lane = decltype(c::ReorderLane{std::declval<bar_view&>(), std::declval<bar_rows&>(), skia::SkRect{}, 4.0f});
    using list_lane = decltype(c::ReorderLane{std::declval<list_view&>(), std::declval<list_rows&>(), skia::SkRect{}, 8.0f});
    bar_view* bar = found.template of<bar_view>();
    bar_rows* across = found.template of<bar_rows>();
    list_view* side = found.template of<list_view>();
    list_rows* down = found.template of<list_rows>();
    // The bar's drop reaches over the head around it.
    return std::tuple{bar && across ? std::optional(bar_lane{*bar, *across, bar->bounds().makeOutset(0.0f, 9.0f), 4.0f}) : std::optional<bar_lane>(),
                      side && down ? std::optional(list_lane{*side, *down, side->bounds(), 8.0f}) : std::optional<list_lane>()};
  }
};

// ---- in the side column (contract::side_bars) ------------------------------------------
// The side bar: its lane of icons, down the side of the list, while it is up
// or an icon is being dragged.
inline auto side_lane(contract::side_bars, type_tag<feature>, const palette& colours) {
  namespace c = skiff::compose;
  return c::shown_for<side_shown>([](const side_shown& now) { return now.side_on || now.dragging; },
      c::styled({.fillY = true, .width = 56.0f},
          c::LaneMark<space_list_view, space_list_view_t<decltype(colours)>>(
              nodes::ScrollContainer(side_spaces_for<space_list_rows>(colours, side_pick_folder{}, false)))));
}
// The top bar: in the head, or under the search field where the bars are big.
inline auto top_bar(contract::side_bars, type_tag<feature>, const palette& colours, bool big) { return side_space_bar(colours, big); }
// The column, wrapped: an icon's menu over it, and an icon dragged between
// the bars and along them.
template <class Surface>
auto wrap_side(contract::side_bars, type_tag<feature>, const palette& colours, Surface surface) {
  namespace c = skiff::compose;
  auto popup = c::tapped_off(sends<close_space_menu>{},
      c::Box<decltype(side_space_popup(colours))>(c::vbox(0.0f, {.place = scene::anchor::kTopLeft, .fill = true}), side_space_popup(colours)));
  auto over = c::column(c::vbox(0.0f, {.fill = true}), std::move(surface), std::move(popup));
  auto reorder = c::reorder_bars<config::space_item_t, side_space_view>(std::move(over),
      side_space_row_make<side_pick_folder>{&colours, {}}, side_lanes<decltype(colours)>{}, colours.chosen);
  return c::local<space_menu_state>(space_menu_events{}, std::move(reorder));
}

}  // namespace mux::plugins::spaces
