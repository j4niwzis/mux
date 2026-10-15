// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui.proto.matrix:manage -- Cinny's room settings, expressed with the
// client's typed requests and skiff combinators. Reference: cinnyapp/cinny,
// 8967c13878137841e49ee17184b689a6b4da334a, features/room-settings and
// features/common-settings. The five pages keep the same responsibilities.
// Each reads the settings as the program says them (room_settings_facts,
// what Matrix keeps of them in its manage_state); a press asks the program
// (request::manage_room), which keeps the rules; what is typed is the
// page's own (a Local), made again where the program settles it.
export module mux.ui.proto.matrix:manage;

import std;
import splice;
import skia;
import skiff.scene;
import skiff.compose;
import skiff.model;
import skiff.nodes.flow;
import skiff.nodes.text;
import skiff.nodes.icon;
import skiff.widgets.dropdown;
import skiff.widgets.sliderbar;
import skiff.widgets.textarea;
import skiff.widgets.button;
import skiff.widgets.model;
import mux.core;
import mux.config;
import mux.proto.kept;
import mux.proto.matrix;
import mux.proto.matrix.requests;
import mux.proto.matrix.changes;
import mux.logic.text;
import mux.ui.base;

export namespace mux::proto::matrix::manage_detail {
using namespace ::mux::ui;

// ---- what the pages read --------------------------------------------------------
using facts_t = std::optional<room_settings_facts>;
[[nodiscard]] inline const room_settings_facts& facts_of(const facts_t& now) {
  static const room_settings_facts none;
  return now ? *now : none;
}
[[nodiscard]] inline const manage_state& kept_of(const facts_t& now) {
  static const manage_state none;
  return now ? spl::visit(spl::overloaded{[](const manage_state& kept) -> const manage_state& { return kept; },
                                          [](const auto&) -> const manage_state& { return none; }},
                          now->manage)
             : none;
}
[[nodiscard]] inline const room_rules& rules_now(const facts_t& now) { return rules_of(facts_of(now).theirs); }
// Whether one may do a thing now: as the levels allow, and no write on its way.
[[nodiscard]] inline bool editable(const facts_t& now, const power_need_t& need) {
  return !kept_of(now).pending && may(facts_of(now), need);
}
template <class Need>
struct may_now {
  bool operator()(const facts_t& now) const { return editable(now, Need{}); }
};
[[nodiscard]] inline std::optional<std::int64_t> integer(std::string_view text) {
  std::int64_t value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  return error == std::errc{} && end == text.data() + text.size() ? std::optional(value) : std::nullopt;
}
[[nodiscard]] inline std::string power_name(std::int64_t value, const room_management& data) {
  if (value == kCreatorPower) return "Creator";
  if (const auto tag = data.tags.find(std::to_string(value)); tag != data.tags.end()) return tag->second.name;
  static const std::map<std::int64_t, std::string_view, std::greater<>> names{{-1, "Muted"}, {0, "Member"}, {50, "Moderator"},
      {100, "Admin"}, {101, "Founder"}, {150, "Manager"}, {9001, "Goku"}};
  if (const auto name = names.find(value); name != names.end()) return std::string(name->second);
  const auto lower = std::ranges::find_if(names, [&](const auto& named) { return named.first < value; });
  return std::format("{} {}", lower == names.end() ? "Team" : lower->second, value);
}
[[nodiscard]] inline std::vector<std::int64_t> power_values(const room_rules& rules, const room_management& data) {
  const auto& n = rules.needs;
  std::set<std::int64_t, std::greater<>> values{n.users_default, n.events_default, n.state_default,
      n.invite, n.kick, n.ban, n.redact, n.notify_room};
  std::ranges::for_each(rules.powers, [&](const auto& entry) { if (entry.second != kCreatorPower) values.insert(entry.second); });
  std::ranges::for_each(n.events, [&](const auto& entry) { values.insert(entry.second); });
  std::ranges::for_each(data.tags, [&](const auto& entry) { if (const auto number = integer(entry.first); number && *number != kCreatorPower) values.insert(*number); });
  return values | std::ranges::to<std::vector<std::int64_t>>();
}
[[nodiscard]] inline std::int64_t required(const power_needs& needs, const permission_t& target) {
  return spl::visit(spl::overloaded{[&](const power_need_t& need) { return needs.of(need); },
      [&](const event_permission& event) {
        const auto found = needs.events.find(event.type);
        return found == needs.events.end() ? (event.state ? needs.state_default : needs.events_default) : found->second;
      }}, target);
}
[[nodiscard]] inline std::int64_t event_need(const room_rules& rules, std::string_view type) {
  const auto found = rules.needs.events.find(std::string(type));
  return found == rules.needs.events.end() ? rules.needs.state_default : found->second;
}
// What the room's tags are, with those staged.
[[nodiscard]] inline room_management staged(const manage_state& kept) {
  auto out = kept.data;
  if (kept.tag_draft)
    out.tags = *kept.tag_draft;
  return out;
}

// ---- what a press asks --------------------------------------------------------
template <class Change>
struct asks {
  using Answer = request::manage_room;
  Change change;
  Answer operator()() const { return {manage_change_t{change}}; }
};
template <class Change>
asks<Change> ask(Change change) { return {std::move(change)}; }

// ---- Folds 2.7.2, as Cinny's components -----------------------------------------
// S100=4, S200=8, S300=12, S400=16, S700=32.
inline skiff::compose::Look page_look() {
  return skiff::compose::vbox(32.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {16.0f, 16.0f, 100.0f, 16.0f}});
}
inline auto settings_section_heading(const palette& colours, std::string title) { return nodes::Text(std::move(title), 12.0f, colours.text); }
template <class... Children>
auto settings_surface(const palette& colours, skia::SkColor background, scene::Margin padding, float gap, Children... children) {
  return skiff::compose::column(skiff::compose::vbox(gap, {.fillX = true, .autoSize = scene::axes::kY, .padding = padding,
                                                           .cornerRadius = std::max(10.0f, colours.widgets.fCorner), .background = background,
                                                           .masking = true}),
                                std::move(children)...);
}
template <class... Children> auto card(const palette& colours, Children... children) {
  return settings_surface(colours, colours.tile, {12.0f, 12.0f, 12.0f, 12.0f}, 16.0f, std::move(children)...);
}
template <class... Children> auto cutout(const palette& colours, scene::Margin padding, Children... children) {
  return settings_surface(colours, colours.background, padding, 4.0f, std::move(children)...);
}
template <class... Children> auto section(const palette& colours, std::string title, Children... children) {
  return skiff::compose::column(skiff::compose::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                                settings_section_heading(colours, std::move(title)), std::move(children)...);
}
template <class Control> auto setting_tile(const palette& colours, std::string title, std::string description, Control control) {
  namespace c = skiff::compose;
  const bool described = !description.empty();
  return c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::column(c::vbox(4.0f, {.relativeSize = scene::axes::kNone, .autoSize = scene::axes::kY, .grow = scene::axes::kX}),
          wrapped(nodes::Text(std::move(title), 14.0f, colours.text)),
          c::visible(described, wrapped(nodes::Text(std::move(description), 12.0f, colours.dim)))),
      c::styled({.alignSelf = scene::align::kMiddle}, std::move(control)));
}
template <class Control> auto tile(const palette& colours, std::string title, std::string description, Control control) {
  return card(colours, setting_tile(colours, std::move(title), std::move(description), std::move(control)));
}
// A tile whose words move with the facts.
template <class Title, class Description, class Control>
auto live_tile(const palette& colours, Title title, Description description, Control control) {
  namespace c = skiff::compose;
  return card(colours, c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::column(c::vbox(4.0f, {.relativeSize = scene::axes::kNone, .autoSize = scene::axes::kY, .grow = scene::axes::kX}),
          c::text_for<facts_t>(std::move(title), wrapped(nodes::Text("", 14.0f, colours.text))),
          c::text_for<facts_t>(std::move(description), wrapped(nodes::Text("", 12.0f, colours.dim)))),
      c::styled({.alignSelf = scene::align::kMiddle}, std::move(control))));
}
enum class action_style { secondary, primary, success };
[[nodiscard]] inline scene::Spec allowed_spec(bool on) { return {.alpha = on ? 1.0f : 0.38f, .disabled = !on}; }
// A button's face: its words on its plate.
inline auto action_face(const palette& colours, std::string label, action_style style = action_style::secondary, bool allowed = true) {
  namespace c = skiff::compose;
  const auto background = style == action_style::secondary ? colours.chosen : colours.accent;
  const auto ink = style == action_style::secondary ? colours.text : colours.on_accent;
  return c::row(c::hbox(0.0f, {.height = 32.0f, .relativeSize = scene::axes::kNone, .autoSize = scene::axes::kX, .alignSelf = scene::align::kMiddle,
                               .padding = {0.0f, 12.0f, 0.0f, 12.0f}, .cornerRadius = colours.widgets.fCorner, .background = background,
                               .hoverBackground = colours.widgets.fSurfaceHover,
                               .border = scene::Border{colours.widgets.fSurfaceActive, style == action_style::secondary ? 1.0f : 0.0f},
                               .alpha = allowed ? 1.0f : 0.38f, .disabled = !allowed}),
                c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(std::move(label), 12.0f, ink)));
}
// A button: its words, and whether it does anything now, as said once or
// read as the facts move.
template <class Act>
auto action(const palette& colours, std::string label, Act act, bool allowed = true, action_style style = action_style::secondary) {
  auto spoken = label;
  return skiff::compose::onPress(std::move(act), action_face(colours, std::move(label), style, allowed), std::move(spoken));
}
// A button that sends what is typed in its page's draft (Draft) as `make`
// makes of it.
template <class Draft, class Make, class Allowed>
auto action_asking(const palette& colours, std::string label, Make make, Allowed allowed, action_style style = action_style::secondary) {
  namespace c = skiff::compose;
  return c::spec_for<facts_t>([allowed](const facts_t& now) { return allowed_spec(allowed(now)); },
      c::column(c::vbox(0.0f, {.relativeSize = scene::axes::kNone, .autoSize = scene::axes::kBoth, .alignSelf = scene::align::kMiddle}),
                c::asking_for<Draft>(std::move(make), action_face(colours, std::move(label), style))));
}
template <class Act, class Allowed>
auto action_if(const palette& colours, std::string label, Act act, Allowed allowed, action_style style = action_style::secondary) {
  return skiff::compose::spec_for<facts_t>([allowed](const facts_t& now) { return allowed_spec(allowed(now)); },
                                           action(colours, std::move(label), std::move(act), true, style));
}
// And one whose words move too.
template <class Act, class Label, class Allowed>
auto action_said(const palette& colours, Label label, Act act, Allowed allowed, action_style style = action_style::secondary) {
  namespace c = skiff::compose;
  const auto background = style == action_style::secondary ? colours.chosen : colours.accent;
  const auto ink = style == action_style::secondary ? colours.text : colours.on_accent;
  return c::spec_for<facts_t>([allowed](const facts_t& now) { return allowed_spec(allowed(now)); },
      c::onPress(std::move(act),
          c::row(c::hbox(0.0f, {.height = 32.0f, .relativeSize = scene::axes::kNone, .autoSize = scene::axes::kX, .alignSelf = scene::align::kMiddle,
                                .padding = {0.0f, 12.0f, 0.0f, 12.0f}, .cornerRadius = colours.widgets.fCorner, .background = background,
                                .hoverBackground = colours.widgets.fSurfaceHover,
                                .border = scene::Border{colours.widgets.fSurfaceActive, style == action_style::secondary ? 1.0f : 0.0f}}),
                 c::text_for<facts_t>(std::move(label), c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text("", 12.0f, ink))))));
}
// A switch: on as the facts say, pressed asking `act`.
template <class Act, class On, class Allowed>
auto switch_control(const palette& colours, On on, Act act, Allowed allowed) {
  namespace c = skiff::compose;
  using toggle_t = widgets::Toggle<Act>;
  return c::spec_for<facts_t>([allowed](const facts_t& now) { return scene::Spec{.disabled = !allowed(now)}; },
      c::column(c::vbox(0.0f, {.relativeSize = scene::axes::kNone, .autoSize = scene::axes::kBoth, .alignSelf = scene::align::kMiddle}),
          c::applied<facts_t>(std::move(on), [](toggle_t& toggle, bool now) { toggle.setOnNow(now); },
                              c::styled({.relativeSize = scene::axes::kNone, .alignSelf = scene::align::kMiddle}, toggle_t(colours.widgets, std::move(act))))));
}
inline auto choice_head(const palette& colours, std::string value) {
  namespace c = skiff::compose;
  return c::row(c::hbox(4.0f, {.fillX = true, .height = 24.0f, .padding = {0.0f, 8.0f, 0.0f, 8.0f},
      .cornerRadius = colours.widgets.fCorner, .background = colours.chosen, .hoverBackground = colours.widgets.fSurfaceHover}),
      c::styled({.width = 12.0f, .height = 12.0f, .alignSelf = scene::align::kMiddle}, nodes::Icon(shape_of(icon::down{}), colours.text)),
      c::styled({.shrink = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(nodes::Text(std::move(value), 12.0f, colours.text))));
}
template <class Choose>
auto compact_choice(const palette& colours, std::vector<std::string> names, std::size_t selected, Choose choose) {
  const auto value = selected < names.size() ? names[selected] : std::string();
  const auto width = std::clamp(static_cast<float>(value.size()) * 6.5f + 32.0f, 72.0f, 180.0f);
  auto head = choice_head(colours, value);
  auto rows = names | std::views::transform([&](const auto& name) { return choice_menu_option(colours, name); }) | std::ranges::to<std::vector>();
  auto menu = widgets::ChoiceMenu<Choose, decltype(head), choice_menu_option_t, show_choice_menu>(
      std::move(names), selected, std::move(choose), std::move(head), std::move(rows), show_choice_menu{colours.text, colours.accent});
  menu.apply({.width = width, .relativeSize = scene::axes::kNone, .alignSelf = scene::align::kMiddle});
  return menu;
}
// A compact choice whose names and choice move with the facts: made again
// where they do -- Key says its names, the one chosen, whether it does
// anything now, and what its choices ask -- greyed where it does nothing.
template <class Key, class Now, class Of>
auto live_choice(const palette& colours, Now now, Of choose_of) {
  namespace c = skiff::compose;
  using choose_t = std::invoke_result_t<Of&, const Key&>;
  using menu_t = decltype(compact_choice(colours, {}, 0, std::declval<choose_t>()));
  return c::projected<facts_t>([now](const facts_t& facts) { return facts ? std::optional<Key>(now(facts)) : std::nullopt; },
      c::mount<menu_t, Key>([at = &colours, choose_of](const Key& shown) {
        auto menu = compact_choice(*at, shown.names, shown.selected, choose_of(shown));
        menu.apply(allowed_spec(shown.allowed));
        return std::tuple{std::move(menu)};
      },
          {.relativeSize = scene::axes::kNone, .autoSize = scene::axes::kBoth, .alignSelf = scene::align::kMiddle}));
}
// Rows made from what the facts say, again where that changes.
template <class Row, class Key, class Of, class Make>
auto rows_of(Of of, Make make, scene::Spec spec = {.fillX = true, .autoSize = scene::axes::kY}) {
  namespace c = skiff::compose;
  return c::projected<facts_t>([of](const facts_t& facts) { return facts ? std::optional<Key>(of(facts)) : std::nullopt; },
                               c::mount<Row, Key>([make](const Key& key) { return std::tuple{make(key)}; }, spec));
}
// A word under a page's head: saving, loading, or what went wrong.
template <class Said>
auto status_line(const palette& colours, Said said) {
  namespace c = skiff::compose;
  return c::shown_for<facts_t>([said](const facts_t& now) { return !said(now).empty(); },
                               c::text_for<facts_t>(said, wrapped(nodes::Text("", 12.0f, colours.dim))));
}

// ---- General: profile, options, addresses, advanced options, Cinny's order ------
struct profile_draft {
  std::string name, topic;
  friend bool operator==(const profile_draft&, const profile_draft&) = default;
};
// What the profile's editor is made from: again where it is opened or saved.
struct profile_basis {
  std::uint32_t round = 0;
  std::string name, topic;
  friend bool operator==(const profile_basis&, const profile_basis&) = default;
};
[[nodiscard]] inline bool may_profile(const facts_t& now) {
  return editable(now, power_need::rename{}) || editable(now, power_need::retopic{}) || editable(now, power_need::change_avatar{});
}
inline auto profile_editor(const palette& colours, const profile_basis& basis, const room_settings_facts& first) {
  namespace c = skiff::compose;
  using draft = profile_draft;
  return c::local<draft>(c::column(c::vbox(16.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::row(c::hbox(16.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          c::column(c::vbox(4.0f, {.relativeSize = scene::axes::kNone, .autoSize = scene::axes::kY, .grow = scene::axes::kX}),
              settings_section_heading(colours, "Avatar"),
              c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                  action_if(colours, "Upload", sends<::mux::ui::request::pick_room_avatar>{{first.chat}}, may_now<power_need::change_avatar>{}),
                  action_if(colours, "Remove", ask(manage_change::remove_avatar{}), may_now<power_need::change_avatar>{}))),
          avatar_mark(avatar_of::chat{}, first.id, basis.name, 96.0f)),
      c::shown_for<facts_t>([](const facts_t& now) { return kept_of(now).avatar.has_value(); },
          wrapped(nodes::Text("Avatar change pending. Save to apply.", 12.0f, colours.dim))),
      c::spec_for<facts_t>([](const facts_t& now) { return scene::Spec{.disabled = !editable(now, power_need::rename{})}; },
          c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), model_field<&draft::name>(colours, "Name", ""))),
      c::spec_for<facts_t>([](const facts_t& now) { return scene::Spec{.disabled = !editable(now, power_need::retopic{})}; },
          c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), model_area<&draft::topic>(colours, "Topic", "", 6))),
      c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          action_asking<draft>(colours, "Save", [](const draft& now) { return request::manage_room{manage_change::save_profile{now.name, now.topic}}; },
                               may_profile, action_style::success),
          action(colours, "Cancel", ask(manage_change::edit{editor::profile, false})))),
      draft{basis.name, basis.topic});
}
using profile_editor_t = decltype(profile_editor(std::declval<const palette&>(), profile_basis{}, std::declval<const room_settings_facts&>()));

// Who may join: as the room's version allows, the spaces it is in naming
// those whose members may.
[[nodiscard]] inline std::vector<std::pair<join_rule_t, std::string>> access_rules(std::int64_t version, const std::vector<std::string>& parents) {
  std::vector<std::pair<join_rule_t, std::string>> out{{join_rule::invite{}, "Invite Only"}};
  if (version >= 7) out.emplace_back(join_rule::knock{}, "Knock & Invite");
  if (version >= 8 && !parents.empty()) out.emplace_back(join_rule::restricted{parents}, "Space Members");
  if (version >= 10 && !parents.empty()) out.emplace_back(join_rule::knock_restricted{parents}, "Space Members or Knock");
  out.emplace_back(join_rule::open{}, "Public");
  return out;
}
struct access_key {
  std::vector<std::string> names;
  std::size_t selected = 0;
  bool allowed = false;
  std::int64_t version = 12;
  std::vector<std::string> parents;
  friend bool operator==(const access_key&, const access_key&) = default;
};
struct choose_access {
  using Answer = std::optional<request::manage_room>;
  std::vector<join_rule_t> rules;
  Answer operator()(std::size_t index) const {
    return index < rules.size() ? Answer(request::manage_room{manage_change::access{rules[index]}}) : std::nullopt;
  }
};
[[nodiscard]] inline access_key access_now(const facts_t& now) {
  const auto& facts = facts_of(now);
  const auto version = integer(rules_now(now).version).value_or(12);
  const auto parents = facts.parents | std::views::transform(&room_settings_facts::named_room::id) | std::ranges::to<std::vector>();
  const auto rules = access_rules(version, parents);
  const auto selected = std::ranges::find_if(rules, [&](const auto& rule) { return rule.first.index() == rules_now(now).join_rule.index(); });
  return {rules | std::views::values | std::ranges::to<std::vector<std::string>>(), static_cast<std::size_t>(selected - rules.begin()),
          editable(now, power_need::change_access{}), version, parents};
}
// A plain choice's: its names, the one chosen, whether it does anything.
struct choice_key {
  std::vector<std::string> names;
  std::size_t selected = 0;
  bool allowed = false;
  friend bool operator==(const choice_key&, const choice_key&) = default;
};
struct choose_history {
  using Answer = std::optional<request::manage_room>;
  Answer operator()(std::size_t index) const {
    const std::array<history_rule_t, 4> rules{history_rule::shared{}, history_rule::invited{}, history_rule::joined{}, history_rule::world_readable{}};
    return index < rules.size() ? Answer(request::manage_room{manage_change::history{rules[index]}}) : std::nullopt;
  }
};
inline auto address_badge(const palette& colours, std::string label) {
  return skiff::compose::row(skiff::compose::hbox(0.0f, {.relativeSize = scene::axes::kNone, .autoSize = scene::axes::kBoth, .alignSelf = scene::align::kMiddle,
                                                         .padding = {4.0f, 8.0f, 4.0f, 8.0f}, .cornerRadius = colours.widgets.fCorner, .background = colours.chosen}),
                             nodes::Text(std::move(label), 12.0f, colours.accent));
}
// The addresses published, the main one marked.
[[nodiscard]] inline std::vector<std::string> published_of(const room_settings_facts& facts) {
  auto out = facts.other_aliases;
  if (facts.alias && !std::ranges::contains(out, *facts.alias))
    out.insert(out.begin(), *facts.alias);
  return out;
}
struct published_key {
  std::vector<std::string> aliases;
  std::optional<std::string> main;
  bool may = false, pending = false;
  friend bool operator==(const published_key&, const published_key&) = default;
};
inline auto published_rows(const palette& colours, const published_key& key) {
  namespace c = skiff::compose;
  using act = manage_change::address;
  return c::many(c::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}), key.aliases | std::views::transform([&](const std::string& alias) {
    const bool main = key.main == alias;
    auto label = nodes::Text(alias, 14.0f, colours.text);
    label.setBold(main);
    return c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
        c::row(c::hbox(8.0f, {.relativeSize = scene::axes::kNone, .autoSize = scene::axes::kY, .grow = scene::axes::kX}),
            c::styled({.shrink = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(std::move(label))),
            c::visible(main, address_badge(colours, "Main"))),
        c::visible(key.may, action(colours, main ? "Unset Main" : "Set Main", ask(act{alias, manage_change::address_kind::main}), !key.pending)));
  }) | std::ranges::to<std::vector>());
}
using published_rows_t = decltype(published_rows(std::declval<const palette&>(), published_key{}));
struct local_key {
  std::vector<std::string> aliases, published, selected;
  bool pending = false;
  friend bool operator==(const local_key&, const local_key&) = default;
};
inline auto local_rows(const palette& colours, const local_key& key) {
  namespace c = skiff::compose;
  using check_t = widgets::internal::Toggle<asks<manage_change::select_address>, widgets::internal::CheckboxLook>;
  return c::many(c::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}), key.aliases | std::views::transform([&](const std::string& alias) {
    auto checkbox = check_t(colours.widgets, ask(manage_change::select_address{alias}));
    checkbox.setOnNow(std::ranges::contains(key.selected, alias));
    checkbox.apply({.relativeSize = scene::axes::kNone, .alignSelf = scene::align::kMiddle, .disabled = key.pending});
    return c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(checkbox),
        c::styled({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX, .shrink = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                  elided(nodes::Text(alias, 14.0f, colours.text))),
        c::visible(std::ranges::contains(key.published, alias), address_badge(colours, "Published")));
  }) | std::ranges::to<std::vector>());
}
using local_rows_t = decltype(local_rows(std::declval<const palette&>(), local_key{}));
struct address_draft {
  std::string local;
  friend bool operator==(const address_draft&, const address_draft&) = default;
};
inline auto local_address_input(const palette& colours, std::string server) {
  namespace c = skiff::compose;
  auto theme = colours.widgets;
  theme.fCorner = std::max(10.0f, theme.fCorner);
  theme.fSurface = colours.chosen;
  return c::row(c::hbox(4.0f, {.height = 44.0f, .relativeSize = scene::axes::kNone, .grow = scene::axes::kX,
      .padding = {0.0f, 8.0f, 0.0f, 8.0f}, .cornerRadius = theme.fCorner, .background = colours.chosen, .masking = true}),
      c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text("#", 12.0f, colours.dim)),
      c::bound<skiff::model::Field<&address_draft::local>>(c::styled({.height = 36.0f, .relativeSize = scene::axes::kNone,
          .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, widgets::TextField<std::string>(theme, "New address"))),
      c::styled({.shrink = scene::axes::kX, .maxWidth = 160.0f, .alignSelf = scene::align::kMiddle},
          elided(nodes::Text(std::move(server), 12.0f, colours.dim))));
}
// A new local address: its field emptied where one is made.
inline auto address_maker(const palette& colours, std::string server) {
  namespace c = skiff::compose;
  return c::local<address_draft>(c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::spec_for<facts_t>([](const facts_t& now) { return scene::Spec{.disabled = kept_of(now).pending}; },
          c::row(c::hbox(0.0f, {.relativeSize = scene::axes::kNone, .autoSize = scene::axes::kY, .grow = scene::axes::kX}),
                 local_address_input(colours, std::move(server)))),
      action_asking<address_draft>(colours, "Save", [](const address_draft& now) { return request::manage_room{manage_change::create_address{now.local}}; },
                                   [](const facts_t& now) { return !kept_of(now).pending; }, action_style::success)),
      address_draft{});
}
using address_maker_t = decltype(address_maker(std::declval<const palette&>(), std::string()));
// A space's rooms: each removed, and the account's others added.
struct children_key {
  std::vector<std::pair<std::string, std::string>> children;
  bool editable = false;
  friend bool operator==(const children_key&, const children_key&) = default;
};
inline auto children_rows(const palette& colours, const children_key& key) {
  namespace c = skiff::compose;
  return c::many(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}), key.children | std::views::transform([&](const auto& child) {
    return c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}), nodes::Text(child.second, 14.0f, colours.text),
                  action(colours, "Remove", ask(manage_change::remove_child{child.first}), key.editable));
  }) | std::ranges::to<std::vector>());
}
using children_rows_t = decltype(children_rows(std::declval<const palette&>(), children_key{}));
struct child_key {
  std::vector<std::string> names;
  std::size_t selected = 0;
  bool allowed = false;
  std::vector<std::string> rooms;
  friend bool operator==(const child_key&, const child_key&) = default;
};
struct choose_child {
  using Answer = std::optional<request::manage_room>;
  std::vector<std::string> rooms;
  Answer operator()(std::size_t index) const {
    return index < rooms.size() ? Answer(request::manage_room{manage_change::add_child{rooms[index]}}) : std::nullopt;
  }
};
inline auto space_children(const palette& colours, bool space) {
  namespace c = skiff::compose;
  return c::visible(space, c::column(c::vbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      part_heading(colours, "Rooms in this Space"),
      rows_of<children_rows_t, children_key>([](const facts_t& now) {
        return children_key{facts_of(now).children | std::views::transform([](const auto& one) { return std::pair{one.id, one.name}; }) | std::ranges::to<std::vector>(),
                            editable(now, power_need::change_settings{})};
      },
          [at = &colours](const children_key& key) { return children_rows(*at, key); }),
      live_choice<child_key>(colours, [](const facts_t& now) {
        const auto& addable = facts_of(now).addable;
        return child_key{addable | std::views::transform(&room_settings_facts::named_room::name) | std::ranges::to<std::vector>(), addable.size(),
                         editable(now, power_need::change_settings{}),
                         addable | std::views::transform(&room_settings_facts::named_room::id) | std::ranges::to<std::vector>()};
      },
          [](const child_key& key) { return choose_child{key.rooms}; })));
}
// The room it continues, or continues in: opened.
struct related_key {
  std::string room;
  friend bool operator==(const related_key&, const related_key&) = default;
};
template <class Of>
auto related_room(const palette& colours, std::string label, Of of) {
  namespace c = skiff::compose;
  using row_t = decltype(action(colours, label, sends<::mux::ui::request::open_url>{}));
  return c::projected<facts_t>([of](const facts_t& now) { return of(rules_now(now)).transform([](const std::string& room) { return related_key{room}; }); },
      c::mount<row_t, related_key>([at = &colours, label](const related_key& key) {
        return std::tuple{action(*at, label, sends<::mux::ui::request::open_url>{{"https://matrix.to/#/" + key.room}})};
      },
          {.relativeSize = scene::axes::kNone, .autoSize = scene::axes::kBoth}));
}
struct upgrade_draft {
  std::string version, creators;
  friend bool operator==(const upgrade_draft&, const upgrade_draft&) = default;
};
inline auto upgrade_card(const palette& colours, const std::string& version) {
  namespace c = skiff::compose;
  using draft = upgrade_draft;
  return c::local<draft>(card(colours,
      wrapped(nodes::Text("Upgrading creates a replacement room. Members will need to move to the new room.", 12.0f, colours.dim)),
      model_field<&draft::version>(colours, "Room Version", ""),
      model_field<&draft::creators>(colours, "Additional Creators (version 12 or newer)", "Space-separated Matrix IDs"),
      c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          action_asking<draft>(colours, "Upgrade", [](const draft& now) { return request::manage_room{manage_change::upgrade{now.version, now.creators}}; },
                               may_now<power_need::upgrade>{}),
          action(colours, "Cancel", ask(manage_change::cancel_upgrade{})))),
      draft{version, {}});
}
using upgrade_card_t = decltype(upgrade_card(std::declval<const palette&>(), std::string()));

inline auto general_page(const palette& colours, const room_settings_facts& first) {
  namespace c = skiff::compose;
  using selection = manage_change::selected;
  using selection_kind = manage_change::selection_kind;
  const auto colon = first.chat.account.address.find(':');
  const auto server = colon == std::string::npos ? std::string() : first.chat.account.address.substr(colon);
  const auto selected_published = [](const facts_t& now) {
    const auto published = published_of(facts_of(now));
    return std::ranges::any_of(kept_of(now).selected_aliases, [&](const std::string& alias) { return std::ranges::contains(published, alias); });
  };
  const auto may_address = [](const facts_t& now) { return may(facts_of(now), power_need::change_address{}); };
  return c::column(page_look(),
      status_line(colours, [](const facts_t& now) {
        const auto& kept = kept_of(now);
        return kept.pending ? std::string("Saving…") : kept.loading ? std::string("Loading…") : kept.error;
      }),
      section(colours, "Profile", card(colours,
          c::shown_for<facts_t>([](const facts_t& now) { return !kept_of(now).profile_editor; },
              c::row(c::hbox(16.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                  c::column(c::vbox(12.0f, {.relativeSize = scene::axes::kNone, .autoSize = scene::axes::kY, .grow = scene::axes::kX}),
                      c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                          c::text_for<facts_t>([](const facts_t& now) { return facts_of(now).name; }, wrapped(nodes::Text("", 16.0f, colours.text))),
                          c::shown_for<facts_t>([](const facts_t& now) { return !facts_of(now).topic.empty(); },
                              c::text_for<facts_t>([](const facts_t& now) { return facts_of(now).topic; }, wrapped(nodes::Text("", 12.0f, colours.text))))),
                      c::shown_for<facts_t>(may_profile, action(colours, "Edit", ask(manage_change::edit{editor::profile, true})))),
                  avatar_mark(avatar_of::chat{}, first.id, first.name, 96.0f))),
          c::shown_for<facts_t>([](const facts_t& now) { return kept_of(now).profile_editor; },
              rows_of<profile_editor_t, profile_basis>([](const facts_t& now) {
                return profile_basis{kept_of(now).profile_round, facts_of(now).name, facts_of(now).topic};
              },
                  [at = &colours, first](const profile_basis& basis) { return profile_editor(*at, basis, first); })))),
      section(colours, "Options",
          tile(colours, first.space ? "Space Access" : "Room Access",
               first.space ? "Change how people can join the space." : "Change how people can join the room.",
               live_choice<access_key>(colours, access_now, [](const access_key& key) {
                 return choose_access{access_rules(key.version, key.parents) | std::views::keys | std::ranges::to<std::vector>()};
               })),
          tile(colours, "Message History Visibility",
               "Changes to history visibility will only apply to future messages. The visibility of existing history will have no effect.",
               live_choice<choice_key>(colours, [](const facts_t& now) {
                 return choice_key{{"All Messages", "After Invite", "After Join", "All Messages (Guests)"}, rules_now(now).history.index(),
                                   editable(now, power_need::change_history{})};
               },
                   [](const choice_key&) { return choose_history{}; })),
          live_tile(colours, [](const facts_t&) { return std::string("Room Encryption"); },
                    [](const facts_t& now) {
                      return std::string(facts_of(now).encrypted ? "Messages in this room are protected by end-to-end encryption."
                                                                 : "Once enabled, encryption cannot be disabled!");
                    },
                    c::row(c::hbox(0.0f, {.relativeSize = scene::axes::kNone, .autoSize = scene::axes::kBoth}),
                        c::shown_for<facts_t>([](const facts_t& now) { return facts_of(now).encrypted; },
                                              action(colours, "Enabled", ask(manage_change::encryption{}), true, action_style::success)),
                        c::shown_for<facts_t>([](const facts_t& now) { return !facts_of(now).encrypted; },
                            action_said(colours, [](const facts_t& now) {
                              return std::string(kept_of(now).confirming_encryption ? "Enable E2E Encryption" : "Enable");
                            },
                                ask(manage_change::encryption{}), may_now<power_need::encrypt>{}, action_style::primary)))),
          c::shown_for<facts_t>([](const facts_t& now) { return kept_of(now).confirming_encryption; }, card(colours,
              wrapped(nodes::Text("Are you sure? Once enabled, encryption cannot be disabled!", 12.0f, colours.dim)),
              action(colours, "Cancel", ask(manage_change::encryption{true})))),
          tile(colours, "Publish to Directory",
               first.space ? "List the space in the public directory to make it discoverable by others."
                           : "List the room in the public directory to make it discoverable by others.",
               switch_control(colours, [](const facts_t& now) { return kept_of(now).data.directory.value_or(false); }, ask(manage_change::directory{}),
                              [](const facts_t& now) { return editable(now, power_need::change_address{}) && kept_of(now).data.directory.has_value(); }))),
      section(colours, "Addresses",
          card(colours, setting_tile(colours, "Published Addresses", "If access is Public, Published addresses will be used to join by anyone.", c::column()),
              cutout(colours, {12.0f, 12.0f, 12.0f, 12.0f},
                  c::shown_for<facts_t>([](const facts_t& now) { return published_of(facts_of(now)).empty(); },
                      c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}), nodes::Text("No Addresses", 14.0f, colours.text),
                                wrapped(nodes::Text("To publish an address, it needs to be set as a local address first.", 12.0f, colours.dim)))),
                  rows_of<published_rows_t, published_key>([](const facts_t& now) {
                    return published_key{published_of(facts_of(now)), facts_of(now).alias, may(facts_of(now), power_need::change_address{}), kept_of(now).pending};
                  },
                      [at = &colours](const published_key& key) { return published_rows(*at, key); }))),
          card(colours,
              setting_tile(colours, "Local Addresses", "Set local address so users can join through your homeserver.",
                  action_said(colours, [](const facts_t& now) { return std::string(kept_of(now).local_addresses_expanded ? "Collapse" : "Expand"); },
                              ask(manage_change::expand_addresses{}), [](const facts_t&) { return true; })),
              c::shown_for<facts_t>([](const facts_t& now) { return kept_of(now).local_addresses_expanded; },
                  c::column(c::vbox(16.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                      cutout(colours, {12.0f, 12.0f, 12.0f, 12.0f},
                          c::shown_for<facts_t>([](const facts_t& now) { return !kept_of(now).selected_aliases.empty(); },
                              c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                                  c::text_for<facts_t>([](const facts_t& now) { return std::format("{} Selected", kept_of(now).selected_aliases.size()); },
                                      c::styled({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                                                nodes::Text("", 12.0f, colours.text))),
                                  c::shown_for<facts_t>([may_address, selected_published](const facts_t& now) { return may_address(now) && selected_published(now); },
                                      action_if(colours, "Unpublish", ask(selection{selection_kind::unpublish}), [](const facts_t& now) { return !kept_of(now).pending; })),
                                  c::shown_for<facts_t>([may_address, selected_published](const facts_t& now) { return may_address(now) && !selected_published(now); },
                                      action_if(colours, "Publish", ask(selection{selection_kind::publish}), [](const facts_t& now) { return !kept_of(now).pending; })),
                                  action_if(colours, "Delete", ask(selection{selection_kind::remove}), [](const facts_t& now) { return !kept_of(now).pending; }))),
                          c::shown_for<facts_t>([](const facts_t& now) { return kept_of(now).data.local_aliases.empty(); },
                              c::text_for<facts_t>([](const facts_t& now) { return std::string(kept_of(now).loading ? "Loading…" : "No Addresses"); },
                                                   nodes::Text("", 14.0f, colours.text))),
                          rows_of<local_rows_t, local_key>([](const facts_t& now) {
                            const auto& kept = kept_of(now);
                            return local_key{kept.data.local_aliases, published_of(facts_of(now)),
                                             kept.selected_aliases | std::ranges::to<std::vector<std::string>>(), kept.pending};
                          },
                              [at = &colours](const local_key& key) { return local_rows(*at, key); })),
                      rows_of<address_maker_t, std::uint32_t>([](const facts_t& now) { return kept_of(now).address_round; },
                          [at = &colours, server](std::uint32_t) { return address_maker(*at, server); }))))),
      space_children(colours, first.space),
      section(colours, "Advanced Options",
          live_tile(colours, [space = first.space](const facts_t&) { return std::string(space ? "Upgrade Space" : "Upgrade Room"); },
                    [](const facts_t& now) { return "Current version: " + rules_now(now).version; },
                    action_if(colours, "Upgrade", ask(manage_change::upgrade{}),
                              [](const facts_t& now) { return editable(now, power_need::upgrade{}) && !kept_of(now).data.versions.empty(); })),
          related_room(colours, "Previous Room", [](const room_rules& rules) { return rules.predecessor; }),
          related_room(colours, "Replacement Room", [](const room_rules& rules) { return rules.replaced_by; }),
          c::shown_for<facts_t>([](const facts_t& now) { return kept_of(now).confirming_upgrade; },
              rows_of<upgrade_card_t, std::string>([](const facts_t& now) { return kept_of(now).data.default_version; },
                  [at = &colours](const std::string& version) { return upgrade_card(*at, version); }))));
}

// ---- Members: Cinny's membership filters and power-first order -----------------
// The query, the filter and the order are the page's own (its Local); the
// rows are made from the members as the program has them and that.
struct member_filter { std::size_t value; };
struct member_order { std::size_t value; };
struct select_filter { using Answer = member_filter; member_filter operator()(std::size_t value) const { return {value}; } };
struct select_order { using Answer = member_order; member_order operator()(std::size_t value) const { return {value}; } };
struct member_draft {
  std::string query;
  std::size_t filter = 0, order = 0;
  friend bool operator==(const member_draft&, const member_draft&) = default;
};
struct member_events {
  auto on(member_filter event, const member_draft&) const {
    return skiff::model::over<skiff::model::Field<&member_draft::filter>>(skiff::model::setTo(event.value));
  }
  auto on(member_order event, const member_draft&) const {
    return skiff::model::over<skiff::model::Field<&member_draft::order>>(skiff::model::setTo(event.value));
  }
};
// A member, as the rows show them: who, how they are in the room, their
// level and its name.
struct member_line {
  std::string id, name;
  std::optional<std::string> avatar;
  std::size_t membership = 0;
  std::int64_t timestamp = 0, level = 0;
  std::string level_name;
  friend bool operator==(const member_line&, const member_line&) = default;
};
// What the rows are made from: the members, the levels one may give and
// their names, and whose they are to give.
struct members_basis {
  std::vector<member_line> members;
  std::vector<std::int64_t> values;
  std::vector<std::string> names;
  bool may_change = false, loading = true;
  std::int64_t mine = 0;
  std::string me;
  friend bool operator==(const members_basis&, const members_basis&) = default;
};
[[nodiscard]] inline members_basis members_now(const facts_t& now) {
  const auto& facts = facts_of(now);
  const auto& kept = kept_of(now);
  const auto& rules = rules_now(now);
  const auto line = [&](std::string id, std::string name, std::optional<std::string> avatar, std::size_t membership, std::int64_t timestamp) {
    const auto level = level_of(rules, id);
    return member_line{std::move(id), std::move(name), std::move(avatar), membership, timestamp, level, power_name(level, kept.data)};
  };
  // The server's list, else the chat's members, joined.
  auto members = kept.data.members.empty()
                     ? facts.members | std::views::transform([&](const mux::member& one) { return line(one.id, one.name, one.avatar, 0, 0); }) | std::ranges::to<std::vector>()
                     : kept.data.members | std::views::transform([&](const managed_member& one) {
                         return line(one.id, one.name, one.avatar, one.membership.index(), one.timestamp);
                       }) | std::ranges::to<std::vector>();
  const auto values = power_values(rules, kept.data);
  return {std::move(members), values,
          values | std::views::transform([&](std::int64_t value) { return power_name(value, kept.data); }) | std::ranges::to<std::vector>(),
          editable(now, power_need::change_permissions{}), kept.loading, facts.mine, facts.chat.account.address};
}
struct member_listing {
  std::vector<member_line> rows;
  std::string empty;
  friend bool operator==(const member_listing&, const member_listing&) = default;
};
[[nodiscard]] inline member_listing listing_of(const members_basis& basis, const member_draft& now) {
  const auto wanted = logic::folded(now.query);
  auto rows = basis.members | std::views::filter([&](const member_line& one) {
    return one.membership == now.filter && (wanted.empty() || logic::folded(one.name).contains(wanted) || logic::folded(one.id).contains(wanted));
  }) | std::ranges::to<std::vector>();
  std::ranges::stable_sort(rows, [&](const member_line& a, const member_line& b) {
    if (a.level != b.level) return a.level > b.level;
    switch (now.order) {
      case 1: return logic::folded(a.name) > logic::folded(b.name);
      case 2: return a.timestamp > b.timestamp;
      case 3: return a.timestamp < b.timestamp;
      default: return logic::folded(a.name) < logic::folded(b.name);
    }
  });
  static const std::array<std::string_view, 5> labels{"Joined", "Invited", "Left", "Kicked", "Banned"};
  return {std::move(rows), basis.loading ? std::string("Loading…") : std::format("No {} Members", labels.at(std::min(now.filter, labels.size() - 1)))};
}
struct choose_member_power {
  using Answer = std::optional<request::manage_room>;
  std::string user;
  std::vector<std::int64_t> values;
  Answer operator()(std::size_t index) const {
    return index < values.size() ? Answer(request::manage_room{manage_change::member_power{user, values[index]}}) : std::nullopt;
  }
};
inline auto member_rows(const palette& colours, const members_basis& basis, const member_listing& listing) {
  namespace c = skiff::compose;
  return c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), listing.rows | std::views::transform([&](const member_line& one) {
        auto values = basis.values;
        auto names = basis.names;
        if (one.level != kCreatorPower && !std::ranges::contains(values, one.level)) {
          values.push_back(one.level);
          names.push_back(one.level_name);
        }
        const auto selected = static_cast<std::size_t>(std::ranges::find(values, one.level) - values.begin());
        const bool locked = !basis.may_change || one.level == kCreatorPower || (one.level >= basis.mine && one.id != basis.me);
        auto choice = compact_choice(colours, std::move(names), selected, choose_member_power{one.id, std::move(values)});
        choice.apply({.disabled = locked});
        return c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
            c::styled({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX},
                      member_row(colours, mux::member{one.id, one.name, std::nullopt, one.avatar}, one.level_name)),
            std::move(choice));
      }) | std::ranges::to<std::vector>()),
      c::visible(listing.rows.empty(), wrapped(nodes::Text(listing.empty, 12.0f, colours.dim))));
}
// The rows for the members as they are: listed as the draft says.
inline auto member_listing_view(const palette& colours, const members_basis& basis) {
  namespace c = skiff::compose;
  using rows_t = decltype(member_rows(colours, basis, member_listing{}));
  return c::derived<member_draft>([basis](const member_draft& now) { return listing_of(basis, now); },
      c::mount<rows_t, member_listing>([at = &colours, basis](const member_listing& listing) { return std::tuple{member_rows(*at, basis, listing)}; },
                                       {.fillX = true, .autoSize = scene::axes::kY}));
}
using member_listing_view_t = decltype(member_listing_view(std::declval<const palette&>(), members_basis{}));
inline auto members_page(const palette& colours, const room_settings_facts&) {
  namespace c = skiff::compose;
  using draft = member_draft;
  return c::local<draft>(member_events{}, c::column(page_look(),
      status_line(colours, [](const facts_t& now) { return kept_of(now).error; }),
      model_field<&draft::query>(colours, "", "Search"),
      c::row(c::justified(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}), nodes::justify::end{}),
          compact_choice(colours, {"Joined", "Invited", "Left", "Kicked", "Banned"}, 0, select_filter{}),
          compact_choice(colours, {"A to Z", "Z to A", "Newest", "Oldest"}, 0, select_order{})),
      rows_of<member_listing_view_t, members_basis>(members_now, [at = &colours](const members_basis& basis) { return member_listing_view(*at, basis); })),
      draft{});
}

// ---- Permissions: named powers, then Users, Messages, Calls, Moderation,
// Room Overview, Settings and Other. Staged until Apply. ----------------------
struct permission_item {
  std::string title;
  permission_t location;
};
struct permission_group {
  std::string title;
  std::vector<permission_item> items;
};
[[nodiscard]] inline std::vector<permission_group> permission_groups() {
  return {
    {"Users", {{"Default Power", power_need_t{power_need::default_role{}}}}},
    {"Messages", {{"Send Messages", event_permission{"m.room.message", false}}, {"Send Stickers", event_permission{"m.sticker", false}},
      {"Send Reactions", event_permission{"m.reaction", false}}, {"Ping @room", power_need_t{power_need::notify_everyone{}}},
      {"Pin Messages", power_need_t{power_need::pin{}}}, {"Other Message Events", power_need_t{power_need::send_messages{}}}}},
    {"Calls", {{"Start or Join Call", event_permission{"org.matrix.msc3401.call.member"}}}},
    {"Moderation", {{"Invite", power_need_t{power_need::invite{}}}, {"Kick", power_need_t{power_need::kick{}}},
      {"Ban", power_need_t{power_need::ban{}}}, {"Delete Others Messages", power_need_t{power_need::redact{}}},
      {"Delete Self Messages", event_permission{"m.room.redaction", false}}}},
    {"Room Overview", {{"Room Avatar", power_need_t{power_need::change_avatar{}}}, {"Room Name", power_need_t{power_need::rename{}}},
      {"Room Topic", power_need_t{power_need::retopic{}}}}},
    {"Settings", {{"Change Room Access", power_need_t{power_need::change_access{}}}, {"Publish Address", power_need_t{power_need::change_address{}}},
      {"Change All Permission", power_need_t{power_need::change_permissions{}}}, {"Edit Power Levels", event_permission{"in.cinny.room.power_level_tags"}},
      {"Enable Encryption", power_need_t{power_need::encrypt{}}}, {"History Visibility", power_need_t{power_need::change_history{}}},
      {"Upgrade Room", power_need_t{power_need::upgrade{}}}, {"Other Settings", power_need_t{power_need::change_settings{}}}}},
    {"Other", {{"Manage Emojis & Stickers", event_permission{"im.ponies.room_emotes"}}, {"Change Server ACLs", power_need_t{power_need::change_acl{}}},
      {"Modify Widgets", event_permission{"im.vector.modular.widgets"}}}}
  };
}
[[nodiscard]] inline std::set<std::string> standard_permission_events() {
  std::set<std::string> names;
  std::ranges::for_each(permission_groups(), [&](const permission_group& group) {
    std::ranges::for_each(group.items, [&](const permission_item& item) {
      spl::visit(spl::overloaded{[&](const event_permission& event) { names.insert(event.type); },
                                 [&](const power_need_t& need) {
                                   spl::visit(spl::overloaded{[&]<sends_state Need>(Need) { names.insert(std::string(Need::event)); }, [](auto) {}}, need);
                                 }},
                 item.location);
    });
  });
  return names;
}
// The needs as staged, else as they are.
[[nodiscard]] inline const power_needs& needs_now(const facts_t& now) {
  const auto& kept = kept_of(now);
  return kept.permissions ? *kept.permissions : rules_now(now).needs;
}
struct permission_key {
  std::vector<std::string> names;
  std::size_t selected = 0;
  bool allowed = false;
  std::vector<std::int64_t> values;
  friend bool operator==(const permission_key&, const permission_key&) = default;
};
struct choose_permission {
  using Answer = std::optional<request::manage_room>;
  permission_t target;
  std::vector<std::int64_t> values;
  Answer operator()(std::size_t index) const {
    return index < values.size() ? Answer(request::manage_room{manage_change::permission{target, values[index]}}) : std::nullopt;
  }
};
[[nodiscard]] inline bool is_user_default(const permission_t& target) {
  return spl::visit(spl::overloaded{[](const power_need_t& need) {
                                      return spl::visit(spl::overloaded{[](power_need::default_role) { return true; }, [](auto) { return false; }}, need);
                                    },
                                    [](const event_permission&) { return false; }},
                    target);
}
inline auto permission_row(const palette& colours, const permission_item& item) {
  const bool user = is_user_default(item.location);
  return setting_tile(colours, item.title, user ? "Default power level for all users." : "",
      live_choice<permission_key>(colours, [target = item.location, user](const facts_t& now) {
        const auto& kept = kept_of(now);
        const auto current = required(needs_now(now), target);
        auto values = power_values(rules_now(now), kept.data);
        if (!std::ranges::contains(values, current)) values.push_back(current);
        std::ranges::sort(values, std::greater{});
        auto names = values | std::views::transform([&](std::int64_t value) { return power_name(value, kept.data) + (user ? "" : " & Above"); }) | std::ranges::to<std::vector>();
        return permission_key{std::move(names), static_cast<std::size_t>(std::ranges::find(values, current) - values.begin()),
                              editable(now, power_need::change_permissions{}) && required(rules_now(now).needs, target) <= facts_of(now).mine, std::move(values)};
      },
          [target = item.location](const permission_key& key) { return choose_permission{target, key.values}; }));
}
inline auto permission_section(const palette& colours, const permission_group& group) {
  namespace c = skiff::compose;
  return section(colours, group.title,
      c::many(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY, .cornerRadius = std::max(10.0f, colours.widgets.fCorner), .background = colours.tile}),
              group.items | std::views::transform([&](const permission_item& item) {
                return c::styled({.padding = {12.0f, 12.0f, 12.0f, 12.0f}}, permission_row(colours, item));
              }) | std::ranges::to<std::vector>()));
}
using permission_section_t = decltype(permission_section(std::declval<const palette&>(), permission_group{}));
// Those set apart from the standard ones: each its level, and removed.
struct custom_key {
  std::vector<std::string> events;
  std::vector<bool> removable;
  friend bool operator==(const custom_key&, const custom_key&) = default;
};
inline auto custom_rows(const palette& colours, const custom_key& key) {
  namespace c = skiff::compose;
  return c::many(c::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      std::views::zip(key.events, key.removable) | std::views::transform([&](const auto& one) {
        const auto& [event, removable] = one;
        return c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
            c::styled({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX}, permission_row(colours, permission_item{event, event_permission{event}})),
            action(colours, "Remove", ask(manage_change::remove_custom_permission{event}), removable));
      }) | std::ranges::to<std::vector>());
}
using custom_rows_t = decltype(custom_rows(std::declval<const palette&>(), custom_key{}));
struct custom_draft {
  std::string event, level;
  friend bool operator==(const custom_draft&, const custom_draft&) = default;
};
inline auto custom_adder(const palette& colours) {
  namespace c = skiff::compose;
  using draft = custom_draft;
  return c::local<draft>(c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::styled({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX}, model_field<&draft::event>(colours, "Event type", "org.example.event")),
      c::styled({.width = 96.0f, .relativeSize = scene::axes::kNone}, model_field<&draft::level>(colours, "Power level", "50")),
      action_asking<draft>(colours, "Add", [](const draft& now) { return request::manage_room{manage_change::add_custom_permission{now.event, now.level}}; },
                           may_now<power_need::change_permissions>{})),
      draft{});
}
using custom_adder_t = decltype(custom_adder(std::declval<const palette&>()));
inline auto custom_permissions(const palette& colours) {
  return section(colours, "Custom Event Permissions", card(colours,
      rows_of<custom_rows_t, custom_key>([](const facts_t& now) {
        const auto known = standard_permission_events();
        custom_key key;
        std::ranges::for_each(needs_now(now).events | std::views::keys | std::views::filter([&](const std::string& event) { return !known.contains(event); }),
                              [&](const std::string& event) {
                                key.events.push_back(event);
                                key.removable.push_back(editable(now, power_need::change_permissions{}) &&
                                                        required(rules_now(now).needs, permission_t{event_permission{event}}) <= facts_of(now).mine);
                              });
        return key;
      },
          [at = &colours](const custom_key& key) { return custom_rows(*at, key); }),
      wrapped(nodes::Text("Set a power level for an additional event type. Removing an override restores its default permission.", 12.0f, colours.dim)),
      rows_of<custom_adder_t, std::uint32_t>([](const facts_t& now) { return kept_of(now).custom_round; },
                                             [at = &colours](std::uint32_t) { return custom_adder(*at); })));
}
// The power levels' chips: each its name, in its colour.
[[nodiscard]] inline skia::SkColor power_colour(std::int64_t power, const room_management& data) {
  if (const auto found = data.tags.find(std::to_string(power)); found != data.tags.end() && found->second.color) {
    auto text = std::string_view(*found->second.color);
    if (text.starts_with('#')) text.remove_prefix(1);
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (text.size() == 6 && error == std::errc{} && end == text.data() + text.size()) return 0xff000000u | value;
  }
  static const std::map<std::int64_t, skia::SkColor> defaults{{9001, 0xffff6a00u}, {150, 0xffff6a7fu}, {101, 0xff0000ffu},
      {100, 0xff0088ffu}, {50, 0xff1fd81fu}, {0, 0xff91cfdfu}, {-1, 0xff888888u}};
  const auto found = defaults.find(power);
  return found == defaults.end() ? 0xff888888u : found->second;
}
inline auto power_chip(const palette& colours, std::string name, skia::SkColor colour) {
  namespace c = skiff::compose;
  return c::row(c::hbox(4.0f, {.height = 24.0f, .relativeSize = scene::axes::kNone, .autoSize = scene::axes::kX, .padding = {0.0f, 8.0f, 0.0f, 8.0f},
                               .cornerRadius = colours.widgets.fCorner, .background = colours.chosen}),
      c::styled({.width = 14.0f, .height = 14.0f, .alignSelf = scene::align::kMiddle}, nodes::Icon(shape_of(icon::dot{colour}), colour)),
      c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(std::move(name), 14.0f, colours.text)));
}
struct chips_key {
  std::vector<std::pair<std::string, skia::SkColor>> chips;
  friend bool operator==(const chips_key&, const chips_key&) = default;
};
inline auto power_chips(const palette& colours, const chips_key& key) {
  auto chips = skiff::compose::many(skiff::compose::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      key.chips | std::views::transform([&](const auto& one) { return power_chip(colours, one.first, one.second); }) | std::ranges::to<std::vector>());
  chips.fStack.wrap = true;
  return chips;
}
using power_chips_t = decltype(power_chips(std::declval<const palette&>(), chips_key{}));
// A power level's form: made from the one edited, or empty for a new one.
struct power_draft {
  std::string power, name, color, icon;
  friend bool operator==(const power_draft&, const power_draft&) = default;
};
struct power_basis {
  bool editing = false;
  power_draft first;
  friend bool operator==(const power_basis&, const power_basis&) = default;
};
[[nodiscard]] inline power_basis power_basis_of(const facts_t& now) {
  const auto& kept = kept_of(now);
  if (!kept.editing_power)
    return {};
  const auto power = *kept.editing_power;
  const auto tags = staged(kept).tags;
  const auto found = tags.find(std::to_string(power));
  return {true, {std::to_string(power), power_name(power, kept.data), found != tags.end() ? found->second.color.value_or("") : "",
                 found != tags.end() && found->second.icon ? found->second.icon->key.value_or("") : ""}};
}
inline auto power_form(const palette& colours, const power_basis& basis) {
  namespace c = skiff::compose;
  using draft = power_draft;
  return c::local<draft>(c::column(c::vbox(16.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          c::styled({.width = 80.0f, .relativeSize = scene::axes::kNone}, model_field<&draft::color>(colours, "Color", "#91cfdf")),
          c::styled({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX}, model_field<&draft::name>(colours, "Name", "Bot")),
          c::styled({.width = 80.0f, .relativeSize = scene::axes::kNone, .disabled = basis.editing}, model_field<&draft::power>(colours, "Power", "75"))),
      model_field<&draft::icon>(colours, "Icon", "Emoji or mxc:// image"),
      c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          action_asking<draft>(colours, "Save", [](const draft& now) {
            return request::manage_room{manage_change::save_power{now.power, now.name, now.color, now.icon}};
          },
              [](const facts_t& now) { return !kept_of(now).pending; }),
          action(colours, "Cancel", ask(manage_change::edit{editor::power, false})))),
      basis.first);
}
using power_form_t = decltype(power_form(std::declval<const palette&>(), power_basis{}));
inline auto power_form_for(const palette& colours) {
  return rows_of<power_form_t, power_basis>(power_basis_of, [at = &colours](const power_basis& basis) { return power_form(*at, basis); });
}
using power_form_for_t = decltype(power_form_for(std::declval<const palette&>()));
// The levels, each its card: deleted (or not, once more), edited -- its
// form under it.
struct level_line {
  std::int64_t value = 0;
  std::string name;
  bool deleted = false, removable = false;
  friend bool operator==(const level_line&, const level_line&) = default;
};
struct levels_key {
  std::vector<level_line> levels;
  bool pending = false;
  friend bool operator==(const levels_key&, const levels_key&) = default;
};
inline auto level_rows(const palette& colours, const levels_key& key) {
  namespace c = skiff::compose;
  return c::many(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}), key.levels | std::views::transform([&](const level_line& one) {
    return card(colours,
        setting_tile(colours, one.name, std::format("({})", one.value),
            c::row(c::hbox(8.0f, {.autoSize = scene::axes::kBoth}),
                action(colours, one.deleted ? "Undo" : "Delete", ask(manage_change::delete_tag{one.value}), !key.pending && one.removable),
                action(colours, "Edit", ask(manage_change::edit_power{one.value}), !key.pending && !one.deleted))),
        c::shown_for<facts_t>([value = one.value](const facts_t& now) { return kept_of(now).power_form && kept_of(now).editing_power == value; },
                              power_form_for(colours)));
  }) | std::ranges::to<std::vector>());
}
using level_rows_t = decltype(level_rows(std::declval<const palette&>(), levels_key{}));
inline auto permissions_page(const palette& colours, const room_settings_facts&) {
  namespace c = skiff::compose;
  const auto not_pending = [](const facts_t& now) { return !kept_of(now).pending; };
  return c::column(page_look(),
      status_line(colours, [](const facts_t& now) { return kept_of(now).pending ? std::string("Saving…") : kept_of(now).error; }),
      c::shown_for<facts_t>([](const facts_t& now) { return !kept_of(now).powers_editor; },
          c::column(c::vbox(32.0f, {.fillX = true, .autoSize = scene::axes::kY}),
              c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                  c::shown_for<facts_t>([](const facts_t& now) {
                    return std::ranges::contains(rules_now(now).powers | std::views::values, kCreatorPower);
                  },
                      card(colours, setting_tile(colours, "Founders", "Founding members has all permissions and can only be changed during upgrade.", c::column()),
                           power_chip(colours, "Founder", 0xff0000ffu))),
                  card(colours,
                      setting_tile(colours, "Power Levels", "Manage and customize incremental power levels for users.",
                          c::shown_for<facts_t>([](const facts_t& now) { return facts_of(now).mine >= event_need(rules_now(now), "in.cinny.room.power_level_tags"); },
                                                action(colours, "Edit", ask(manage_change::edit{editor::powers, true})))),
                      rows_of<power_chips_t, chips_key>([](const facts_t& now) {
                        const auto& kept = kept_of(now);
                        return chips_key{power_values(rules_now(now), kept.data) | std::views::transform([&](std::int64_t value) {
                          return std::pair{power_name(value, kept.data), power_colour(value, kept.data)};
                        }) | std::ranges::to<std::vector>()};
                      },
                          [at = &colours](const chips_key& key) { return power_chips(*at, key); }))),
              c::many(c::vbox(32.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                      permission_groups() | std::views::transform([&](const permission_group& group) { return permission_section(colours, group); }) | std::ranges::to<std::vector>()),
              custom_permissions(colours),
              c::shown_for<facts_t>([](const facts_t& now) { return kept_of(now).permissions.has_value(); }, card(colours,
                  wrapped(nodes::Text("You have unsaved changes.", 12.0f, colours.dim)),
                  c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                      action_if(colours, "Reset", ask(manage_change::apply_permissions{true}), not_pending),
                      action_if(colours, "Apply Changes", ask(manage_change::apply_permissions{}), may_now<power_need::change_permissions>{}, action_style::success)))))),
      c::shown_for<facts_t>([](const facts_t& now) { return kept_of(now).powers_editor; }, section(colours, "Power Levels",
          card(colours,
              setting_tile(colours, "New Power Level", "Create a new power level.",
                  c::shown_for<facts_t>([](const facts_t& now) { return !kept_of(now).power_form; }, action(colours, "Create", ask(manage_change::edit{editor::power, true})))),
              c::shown_for<facts_t>([](const facts_t& now) { return kept_of(now).power_form && !kept_of(now).editing_power; }, power_form_for(colours))),
          rows_of<level_rows_t, levels_key>([](const facts_t& now) {
            const auto& kept = kept_of(now);
            const auto effective = staged(kept);
            const auto fixed = power_values(rules_now(now), room_management{});
            return levels_key{power_values(rules_now(now), effective) | std::views::transform([&](std::int64_t value) {
              return level_line{value, power_name(value, effective), kept.deleted_powers.contains(value), !std::ranges::contains(fixed, value)};
            }) | std::ranges::to<std::vector>(), kept.pending};
          },
              [at = &colours](const levels_key& key) { return level_rows(*at, key); }),
          c::shown_for<facts_t>([](const facts_t& now) { return kept_of(now).tag_draft.has_value() || !kept_of(now).deleted_powers.empty(); }, card(colours,
              wrapped(nodes::Text("Changes saved! Apply when ready.", 12.0f, colours.dim)),
              c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                  action_if(colours, "Reset", ask(manage_change::apply_tags{true}), not_pending),
                  action_if(colours, "Apply Changes", ask(manage_change::apply_tags{}), not_pending, action_style::success)))),
          action(colours, "Permissions", ask(manage_change::edit{editor::powers, false})))));
}

// ---- Emojis & Stickers: the room's packs -----------------------------------------
struct pack_draft {
  std::string name;
  friend bool operator==(const pack_draft&, const pack_draft&) = default;
};
[[nodiscard]] inline bool may_packs(const facts_t& now) { return facts_of(now).mine >= event_need(rules_now(now), "im.ponies.room_emotes"); }
inline auto pack_maker(const palette& colours) {
  namespace c = skiff::compose;
  return c::local<pack_draft>(c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::styled({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX}, model_field<&pack_draft::name>(colours, "Name", "Pack name")),
      action_asking<pack_draft>(colours, "Create", [](const pack_draft& now) { return request::manage_room{manage_change::create_pack{now.name}}; },
                                may_packs)),
      pack_draft{});
}
using pack_maker_t = decltype(pack_maker(std::declval<const palette&>()));
struct pack_line {
  std::string key, name;
  std::size_t pictures = 0;
  friend bool operator==(const pack_line&, const pack_line&) = default;
};
struct packs_key {
  std::vector<pack_line> packs;
  friend bool operator==(const packs_key&, const packs_key&) = default;
};
inline auto pack_cards(const palette& colours, const packs_key& key) {
  namespace c = skiff::compose;
  return c::many(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}), key.packs | std::views::transform([&](const pack_line& one) {
    return card(colours, setting_tile(colours, one.name.empty() ? "Unnamed Pack" : one.name, std::format("{} images", one.pictures),
                                      action(colours, "View", sends<::mux::ui::request::open_room_packs>{{one.key}})));
  }) | std::ranges::to<std::vector>());
}
using pack_cards_t = decltype(pack_cards(std::declval<const palette&>(), packs_key{}));
inline auto packs_page(const palette& colours, const room_settings_facts&) {
  namespace c = skiff::compose;
  return c::column(page_look(), section(colours, "Room Packs",
      c::shown_for<facts_t>(may_packs, card(colours,
          setting_tile(colours, "New Pack", "Add your own emoji and sticker pack to use in room.", c::column()),
          rows_of<pack_maker_t, std::uint32_t>([](const facts_t& now) { return kept_of(now).pack_round; },
                                               [at = &colours](std::uint32_t) { return pack_maker(*at); }))),
      rows_of<pack_cards_t, packs_key>([](const facts_t& now) {
        return packs_key{kept_of(now).packs | std::views::transform([](const mux::emote_pack& pack) {
          return pack_line{pack.key, pack.name, pack.pictures.size()};
        }) | std::ranges::to<std::vector>()};
      },
          [at = &colours](const packs_key& key) { return pack_cards(*at, key); }),
      c::shown_for<facts_t>([](const facts_t& now) { return kept_of(now).packs.empty(); }, wrapped(nodes::Text("No packs found.", 12.0f, colours.dim)))));
}

// ---- Developer Tools: the room's state events and account data --------------------
enum class state_row_kind { add, type, key };
template <class Act>
auto state_list_row(const palette& colours, std::string label, std::string detail, Act act, state_row_kind kind = state_row_kind::type, bool expanded = false) {
  namespace c = skiff::compose;
  return c::onClick(std::move(act), c::row(c::hbox(8.0f, {.fillX = true, .height = 36.0f, .padding = {4.0f, 8.0f, 4.0f, 8.0f},
                                                          .cornerRadius = colours.widgets.fCorner, .hoverBackground = colours.chosen}),
      c::visible(kind != state_row_kind::key,
          c::styled({.width = 12.0f, .height = 12.0f, .alignSelf = scene::align::kMiddle},
                    nodes::Icon(shape_of(kind == state_row_kind::add ? icon_t{icon::plus{}} : expanded ? icon_t{icon::down{}} : icon_t{icon::right{}}), colours.dim))),
      c::styled({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX, .shrink = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                elided(nodes::Text(std::move(label), 12.0f, colours.text))),
      c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(std::move(detail), 12.0f, colours.dim)),
      c::visible(kind == state_row_kind::key,
          c::styled({.width = 12.0f, .height = 12.0f, .alignSelf = scene::align::kMiddle}, nodes::Icon(shape_of(icon::right{}), colours.dim)))));
}
// The state events, by their types: each type unfolded to its keys.
struct state_group {
  std::string type;
  std::vector<std::string> keys;
  friend bool operator==(const state_group&, const state_group&) = default;
};
struct state_list_key {
  std::vector<state_group> groups;
  std::string expanded;
  friend bool operator==(const state_list_key&, const state_list_key&) = default;
};
[[nodiscard]] inline state_list_key state_list_of(const facts_t& now) {
  const auto& kept = kept_of(now);
  std::map<std::string, std::vector<std::string>> groups;
  std::ranges::for_each(kept.data.state_events, [&](const managed_state_event& event) { groups[event.type].push_back(event.key); });
  return {groups | std::views::transform([](auto& group) {
            std::ranges::sort(group.second);
            return state_group{group.first, std::move(group.second)};
          }) | std::ranges::to<std::vector>(),
          kept.expanded_state_type};
}
inline auto state_event_list(const palette& colours, const state_list_key& key) {
  namespace c = skiff::compose;
  using open = manage_change::state_open;
  return c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      setting_tile(colours, "Events", "", nodes::Text(std::format("Total: {}", key.groups.size()), 12.0f, colours.dim)),
      cutout(colours, {}, state_list_row(colours, "Add New", "", ask(open{state_screen::compose, "", ""}), state_row_kind::add),
          c::many(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}), key.groups | std::views::transform([&](const state_group& group) {
            const bool expanded = key.expanded == group.type;
            return c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                state_list_row(colours, group.type, std::to_string(group.keys.size()), ask(manage_change::state_expand{group.type}), state_row_kind::type, expanded),
                c::visible(expanded, c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY, .margin = {0.0f, 0.0f, 0.0f, 16.0f},
                                                              .padding = {0.0f, 0.0f, 0.0f, 8.0f}, .border = scene::Border{colours.band, 1.0f}}),
                    state_list_row(colours, "Add New", "", ask(open{state_screen::compose, group.type, ""}), state_row_kind::add),
                    c::many(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}), group.keys | std::views::transform([&](const std::string& one) {
                      return state_list_row(colours, one.empty() ? "Default" : "\"" + one + "\"", "", ask(open{state_screen::view, group.type, one}),
                                            state_row_kind::key);
                    }) | std::ranges::to<std::vector>()))));
          }) | std::ranges::to<std::vector>())));
}
using state_event_list_t = decltype(state_event_list(std::declval<const palette&>(), state_list_key{}));
// A state event edited or written: its draft made from the one open.
struct state_draft {
  std::string type, key, json;
  friend bool operator==(const state_draft&, const state_draft&) = default;
};
struct state_basis {
  std::uint32_t round = 0;
  bool composing = false;
  state_draft first;
  friend bool operator==(const state_basis&, const state_basis&) = default;
};
inline auto state_editor(const palette& colours, const state_basis& basis) {
  namespace c = skiff::compose;
  using draft = state_draft;
  const auto not_pending = [](const facts_t& now) { return !kept_of(now).pending; };
  const auto save = [](const draft& now) { return request::manage_room{manage_change::save_state{now.type, now.key, now.json}}; };
  return c::local<draft>(c::column(c::vbox(16.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::visible(!basis.composing, section(colours, "State Event", tile(colours, basis.first.type, basis.first.key,
          c::row(c::hbox(8.0f, {.autoSize = scene::axes::kBoth}),
              action_asking<draft>(colours, "Save", save, not_pending, action_style::success),
              action_if(colours, "Cancel", ask(manage_change::state_open{state_screen::view, basis.first.type, basis.first.key}), not_pending))))),
      c::visible(basis.composing, c::column(c::vbox(16.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
              c::styled({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX}, model_field<&draft::type>(colours, "State Event Type", "")),
              action_asking<draft>(colours, "Send", save, not_pending, action_style::success)),
          model_field<&draft::key>(colours, "State Key (Optional)", ""))),
      section(colours, "JSON Content", model_area<&draft::json>(colours, "", "{}", 20))),
      basis.first);
}
using state_editor_t = decltype(state_editor(std::declval<const palette&>(), state_basis{}));
// Account data written: its draft made from the one chosen.
struct account_draft {
  std::string type, json;
  friend bool operator==(const account_draft&, const account_draft&) = default;
};
struct account_basis {
  std::uint32_t round = 0;
  account_draft first;
  friend bool operator==(const account_basis&, const account_basis&) = default;
};
inline auto account_editor(const palette& colours, const account_basis& basis) {
  namespace c = skiff::compose;
  using draft = account_draft;
  return c::local<draft>(section(colours, "Account Data",
      model_field<&draft::type>(colours, "Type", ""),
      model_area<&draft::json>(colours, "Content", "{}", 12),
      c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          action_asking<draft>(colours, "Save", [](const draft& now) { return request::manage_room{manage_change::save_account_data{now.type, now.json}}; },
                               [](const facts_t& now) { return !kept_of(now).pending; }),
          action(colours, "Cancel", ask(manage_change::edit{editor::account_form, false})))),
      basis.first);
}
using account_editor_t = decltype(account_editor(std::declval<const palette&>(), account_basis{}));
struct choose_account_data {
  using Answer = request::manage_room;
  Answer operator()(std::size_t index) const { return {manage_change::choose_account_data{index}}; }
};
// The developer tools as they are kept: on, or off.
inline auto developer_switch(const palette& colours) {
  namespace c = skiff::compose;
  using toggle_t = widgets::Toggle<sends<::mux::ui::request::flip_developer_tools>>;
  return c::applied<looks_picked>([](const looks_picked& now) { return now.developer_tools; }, [](toggle_t& toggle, bool on) { toggle.setOnNow(on); },
                                  c::styled({.relativeSize = scene::axes::kNone, .alignSelf = scene::align::kMiddle}, toggle_t(colours.widgets, {})));
}
inline auto developer_page(const palette& colours, const room_settings_facts& first) {
  namespace c = skiff::compose;
  const auto tools_on = [](const looks_picked& now) { return now.developer_tools; };
  return c::column(page_look(),
      status_line(colours, [](const facts_t& now) { return kept_of(now).pending ? std::string("Saving…") : kept_of(now).error; }),
      c::shown_for<facts_t>([](const facts_t& now) { return !kept_of(now).account_editor && kept_of(now).state_page == state_screen::overview; },
          c::column(c::vbox(32.0f, {.fillX = true, .autoSize = scene::axes::kY}),
              section(colours, "Options",
                  tile(colours, "Enable Developer Tools", "", developer_switch(colours)),
                  c::shown_for<looks_picked>(tools_on, tile(colours, "Room ID", "Copy room ID to clipboard. (\"" + first.id + "\")",
                                                           action(colours, "Copy", sends<::mux::ui::request::copy_text>{{first.id}})))),
              c::shown_for<looks_picked>(tools_on, section(colours, "Data",
                  tile(colours, "New Message Event", "Create and send a new message event within the room.",
                       action(colours, "Compose", sends<request::open_send_custom>{})),
                  card(colours,
                      setting_tile(colours, "Room State", "State events of the room.",
                          action_said(colours, [](const facts_t& now) { return std::string(kept_of(now).expanded_state ? "Collapse" : "Expand"); },
                                      ask(manage_change::edit{editor::state_events}), [](const facts_t&) { return true; })),
                      c::shown_for<facts_t>([](const facts_t& now) { return kept_of(now).expanded_state; },
                          rows_of<state_event_list_t, state_list_key>(state_list_of, [at = &colours](const state_list_key& key) { return state_event_list(*at, key); }))),
                  card(colours,
                      setting_tile(colours, "Account Data", "Private personalization data stored within room.",
                          action_said(colours, [](const facts_t& now) { return std::string(kept_of(now).expanded_account_data ? "Collapse" : "Expand"); },
                                      ask(manage_change::edit{editor::account_data}), [](const facts_t&) { return true; })),
                      c::shown_for<facts_t>([](const facts_t& now) { return kept_of(now).expanded_account_data; },
                          c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                              action(colours, "Add New", ask(manage_change::edit{editor::account_form, true})),
                              live_choice<choice_key>(colours, [](const facts_t& now) {
                                auto names = kept_of(now).data.account_data | std::views::transform(&managed_account_data::type) | std::ranges::to<std::vector>();
                                const auto count = names.size();
                                return choice_key{std::move(names), count, true};
                              },
                                  [](const choice_key&) { return choose_account_data{}; })))))))),
      c::shown_for<facts_t>([](const facts_t& now) { return kept_of(now).state_page != state_screen::overview; },
          c::column(c::vbox(16.0f, {.fillX = true, .autoSize = scene::axes::kY}),
              c::shown_for<facts_t>([](const facts_t& now) { return kept_of(now).state_page == state_screen::view; },
                  c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                      setting_tile(colours, "State Event", "",
                          c::shown_for<facts_t>([](const facts_t& now) {
                            return facts_of(now).mine >= required(rules_now(now).needs, permission_t{event_permission{kept_of(now).state_type}});
                          },
                              action(colours, "Edit", ask(manage_change::state_open{state_screen::edit, "", ""})))),
                      card(colours, c::text_for<facts_t>([](const facts_t& now) { return kept_of(now).state_source; }, [&] {
                        auto text = nodes::Text("", 13.0f, colours.text);
                        text.setMonospace(true);
                        text.setSelectable(true);
                        return wrapped(std::move(text));
                      }())))),
              c::shown_for<facts_t>([](const facts_t& now) { return kept_of(now).state_page != state_screen::view; },
                  rows_of<state_editor_t, state_basis>([](const facts_t& now) {
                    const auto& kept = kept_of(now);
                    return state_basis{kept.state_round, kept.state_page == state_screen::compose,
                                       {kept.state_type, kept.state_key, kept.state_json.empty() ? std::string("{}") : kept.state_json}};
                  },
                      [at = &colours](const state_basis& basis) { return state_editor(*at, basis); })))),
      c::shown_for<facts_t>([](const facts_t& now) { return kept_of(now).account_editor; },
          rows_of<account_editor_t, account_basis>([](const facts_t& now) {
            const auto& kept = kept_of(now);
            return account_basis{kept.account_round, {kept.account_type, kept.account_json.empty() ? std::string("{}") : kept.account_json}};
          },
              [at = &colours](const account_basis& basis) { return account_editor(*at, basis); })));
}
}  // namespace mux::proto::matrix::manage_detail

// ---- the tabs: their titles, icons and pages ----------------------------------------
export namespace mux::proto::matrix {
inline ui::icon_t manage_avatar_icon(const state&, const ui::room_settings_facts& facts) {
  return facts.encrypted ? ui::icon_t{ui::icon::lock{}} : ui::icon_t{ui::icon::none{}};
}
inline ui::room_manage_style manage_style(const state&, const ui::palette& original) { return {original, true, 222.0f}; }
constexpr std::string_view manage_preferences_title(const state&) { return "Preferences"; }
// A room avatar being sent: shown pending, the last error gone.
inline void avatar_sending(manage_state& kept) {
  kept.pending = true;
  kept.error.clear();
}
}  // namespace mux::proto::matrix
export namespace mux::proto::matrix::manage {
constexpr std::string_view tab_title(room) { return "General"; }
constexpr std::string_view tab_title(members) { return "Members"; }
constexpr std::string_view tab_title(roles) { return "Permissions"; }
constexpr std::string_view tab_title(packs) { return "Emojis & Stickers"; }
constexpr std::string_view tab_title(advanced) { return "Developer Tools"; }
inline ui::icon_t tab_icon(room) { return ui::icon::gear{}; }
inline ui::icon_t tab_icon(members) { return ui::icon::person{}; }
inline ui::icon_t tab_icon(roles) { return ui::icon::lock{}; }
inline ui::icon_t tab_icon(packs) { return ui::icon::smile{}; }
inline ui::icon_t tab_icon(advanced) { return ui::icon::terminal{}; }
// What these pages may ask, whatever their trees: outside a release build
// each is held erased (made in its own unit, manage_*.cc), so the dialog
// that holds them is not made again for every change of a page.
using manage_out = skiff::model::Types<request::manage_room, request::open_send_custom, ui::request::open_url, ui::request::pick_room_avatar,
                                       ui::request::open_room_packs, ui::request::flip_developer_tools, ui::request::copy_text,
                                       ui::request::open_member_info>;
using erased_page = skiff::compose::Erased<manage_out>;
erased_page manage_page_erased(room, const ui::palette&, const ui::room_settings_facts&);
erased_page manage_page_erased(members, const ui::palette&, const ui::room_settings_facts&);
erased_page manage_page_erased(roles, const ui::palette&, const ui::room_settings_facts&);
erased_page manage_page_erased(packs, const ui::palette&, const ui::room_settings_facts&);
erased_page manage_page_erased(advanced, const ui::palette&, const ui::room_settings_facts&);
// Each tab's page.
inline auto page_of(room, const ui::palette& colours, const ui::room_settings_facts& facts) { return manage_detail::general_page(colours, facts); }
inline auto page_of(members, const ui::palette& colours, const ui::room_settings_facts& facts) { return manage_detail::members_page(colours, facts); }
inline auto page_of(roles, const ui::palette& colours, const ui::room_settings_facts& facts) { return manage_detail::permissions_page(colours, facts); }
inline auto page_of(packs, const ui::palette& colours, const ui::room_settings_facts& facts) { return manage_detail::packs_page(colours, facts); }
inline auto page_of(advanced, const ui::palette& colours, const ui::room_settings_facts& facts) { return manage_detail::developer_page(colours, facts); }
template <class Tab>
auto make_manage_page(Tab tab, const ui::palette& colours, const ui::room_settings_facts& facts)
  requires requires { page_of(tab, colours, facts); }
{
  if constexpr (skiff::scene::kErasedWalks)
    return manage_page_erased(tab, colours, facts);
  else
    return page_of(tab, colours, facts);
}
}  // namespace mux::proto::matrix::manage
