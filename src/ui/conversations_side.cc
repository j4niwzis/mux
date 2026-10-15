// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:conversations_side -- The chat list's side: its folders and spaces, the space bars, and the column of chats.
export module mux.ui:conversations_side;

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
import skiff.widgets.pill;
import skiff.widgets.textarea;
import mux.core;
import mux.protocols;
import mux.config;
import mux.ui.base;
import :chat_list;
import mux.ui.messages;
import :header;
import :info_new_chats;
import :info_explore;
import :list_shown;

// Bars and tabs are keyed by their values, ordered for memo reconciliation.
export namespace mux::config::space_item {
inline bool operator<(const space_item_t& a, const space_item_t& b) {
  if (a.index() != b.index()) return a.index() < b.index();
  return spl::visit(spl::overloaded{
      [&](const space& left) { return left.room < spl::get<space>(b).room; },
      [](const auto&) { return false; }}, a);
}
}
export namespace mux::ui {

namespace folder {
inline bool operator<(const folder_t& a, const folder_t& b) {
  if (a.index() != b.index()) return a.index() < b.index();
  return spl::visit(spl::overloaded{
      [&](const space& left) { return left.room < spl::get<space>(b).room; },
      [&](const group& left) { return left.name < spl::get<group>(b).name; },
      [](const auto&) { return false; }}, a);
}
}

auto server_search_arguments(const palette& colours, const server_search_facts& facts) {
  namespace c = skiff::compose;
  auto rooms = std::ranges::to<std::vector>(std::views::transform(std::views::take(facts.rooms, 30), [&](const auto& room) {
    return directory_room_row(colours, room, std::string(), facts.by);
  }));
  auto people = std::ranges::to<std::vector>(std::views::transform(std::views::take(facts.people, 30), [&](const auto& person) {
    return found_person_row(colours, person);
  }));
  const scene::Spec caption{.margin = {6.0f, 16.0f, 2.0f, 16.0f}};
  return std::tuple{
      c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 0.0f, 8.0f, 0.0f}}),
      c::visible(!rooms.empty(), c::styled(caption, nodes::Text("Rooms", 13.0f, colours.dim, true))),
      c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(rooms)),
      c::visible(!people.empty(), c::styled(caption, nodes::Text("People", 13.0f, colours.dim, true))),
      c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(people)),
      c::visible(facts.waiting || (facts.rooms.empty() && facts.people.empty()), c::styled(caption,
          wrapped(nodes::Text(facts.waiting ? "Searching the server…" : "No rooms or people found.", 13.0f, colours.dim))))};
}
using server_search_content_t = decltype(std::apply([](auto&&... parts) {
  return skiff::compose::Box(std::forward<decltype(parts)>(parts)...);
}, server_search_arguments(std::declval<const palette&>(), std::declval<const server_search_facts&>())));
auto server_search_list(const palette& colours) {
  return skiff::compose::mount<server_search_content_t, server_search_facts>(
      [colours = &colours](const auto& facts) { return server_search_arguments(*colours, facts); },
      {.fillX = true, .autoSize = scene::axes::kY});
}

// SPDX-License-Identifier: AGPL-3.0-only
// Functional pieces of the chat list's side. Folded into conversations_side
// when its gesture surface and model contract are ready.

// The program's descriptions of the bars and tabs. Item keys stay separate
// from their rendered snapshots so memoized rows survive an unrelated edit.
struct side_space_view {
  folder_t folder;
  std::string account, key, name;
  bool chosen = false;
  float size = 40.0f;
  friend bool operator==(const side_space_view&, const side_space_view&) = default;
};
using side_folder_view = folder_tab_shown;
struct side_list_place {
  std::optional<account_id> account;
  folder_t folder;
  std::optional<std::string> forum;
  friend bool operator==(const side_list_place&, const side_list_place&) = default;
};
struct side_space_menu_facts {
  std::string account;
  config::space_item_t item;
  std::string name;
  skia::SkPoint at{};
  friend bool operator==(const side_space_menu_facts&, const side_space_menu_facts&) = default;
};
struct side_chat_menu_facts {
  conversation_id chat;
  std::string name;
  account_id listing;
  std::vector<account_id> accounts;
  config::theme_t theme = config::theme::tinted{};
  skia::SkPoint at{};
  friend bool operator==(const side_chat_menu_facts&, const side_chat_menu_facts&) = default;
};
struct side_menu_context {
  std::optional<account_id> listing;
  std::vector<account_id> accounts;
  config::theme_t theme = config::theme::tinted{};
  friend bool operator==(const side_menu_context&, const side_menu_context&) = default;
};
inline bool same_side_menu_context(const std::shared_ptr<const side_menu_context>& a,
                                   const std::shared_ptr<const side_menu_context>& b) {
  if (a == b) return true;
  return a && b && *a == *b;
}
struct side_found_view {
  std::size_t index = 0;
  std::string sender, name;
  std::chrono::sys_time<std::chrono::milliseconds> at{};
  std::string snippet;
  friend bool operator==(const side_found_view&, const side_found_view&) = default;
};
inline side_found_view side_found_of(const search_result& one) {
  return {one.index, one.sender, one.name, one.at, one.snippet};
}
// Menus are UI state. An answer choosing an item clears this part at the
// next model refresh, after the input handler has finished.
struct side_menu_state {
  std::optional<side_space_menu_facts> space;
  std::optional<side_chat_menu_facts> chat;
};
struct close_side_menu {};
struct side_folder_chosen { folder_t folder; };
struct choose_side_folder {
  using Answer = side_folder_chosen;
  Answer operator()(const folder_t& folder) const { return {folder}; }
};
struct side_menu_events {
  auto on(const side_space_menu_facts& facts, const side_menu_state&) const {
    return skiff::model::over<side_menu_state>(skiff::model::setTo(side_menu_state{.space = facts}));
  }
  auto on(const side_chat_menu_facts& facts, const side_menu_state&) const {
    return skiff::model::over<side_menu_state>(skiff::model::setTo(side_menu_state{.chat = facts}));
  }
  auto on(close_side_menu, const side_menu_state&) const {
    return skiff::model::over<side_menu_state>(skiff::model::setTo(side_menu_state{}));
  }
};
template <class Act> struct side_menu_pick {
  using Answer = std::tuple<close_side_menu, typename Act::Answer>;
  Act act;
  Answer operator()() { return {close_side_menu{}, act()}; }
};

template <class Pick>
struct pick_side_folder {
  using Answer = std::invoke_result_t<Pick&, const folder_t&>;
  Pick pick;
  folder_t which;
  Answer operator()() { return std::invoke(pick, which); }
};

template <class Pick>
auto side_space_icon(const palette& colours, const config::space_item_t& item,
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
auto side_folder_tab(const palette& colours, std::string name, folder_t which,
                     bool chosen, Pick pick) {
  namespace c = skiff::compose;
  auto label = elided(nodes::Text(name, 13.0f, chosen ? colours.accent : colours.dim, true));
  label.setMaxWidth(160.0f);
  return c::with_cursor(scene::cursor::arrow{}, c::said(scene::SemanticRole{scene::semantic_role::tab{}}, chosen,
      c::onPress(pick_side_folder<Pick>{std::move(pick), std::move(which)},
          c::column(c::vbox(0.0f,
              {.height = 32.0f, .autoSize = scene::axes::kX,
               .padding = {0.0f, 10.0f, 0.0f, 10.0f}, .cornerRadius = 6.0f,
               .hoverBackground = colours.chosen, .focusBackground = colours.chosen}),
              c::styled({.anchor = scene::anchor::kCentreLeft, .origin = scene::anchor::kCentreLeft}, std::move(label)),
              c::visible(chosen, c::styled(
                  {.place = scene::anchor::kBottomLeft, .fillX = true, .height = 3.0f, .cornerRadius = 1.5f},
                  nodes::Box<>(colours.accent)))),
          std::move(name))));
}

inline auto side_found_row(const palette& colours, const search_result& one) {
  namespace c = skiff::compose;
  return c::with_cursor(scene::cursor::arrow{}, c::onClick(request::search_pick{one.index},
      c::row(c::hbox(10.0f, {.fillX = true, .height = 56.0f,
          .padding = {0.0f, 12.0f, 0.0f, 10.0f}, .hoverBackground = colours.chosen}),
          c::styled({.alignSelf = scene::align::kMiddle},
              avatar_mark(avatar_of::person{}, one.sender, one.name, 40.0f)),
          c::column(c::vbox(4.0f,
              {.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}),
              name_time_line(one.name, std::format("{:%d.%m.%y}", std::chrono::floor<std::chrono::days>(one.at)),
                             colours.text, colours.dim, 12.0f),
              c::styled({.fillX = true}, elided(nodes::Text(one.snippet, 13.0f, colours.dim))))),
      std::format("{}: {}", one.name, one.snippet)));
}

inline auto side_forum_head(const palette& colours) {
  namespace c = skiff::compose;
  return c::shown_for<side_shown>([](const side_shown& now) { return now.forum.has_value(); },
      c::row(c::hbox(8.0f, {.fillX = true, .height = 40.0f,
          .padding = {0.0f, 8.0f, 0.0f, 8.0f}}),
          c::styled({.alignSelf = scene::align::kMiddle},
              icon_button(colours, icon::back{}, sends<request::close_forum>{})),
          c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
              c::text_for<side_shown>([](const side_shown& now) { return now.forum.value_or(""); },
                  elided(nodes::Text("", 15.0f, colours.text, true)))),
          c::styled({.alignSelf = scene::align::kMiddle},
              icon_button(colours, icon::gear{}, sends<request::manage_forum>{}))));
}
struct side_search_typed {
  static constexpr bool kEveryEdit = true;
  using Answer = request::list_search;
  Answer operator()(std::string_view text) const { return {std::string(text)}; }
};
inline auto side_search_field(const palette& colours) {
  namespace c = skiff::compose;
  auto field = widgets::TextArea<side_search_typed>(colours.widgets, "Search", {});
  field.setSingleLine(true);
  field.setFontSize(14.0f);
  field.apply({.fillX = true, .margin = {2.0f, 14.0f, 0.0f, 14.0f}});
  auto text = c::applied<list_shown>([](const list_shown& now) { return now.search; },
      [](auto& node, const std::string& now) { if (node.text() != now) node.setText(now); }, std::move(field));
  return c::focus_frame(c::vbox(0.0f, {.fillX = true, .height = 36.0f,
      .margin = {0.0f, 10.0f, 8.0f, 10.0f}, .cornerRadius = 18.0f,
      .background = colours.tile, .selectedBackground = colours.chosen}), std::move(text));
}

struct set_bars_act {
  using Answer = ::mux::ui::request::set_space_bars;
  std::string account;
  config::space_item_t item;
  bool side = true, top = false;
  ::mux::ui::request::set_space_bars operator()() { return ::mux::ui::request::set_space_bars{account, item, side, top}; }
};
struct explore_act {
  using Answer = ::mux::ui::request::explore_space;
  std::string room;
  ::mux::ui::request::explore_space operator()() { return ::mux::ui::request::explore_space{room}; }
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
struct chat_settings_act {
  using Answer = std::tuple<::mux::ui::request::choose, ::mux::ui::request::open_manage>;
  conversation_id id;
  Answer operator()() const { return {::mux::ui::request::choose{id}, ::mux::ui::request::open_manage{}}; }
};
// Listed in another account's list too, or moved there.
struct place_act {
  using Answer = ::mux::ui::request::place_chat;
  conversation_id chat;
  account_id to;
  bool moved;
  ::mux::ui::request::place_chat operator()() { return ::mux::ui::request::place_chat{chat, to, moved}; }
};
// Out of this list, where it is another account's: back to its own
// where it was moved.
struct unplace_act {
  using Answer = ::mux::ui::request::unplace_chat;
  conversation_id chat;
  account_id from;
  ::mux::ui::request::unplace_chat operator()() { return ::mux::ui::request::unplace_chat{chat, from}; }
};
struct strip_act {
  using Answer = ::mux::ui::request::flip_chat_strip;
  conversation_id chat;
  account_id in;
  ::mux::ui::request::flip_chat_strip operator()() { return ::mux::ui::request::flip_chat_strip{chat, in}; }
};
struct strip_colour_act {
  using Answer = std::tuple<close_side_menu, std::optional<::mux::ui::request::set_chat_strip_colour>>;
  conversation_id chat;
  account_id in;
  Answer operator()(const config::accent_t& colour) {
    return {close_side_menu{}, ::mux::ui::request::set_chat_strip_colour{chat, in, colour}};
  }
};

inline auto side_popup_look(const palette& colours, float width) {
  return skiff::compose::vbox(4.0f,
      {.width = width, .autoSize = scene::axes::kY, .padding = {8.0f, 8.0f, 8.0f, 8.0f},
       .cornerRadius = 10.0f, .background = colours.popup(),
       .border = scene::Border{colours.band, 1.0f},
       .shadow = scene::Shadow{skia::colorSetARGB(70, 0, 0, 0), 3.0f}});
}
template <class Act>
auto side_menu_button(const palette& colours, std::string label, Act act) {
  return skiff::compose::styled({.fillX = true, .height = 30.0f},
      button_for<side_menu_pick<Act>>(colours.widgets, std::move(label), side_menu_pick<Act>{std::move(act)}));
}
inline auto side_chat_menu(const palette& colours, const conversation_id& id, std::string name,
                           const account_id& listing, const std::vector<account_id>& accounts,
                           const config::theme_t& theme) {
  namespace c = skiff::compose;
  auto places = std::ranges::to<std::vector>(std::views::transform(std::views::filter(accounts, [&](const auto& to) { return to != id.account && to != listing; }), [&](const auto& to) {
        return c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
            side_menu_button(colours, std::format("Copy to {}", to.address), place_act{id, to, false}),
            side_menu_button(colours, std::format("Move to {}", to.address), place_act{id, to, true}));
      }));
  auto accents = std::ranges::to<std::vector>(std::views::transform(accents_of(false), [&](const auto& accent) {
    return accent_circle(strip_colour_act{id, listing}, accent, theme, false);
  }));
  return c::column(side_popup_look(colours, 320.0f),
      c::styled({.fillX = true}, elided(nodes::Text(std::move(name), 13.0f, colours.dim, true))),
      side_menu_button(colours, "Chat settings\u2026", chat_settings_act{id}),
      std::move(places),
      c::visible(id.account != listing,
          side_menu_button(colours, "Remove from this list", unplace_act{id, listing})),
      c::visible(id.account != listing,
          side_menu_button(colours, "Strip on or off", strip_act{id, listing})),
      c::visible(id.account != listing,
          c::many(c::hbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY,
              .margin = {4.0f, 0.0f, 0.0f, 0.0f}}), std::move(accents))));
}
[[nodiscard]] inline std::string side_space_room(const config::space_item_t& item) {
  return spl::visit(spl::overloaded{
      [](const config::space_item::space& space) { return space.room; },
      [](const auto&) { return std::string(); }}, item);
}
inline auto side_space_menu(const palette& colours, const std::string& account,
                            const config::space_item_t& item, std::string name) {
  namespace c = skiff::compose;
  const auto room = side_space_room(item);
  return c::column(side_popup_look(colours, 190.0f),
      c::styled({.fillX = true}, elided(nodes::Text(name, 13.0f, colours.dim, true))),
      c::visible(!room.empty(), side_menu_button(colours, "Explore its rooms\u2026", explore_act{room})),
      c::visible(!room.empty(), side_menu_button(colours, "Space settings\u2026", manage_act{room})),
      c::visible(!room.empty(), side_menu_button(colours, "Create a room in it\u2026", create_in_act{account, room, name, false})),
      c::visible(!room.empty(), side_menu_button(colours, "Create a space in it\u2026", create_in_act{account, room, name, true})),
      c::visible(!room.empty(), side_menu_button(colours, "Leave space\u2026", leave_act{account, room})),
      side_menu_button(colours, "Side bar only", set_bars_act{account, item, true, false}),
      side_menu_button(colours, "Top bar only", set_bars_act{account, item, false, true}),
      side_menu_button(colours, "Both bars", set_bars_act{account, item, true, true}),
      side_menu_button(colours, "Hide", set_bars_act{account, item, false, false}));
}

using side_space_menu_t = decltype(side_space_menu(std::declval<const palette&>(),
    std::declval<const std::string&>(), std::declval<const config::space_item_t&>(), std::string{}));
using side_chat_menu_t = decltype(side_chat_menu(std::declval<const palette&>(),
    std::declval<const conversation_id&>(), std::string{}, std::declval<const account_id&>(),
    std::declval<const std::vector<account_id>&>(), std::declval<const config::theme_t&>()));
} // namespace mux::ui

template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::side_space_menu_t> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::side_space_menu_t>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::side_chat_menu_t> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::side_chat_menu_t>() noexcept;

export namespace mux::ui {

struct side_space_menu_at {
  std::string account;
  config::space_item_t item;
  std::string name;
  std::optional<side_space_menu_facts> operator()(const auto&, float x, float y) const {
    return side_space_menu_facts{account, item, name, {x, y}};
  }
};
struct side_chat_menu_at {
  conversation_id chat;
  std::string name;
  std::shared_ptr<const side_menu_context> context;
  std::optional<side_chat_menu_facts> operator()(const auto&, float x, float y) const {
    return side_chat_menu_facts{chat, name,
        context && context->listing ? *context->listing : chat.account,
        context ? context->accounts : std::vector<account_id>{},
        context ? context->theme : config::theme_t{config::theme::tinted{}}, {x, y}};
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
template <class Pick>
auto side_spaces_for(const palette& colours, Pick pick, bool horizontal) {
  namespace c = skiff::compose;
  using make = side_space_row_make<Pick>;
  using row = std::invoke_result_t<make, const config::space_item_t&, const side_space_view&, bool>;
  using rows = nodes::MemoRows<config::space_item_t, side_space_view, row, make, c::Box<>>;
  auto line = rows(make{&colours, std::move(pick)}, c::Box<>(horizontal
      ? c::hbox(4.0f, {.fillY = true, .autoSize = scene::axes::kX})
      : c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 6.0f, 8.0f, 6.0f}})),
      {.autoSize = scene::axes::kBoth, .alignSelf = scene::align::kMiddle});
  return c::projected<side_shown>([horizontal](const side_shown& now) {
    return std::ranges::to<std::vector>(std::views::transform((horizontal ? now.top_bar : now.side_bar), [&](const auto& entry) {
      return std::pair{entry.item, side_space_view{entry.folder, now.account, entry.key, entry.name, entry.chosen, entry.size}};
    }));
  }, std::move(line));
}
template <class Pick> struct side_folder_row_make {
  const palette* colours;
  Pick pick;
  auto operator()(const std::pair<folder_t, side_folder_view>& entry) const {
    return side_folder_tab(*colours, entry.second.name, entry.first, entry.second.chosen, pick);
  }
};
template <class Pick>
auto side_folders_for(const palette& colours, Pick pick) {
  namespace c = skiff::compose;
  using make = side_folder_row_make<Pick>;
  using row = std::invoke_result_t<make, const std::pair<folder_t, side_folder_view>&>;
  using rows = nodes::MemoRows<folder_t, side_folder_view, row, make>;
  nodes::Stack flow;
  flow.fStack = {.direction = nodes::direction::horizontal{}, .spacingX = 2.0f, .spacingY = 2.0f, .wrap = true};
  auto tabs = rows(make{&colours, std::move(pick)}, std::move(flow), {.autoSize = scene::axes::kBoth});
  tabs.apply({.fillX = true, .autoSize = scene::axes::kY, .margin = {0.0f, 8.0f, 6.0f, 8.0f}});
  return c::projected<side_shown>([](const side_shown& now) {
    return std::ranges::to<std::vector>(std::views::transform(now.folders, [](const side_folder_view& tab) { return std::pair{tab.folder, tab}; }));
  }, std::move(tabs));
}

struct side_chat_view {
  conversation_view row;
  std::shared_ptr<const side_menu_context> context;
  friend bool operator==(const side_chat_view& a, const side_chat_view& b) {
    return a.row == b.row && same_side_menu_context(a.context, b.context);
  }
};
using side_chat_item = std::tuple<conversation_id, side_chat_view, const conversation*, const listed_chat*, const ui_needs*>;
struct side_chats_told {
  std::shared_ptr<const chat_listing> chats;
  std::shared_ptr<const side_menu_context> context;
};
inline constexpr auto make_side_chat_row = [](const side_chat_item& item) {
  const auto& [id, view, one, listed, needs] = item;
  return skiff::compose::onRightPressAt(side_chat_menu_at{id, view.row.name, view.context},
      conversation_row(*needs, *one, view.row, listed->events));
};
inline auto side_chats_for(const ui_needs& needs) {
  namespace c = skiff::compose;
  using row = std::invoke_result_t<decltype(make_side_chat_row), const side_chat_item&>;
  using rows = nodes::MemoRows<conversation_id, side_chat_view, row, std::remove_cv_t<decltype(make_side_chat_row)>>;
  auto line = c::styled({.fillX = true, .autoSize = scene::axes::kY}, rows(make_side_chat_row));
  auto viewport = c::virtual_rows<side_chat_item>([](const side_chat_item& item) {
    return conversation_height(*std::get<2>(item));
  }, c::styled({.fillX = true, .grow = scene::axes::kY}, nodes::ScrollContainer(std::move(line))));
  auto contents = c::projected<listing_shown>([](const listing_shown& now) {
    return side_chats_told{now.listing, std::make_shared<const side_menu_context>(
        side_menu_context{now.account, now.accounts, now.theme})};
  }, c::with_model<chats_model, side_chats_told>([needs](const chats_model& chats, const side_chats_told& now) {
    const auto find = [&](const conversation_id& id) -> const conversation* {
      if (const auto forum = now.chats->forums.find(id); forum != now.chats->forums.end()) return &forum->second;
      const account* owner = chats.root().accounts.find(id.account);
      return owner ? owner->conversations.find(id.id) : nullptr;
    };
    if (!now.chats) return std::vector<side_chat_item>{};
    return std::ranges::to<std::vector>(std::views::transform(std::views::filter(now.chats->chats, [&](const listed_chat& one) { return find(one.id) != nullptr; }), [&](const listed_chat& one) {
          return side_chat_item{one.id, {one.view, now.context}, find(one.id), &one, &needs};
        }));
  }, std::move(viewport)));
  return c::projected<list_shown>([](const list_shown& now) {
    return side_list_place{now.account, now.folder, now.forum};
  }, c::remembered_scroll<side_list_place>(std::move(contents)));
}
struct side_found_row_make {
  const palette* colours;
  auto operator()(const std::pair<std::size_t, side_found_view>& item) const {
    const auto& one = item.second;
    return side_found_row(*colours, search_result{one.index, one.sender, one.name, one.at, one.snippet});
  }
};
inline auto side_found_for(const palette& colours) {
  namespace c = skiff::compose;
  using row = std::invoke_result_t<side_found_row_make, const std::pair<std::size_t, side_found_view>&>;
  using rows = nodes::MemoRows<std::size_t, side_found_view, row, side_found_row_make>;
  auto contents = c::projected<found_shown>([](const found_shown& now) {
    return std::ranges::to<std::vector>(std::views::transform(now.results, [](const search_result& one) { return std::pair{one.index, side_found_of(one)}; }));
  }, c::styled({.fillX = true, .autoSize = scene::axes::kY}, rows(side_found_row_make{&colours})));
  return c::shown_for<found_shown>([](const found_shown& now) { return now.count.has_value(); },
      c::projected<found_shown>([](const found_shown& now) { return now; },
          c::restarted_scroll<found_shown>(c::styled({.fillX = true, .grow = scene::axes::kY}, nodes::ScrollContainer(std::move(contents))))));
}
inline auto side_server_for(const palette& colours) {
  namespace c = skiff::compose;
  auto contents = c::projected<elsewhere_shown>([](const elsewhere_shown& now) { return now.found; }, server_search_list(colours));
  return c::shown_for<elsewhere_shown>([](const elsewhere_shown& now) { return now.found.has_value(); },
      c::projected<elsewhere_shown>([](const elsewhere_shown& now) { return now; },
          c::restarted_scroll<elsewhere_shown>(c::styled({.fillX = true, .grow = scene::axes::kY}, nodes::ScrollContainer(std::move(contents))))));
}

using side_chat_list_t = decltype(side_chats_for(std::declval<const ui_needs&>()));
using side_found_list_t = decltype(side_found_for(std::declval<const palette&>()));
using side_server_list_t = decltype(side_server_for(std::declval<const palette&>()));
} // namespace mux::ui

template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::side_chat_list_t> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::side_chat_list_t>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::side_found_list_t> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::side_found_list_t>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::side_server_list_t> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::side_server_list_t>() noexcept;

export namespace mux::ui {

inline constexpr float kMinSidebar = 240.0f;
struct side_bar_layout {
  struct State {
    bool big = false;
    friend bool operator==(const State&, const State&) = default;
  };
  static auto& top(auto& node) { return std::get<2>(std::get<0>(node.fParts).fParts); }
  static auto& search(auto& node) { return std::get<1>(std::get<1>(std::get<2>(node.fParts).fParts).fParts); }
  static void prepare(auto& node, const State& state) {
    auto& bar = top(node);
    bar.fState.setOutOfFlow(state.big);
    bar.apply({.height = state.big ? 58.0f : 34.0f});
    search(node).apply({.margin = {0.0f, 10.0f, state.big ? 74.0f : 8.0f, 10.0f}});
    if (!state.big) bar.apply({.x = 0.0f, .y = 0.0f});
  }
  static float below(const auto& node) {
    const auto& head = std::get<0>(node.fParts);
    const auto& field = std::get<1>(std::get<1>(std::get<2>(node.fParts).fParts).fParts);
    return field.bounds().fBottom + 8.0f - head.fState.contentBox().fTop;
  }
  static void arrange(auto& node, const State& state) {
    if (!state.big) return;
    auto& head = std::get<0>(node.fParts);
    auto& bar = top(node);
    bar.apply({.place = scene::anchor::kTopLeft, .x = 0.0f, .y = below(node),
        .width = std::max(0.0f, node.fState.contentBox().width() - 16.0f)});
    scene::layout(bar, head.fState.contentBox());
  }
  static bool settling(const auto& node, const State& state) {
    if (!state.big) return false;
    const auto& bar = std::get<2>(std::get<0>(node.fParts).fParts);
    return std::abs(bar.bounds().fTop - (std::get<0>(node.fParts).fState.contentBox().fTop + below(node))) > 0.5f;
  }
  static void settle(auto& node, const State& state) { arrange(node, state); }
};

inline auto side_space_popup(const palette& colours) {
  namespace c = skiff::compose;
  using facts = side_space_menu_facts;
  using field = skiff::model::Field<&side_menu_state::space>;
  using menu = decltype(side_space_menu(colours, std::string{}, config::space_item_t{}, std::string{}));
  using content = c::Box<menu>;
  auto box = c::projected<field>([](const std::optional<facts>& now) { return now; },
      c::mount<content, facts>([&colours](const facts& now) {
        return std::tuple{c::vbox(0.0f, {.autoSize = scene::axes::kBoth}),
            side_space_menu(colours, now.account, now.item, now.name)};
      }, {.autoSize = scene::axes::kBoth}, true));
  return c::placed_at<field>([](const std::optional<facts>& now) {
    return now ? std::optional(now->at) : std::nullopt;
  }, std::move(box));
}
inline auto side_chat_popup(const palette& colours) {
  namespace c = skiff::compose;
  using facts = side_chat_menu_facts;
  using field = skiff::model::Field<&side_menu_state::chat>;
  using menu = decltype(side_chat_menu(colours, conversation_id{}, std::string{}, account_id{},
      std::vector<account_id>{}, config::theme_t{}));
  using content = c::Box<menu>;
  auto box = c::projected<field>([](const std::optional<facts>& now) { return now; },
      c::mount<content, facts>([&colours](const facts& now) {
        return std::tuple{c::vbox(0.0f, {.autoSize = scene::axes::kBoth}),
            side_chat_menu(colours, now.chat, now.name, now.listing, now.accounts, now.theme)};
      }, {.autoSize = scene::axes::kBoth}, true));
  return c::placed_at<field>([](const std::optional<facts>& now) {
    return now ? std::optional(now->at) : std::nullopt;
  }, std::move(box));
}
struct side_pick_folder {
  using Answer = request::pick_folder;
  Answer operator()(const folder_t& folder) const { return {folder}; }
};
using side_space_rows_t = decltype(side_spaces_for(std::declval<const palette&>(), side_pick_folder{}, true));
using side_folder_rows_t = decltype(side_folders_for(std::declval<const palette&>(), side_pick_folder{}));
} // namespace mux::ui

template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::side_folder_rows_t> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::side_folder_rows_t>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::side_space_rows_t> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::side_space_rows_t>() noexcept;

export namespace mux::ui {

inline auto side_header_for(const palette& colours) {
  namespace c = skiff::compose;
  auto top = c::shown_for<side_shown>([](const side_shown& now) { return now.top_on || now.dragging; },
      c::styled({.height = 34.0f, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle,
          .cornerRadius = 8.0f, .masking = true},
          c::SideScroll(side_spaces_for(colours, side_pick_folder{}, true), true)));
  auto head = c::row(c::hbox(10.0f, {.fillX = true, .height = 52.0f,
      .padding = {8.0f, 8.0f, 8.0f, 8.0f}}),
      menu_button(colours),
      c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text("mux", 17.0f, colours.text, true)),
      std::move(top), c::styled({.alignSelf = scene::align::kMiddle},
          icon_button(colours, icon::compass{}, sends<request::open_explore>{})));
  return head;
}
inline auto side_body_for(const ui_needs& needs) {
  namespace c = skiff::compose;
  const auto& colours = *needs.colours;
  auto side = c::shown_for<side_shown>([](const side_shown& now) { return now.side_on || now.dragging; },
      c::styled({.fillY = true, .width = 56.0f},
          nodes::ScrollContainer(side_spaces_for(colours, side_pick_folder{}, false))));
  auto chats = c::shown_for<side_shown>([](const side_shown& now) { return now.chats_on; },
      c::column(c::vbox(0.0f, {.fillX = true, .grow = scene::axes::kY}), side_chats_for(needs)));
  auto rest = c::column(c::vbox(0.0f, {.fillY = true, .grow = scene::axes::kX}),
      side_forum_head(colours), side_search_field(colours), side_folders_for(colours, side_pick_folder{}),
      c::shown_for<side_shown>([](const side_shown& now) { return now.no_chats; },
          c::styled({.margin = {12.0f, 16.0f, 0.0f, 16.0f}}, nodes::Text("No chats yet.", 13.0f, colours.dim))),
      std::move(chats),
      c::shown_for<found_shown>([](const found_shown& now) { return now.count.has_value(); },
          c::styled({.margin = {4.0f, 16.0f, 6.0f, 16.0f}},
          c::text_for<found_shown>([](const found_shown& now) {
            return !now.count ? std::string{} : *now.count == 0 ? std::string("No messages found")
                : *now.count == 1 ? std::string("1 message found") : std::format("{} messages found", *now.count);
          }, nodes::Text("", 13.0f, colours.dim, true)))),
      side_found_for(colours), side_server_for(colours));
  auto body = c::row(c::hbox(0.0f, {.fillX = true, .grow = scene::axes::kY}), std::move(side), std::move(rest));
  return body;
}
using side_header_t = decltype(side_header_for(std::declval<const palette&>()));
using side_body_t = decltype(side_body_for(std::declval<const ui_needs&>()));
} // namespace mux::ui

template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::side_header_t> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::side_header_t>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::side_body_t> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::side_body_t>() noexcept;

export namespace mux::ui {
inline auto side_column(const ui_needs& needs) {
  namespace c = skiff::compose;
  const auto& colours = *needs.colours;
  auto head = side_header_for(colours);
  auto body = side_body_for(needs);
  auto surface = c::projected<list_shown>([](const list_shown& now) { return side_bar_layout::State{now.big_bars}; },
      c::arranged<side_bar_layout>({}, c::column(c::vbox(0.0f, {.fill = true, .background = colours.sidebar}),
          std::move(head), connection_bar_for<connection_shown>(colours, &connection_shown::list),
          std::move(body), side_space_popup(colours), side_chat_popup(colours))));
  auto observed = c::onCapture<close_side_menu>([](const auto& node, const scene::pointer::down& at, scene::PointerReply&) {
    const auto& space = std::get<3>(node.fParts);
    const auto& chat = std::get<4>(node.fParts);
    const bool inside = (space.visible() && space.bounds().contains(at.x, at.y)) ||
        (chat.visible() && chat.bounds().contains(at.x, at.y));
    return !inside && (space.visible() || chat.visible()) ? std::optional(close_side_menu{}) : std::nullopt;
  }, std::move(surface));
  auto reorder = c::reorder_bars<config::space_item_t, side_space_view>(std::move(observed),
      side_space_row_make<side_pick_folder>{&colours, {}}, [](auto& node) {
        auto& head = std::get<0>(node.fParts);
        auto& body = std::get<2>(node.fParts);
        auto& top = std::get<2>(head.fParts);
        auto& side = std::get<0>(body.fParts);
        return std::tuple{
            c::ReorderLane{top, top.line(), node.fRules.big ? top.bounds() : head.bounds(), 4.0f},
            c::ReorderLane{side, std::get<0>(side.fChildren), side.bounds(), 8.0f}};
      }, colours.chosen);
  return c::projected<screen_shown>([](const screen_shown& now) {
    return c::WidthLimit{.wanted = now.side_width, .whole = now.single};
  }, c::clamped_width({}, c::local<side_menu_state>(side_menu_events{}, std::move(reorder))));
}

using side_column_t = decltype(side_column(std::declval<const ui_needs&>()));

} // namespace mux::ui

template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::side_column_t> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::side_column_t>() noexcept;
