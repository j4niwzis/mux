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
import skiff.widgets.model;
import mux.composition;
import mux.core;
import mux.config;
import mux.protocols;
import mux.ui.base;
import mux.ui.messages;
import mux.ui.lists;
import mux.plugins.ui;
import mux.plugin.spaces.types;
import mux.plugin.room_settings.types;
import mux.plugin.room_settings.ui;

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

// A space's icon pressed: the folder it is, picked.
template <class Pick>
struct pick_side_folder {
  using Answer = std::invoke_result_t<Pick&, const folder_t&>;
  Pick pick;
  folder_t which;
  Answer operator()() { return std::invoke(pick, which); }
};

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
                  face_spot(avatar_of::chat{}, std::move(key), name, size - 6.0f)))),
          std::move(name))));
}




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
// The icons, kept by their items: a builder (their faces are pieces), built
// under the rules the bars are.
using side_space_item = std::pair<config::space_item_t, side_space_view>;
template <class Pick>
auto side_space_rows(const auto& colours, Pick pick, bool horizontal) {
  namespace c = skiff::compose;
  return c::memo_rows<config::space_item_t, side_space_view, side_space_item>(side_space_row_make<Pick>{&colours, std::move(pick)}, c::Box<>(horizontal
      ? c::hbox(4.0f, {.fillY = true, .autoSize = scene::axes::kX})
      : c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 6.0f, 8.0f, 6.0f}})),
      {.autoSize = scene::axes::kBoth, .alignSelf = scene::align::kMiddle});
}
template <class Lane, class Pick>
auto side_spaces_for(const auto& colours, Pick pick, bool horizontal) {
  namespace c = skiff::compose;
  auto line = side_space_rows(colours, std::move(pick), horizontal);
  return c::projected<side_shown>([horizontal](const side_shown& now) {
    return ((horizontal ? now.top_bar : now.side_bar)) | std::views::transform([&](const auto& entry) {
      return std::pair{entry.item, side_space_view{entry.folder, now.account, entry.key, entry.name, entry.chosen, entry.size}};
    }) | std::ranges::to<std::vector>();
  // Marked concrete in every build (reorder_lane's is held outside a release
  // one): what reads the list's items into it reads them into its rows.
  }, c::marked_as<Lane>(std::move(line)));
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
// The views the lanes scroll in, as builders: the bar's sideways, the
// list's down.
// The bar's view a SideScroll in every build -- not side_scroll's, held
// outside a release build: the drag asks it how far it is scrolled.
template <class B> auto side_scrolled(B line) {
  return skiff::compose::lifted([](auto built) { return skiff::compose::SideScroll(std::move(built), true); }, std::move(line));
}
template <class P> auto bar_lane_view(const P& colours) {
  return skiff::compose::marked_as<space_bar_view>(side_scrolled(side_spaces_for<space_bar_rows>(colours, side_pick_folder{}, true)));
}
template <class P> auto list_lane_view(const P& colours) {
  return skiff::compose::marked_as<space_list_view>(skiff::compose::scrolled(side_spaces_for<space_list_rows>(colours, side_pick_folder{}, false)));
}
// A lane as it is built under the rules: its mark over what it marks, built.
template <class Tag, class B, class Rules>
using built_lane = skiff::compose::LaneMark<Tag, decltype(skiff::compose::built(std::declval<B>(), std::declval<const Rules&>()))>;
// The folder bar, where it is shown: in the head, or under the search field
// where the bars are big.
auto side_space_bar(const auto& colours, bool big) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(side_space_bar, c::shown_for<side_shown>([big](const side_shown& now) { return (now.top_on || now.dragging) && now.big == big; },
      c::styled(big ? scene::Spec{.fillX = true, .height = 58.0f, .margin = {0.0f, 8.0f, 8.0f, 8.0f}, .cornerRadius = 8.0f, .masking = true}
                    : scene::Spec{.height = 34.0f, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle, .cornerRadius = 8.0f,
                                  .masking = true},
          bar_lane_view(colours))));
}

// The lanes of the drag between the bars, as the reorder finds them shown
// -- by their types as they were built under the rules (Rules).
template <class P> struct side_lanes {
  template <class Rules, class Found> auto operator()(const Found& found) const {
    namespace c = skiff::compose;
    using rows_b = decltype(side_space_rows(std::declval<const P&>(), side_pick_folder{}, true));
    using bar_view = built_lane<space_bar_view, decltype(side_scrolled(side_spaces_for<space_bar_rows>(std::declval<const P&>(), side_pick_folder{}, true))), Rules>;
    using bar_rows = built_lane<space_bar_rows, rows_b, Rules>;
    using list_view = built_lane<space_list_view, decltype(skiff::compose::scrolled(std::declval<decltype(side_spaces_for<space_list_rows>(std::declval<const P&>(), side_pick_folder{}, false))>())), Rules>;
    using list_rows = built_lane<space_list_rows, rows_b, Rules>;
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
          list_lane_view(colours)));
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


// ---- leaving a space ------------------------------------------------------------
// Asked of the program by its box: the space left, and the rooms of it
// chosen with it. Closed by request::close_shown<leave_space_facts>.
struct leave_space {
  conversation_id space;
  std::vector<std::string> rooms;
};
// Leaving a space, as Element's LeaveSpaceDialog: the rooms of it one is in
// left with it -- none of them, all, or those chosen, each by a switch.
struct leave_space_facts {
  conversation_id space;
  std::string name;
  std::vector<named_room> rooms;  // its rooms one is in
};
namespace leave_choice {
struct none { friend bool operator==(none, none) = default; };
struct all { friend bool operator==(all, all) = default; };
struct some { friend bool operator==(some, some) = default; };
}
using leave_choice_t = spl::variant<leave_choice::none, leave_choice::all, leave_choice::some>;
struct leave_draft {
  leave_choice_t choice = leave_choice::none{};
  std::set<std::string> chosen;
};
struct flip_leave_room { std::string room; };
// A room's switch pressed: it flipped among those to leave.
struct flip_leave_toggle {
  using Answer = flip_leave_room;
  std::string room;
  flip_leave_room operator()() const { return {room}; }
};
struct submit_leave_space {};
struct leave_events {
  leave_space_facts facts;
  auto on(const flip_leave_room& event, const leave_draft& draft) const {
    auto chosen = draft.chosen;
    if (!chosen.erase(event.room))
      chosen.insert(event.room);
    return skiff::model::over<skiff::model::Field<&leave_draft::chosen>>(skiff::model::setTo(std::move(chosen)));
  }
  auto on(submit_leave_space, const leave_draft& draft) const {
    auto leaving = spl::visit(spl::overloaded{
        [](leave_choice::none) { return std::vector<std::string>{}; },
        [&](leave_choice::all) {
          return facts.rooms | std::views::transform([](const auto& room) { return room.id; }) | std::ranges::to<std::vector>();
        },
        [&](leave_choice::some) { return std::vector<std::string>(draft.chosen.begin(), draft.chosen.end()); }}, draft.choice);
    return skiff::model::Up{leave_space{facts.space, std::move(leaving)}};
  }
};
inline auto leave_room_row(const palette& colours, const named_room& room) {
  return SKIFF_NAMED(leave_room_row, skiff::compose::row(skiff::compose::hbox(10.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {6.0f, 0.0f, 6.0f, 0.0f}}),
      skiff::compose::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, wrapped(nodes::Text(room.name, 14.0f, colours.text))),
      // On while the room is among those to leave; pressed, flipped there.
      skiff::compose::named(room.name, {}, skiff::compose::applied<skiff::model::Field<&leave_draft::chosen>>(
          [id = room.id](const auto& chosen) { return chosen.contains(id); },
          [](widgets::Toggle<flip_leave_toggle>& toggle, bool on) { toggle.setOn(on); },
          skiff::compose::styled({.alignSelf = scene::align::kMiddle}, widgets::Toggle<flip_leave_toggle>(colours.widgets, {room.id}))))));
}
inline auto leave_space_box(const palette& colours, const leave_space_facts& facts) {
  using choice = skiff::model::Field<&leave_draft::choice>;
  return SKIFF_NAMED(leave_space_box, skiff::compose::local<leave_draft>(leave_events{facts}, skiff::compose::column(
      skiff::compose::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {20.0f, 22.0f, 20.0f, 22.0f}}),
      nodes::Text("Leave " + facts.name, 17.0f, colours.text, true),
      explained(colours, facts.rooms.empty() ? "You are in none of its rooms." : "Would you like to leave the rooms in this space too?"),
      skiff::compose::visible(!facts.rooms.empty(), skiff::compose::bound<choice>(
          widgets::ChoiceRowField<leave_choice_t>(colours.widgets, "Don't leave any rooms", leave_choice::none{}))),
      skiff::compose::visible(!facts.rooms.empty(), skiff::compose::bound<choice>(
          widgets::ChoiceRowField<leave_choice_t>(colours.widgets, "Leave all rooms", leave_choice::all{}))),
      skiff::compose::visible(!facts.rooms.empty(), skiff::compose::bound<choice>(
          widgets::ChoiceRowField<leave_choice_t>(colours.widgets, "Leave some rooms", leave_choice::some{}))),
      skiff::compose::shown_for<choice>([](const auto& picked) { return picked == leave_choice_t(leave_choice::some{}); },
          skiff::compose::many(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
              facts.rooms | std::views::transform([&](const auto& room) { return leave_room_row(colours, room); }) | std::ranges::to<std::vector>())),
      skiff::compose::row(skiff::compose::justified(
          skiff::compose::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .margin = {8.0f, 0.0f, 0.0f, 0.0f}}), nodes::justify::end{}),
          skiff::compose::styled({.width = 130.0f, .height = 36.0f}, widgets::SendButton<::mux::ui::request::close_shown<leave_space_facts>>(colours.widgets, "Cancel", {})),
          skiff::compose::styled({.width = 130.0f, .height = 36.0f}, primary(widgets::SendButton<submit_leave_space>(colours.widgets, "Leave space", {})))))));
}
inline dialog_look content_look(std::type_identity<leave_space_facts>) { return {.size = dialog_size::fitting{440.0f}}; }
template <class Needs>
auto make_content(const Needs& needs, const leave_space_facts& facts) {
  return leave_space_box(*needs.colours, facts);
}
using leave_space_box_t = decltype(leave_space_box(std::declval<const palette&>(), std::declval<const leave_space_facts&>()));
// Its box, among the window's dialogs (contract::window_dialogs).
constexpr type_tag<leave_space_facts> facts_type(contract::window_dialogs, type_tag<feature>) { return {}; }
constexpr type_tag<leave_space_box_t> extend(contract::window_dialogs, type_tag<feature>) { return {}; }


// ---- its settings, on the pages they are chosen on ---------------------------------
// The items of the space bars -- Home, Direct messages, each space -- each
// with where it is: the side bar, the top one, both, or hidden. A choice
// asks for the item's bars to be set so.
struct pick_space_bars {
  using Answer = std::optional<request::set_space_bars>;
  std::string account;
  config::space_item_t item;
  Answer operator()(std::size_t index) const {
    static constexpr std::array<std::pair<bool, bool>, 4> kWays{{{true, false}, {false, true}, {true, true}, {false, false}}};
    if (index >= kWays.size())
      return std::nullopt;
    return request::set_space_bars{account, item, kWays[index].first, kWays[index].second};
  }
};
// Where an item is now, as the dropdown lists the ways.
[[nodiscard]] inline std::size_t space_bars_way(const space_item_shown& one) {
  return one.side && !one.top ? 0 : one.top && !one.side ? 1 : one.side && one.top ? 2 : 3;
}
inline auto space_bars_row(const palette& colours, const std::string& account, const space_item_shown& one) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(space_bars_row, c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY, .margin = {4.0f, 20.0f, 4.0f, 20.0f}}),
                   nodes::Text(one.name, 14.0f, colours.text),
                   make_choice_menu(colours, "", {"Side bar", "Top bar", "Both bars", "Hidden"}, space_bars_way(one),
                                    pick_space_bars{account, one.item})));
}
inline auto spaces_choices(const palette& colours, const ui_shared& shared) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(spaces_choices, c::many(c::vbox(2.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                 shared.space_items | std::views::transform([&](const space_item_shown& one) { return space_bars_row(colours, shared.space_account, one); }) | std::ranges::to<std::vector>()));
}
// Home without what an account's spaces hold -- but direct messages -- or
// as every account's.
struct pick_home {
  using Answer = std::tuple<request::set_home_hides, request::set_home_direct>;
  Answer operator()(std::size_t index) const {
    const auto said = [&](bool on) { return index == 0 ? std::nullopt : std::optional<bool>(on); };
    return {request::set_home_hides{choice_level::account{}, said(index >= 2)}, request::set_home_direct{choice_level::account{}, said(index == 3)}};
  }
};
inline auto home_choice(const palette& colours, std::size_t way) {
  return make_choice_menu(colours, "Home", {"As above", "Every chat", "Without chats spaces hold", "Without those and direct messages"},
                          way, pick_home{});
}
using home_choice_t = decltype(home_choice(std::declval<const palette&>(), 0));
// An account's Home menu, by its address: its own way, else as every
// account's.
inline auto home_choice_view(const palette& colours, std::string address) {
  namespace c = skiff::compose;
  return c::projected<shown>([address = std::move(address)](const shown& now) {
    const auto way = now.home_ways.find(address);
    return std::optional<std::size_t>(way == now.home_ways.end() ? 0 : way->second);
  }, c::mount<home_choice_t, std::size_t>([at = &colours](std::size_t way) { return std::tuple{home_choice(*at, way)}; },
                                          {.fillX = true, .autoSize = scene::axes::kY}));
}
using home_choice_view_t = decltype(home_choice_view(std::declval<const palette&>(), std::string()));
// An account's Home, as its page shows the account.
inline auto account_home_view(const palette& colours) {
  namespace c = skiff::compose;
  using page_read = std::optional<account_page_facts>;
  return c::projected<page_read>([](const page_read& now) { return now ? std::optional(now->by.address) : std::nullopt; },
      c::mount<home_choice_view_t, std::string>([at = &colours](const std::string& address) { return std::tuple{home_choice_view(*at, address)}; },
                                                {.fillX = true, .autoSize = scene::axes::kY}));
}
// Every account's: the bars at all, the one along the top, and Home
// without what spaces hold -- and without direct messages, only then.
inline auto everywhere_view(const palette& colours) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(spaces_everywhere, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      spaced_title(colours, "SPACES"),
      setting_switch<&kept::bars>(colours, "Space bars"),
      setting_switch<&kept::top_bar>(colours, "The bar after \"mux\""),
      setting_switch<&kept::home_hides_spaced>(colours, "Home without chats spaces hold (not direct messages)"),
      c::spec_for<kept>([](const kept& now) -> scene::Spec {
        return {.alpha = now.home_hides_spaced ? 1.0f : 0.4f, .disabled = !now.home_hides_spaced};
      }, setting_switch<&kept::home_hides_direct>(colours, "And without direct messages"))));
}
// Its section on the pages a level's looks are chosen on
// (contract::level_sections): every account's, on Appearance; an account's
// own Home, on its page; and the bars' items, on both.
inline auto extend(contract::level_sections, type_tag<feature>, const palette& colours, choice_level_t level, const ui_shared& shared) {
  namespace c = skiff::compose;
  const auto is = [&](auto which) {
    return spl::visit(spl::overloaded{[](decltype(which)) { return true; }, [](const auto&) { return false; }}, level);
  };
  return SKIFF_NAMED(spaces_section, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::visible(is(choice_level::everywhere{}), everywhere_view(colours)),
      c::visible(is(choice_level::account{}), c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                                                        spaced_title(colours, "SPACES"), account_home_view(colours))),
      c::visible(!is(choice_level::chat{}), spaces_choices(colours, shared))));
}
// A space's own, in its settings: its rooms out of Home, or not
// (room_settings' general_sections).
template <class Colours>
auto extend(room_settings::contract::general_sections, type_tag<feature>, const Colours& colours,
            const auto& facts) {
  namespace c = skiff::compose;
  using toggle_t = widgets::Toggle<sends<request::flip_home_hide>>;
  return SKIFF_NAMED(home_section, c::visible(facts.space,
      c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {6.0f, 0.0f, 6.0f, 0.0f}}),
          c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, wrapped(nodes::Text("Its rooms not in Home", 14.0f, colours.text))),
          c::applied<shown>([chat = facts.chat](const shown& now) { return now.hidden.contains(chat); },
                            [](toggle_t& toggle, bool on) { toggle.setOn(on); },
                            c::styled({.alignSelf = scene::align::kMiddle}, toggle_t(colours.widgets, {request::flip_home_hide{facts.id}}))))));
}

}  // namespace mux::plugins::spaces
