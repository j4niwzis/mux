// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:conversations_side -- The chat list's side: its folders and spaces, the space bars, and the column of chats.
module;
#include "skiff/named.h"
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
import mux.plugins.ui;
import mux.plugins.ui.top;
import mux.ui.lists;
import mux.ui.messages;
import :header;
import :info_explore;

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

auto server_search_arguments(const auto& colours, const server_search_facts& facts) {
  namespace c = skiff::compose;
  auto rooms = facts.rooms | std::views::take(30) | std::views::transform([&](const auto& room) {
    return directory_room_row(colours, room, std::string(), facts.by);
  }) | std::ranges::to<std::vector>();
  auto people = facts.people | std::views::take(30) | std::views::transform([&](const auto& person) {
    return found_person_row(colours, person);
  }) | std::ranges::to<std::vector>();
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
template <class P> using server_search_content_t = decltype(std::apply([](auto&&... parts) {
  return skiff::compose::Box(std::forward<decltype(parts)>(parts)...);
}, server_search_arguments(std::declval<P>(), std::declval<const server_search_facts&>())));
auto server_search_list(const auto& colours) {
  return SKIFF_NAMED(server_search_list, skiff::compose::mount<server_search_content_t<decltype(colours)>, server_search_facts>(
      [colours = &colours](const auto& facts) { return server_search_arguments(*colours, facts); },
      {.fillX = true, .autoSize = scene::axes::kY}));
}

// SPDX-License-Identifier: AGPL-3.0-only
// Functional pieces of the chat list's side. Folded into conversations_side
// when its gesture surface and model contract are ready.

// The program's descriptions of the bars and tabs. Item keys stay separate
// from their rendered snapshots so memoized rows survive an unrelated edit.
using side_folder_view = folder_tab_shown;
struct side_list_place {
  std::optional<account_id> account;
  folder_t folder;
  std::optional<std::string> forum;
  friend bool operator==(const side_list_place&, const side_list_place&) = default;
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
  std::optional<side_chat_menu_facts> chat;
};
struct close_side_menu {};
struct side_folder_chosen { folder_t folder; };
struct choose_side_folder {
  using Answer = side_folder_chosen;
  Answer operator()(const folder_t& folder) const { return {folder}; }
};
struct side_menu_events {
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
auto side_folder_tab(const auto& colours, std::string name, folder_t which,
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

auto side_found_row(const auto& colours, const search_result& one) {
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

auto side_forum_head(const auto& colours) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(side_forum_head, c::shown_for<side_shown>([](const side_shown& now) { return now.forum.has_value(); },
      c::row(c::hbox(8.0f, {.fillX = true, .height = 40.0f,
          .padding = {0.0f, 8.0f, 0.0f, 8.0f}}),
          c::styled({.alignSelf = scene::align::kMiddle},
              icon_button(colours, icon::back{}, sends<request::close_forum>{})),
          c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
              c::text_for<side_shown>([](const side_shown& now) { return now.forum.value_or(""); },
                  elided(nodes::Text("", 15.0f, colours.text, true)))),
          c::styled({.alignSelf = scene::align::kMiddle},
              icon_button(colours, icon::gear{}, sends<request::manage_forum>{})))));
}
struct side_search_typed {
  static constexpr bool kEveryEdit = true;
  using Answer = request::list_search;
  Answer operator()(std::string_view text) const { return {std::string(text)}; }
};
auto side_search_field(const auto& colours) {
  namespace c = skiff::compose;
  auto field = widgets::TextArea<side_search_typed>(colours.widgets, "Search", {});
  field.setSingleLine(true);
  field.setFontSize(14.0f);
  field.apply({.fillX = true, .margin = {2.0f, 14.0f, 0.0f, 14.0f}});
  auto text = c::applied<list_shown>([](const list_shown& now) { return now.search; },
      [](auto& node, const std::string& now) { if (node.text() != now) node.setText(now); }, std::move(field));
  return SKIFF_NAMED(side_search_field, c::focus_frame(c::vbox(0.0f, {.fillX = true, .height = 36.0f,
      .margin = {0.0f, 10.0f, 8.0f, 10.0f}, .cornerRadius = 18.0f,
      .background = colours.tile, .selectedBackground = colours.chosen}), std::move(text)));
}

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

auto side_popup_look(const auto& colours, float width) {
  return skiff::compose::vbox(4.0f,
      {.width = width, .autoSize = scene::axes::kY, .padding = {8.0f, 8.0f, 8.0f, 8.0f},
       .cornerRadius = 10.0f, .background = colours.popup(),
       .border = scene::Border{colours.band, 1.0f},
       .shadow = scene::Shadow{skia::colorSetARGB(70, 0, 0, 0), 3.0f}});
}
template <class Act>
auto side_menu_button(const auto& colours, std::string label, Act act) {
  return SKIFF_NAMED(side_menu_button, skiff::compose::styled({.fillX = true, .height = 30.0f},
      button_for<side_menu_pick<Act>>(colours.widgets, std::move(label), side_menu_pick<Act>{std::move(act)})));
}
auto side_chat_menu(const auto& colours, const conversation_id& id, std::string name,
                           const account_id& listing, const std::vector<account_id>& accounts,
                           const config::theme_t& theme) {
  namespace c = skiff::compose;
  auto places = accounts | std::views::filter([&](const auto& to) { return to != id.account && to != listing; }) | std::views::transform([&](const auto& to) {
        return c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
            side_menu_button(colours, std::format("Copy to {}", to.address), place_act{id, to, false}),
            side_menu_button(colours, std::format("Move to {}", to.address), place_act{id, to, true}));
      }) | std::ranges::to<std::vector>();
  auto accents = accents_of(false) | std::views::transform([&](const auto& accent) {
    return accent_circle(strip_colour_act{id, listing}, accent, theme, false);
  }) | std::ranges::to<std::vector>();
  return SKIFF_NAMED(side_chat_menu, c::column(side_popup_look(colours, 320.0f),
      c::styled({.fillX = true}, elided(nodes::Text(std::move(name), 13.0f, colours.dim, true))),
      side_menu_button(colours, "Chat settings\u2026", chat_settings_act{id}),
      std::move(places),
      c::visible(id.account != listing,
          side_menu_button(colours, "Remove from this list", unplace_act{id, listing})),
      c::visible(id.account != listing,
          side_menu_button(colours, "Strip on or off", strip_act{id, listing})),
      c::visible(id.account != listing,
          c::many(c::hbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY,
              .margin = {4.0f, 0.0f, 0.0f, 0.0f}}), std::move(accents)))));
}
} // namespace mux::ui


export namespace mux::ui {

struct side_chat_menu_at {
  conversation_id chat;
  std::string name;
  std::shared_ptr<const side_menu_context> context;
  std::optional<side_chat_menu_facts> operator()(auto& row, float x, float y) const {
    const auto shift = skiff::scene::work::drawnOffset(skiff::compose::state_of(row).fId);
    x += shift.x;
    y += shift.y;
    return side_chat_menu_facts{chat, name,
        context && context->listing ? *context->listing : chat.account,
        context ? context->accounts : std::vector<account_id>{},
        context ? context->theme : config::theme_t{config::theme::tinted{}}, {x, y}};
  }
};
template <class Pick> struct side_folder_row_make {
  const palette* colours;
  Pick pick;
  auto operator()(const std::pair<folder_t, side_folder_view>& entry) const {
    return side_folder_tab(*colours, entry.second.name, entry.first, entry.second.chosen, pick);
  }
};
template <class Pick>
auto side_folders_for(const auto& colours, Pick pick) {
  namespace c = skiff::compose;
  using make = side_folder_row_make<Pick>;
  using row = std::invoke_result_t<make, const std::pair<folder_t, side_folder_view>&>;
  using rows = nodes::MemoRows<folder_t, side_folder_view, row, make>;
  nodes::Stack flow;
  flow.fStack = {.direction = nodes::direction::horizontal{}, .spacingX = 2.0f, .spacingY = 2.0f, .wrap = true};
  auto tabs = rows(make{&colours, std::move(pick)}, std::move(flow), {.autoSize = scene::axes::kBoth});
  tabs.apply({.fillX = true, .autoSize = scene::axes::kY, .margin = {0.0f, 8.0f, 6.0f, 8.0f}});
  return SKIFF_NAMED(side_folders_for, c::projected<side_shown>([](const side_shown& now) {
    return now.folders | std::views::transform([](const side_folder_view& tab) { return std::pair{tab.folder, tab}; }) | std::ranges::to<std::vector>();
  }, std::move(tabs)));
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
// The chats' view that scrolls, by its mark.
struct side_chats_view {};
auto side_chats_for(const auto& needs) {
  namespace c = skiff::compose;
  using row = std::invoke_result_t<decltype(make_side_chat_row), const side_chat_item&>;
  using rows = nodes::MemoRows<conversation_id, side_chat_view, row, std::remove_cv_t<decltype(make_side_chat_row)>>;
  auto line = c::styled({.fillX = true, .autoSize = scene::axes::kY}, rows(make_side_chat_row));
  // The view that scrolls, marked: what keeps each list's offset finds it
  // under what tells it the list.
  auto scroll = c::virtual_rows<side_chat_item>([](const side_chat_item& item) {
    return conversation_height(*std::get<2>(item));
  }, c::styled({.fillX = true, .grow = scene::axes::kY}, nodes::ScrollContainer(std::move(line))));
  c::Marked<side_chats_view, decltype(scroll)> viewport(std::move(scroll));
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
    return now.chats->chats | std::views::filter([&](const listed_chat& one) { return find(one.id) != nullptr; }) | std::views::transform([&](const listed_chat& one) {
          return side_chat_item{one.id, {one.view, now.context}, find(one.id), &one, &needs};
        }) | std::ranges::to<std::vector>();
  }, std::move(viewport)));
  return SKIFF_NAMED(side_chats_for, c::projected<list_shown>([](const list_shown& now) {
    return side_list_place{now.account, now.folder, now.forum};
  }, c::remembered_scroll<side_list_place, decltype(viewport)>(std::move(contents))));
}
struct side_found_row_make {
  const palette* colours;
  auto operator()(const std::pair<std::size_t, side_found_view>& item) const {
    const auto& one = item.second;
    return side_found_row(*colours, search_result{one.index, one.sender, one.name, one.at, one.snippet});
  }
};
auto side_found_for(const auto& colours) {
  namespace c = skiff::compose;
  using row = std::invoke_result_t<side_found_row_make, const std::pair<std::size_t, side_found_view>&>;
  using rows = nodes::MemoRows<std::size_t, side_found_view, row, side_found_row_make>;
  auto contents = c::projected<found_shown>([](const found_shown& now) {
    return now.results | std::views::transform([](const search_result& one) { return std::pair{one.index, side_found_of(one)}; }) | std::ranges::to<std::vector>();
  }, c::styled({.fillX = true, .autoSize = scene::axes::kY}, rows(side_found_row_make{&colours})));
  return SKIFF_NAMED(side_found_for, c::shown_for<found_shown>([](const found_shown& now) { return now.count.has_value(); },
      c::projected<found_shown>([](const found_shown& now) { return now; },
          c::restarted_scroll<found_shown>(c::styled({.fillX = true, .grow = scene::axes::kY}, nodes::ScrollContainer(std::move(contents)))))));
}
auto side_server_for(const auto& colours) {
  namespace c = skiff::compose;
  auto contents = c::projected<elsewhere_shown>([](const elsewhere_shown& now) { return now.found; }, server_search_list(colours));
  return SKIFF_NAMED(side_server_for, c::shown_for<elsewhere_shown>([](const elsewhere_shown& now) { return now.found.has_value(); },
      c::projected<elsewhere_shown>([](const elsewhere_shown& now) { return now; },
          c::restarted_scroll<elsewhere_shown>(c::styled({.fillX = true, .grow = scene::axes::kY}, nodes::ScrollContainer(std::move(contents)))))));
}

} // namespace mux::ui


export namespace mux::ui {

inline constexpr float kMinSidebar = 240.0f;

auto side_chat_popup(const auto& colours) {
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
  return SKIFF_NAMED(side_chat_popup, c::placed_at<field>([](const std::optional<facts>& now) {
    return now ? std::optional(now->at) : std::nullopt;
  }, std::move(box)));
}
struct side_pick_folder {
  using Answer = request::pick_folder;
  Answer operator()(const folder_t& folder) const { return {folder}; }
};
} // namespace mux::ui


export namespace mux::ui {

// The buttons plugins put in the side's head (contract::side_header), after
// the space bar, in their order -- Explore's, for one.
template <class List>
struct side_buttons;
template <class... Features>
struct side_buttons<plugin_list<Features...>> {
  template <class Feature>
  static auto button_of(const palette& colours) -> decltype(extend(contract::side_header{}, type_tag<Feature>{}, colours)) {
    static_assert(requires { extend(contract::side_header{}, type_tag<Feature>{}, colours); },
                  composition::broken<contract::side_header, Feature>());
    return extend(contract::side_header{}, type_tag<Feature>{}, colours);
  }
  static auto row_of(const palette& colours) {
    return skiff::compose::row(skiff::compose::hbox(10.0f, {.autoSize = scene::axes::kBoth, .alignSelf = scene::align::kMiddle}),
                               button_of<Features>(colours)...);
  }
};
using side_header_plugins = side_buttons<composition::providers<contract::side_header, enabled_plugins>>;
// The bars a plugin puts beside the list (contract::side_bars) -- the
// spaces' -- or none.
template <class List>
struct side_bar_list;
template <>
struct side_bar_list<plugin_list<>> {
  static auto lane(const palette&) { return skiff::compose::column(skiff::compose::vbox(0.0f, {})); }
  static auto top(const palette&, bool) { return skiff::compose::column(skiff::compose::vbox(0.0f, {})); }
  template <class Surface>
  static Surface wrap(const palette&, Surface surface) { return surface; }
};
template <class Feature>
struct side_bar_list<plugin_list<Feature>> {
  static auto lane(const palette& colours) { return side_lane(contract::side_bars{}, type_tag<Feature>{}, colours); }
  static auto top(const palette& colours, bool big) { return top_bar(contract::side_bars{}, type_tag<Feature>{}, colours, big); }
  template <class Surface>
  static auto wrap(const palette& colours, Surface surface) {
    return wrap_side(contract::side_bars{}, type_tag<Feature>{}, colours, std::move(surface));
  }
};
using side_bar_plugins = side_bar_list<composition::providers<contract::side_bars, enabled_plugins>>;
auto side_header_for(const auto& colours) {
  namespace c = skiff::compose;
  auto top = side_bar_plugins::top(colours, false);
  auto head = c::row(c::hbox(10.0f, {.fillX = true, .height = 52.0f,
      .padding = {8.0f, 8.0f, 8.0f, 8.0f}}),
      menu_button(colours),
      c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text("mux", 17.0f, colours.text, true)),
      std::move(top), side_header_plugins::row_of(colours));
  return head;
}
auto side_body_for(const auto& needs) {
  namespace c = skiff::compose;
  const auto& colours = *needs.colours;
  auto side = side_bar_plugins::lane(colours);
  auto chats = c::shown_for<side_shown>([](const side_shown& now) { return now.chats_on; },
      c::column(c::vbox(0.0f, {.fillX = true, .grow = scene::axes::kY}), side_chats_for(needs)));
  auto rest = c::column(c::vbox(0.0f, {.fillY = true, .grow = scene::axes::kX}),
      side_forum_head(colours), side_search_field(colours), side_bar_plugins::top(colours, true), side_folders_for(colours, side_pick_folder{}),
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
} // namespace mux::ui


export namespace mux::ui {
auto side_column(const auto& needs) {
  namespace c = skiff::compose;
  const auto& colours = *needs.colours;
  auto head = side_header_for(colours);
  auto body = side_body_for(needs);
  // The space's and the chat's menus, over the column: a tap off them closes
  // the one up.
  auto popups = c::tapped_off(sends<close_side_menu>{},
      c::Box<decltype(side_chat_popup(colours))>(
          c::vbox(0.0f, {.place = scene::anchor::kTopLeft, .fill = true}), side_chat_popup(colours)));
  auto surface = c::column(c::vbox(0.0f, {.fill = true, .background = colours.sidebar}),
      std::move(head), connection_bar_for<connection_shown>(colours, &connection_shown::list), std::move(body), std::move(popups));
  // The plugin's bars' own around it: their menus, an icon dragged.
  auto reorder = side_bar_plugins::wrap(colours, std::move(surface));
  return SKIFF_NAMED(side_column, c::projected<screen_shown>([](const screen_shown& now) {
    return c::WidthLimit{.wanted = now.side_width, .whole = now.single};
  }, c::clamped_width({}, c::local<side_menu_state>(side_menu_events{}, std::move(reorder)))));
}



// The column as the screen holds it: outside a release build made once, in
// its own unit (conversations_side_made.cc), and held; in one, made where it
// is put, its type kept.
skiff::compose::Held side_column_held(const ui_needs& needs);
template <bool Erase = skiff::scene::kErasedCombinators>
auto side_column_of(const auto& needs) {
  if constexpr (Erase)
    return side_column_held(needs);
  else
    return side_column(needs);
}
} // namespace mux::ui

