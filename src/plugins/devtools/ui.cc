// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.devtools.ui -- The developer tools, as Element's: View Source
// in a message's menu; a room's settings' tab, Developer Tools -- the tools
// turned on, the room's ID, and the ways to its state, its account data and
// an event composed; and the dialog they are shown in: some JSON to read
// and copy, the state by type and key, the account data, an editor for
// either, an event of any type composed.
module;
#include "../../inline_in_release.h"
#include "skiff/named.h"
export module mux.plugin.devtools.ui;

import std;
import splice;
import skia;
import skiff.scene;
import skiff.compose;
import skiff.model;
import skiff.nodes.box;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.nodes.icon;
import skiff.widgets.button;
import skiff.widgets.sliderbar;
import skiff.widgets.textbox;
import skiff.widgets.textarea;
import skiff.widgets.motion;
import skiff.widgets.model;
import mux.composition;
import mux.core;
import mux.protocols;
import mux.ui.base;
import mux.plugin.devtools.types;
import mux.plugin.room_settings.types;
import mux.plugin.room_settings.ui;

namespace mux::plugins::devtools::detail {
using namespace ::mux::ui;

// ---- what each screen keeps of its own -------------------------------------------
// Copied: the reading pane's text, what is selected in it or all of it --
// said in the title a moment.
struct text_copied {};
// The state explored: a type shown, and a key of it -- its event.
struct said {
  std::uint64_t copied = 0;
  std::optional<std::string> type, key;
  friend bool operator==(const said&, const said&) = default;
};
struct state_picked {
  std::string type;
  std::optional<std::string> key;
};
struct state_back {};
struct said_events {
  auto put(said next) const { return skiff::model::over<said>(skiff::model::setTo(std::move(next))); }
  auto on(text_copied, const said& now) const {
    auto next = now;
    ++next.copied;
    return put(std::move(next));
  }
  auto on(const state_picked& one, const said& now) const {
    auto next = now;
    next.type = one.type;
    next.key = one.key;
    return put(std::move(next));
  }
  auto on(state_back, const said& now) const {
    auto next = now;
    if (next.key)
      next.key.reset();
    else
      next.type.reset();
    return put(std::move(next));
  }
};

// ---- the pieces ---------------------------------------------------------------------
// What a right press on the reading pane copies.
struct copy_reading {
  std::optional<std::tuple<request::copy_text, text_copied>> operator()(auto& reading, float, float) const {
    auto& text = std::get<0>(reading.fChildren);
    return std::tuple{request::copy_text{text.hasSelection() ? text.selected() : text.text()}, text_copied{}};
  }
};
// Some JSON to read: selectable, scrolling, copied by a right press.
MUX_INLINE_IN_RELEASE auto reading_of(const palette& colours, std::string text) {
  namespace c = skiff::compose;
  auto shown_text = wrapped(nodes::Text(std::move(text), 13.0f, colours.text));
  shown_text.setMonospace(true);
  shown_text.setSelectable(true);
  return SKIFF_NAMED(devtools_reading, c::onRightPressAt(copy_reading{},
      c::styled({.fillX = true, .grow = scene::axes::kY},
                c::scrolled(c::styled({.fillX = true, .margin = {6.0f, 16.0f, 12.0f, 16.0f}}, std::move(shown_text))))));
}
// A screen's head: back where there is somewhere to go back to, the title
// -- "Copied" over it a moment -- and close.
template <class Back>
auto head(const palette& colours, std::string title, Back back) {
  namespace c = skiff::compose;
  const scene::Spec middle{.alignSelf = scene::align::kMiddle};
  return SKIFF_NAMED(devtools_head, c::row(c::hbox(8.0f, {.fillX = true, .height = kPageHeaderHeight, .padding = {0.0f, 10.0f, 0.0f, 10.0f}}),
      std::move(back),
      c::column(c::vbox(0.0f, {.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}),
                elided(nodes::Text(std::move(title), 16.0f, colours.text, true)),
                c::fading_for<said>([](const said& now) { return now.copied; }, 1200.0, nodes::Text("Copied", 12.0f, colours.accent))),
      c::styled(middle, icon_button(colours, icon::close{}, sends<request::close_shown<shown>>{}))));
}
MUX_INLINE_IN_RELEASE auto no_back() { return skiff::compose::visible(false, nodes::Box<>(skia::SkColor{0})); }
template <class Asked>
auto back_to(const palette& colours, Asked asked) {
  return skiff::compose::styled({.alignSelf = scene::align::kMiddle}, icon_button(colours, icon::back{}, sends<Asked>{std::move(asked)}));
}
// A screen in the dialog, as tall as the others.
template <class... Parts>
auto screen_column(Parts... parts) {
  namespace c = skiff::compose;
  return c::local<said>(said_events{}, c::column(c::vbox(0.0f, {.fillX = true, .height = 560.0f}), std::move(parts)...));
}
// A Save or Send button, asking what the draft makes while it has a type.
template <class Draft, class Make>
auto asking_button(const palette& colours, std::string label, Make make) {
  namespace c = skiff::compose;
  return c::spec_for<Draft>([](const Draft& now) {
    return scene::Spec{.width = 120.0f, .height = 34.0f, .margin = {0.0f, 10.0f, 0.0f, 10.0f}, .disabled = now.type.empty()};
  },
      c::asking_for<Draft>(std::move(make), c::primary(widgets::Button<scene::NoAction>(colours.widgets, std::move(label), {}))));
}
// A content to write, as JSON.
template <auto Field>
auto content_area(const palette& colours) {
  namespace c = skiff::compose;
  return c::bound<skiff::model::Field<Field>>(c::styled(
      {.fillX = true, .height = 220.0f, .margin = {0.0f, 10.0f, 0.0f, 10.0f}, .cornerRadius = 6.0f, .background = colours.tile,
       .border = scene::Border{colours.band, 1.0f}},
      widgets::TextAreaField<std::string>(colours.widgets, "{}", 12)));
}
MUX_INLINE_IN_RELEASE auto caption(const palette& colours, std::string text) {
  return skiff::compose::styled({.margin = {0.0f, 10.0f, 0.0f, 10.0f}}, nodes::Text(std::move(text), 13.0f, colours.dim));
}

// ---- the screens --------------------------------------------------------------------
// Some JSON to read: a message's source, what a write came to.
MUX_INLINE_IN_RELEASE auto reading_screen(const palette& colours, const screen::reading& facts) {
  return screen_column(head(colours, facts.title, no_back()), reading_of(colours, facts.text));
}
// The room's state: every type and how many events of it; a type's keys;
// an event's JSON, and Edit -- back up a level at a time. A new one from
// the top.
MUX_INLINE_IN_RELEASE auto state_screen(const palette& colours, const screen::state& facts) {
  namespace c = skiff::compose;
  std::map<std::string, std::vector<state_entry>> by_type;
  for (const state_entry& one : facts.entries)
    by_type[one.type].push_back(one);
  auto types = by_type | std::views::transform([&](const auto& group) {
                 return row_item(colours, group.first + "  (" + std::to_string(group.second.size()) + ")",
                                 sends<state_picked>{{group.first, std::nullopt}});
               }) | std::ranges::to<std::vector>();
  using keys_t = decltype(c::many(c::vbox(), std::vector<row_item_t<sends<state_picked>>>{}));
  using event_t = decltype(reading_of(colours, std::string()));
  return screen_column(
      head(colours, "Room state",
           c::shown_for<said>([](const said& now) { return now.type.has_value(); },
               c::styled({.alignSelf = scene::align::kMiddle}, icon_button(colours, icon::back{}, sends<state_back>{})))),
      c::shown_for<said>([](const said& now) { return !now.type; },
          c::styled({.fillX = true, .grow = scene::axes::kY},
                    skiff::compose::scrolled(c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(types))))),
      c::shown_for<said>([](const said& now) { return !now.type; },
          c::styled({.width = 200.0f, .height = 34.0f, .margin = {8.0f, 10.0f, 12.0f, 10.0f}},
                    widgets::Button<sends<edit_state>>(colours.widgets, "New state event", {{std::string(), std::string()}}))),
      // A type's keys, made as it is chosen.
      c::projected<said>([](const said& now) { return now.type && !now.key ? now.type : std::nullopt; },
          c::mount<keys_t, std::string>([&colours, by_type](const std::string& type) {
            const auto found = by_type.find(type);
            // Named: a conditional's temporary is no view to pipe.
            const std::vector<state_entry> entries = found == by_type.end() ? std::vector<state_entry>{} : found->second;
            return std::tuple{c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                entries | std::views::transform([&](const state_entry& one) {
                      return row_item(colours, one.key.empty() ? std::string("(empty key)") : one.key, sends<state_picked>{{one.type, one.key}});
                    }) | std::ranges::to<std::vector>())};
          },
              {.fillX = true, .grow = scene::axes::kY})),
      // An event's JSON, made as it is chosen; and Edit.
      c::projected<said>([](const said& now) {
        return now.type && now.key ? std::optional(std::pair{*now.type, *now.key}) : std::nullopt;
      },
          c::mount<event_t, std::pair<std::string, std::string>>([&colours, entries = facts.entries](const std::pair<std::string, std::string>& at) {
            const auto found = std::ranges::find_if(entries, [&](const state_entry& one) { return one.type == at.first && one.key == at.second; });
            return std::tuple{reading_of(colours, found == entries.end() ? std::string() : found->json)};
          },
              {.fillX = true, .grow = scene::axes::kY})),
      c::shown_for<said>([](const said& now) { return now.type && now.key; },
          c::styled({.width = 120.0f, .height = 34.0f, .margin = {8.0f, 10.0f, 12.0f, 10.0f}},
              c::asking_for<said>([](const said& now) { return edit_state{now.type.value_or(""), now.key.value_or("")}; },
                  c::primary(widgets::Button<scene::NoAction>(colours.widgets, "Edit", {}))))));
}
// The room's account data: each type, opened to edit; a new one.
MUX_INLINE_IN_RELEASE auto data_screen(const palette& colours, const screen::data& facts) {
  namespace c = skiff::compose;
  auto rows = facts.entries | std::views::transform([&](const data_entry& one) {
                return row_item(colours, one.type, sends<edit_data>{{one.type}});
              }) | std::ranges::to<std::vector>();
  return screen_column(
      head(colours, "Account data", no_back()),
      c::styled({.fillX = true, .grow = scene::axes::kY},
                skiff::compose::scrolled(c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(rows)))),
      c::styled({.width = 200.0f, .height = 34.0f, .margin = {8.0f, 10.0f, 12.0f, 10.0f}},
                widgets::Button<sends<edit_data>>(colours.widgets, "New account data", {{std::string()}})));
}
// A state event written: a new one's type and key typed, an edited one's
// said; its content.
struct state_draft {
  std::string type, key, content;
  friend bool operator==(const state_draft&, const state_draft&) = default;
};
MUX_INLINE_IN_RELEASE auto state_editor_screen(const palette& colours, const screen::state_editor& facts) {
  namespace c = skiff::compose;
  using draft = state_draft;
  return screen_column(
      head(colours, facts.fresh ? std::string("New state event") : facts.type + (facts.key.empty() ? std::string() : "  \"" + facts.key + "\""),
           back_to(colours, open_state{})),
      c::local<draft>(c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 12.0f, 12.0f, 12.0f}}),
          c::visible(facts.fresh, model_field<&draft::type>(colours, "Event type", "m.room.topic")),
          c::visible(facts.fresh, model_field<&draft::key>(colours, "State key (may be empty)", "")),
          caption(colours, "Content (a JSON object)"),
          content_area<&draft::content>(colours),
          asking_button<draft>(colours, "Save", [](const draft& now) { return save_state{now.type, now.key, now.content}; })),
          draft{facts.type, facts.key, facts.content.empty() ? std::string("{}") : facts.content}));
}
// The room's account data written: a new one's type typed; its content.
struct data_draft {
  std::string type, content;
  friend bool operator==(const data_draft&, const data_draft&) = default;
};
MUX_INLINE_IN_RELEASE auto data_editor_screen(const palette& colours, const screen::data_editor& facts) {
  namespace c = skiff::compose;
  using draft = data_draft;
  return screen_column(
      head(colours, facts.fresh ? std::string("New account data") : facts.type, back_to(colours, open_data{})),
      c::local<draft>(c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 12.0f, 12.0f, 12.0f}}),
          c::visible(facts.fresh, model_field<&draft::type>(colours, "Type", "org.example.data")),
          caption(colours, "Content (a JSON object)"),
          content_area<&draft::content>(colours),
          asking_button<draft>(colours, "Save", [](const draft& now) { return save_data{now.type, now.content}; })),
          draft{facts.type, facts.content.empty() ? std::string("{}") : facts.content}));
}
// An event of any type sent: its type, a state key for a state event, its
// content as JSON; Send while it has a type.
struct composed {
  std::string type, key, content = "{\n  \n}";
  friend bool operator==(const composed&, const composed&) = default;
};
MUX_INLINE_IN_RELEASE auto composer_screen(const palette& colours, const screen::composer&) {
  namespace c = skiff::compose;
  return screen_column(
      head(colours, "Send custom event", no_back()),
      c::local<composed>(c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 12.0f, 12.0f, 12.0f}}),
          model_field<&composed::type>(colours, "Event type", "m.room.message"),
          model_field<&composed::key>(colours, "State key (for a state event; empty for a timeline one)", ""),
          caption(colours, "Content (a JSON object)"),
          content_area<&composed::content>(colours),
          asking_button<composed>(colours, "Send", [](const composed& now) {
            return send_event{now.type, now.key.empty() ? std::nullopt : std::optional(now.key), now.content};
          }))));
}

// Which screen is open, and what it shows: the one open's facts, the
// others' empty -- each made, only the one open seen.
template <class Screen>
struct is_screen {
  bool operator()(const Screen&) const { return true; }
  bool operator()(const auto&) const { return false; }
};
template <class Screen>
struct screen_facts {
  Screen operator()(const Screen& one) const { return one; }
  Screen operator()(const auto&) const { return {}; }
};
template <class Screen, class Make>
auto screen_if(const shown& facts, Make make) {
  return skiff::compose::visible(spl::visit(is_screen<Screen>{}, facts.screen), make(spl::visit(screen_facts<Screen>{}, facts.screen)));
}

}  // namespace mux::plugins::devtools::detail

export namespace mux::plugins::devtools {
using namespace ::mux::ui;

// View Source, in a message's menu, where its account can tell it: the
// message as the server has it.
MUX_INLINE_IN_RELEASE auto extend(contract::message_menu, type_tag<feature>, const palette& colours, const emoji_kept&, const menu_facts& facts) {
  return skiff::compose::visible(facts.can.view_source && !facts.link.empty(),
      row_item(colours, "View Source", sends<view_source>{view_source{facts.id}}, icon::info{}, std::nullopt, menu_row_look));
}

// A room's settings' tab, where its protocol has the tools: turned on or
// off, kept; on, the room's ID copied, and the ways to its state, its
// account data and an event composed.
constexpr std::string_view tab_title(room_tab) { return "Developer Tools"; }
inline icon_t tab_icon(room_tab) { return icon::terminal{}; }
MUX_INLINE_IN_RELEASE auto developer_switch(const palette& colours) {
  namespace c = skiff::compose;
  using toggle_t = widgets::Toggle<sends<flip_tools>>;
  return SKIFF_NAMED(developer_switch, c::applied<tab_facts>([](const tab_facts& now) { return now.tools; },
      [](toggle_t& toggle, bool on) { toggle.setOnNow(on); },
      c::styled({.relativeSize = scene::axes::kNone, .alignSelf = scene::align::kMiddle}, toggle_t(colours.widgets, {}))));
}
// ---- the tab's lists, as Cinny's: the state by type, each unfolded to its
// keys; the account data by type -- each opened to write where pressed --------
enum class state_row_kind { add, type, key };
template <class Act>
auto state_list_row(const palette& colours, std::string label, std::string detail, Act act, state_row_kind kind = state_row_kind::type,
                    bool expanded = false) {
  namespace c = skiff::compose;
  auto spoken = label;
  return SKIFF_NAMED(state_list_row, c::onPress(std::move(act), c::row(c::hbox(8.0f, {.fillX = true, .height = 36.0f, .padding = {4.0f, 8.0f, 4.0f, 8.0f},
                                                          .cornerRadius = colours.widgets.fCorner, .hoverBackground = colours.chosen}),
      c::visible(kind != state_row_kind::key,
          c::styled({.width = 12.0f, .height = 12.0f, .alignSelf = scene::align::kMiddle},
                    nodes::Icon(shape_of(kind == state_row_kind::add ? icon_t{icon::plus{}} : expanded ? icon_t{icon::down{}} : icon_t{icon::right{}}),
                                colours.dim))),
      c::styled({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX, .shrink = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                elided(nodes::Text(std::move(label), 12.0f, colours.text))),
      c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(std::move(detail), 12.0f, colours.dim)),
      c::visible(kind == state_row_kind::key,
          c::styled({.width = 12.0f, .height = 12.0f, .alignSelf = scene::align::kMiddle}, nodes::Icon(shape_of(icon::right{}), colours.dim)))),
      std::move(spoken)));
}
// The state events, by their types: each type's keys, the one unfolded.
struct state_group {
  std::string type;
  std::vector<std::string> keys;
  friend bool operator==(const state_group&, const state_group&) = default;
};
struct state_list_key {
  std::vector<state_group> groups;
  std::string unfolded;
  friend bool operator==(const state_list_key&, const state_list_key&) = default;
};
[[nodiscard]] inline state_list_key state_list_of(const tab_facts& now) {
  std::map<std::string, std::vector<std::string>> groups;
  for (const state_entry& event : now.state)
    groups[event.type].push_back(event.key);
  return {groups | std::views::transform([](auto& group) {
            std::ranges::sort(group.second);
            return state_group{group.first, std::move(group.second)};
          }) | std::ranges::to<std::vector>(),
          now.unfolded};
}
MUX_INLINE_IN_RELEASE auto state_event_list(const palette& colours, const state_list_key& key) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(state_event_list, c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      setting_tile(colours, "Events", "", nodes::Text(std::format("Total: {}", key.groups.size()), 12.0f, colours.dim)),
      cutout(colours, {}, state_list_row(colours, "Add New", "", sends<open_state_event>{{"", std::nullopt}}, state_row_kind::add),
          c::many(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}), key.groups | std::views::transform([&](const state_group& group) {
            const bool expanded = key.unfolded == group.type;
            return c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                state_list_row(colours, group.type, std::to_string(group.keys.size()), sends<unfold_type>{{group.type}}, state_row_kind::type, expanded),
                c::visible(expanded, c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY, .margin = {0.0f, 0.0f, 0.0f, 16.0f},
                                                              .padding = {0.0f, 0.0f, 0.0f, 8.0f}, .border = scene::Border{colours.band, 1.0f}}),
                    state_list_row(colours, "Add New", "", sends<open_state_event>{{group.type, std::nullopt}}, state_row_kind::add),
                    c::many(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}), group.keys | std::views::transform([&](const std::string& one) {
                      return state_list_row(colours, one.empty() ? "Default" : "\"" + one + "\"", "", sends<open_state_event>{{group.type, one}},
                                            state_row_kind::key);
                    }) | std::ranges::to<std::vector>()))));
          }) | std::ranges::to<std::vector>()))));
}
using state_event_list_t = decltype(state_event_list(std::declval<const palette&>(), state_list_key{}));
// The account data, by its types.
struct data_list_key {
  std::vector<std::string> types;
  friend bool operator==(const data_list_key&, const data_list_key&) = default;
};
[[nodiscard]] inline data_list_key data_list_of(const tab_facts& now) {
  return {now.data | std::views::transform(&data_entry::type) | std::ranges::to<std::vector>()};
}
MUX_INLINE_IN_RELEASE auto data_list(const palette& colours, const data_list_key& key) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(data_list, c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      action(colours, "Add New", sends<open_data_entry>{{std::nullopt}}),
      cutout(colours, {}, c::many(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}), key.types | std::views::transform([&](const std::string& type) {
        return state_list_row(colours, type, "", sends<open_data_entry>{{type}}, state_row_kind::key);
      }) | std::ranges::to<std::vector>()))));
}
using data_list_t = decltype(data_list(std::declval<const palette&>(), data_list_key{}));
// A list shown as the facts say, made again where what it lists moves.
template <class Row, class Key, class Of, class Make>
auto listed_in_tab(Of of, Make make) {
  namespace c = skiff::compose;
  return c::projected<tab_facts>([of](const tab_facts& now) { return std::optional<Key>(of(now)); },
                                 c::mount<Row, Key>([make](const Key& key) { return std::tuple{make(key)}; }, {.fillX = true, .autoSize = scene::axes::kY}));
}
// Expand, or Collapse where it is open: pressed, it opens or shuts.
template <class Flip>
auto unfold_button(const palette& colours, std::string spoken, bool tab_facts::*open) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(unfold_button, c::onPress(sends<Flip>{},
      c::row(c::hbox(0.0f, {.height = 32.0f, .relativeSize = scene::axes::kNone, .autoSize = scene::axes::kX, .alignSelf = scene::align::kMiddle,
                            .padding = {0.0f, 12.0f, 0.0f, 12.0f}, .cornerRadius = colours.widgets.fCorner, .background = colours.chosen,
                            .hoverBackground = colours.widgets.fSurfaceHover, .border = scene::Border{colours.widgets.fSurfaceActive, 1.0f}}),
             c::text_for<tab_facts>([open](const tab_facts& now) { return std::string(now.*open ? "Collapse" : "Expand"); },
                                    c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text("", 12.0f, colours.text)))),
      std::move(spoken)));
}

// The tab as Cinny's, as it was before the plugins: Options -- the switch,
// the room's ID copied -- and, with the tools on, Data -- an event
// composed; the room's state and its account data, each unfolded in place.
MUX_INLINE_IN_RELEASE auto make_manage_page(room_tab, const palette& colours, const auto& facts) {
  namespace c = skiff::compose;
  const auto tools_on = [](const tab_facts& now) { return now.tools; };
  return SKIFF_NAMED(devtools_tab_page, c::column(page_look(),
      section(colours, "Options",
          tile(colours, "Enable Developer Tools", "", developer_switch(colours)),
          c::shown_for<tab_facts>(tools_on, tile(colours, "Room ID", "Copy room ID to clipboard. (\"" + facts.id + "\")",
                                                 action(colours, "Copy", sends<request::copy_text>{{facts.id}})))),
      c::shown_for<tab_facts>(tools_on, section(colours, "Data",
          tile(colours, "New Message Event", "Create and send a new message event within the room.",
               action(colours, "Compose", sends<open_composer>{})),
          card(colours,
              setting_tile(colours, "Room State", "State events of the room.",
                           unfold_button<flip_state_list>(colours, "Room state", &tab_facts::state_open)),
              c::shown_for<tab_facts>([](const tab_facts& now) { return now.state_open; },
                  listed_in_tab<state_event_list_t, state_list_key>(state_list_of, [at = &colours](const state_list_key& key) {
                    return state_event_list(*at, key);
                  }))),
          card(colours,
              setting_tile(colours, "Account Data", "Private personalization data stored within room.",
                           unfold_button<flip_data_list>(colours, "Account data", &tab_facts::data_open)),
              c::shown_for<tab_facts>([](const tab_facts& now) { return now.data_open; },
                  listed_in_tab<data_list_t, data_list_key>(data_list_of, [at = &colours](const data_list_key& key) {
                    return data_list(*at, key);
                  })))))));
}

// The dialog: the screen open, made again as what it shows moves.
MUX_INLINE_IN_RELEASE auto devtools_box(const ui_needs& n, const shown& facts) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  return SKIFF_NAMED(devtools_box, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      detail::screen_if<screen::reading>(facts, [&](const screen::reading& one) { return detail::reading_screen(colours, one); }),
      detail::screen_if<screen::state>(facts, [&](const screen::state& one) { return detail::state_screen(colours, one); }),
      detail::screen_if<screen::data>(facts, [&](const screen::data& one) { return detail::data_screen(colours, one); }),
      detail::screen_if<screen::state_editor>(facts, [&](const screen::state_editor& one) { return detail::state_editor_screen(colours, one); }),
      detail::screen_if<screen::data_editor>(facts, [&](const screen::data_editor& one) { return detail::data_editor_screen(colours, one); }),
      detail::screen_if<screen::composer>(facts, [&](const screen::composer& one) { return detail::composer_screen(colours, one); })));
}
using box = decltype(devtools_box(std::declval<const ui_needs&>(), std::declval<const shown&>()));
inline dialog_look content_look(std::type_identity<shown>) { return {}; }
MUX_INLINE_IN_RELEASE auto make_content(const ui_needs& n, const shown& facts) { return devtools_box(n, facts); }

// Its dialog, among the window's (mux.ui:window).
constexpr type_tag<box> extend(contract::window_dialogs, type_tag<feature>) { return {}; }

}  // namespace mux::plugins::devtools
