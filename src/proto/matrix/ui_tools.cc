// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui.proto.matrix:tools -- The developer tools.
export module mux.ui.proto.matrix:tools;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.nodes.flow;
import skiff.nodes.text;
import skiff.nodes.scroll;
import skiff.widgets.button;
import skiff.widgets.motion;
import skiff.widgets.sliderbar;
import skiff.widgets.textarea;
import skiff.widgets.textbox;
import skiff.widgets.model;
import skiff.model;
import mux.core;
import mux.config;
import mux.proto.kept;
import mux.proto.matrix;
import mux.proto.matrix.requests;
import mux.ui.base;

// ---- the developer tools ----------------------------------------------------------
namespace mux::proto::matrix::tools_detail {
using namespace ::mux::ui;

// Copied: the reading pane's text, what is selected in it or all of it --
// said in the title a moment.
struct text_copied {};
struct devtools_said {
  std::uint64_t copied = 0;
  // The state: a type shown, and a key of it -- its event.
  std::optional<std::string> type, key;
  friend bool operator==(const devtools_said&, const devtools_said&) = default;
};
struct state_picked {
  std::string type;
  std::optional<std::string> key;
};
struct state_back {};
struct devtools_events {
  auto put(devtools_said next) const { return skiff::model::over<devtools_said>(skiff::model::setTo(std::move(next))); }
  auto on(text_copied, const devtools_said& now) const {
    auto next = now;
    ++next.copied;
    return put(std::move(next));
  }
  auto on(const state_picked& one, const devtools_said& now) const {
    auto next = now;
    next.type = one.type;
    next.key = one.key;
    return put(std::move(next));
  }
  auto on(state_back, const devtools_said& now) const {
    auto next = now;
    if (next.key)
      next.key.reset();
    else
      next.type.reset();
    return put(std::move(next));
  }
};
// What a right press on the reading pane copies.
struct copy_reading {
  std::optional<std::tuple<::mux::ui::request::copy_text, text_copied>> operator()(auto& reading, float, float) const {
    auto& text = std::get<0>(reading.fChildren);
    return std::tuple{::mux::ui::request::copy_text{text.hasSelection() ? text.selected() : text.text()}, text_copied{}};
  }
};
// Some JSON to read: selectable, scrolling, copied by a right press.
inline auto reading_of(const palette& colours, std::string text) {
  namespace c = skiff::compose;
  auto shown = wrapped(nodes::Text(std::move(text), 13.0f, colours.text));
  shown.setSelectable(true);
  return c::onRightPressAt(copy_reading{},
      c::styled({.fillX = true, .grow = scene::axes::kY},
                nodes::ScrollContainer<nodes::Text>(c::styled({.fillX = true, .margin = {6.0f, 16.0f, 12.0f, 16.0f}}, std::move(shown)))));
}
// The tools' head: back where there is somewhere to go back to, the title
// -- "Copied" over it a moment -- and close.
template <class Facts, class Back>
auto devtools_head(const palette& colours, std::string title, Back back) {
  namespace c = skiff::compose;
  const scene::Spec middle{.alignSelf = scene::align::kMiddle};
  return c::row(c::hbox(8.0f, {.fillX = true, .height = kPageHeaderHeight, .padding = {0.0f, 10.0f, 0.0f, 10.0f}}),
      std::move(back),
      c::column(c::vbox(0.0f, {.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}),
                elided(nodes::Text(std::move(title), 16.0f, colours.text, true)),
                c::fading_for<devtools_said>([](const devtools_said& now) { return now.copied; }, 1200.0,
                    nodes::Text("Copied", 12.0f, colours.accent))),
      c::styled(middle, icon_button(colours, icon::close{}, sends<request::close_tool<Facts>>{})));
}
// The developer tools reading some JSON: copied by a right press on it.
inline auto devtools_text_view(const palette& colours, const tool::devtools_text& facts) {
  namespace c = skiff::compose;
  return c::local<devtools_said>(devtools_events{},
      c::column(c::vbox(0.0f, {.fillX = true, .height = 560.0f}),
                devtools_head<tool::devtools_text>(colours, facts.title, c::visible(false, nodes::Box<>(skia::SkColor{0}))),
                reading_of(colours, facts.text)));
}
// The developer tools over a room's state: every type and how many events
// of it; a type's keys; an event's JSON -- back up a level at a time.
inline auto devtools_state_view(const palette& colours, const tool::devtools_state& facts) {
  namespace c = skiff::compose;
  std::map<std::string, std::vector<tool::devtools_entry>> by_type;
  for (const tool::devtools_entry& one : facts.entries)
    by_type[one.type].push_back(one);
  // Joined, not formatted: clang 23 crashed on such format strings made
  // first in this module (see app/network.cc).
  auto types = by_type | std::views::transform([&](const auto& group) {
                 return row_item(colours, group.first + "  (" + std::to_string(group.second.size()) + ")",
                                 sends<state_picked>{{group.first, std::nullopt}});
               }) | std::ranges::to<std::vector>();
  using keys_t = decltype(c::many(c::vbox(), std::vector<row_item_t<sends<state_picked>>>{}));
  using event_t = decltype(reading_of(colours, std::string()));
  return c::local<devtools_said>(devtools_events{},
      c::column(c::vbox(0.0f, {.fillX = true, .height = 560.0f}),
          devtools_head<tool::devtools_state>(colours, "Room state",
              c::shown_for<devtools_said>([](const devtools_said& now) { return now.type.has_value(); },
                  c::styled({.alignSelf = scene::align::kMiddle}, icon_button(colours, icon::back{}, sends<state_back>{})))),
          c::shown_for<devtools_said>([](const devtools_said& now) { return !now.type; },
              c::styled({.fillX = true, .grow = scene::axes::kY},
                        nodes::ScrollContainer(c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(types))))),
          // A type's keys, made as it is chosen.
          c::projected<devtools_said>([](const devtools_said& now) { return now.type && !now.key ? now.type : std::nullopt; },
              c::mount<keys_t, std::string>([&colours, by_type](const std::string& type) {
                const auto found = by_type.find(type);
                // Named: a conditional's temporary is no view to pipe.
                const std::vector<tool::devtools_entry> entries = found == by_type.end() ? std::vector<tool::devtools_entry>{} : found->second;
                return std::tuple{c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                    entries | std::views::transform([&](const tool::devtools_entry& one) {
                          return row_item(colours, one.key.empty() ? std::string("(empty key)") : one.key, sends<state_picked>{{one.type, one.key}});
                        }) | std::ranges::to<std::vector>())};
              },
                  {.fillX = true, .grow = scene::axes::kY})),
          // An event's JSON, made as it is chosen.
          c::projected<devtools_said>([](const devtools_said& now) {
            return now.type && now.key ? std::optional(std::pair{*now.type, *now.key}) : std::nullopt;
          },
              c::mount<event_t, std::pair<std::string, std::string>>([&colours, entries = facts.entries](const std::pair<std::string, std::string>& at) {
                const auto found = std::ranges::find_if(entries, [&](const tool::devtools_entry& one) { return one.type == at.first && one.key == at.second; });
                return std::tuple{reading_of(colours, found == entries.end() ? std::string() : found->json)};
              },
                  {.fillX = true, .grow = scene::axes::kY}))));
}
// An event of any type sent: its type, a state key for a state event, its
// content as JSON; Send while it has a type.
struct custom_draft {
  std::string type, key, body = "{\n  \n}";
  friend bool operator==(const custom_draft&, const custom_draft&) = default;
};
inline auto send_custom_view(const palette& colours, const tool::send_custom_form&) {
  namespace c = skiff::compose;
  return c::local<custom_draft>(
      c::column(c::vbox(0.0f, {.fillX = true, .height = 560.0f}),
          devtools_head<tool::send_custom_form>(colours, "Send custom event", c::visible(false, nodes::Box<>(skia::SkColor{0}))),
          c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 12.0f, 12.0f, 12.0f}}),
              model_field<&custom_draft::type>(colours, "Event type", "m.room.message"),
              model_field<&custom_draft::key>(colours, "State key (for a state event; empty for a timeline one)", ""),
              c::styled({.margin = {0.0f, 10.0f, 0.0f, 10.0f}}, nodes::Text("Content (a JSON object)", 13.0f, colours.dim)),
              c::bound<skiff::model::Field<&custom_draft::body>>(c::styled(
                  {.fillX = true, .height = 180.0f, .margin = {0.0f, 10.0f, 0.0f, 10.0f}, .cornerRadius = 6.0f, .background = colours.tile,
                   .border = scene::Border{colours.band, 1.0f}},
                  widgets::TextAreaField<std::string>(colours.widgets, "{}", 12))),
              c::spec_for<custom_draft>([](const custom_draft& now) {
                return scene::Spec{.width = 120.0f, .height = 34.0f, .margin = {0.0f, 10.0f, 0.0f, 10.0f}, .disabled = now.type.empty()};
              },
                  c::asking_for<custom_draft>([](const custom_draft& now) {
                    return request::send_custom{now.type, now.key.empty() ? std::nullopt : std::optional(now.key), now.body};
                  },
                      primary(widgets::Button<scene::NoAction>(colours.widgets, "Send", {})))))));
}
// A step of interactive auth done in the browser, as Element's: what it is
// for, its page opened there again where it was closed, and Continue once
// it is done there -- or Cancel.
inline auto uia_view(const palette& colours, const tool::uia_step& facts) {
  namespace c = skiff::compose;
  return c::column(c::vbox(10.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {20.0f, 22.0f, 20.0f, 22.0f}}),
      nodes::Text(facts.what, 17.0f, colours.text, true),
      c::styled({.fillX = true},
                wrapped(nodes::Text("Your server asks you to confirm this in your browser: the page is open there. Once you have done what it asks, "
                                    "press Continue.",
                                    14.0f, colours.dim))),
      c::styled({.width = 200.0f, .height = 32.0f},
                widgets::Button<sends<::mux::ui::request::open_url>>(colours.widgets, "Open the page again", {{facts.url}})),
      dialog_buttons(colours, "Continue", sends<request::cancel_uia>{}, sends<request::continue_uia>{}, 120.0f));
}

}  // namespace mux::proto::matrix::tools_detail

// Each dialog's content, by what it shows -- found where the window makes
// its dialogs, by the facts' namespace.
export namespace mux::proto::matrix::tool {
inline auto dialog_content(const ui::ui_needs& n, const devtools_text& facts) { return tools_detail::devtools_text_view(*n.colours, facts); }
inline auto dialog_content(const ui::ui_needs& n, const devtools_state& facts) { return tools_detail::devtools_state_view(*n.colours, facts); }
inline auto dialog_content(const ui::ui_needs& n, const send_custom_form& facts) { return tools_detail::send_custom_view(*n.colours, facts); }
inline auto dialog_content(const ui::ui_needs& n, const uia_step& facts) { return tools_detail::uia_view(*n.colours, facts); }
template <class Content, class Needs, class Facts>
  requires requires(const Needs& n, const Facts& f) { dialog_content(n, f); }
auto make_content(std::type_identity<Content>, const Needs& n, const Facts& facts) {
  return dialog_content(n, facts);
}
template <class Content, class Facts>
  requires requires(const ui::ui_needs& n, const Facts& f) { dialog_content(n, f); }
ui::dialog_look content_look(std::type_identity<Content>, std::type_identity<Facts>) {
  return {};
}
}  // namespace mux::proto::matrix::tool
