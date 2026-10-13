// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui.proto.matrix:manage -- Cinny's room settings, expressed with the
// client's typed requests and skiff combinators. Reference: cinnyapp/cinny,
// 8967c13878137841e49ee17184b689a6b4da334a, features/room-settings and
// features/common-settings. The five pages keep the same responsibilities.
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

// Drafts belong to the settings window, so changing a page does not submit
// or discard them. Only an acknowledged write clears a permission draft.
enum class state_screen { overview, view, edit, compose };
struct part {
  room_management data;
  bool loading = true, pending = false;
  std::string error;
  bool confirming_encryption = false, confirming_upgrade = false;
  std::optional<power_needs> permissions;
  std::optional<room_change::set_profile> profile;
  bool profile_editor = false, powers_editor = false, power_form = false;
  std::optional<std::int64_t> editing_power;
  std::optional<std::map<std::string, power_tag>> tag_draft;
  std::set<std::int64_t> deleted_powers;
  std::string local_address, upgrade_version, upgrade_creators;
  bool local_addresses_expanded = false;
  std::set<std::string> selected_aliases;
  std::string custom_event, custom_level;
  std::string tag_power, tag_name, tag_color, tag_icon;
  std::string account_type, account_json;
  state_screen state_page = state_screen::overview;
  std::string expanded_state_type, state_type, state_key, state_json, state_source;
  bool expanded_state = false, expanded_account_data = false, account_editor = false;
  std::vector<emote_pack> packs;
  std::string pack_name;
  std::string member_query;
  std::size_t member_filter = 0, member_order = 0;
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
  return std::ranges::to<std::vector<std::int64_t>>(values);
}
inline void assign_need(power_needs& needs, const power_need_t& need, std::int64_t level) {
  spl::visit(spl::overloaded{
      [&](power_need::default_role) { needs.users_default = level; },
      [&](power_need::send_messages) { needs.events_default = level; },
      [&](power_need::change_settings) { needs.state_default = level; },
      [&](power_need::invite) { needs.invite = level; }, [&](power_need::kick) { needs.kick = level; },
      [&](power_need::ban) { needs.ban = level; }, [&](power_need::redact) { needs.redact = level; },
      [&](power_need::notify_everyone) { needs.notify_room = level; },
      [&]<sends_state Need>(Need) { needs.events.insert_or_assign(std::string(Need::event), level); }}, need);
}
// Message events and state events have different defaults, even though
// Matrix stores their overrides together in power_levels.events.
struct event_permission { std::string type; bool state = true; };
using permission_t = spl::variant<power_need_t, event_permission>;
[[nodiscard]] inline std::int64_t required(const power_needs& needs, const permission_t& target) {
  return spl::visit(spl::overloaded{[&](const power_need_t& need) { return needs.of(need); },
      [&](const event_permission& event) {
        const auto found = needs.events.find(event.type);
        return found == needs.events.end() ? (event.state ? needs.state_default : needs.events_default) : found->second;
      }}, target);
}
inline void assign_permission(power_needs& needs, const permission_t& target, std::int64_t value) {
  spl::visit(spl::overloaded{[&](const power_need_t& need) { assign_need(needs, need, value); },
      [&](const event_permission& event) { needs.events.insert_or_assign(event.type, value); }}, target);
}
template <class Box> [[nodiscard]] bool editable(Box* box, const power_need_t& need) {
  const auto& kept = box->template part<state>();
  return !kept.pending && may(box->facts, need);
}
template <class Box> std::optional<request::change_room> submit(Box* box, room_change_t change) {
  auto& kept = box->template part<state>();
  if (kept.pending) return std::nullopt;
  kept.pending = true;
  kept.error.clear();
  box->show_again();
  return request::change_room{std::move(change)};
}
template <class Box> void loaded(Box* box, const management_loaded& one) {
  auto& kept = box->template part<state>();
  box->show_again(); // Preserve editor text before replacing metadata.
  kept.data = one.data;
  kept.loading = false;
  kept.error = one.error;
  if (kept.state_page == state_screen::view) {
    const auto found = std::ranges::find_if(kept.data.state_events, [&](const auto& event) {
      return event.type == kept.state_type && event.key == kept.state_key;
    });
    if (found != kept.data.state_events.end()) { kept.state_source = found->json; kept.state_json = found->content_json; }
  }
}
// A room avatar being sent: shown as pending, the last error gone.
inline void avatar_sending(part& kept) { kept.pending = true; kept.error.clear(); }
template <class Box> void avatar_uploaded(Box* box, const room_avatar_uploaded& one) {
  auto& kept = box->template part<state>();
  box->show_again(); // Preserve fields before adding the upload to their draft.
  kept.pending = false; kept.error = one.error;
  if (one.error.empty()) {
    if (!kept.profile) kept.profile.emplace();
    kept.profile->avatar = one.uri;
  }
}
template <class Box> void completed(Box* box, const management_saved& one) {
  auto& kept = box->template part<state>();
  kept.pending = false;
  kept.error = one.error;
  if (one.error.empty()) {
    auto& facts = box->facts;
    auto& rules = rules_in(facts.theirs);
    spl::visit(spl::overloaded{
        [&](const room_change::set_join_rule& change) { rules.join_rule = change.rule; },
        [&](const room_change::set_history& change) { rules.history = change.rule; },
        [&](room_change::encrypt) { facts.encrypted = true; },
        [&](const room_change::set_permissions& change) { rules.needs = change.after; rules.power_default = change.after.users_default; kept.permissions.reset(); },
        [&](const room_change::set_tags& change) { kept.data.tags = change.tags; kept.tag_draft.reset(); kept.deleted_powers.clear(); },
        [&](const room_change::set_profile& change) { if (change.name) facts.name = *change.name; if (change.topic) facts.topic = *change.topic; if (change.avatar) facts.avatar = change.avatar->empty() ? std::nullopt : change.avatar; kept.profile.reset(); kept.profile_editor = false; },
        [&](const room_change::set_addresses& change) { facts.alias = change.main; facts.other_aliases = std::ranges::to<std::vector<std::string>>(std::views::filter(change.published, [&](const auto& alias) { return !change.main || alias != *change.main; })); },
        [&](const room_change::set_directory& change) { kept.data.directory = change.published; },
        [&](const room_change::set_power& change) { rules.powers.insert_or_assign(change.user, change.level);
          if (change.user == facts.chat.account.address) facts.mine = change.level; },
        [&](const room_change::set_need& change) { assign_need(rules.needs, change.need, change.level); },
        [&](const room_change::set_event_need& change) { rules.needs.events.insert_or_assign(change.event, change.level); },
        [&](const room_change::add_child& change) {
          const auto child = std::ranges::find(facts.addable, change.room, &room_settings_facts::named_room::id);
          if (child != facts.addable.end()) { facts.children.push_back(*child); facts.addable.erase(child); }
        },
        [&](const room_change::remove_child& change) {
          const auto child = std::ranges::find(facts.children, change.room, &room_settings_facts::named_room::id);
          if (child != facts.children.end()) { facts.addable.push_back(*child); facts.children.erase(child); }
        },
        [](const auto&) {}}, one.change);
  }
  box->show_again();
  if (one.error.empty()) spl::visit(spl::overloaded{
      [&](const room_change::set_state_event& change) {
        kept.state_json = change.json;
        if (one.saved_state) {
          kept.state_source = one.saved_state->json;
          kept.state_json = one.saved_state->content_json;
          const auto found = std::ranges::find_if(kept.data.state_events, [&](const auto& event) { return event.type == change.type && event.key == change.key; });
          if (found == kept.data.state_events.end()) kept.data.state_events.push_back(*one.saved_state);
          else *found = *one.saved_state;
        }
        kept.state_page = kept.state_page == state_screen::compose ? state_screen::overview : state_screen::view;
      },
      [&](const room_change::create_alias&) { kept.local_address.clear(); },
      [&](const room_change::delete_aliases&) { kept.selected_aliases.clear(); },
      [&](const room_change::set_addresses&) { kept.selected_aliases.clear(); },
      [](const auto&) {}}, one.change);
}

// Folds 2.7.2: S100=4, S200=8, S300=12, S400=16, S700=32.
// Page content, cards and controls use the same tokens as Cinny's components.
inline skiff::compose::Look page_look() {
  return skiff::compose::vbox(32.0f, {.fillX = true, .autoSize = scene::axes::kY,
      .padding = {16.0f, 16.0f, 100.0f, 16.0f}});
}
inline auto settings_section_heading(const palette& colours, std::string title) {
  return nodes::Text(std::move(title), 12.0f, colours.text);
}
template <class... Children> auto settings_surface(const palette& colours, skia::SkColor background,
    scene::Margin padding, float gap, Children... children) {
  return skiff::compose::column(skiff::compose::vbox(gap, {.fillX = true, .autoSize = scene::axes::kY,
      .padding = padding, .cornerRadius = std::max(10.0f, colours.widgets.fCorner), .background = background, .masking = true}), std::move(children)...);
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
enum class action_style { secondary, primary, success };
template <class Act> auto action(const palette& colours, std::string label, Act act, bool allowed = true, action_style style = action_style::secondary) {
  namespace c = skiff::compose;
  auto spoken = label;
  const auto background = style == action_style::secondary ? colours.chosen : colours.accent;
  const auto ink = style == action_style::secondary ? colours.text : colours.on_accent;
  return c::onPress(std::move(act), c::row(c::hbox(0.0f, {.height = 32.0f, .relativeSize = scene::axes::kNone, .autoSize = scene::axes::kX, .alignSelf = scene::align::kMiddle, .padding = {0.0f, 12.0f, 0.0f, 12.0f}, .cornerRadius = colours.widgets.fCorner, .background = background, .hoverBackground = colours.widgets.fSurfaceHover, .border = scene::Border{colours.widgets.fSurfaceActive, style == action_style::secondary ? 1.0f : 0.0f}, .alpha = allowed ? 1.0f : 0.38f, .disabled = !allowed}),
      c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(std::move(label), 12.0f, ink))), std::move(spoken));
}
template <class Act> auto switch_control(const palette& colours, bool on, Act act, bool allowed = true) {
  auto toggle = widgets::Toggle<Act>(colours.widgets, std::move(act));
  toggle.setOnNow(on);
  toggle.apply({.relativeSize = scene::axes::kNone, .alignSelf = scene::align::kMiddle, .disabled = !allowed});
  return toggle;
}
inline auto choice_head(const palette& colours, std::string value) {
  namespace c = skiff::compose;
  return c::row(c::hbox(4.0f, {.fillX = true, .height = 24.0f, .padding = {0.0f, 8.0f, 0.0f, 8.0f},
      .cornerRadius = colours.widgets.fCorner, .background = colours.chosen, .hoverBackground = colours.widgets.fSurfaceHover}),
      c::styled({.width = 12.0f, .height = 12.0f, .alignSelf = scene::align::kMiddle}, nodes::Icon(shape_of(icon::down{}), colours.text)),
      c::styled({.shrink = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(nodes::Text(std::move(value), 12.0f, colours.text))));
}
template <class Choose> auto compact_choice(const palette& colours, std::string,
    std::vector<std::string> names, std::size_t selected, Choose choose) {
  const auto value = selected < names.size() ? names[selected] : std::string();
  const auto width = std::clamp(static_cast<float>(value.size()) * 6.5f + 32.0f, 72.0f, 180.0f);
  auto head = choice_head(colours, value);
  auto rows = std::ranges::to<std::vector>(std::views::transform(names, [&](const auto& name) { return choice_menu_option(colours, name); }));
  auto menu = widgets::ChoiceMenu<Choose, decltype(head), choice_menu_option_t, show_choice_menu>(
      std::move(names), selected, std::move(choose), std::move(head), std::move(rows), show_choice_menu{colours.text, colours.accent});
  menu.apply({.width = width, .relativeSize = scene::axes::kNone, .alignSelf = scene::align::kMiddle});
  return menu;
}
inline auto multiline_input(const palette& colours, std::string text, std::string placeholder, float height = 96.0f) {
  auto input = std::make_shared<widgets::TextArea<>>(colours.widgets, std::move(placeholder));
  input->setText(std::move(text));
  input->setSingleLine(false);
  input->setMaxLines(6);
  input->apply({.fillX = true, .height = height, .padding = {8.0f, 12.0f, 8.0f, 12.0f}, .cornerRadius = colours.widgets.fCorner,
                .background = colours.chosen, .border = scene::Border{colours.widgets.fSurfaceActive, 1.0f}});
  return input;
}
enum class editor { profile, powers, power, developer, state_events, account_data, account_form };
template <class Box> struct edit_section {
  using Answer = skiff::model::Nothing;
  Box* box; editor target; bool open = true;
  skiff::model::Nothing operator()() const {
    box->show_again();
    auto& kept = box->template part<state>();
    switch (target) {
      case editor::profile: kept.profile_editor = open; if (!open) kept.profile.reset(); break;
      case editor::powers: kept.powers_editor = open; break;
      case editor::power: kept.power_form = open; if (open) { kept.editing_power.reset(); kept.tag_power.clear(); kept.tag_name.clear(); kept.tag_color.clear(); kept.tag_icon.clear(); } break;
      case editor::developer: break;
      case editor::state_events: kept.expanded_state = !kept.expanded_state; break;
      case editor::account_data: kept.expanded_account_data = !kept.expanded_account_data; break;
      case editor::account_form: kept.account_editor = open; break;
    }
    return {};
  }
};
// General: profile, options, addresses, and advanced options in Cinny's order.
struct remove_avatar {};
struct save_profile {};
struct create_address {};
struct upgrade_room {};
struct cancel_upgrade {};
struct save_power {};
struct delete_power {};
// The view owns each editor; its draft keeps a stable, non-owning pointer
// so skiff can construct the draft while compiling its field indices.
template <class Box> struct general_draft { Box* box; std::string name, topic, address, version, creators; std::optional<std::string> avatar; widgets::TextArea<>* topic_input = nullptr; };
template <class Box> struct general_actions {
  struct choose_access {
    Box* box; std::vector<join_rule_t> rules;
    using Answer = std::optional<request::change_room>;
    Answer operator()(std::size_t index) const {
      if (!editable(box, power_need::change_access{}) || index >= rules.size()) return std::nullopt;
      return submit(box, room_change::set_join_rule{rules[index]});
    }
  };
  struct choose_history {
    Box* box;
    using Answer = std::optional<request::change_room>;
    Answer operator()(std::size_t index) const {
      const std::array<history_rule_t, 4> rules{history_rule::shared{}, history_rule::invited{}, history_rule::joined{}, history_rule::world_readable{}};
      if (!editable(box, power_need::change_history{}) || index >= rules.size()) return std::nullopt;
      return submit(box, room_change::set_history{rules[index]});
    }
  };
  struct encryption {
    Box* box; bool cancel = false;
    using Answer = std::optional<request::change_room>;
    Answer operator()() const {
      auto& kept = box->template part<state>();
      if (cancel) { kept.confirming_encryption = false; box->show_again(); return std::nullopt; }
      if (!editable(box, power_need::encrypt{}) || box->facts.encrypted) return std::nullopt;
      if (!std::exchange(kept.confirming_encryption, true)) { box->show_again(); return std::nullopt; }
      kept.confirming_encryption = false;
      return submit(box, room_change::encrypt{});
    }
  };
  struct directory {
    Box* box;
    using Answer = std::optional<request::change_room>;
    Answer operator()() const {
      const auto& kept = box->template part<state>();
      const auto valid = spl::visit(spl::overloaded{[](join_rule::open) { return true; }, [](join_rule::knock) { return true; },
          [](const join_rule::knock_restricted&) { return true; }, [](const auto&) { return false; }}, rules_of(box->facts.theirs).join_rule);
      if (!editable(box, power_need::change_address{}) || !valid || !kept.data.directory) return std::nullopt;
      return submit(box, room_change::set_directory{!*kept.data.directory});
    }
  };
  struct address_action {
    Box* box; std::string alias;
    enum class kind { main, publish, unpublish, remove } what;
    using Answer = std::optional<request::change_room>;
    Answer operator()() const {
      const auto& kept = box->template part<state>();
      if (kept.pending) return std::nullopt;
      if (what == kind::remove) return submit(box, room_change::delete_alias{alias});
      if (!editable(box, power_need::change_address{})) return std::nullopt;
      auto main = box->facts.alias;
      auto published = box->facts.other_aliases;
      if (main && !std::ranges::contains(published, *main)) published.push_back(*main);
      switch (what) {
        case kind::main: main = main == alias ? std::nullopt : std::optional(alias); break;
        case kind::publish: if (!std::ranges::contains(published, alias)) published.push_back(alias); break;
        case kind::unpublish: std::erase(published, alias); break;
        case kind::remove: break;
      }
      return submit(box, room_change::set_addresses{main, std::move(published)});
    }
  };
};
template <class Box> auto access_menu(Box* box) {
    const auto& facts = box->facts;
    const auto version = integer(rules_of(facts.theirs).version).value_or(12);
    std::vector<join_rule_t> rules{join_rule::invite{}};
    std::vector<std::string> names{"Invite Only"};
    if (version >= 7) { rules.emplace_back(join_rule::knock{}); names.emplace_back("Knock & Invite"); }
    const auto parents = std::ranges::to<std::vector<std::string>>(std::views::transform(facts.parents, &room_settings_facts::named_room::id));
    if (version >= 8 && !parents.empty()) { rules.emplace_back(join_rule::restricted{parents}); names.emplace_back("Space Members"); }
    if (version >= 10 && !parents.empty()) { rules.emplace_back(join_rule::knock_restricted{parents}); names.emplace_back("Space Members or Knock"); }
    rules.emplace_back(join_rule::open{}); names.emplace_back("Public");
    const auto selected = std::ranges::find_if(rules, [&](const auto& rule) { return rule.index() == rules_of(facts.theirs).join_rule.index(); });
    const auto index = static_cast<std::size_t>(selected - rules.begin());
    return skiff::compose::styled({.relativeSize = scene::axes::kNone, .disabled = !editable(box, power_need::change_access{})},
        compact_choice(*box->colours_, "", std::move(names), index, typename general_actions<Box>::choose_access{box, std::move(rules)}));
  }

template <class Box> struct general_events {
  Box* box;
  auto on(save_profile, const general_draft<Box>& draft) const {
    room_change::set_profile change;
    if (editable(box, power_need::rename{}) && draft.name != box->facts.name) change.name = draft.name;
    const auto& topic = draft.topic_input ? draft.topic_input->text() : draft.topic;
    if (editable(box, power_need::retopic{}) && topic != box->facts.topic) change.topic = topic;
    if (editable(box, power_need::change_avatar{}) && draft.avatar && *draft.avatar != box->facts.avatar.value_or("")) change.avatar = draft.avatar;
    return skiff::model::Up{change.name || change.topic || change.avatar ? submit(box, std::move(change)) : std::nullopt};
  }
  auto on(remove_avatar, const general_draft<Box>& draft) const {
    auto avatar = editable(box, power_need::change_avatar{}) ? std::optional<std::string>("") : draft.avatar;
    return skiff::model::over<skiff::model::Field<&general_draft<Box>::avatar>>(skiff::model::setTo(std::move(avatar)));
  }
  auto on(create_address, const general_draft<Box>& draft) const {
    const auto first = draft.address.find_first_not_of(" \t\r\n");
    const auto last = draft.address.find_last_not_of(" \t\r\n");
    auto local = first == std::string::npos ? std::string() : draft.address.substr(first, last - first + 1);
    std::ranges::replace(local, ' ', '-');
    const auto colon = box->facts.chat.account.address.find(':');
    return skiff::model::Up{local.empty() || colon == std::string::npos || local.find_first_of("#:\n\r") != std::string::npos
        ? std::nullopt : submit(box, room_change::create_alias{"#" + local + box->facts.chat.account.address.substr(colon)})};
  }
  auto on(upgrade_room, const general_draft<Box>& draft) const {
    auto& kept = box->template part<state>();
    std::optional<request::change_room> answer;
    if (editable(box, power_need::upgrade{})) {
      if (!std::exchange(kept.confirming_upgrade, true)) box->show_again();
      else if (std::ranges::contains(kept.data.versions, draft.version)) {
        kept.confirming_upgrade = false; const auto creators = std::ranges::to<std::vector<std::string>>(std::views::transform(
            std::views::filter(std::views::split(draft.creators, ' '), [](auto id) { return !id.empty(); }),
            [](auto id) { return std::string(id.begin(), id.end()); }));
        answer = submit(box, room_change::upgrade{draft.version,
            integer(draft.version).value_or(0) >= 12 ? std::optional(creators) : std::nullopt});
      } else { kept.error = "Choose a room version supported by your server."; box->show_again(); }
    }
    return skiff::model::Up{std::move(answer)};
  }
  auto on(cancel_upgrade, const general_draft<Box>&) const {
    box->template part<state>().confirming_upgrade = false;
    box->show_again();
    return skiff::model::Nothing{};
  }
};
template <class Box> void preserve_draft(const general_draft<Box>& draft) {
  auto& kept = draft.box->template part<state>();
  room_change::set_profile profile;
  if (draft.name != draft.box->facts.name) profile.name = draft.name;
  const auto& topic = draft.topic_input ? draft.topic_input->text() : draft.topic;
  if (topic != draft.box->facts.topic) profile.topic = topic;
  if (draft.avatar && *draft.avatar != draft.box->facts.avatar.value_or("")) profile.avatar = draft.avatar;
  kept.profile = profile.name || profile.topic || profile.avatar ? std::optional(profile) : std::nullopt;
  kept.local_address = draft.address; kept.upgrade_version = draft.version; kept.upgrade_creators = draft.creators;
}
template <class Box> struct expand_addresses {
  using Answer = skiff::model::Nothing;
  Box* box;
  Answer operator()() const {
    auto& kept = box->template part<state>();
    kept.local_addresses_expanded = !kept.local_addresses_expanded;
    box->show_again();
    return {};
  }
};
template <class Box> struct select_address {
  using Answer = skiff::model::Nothing;
  Box* box; std::string alias;
  Answer operator()() const {
    auto& kept = box->template part<state>();
    if (kept.pending) return {};
    if (!kept.selected_aliases.erase(alias)) kept.selected_aliases.insert(alias);
    box->show_again();
    return {};
  }
};
template <class Box> struct selected_addresses {
  using Answer = std::optional<request::change_room>;
  Box* box;
  enum class kind { publish, unpublish, remove } what;
  Answer operator()() const {
    const auto& kept = box->template part<state>();
    if (kept.pending || kept.selected_aliases.empty()) return {};
    if (what == kind::remove)
      return submit(box, room_change::delete_aliases{std::ranges::to<std::vector<std::string>>(kept.selected_aliases)});
    if (!editable(box, power_need::change_address{})) return {};
    auto published = box->facts.other_aliases;
    std::ranges::for_each(kept.selected_aliases, [&](const auto& alias) {
      if (what == kind::unpublish) std::erase(published, alias);
      else if (!std::ranges::contains(published, alias)) published.push_back(alias);
    });
    return submit(box, room_change::set_addresses{box->facts.alias, std::move(published)});
  }
};
inline auto address_badge(const palette& colours, std::string label) {
  return skiff::compose::row(skiff::compose::hbox(0.0f, {.relativeSize = scene::axes::kNone,
      .autoSize = scene::axes::kBoth, .alignSelf = scene::align::kMiddle,
      .padding = {4.0f, 8.0f, 4.0f, 8.0f}, .cornerRadius = colours.widgets.fCorner,
      .background = colours.chosen}), nodes::Text(std::move(label), 12.0f, colours.accent));
}
template <class Box> auto published_address_row(Box* box, const std::string& alias) {
  namespace c = skiff::compose; using act = typename general_actions<Box>::address_action;
  const auto& colours = *box->colours_;
  auto label = nodes::Text(alias, 14.0f, colours.text);
  label.setBold(box->facts.alias == alias);
  return c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::row(c::hbox(8.0f, {.relativeSize = scene::axes::kNone, .autoSize = scene::axes::kY, .grow = scene::axes::kX}),
          c::styled({.shrink = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(std::move(label))),
          c::visible(box->facts.alias == alias, address_badge(colours, "Main"))),
      c::visible(may(box->facts, power_need::change_address{}), action(colours,
          box->facts.alias == alias ? "Unset Main" : "Set Main", act{box, alias, act::kind::main}, !box->template part<state>().pending)));
}
template <class Box> auto local_address_row(Box* box, const std::string& alias, bool published) {
  namespace c = skiff::compose;
  const auto& colours = *box->colours_; const auto& kept = box->template part<state>();
  auto checkbox = widgets::internal::Toggle<select_address<Box>, widgets::internal::CheckboxLook>(colours.widgets, {box, alias});
  checkbox.setOnNow(kept.selected_aliases.contains(alias));
  checkbox.apply({.relativeSize = scene::axes::kNone, .alignSelf = scene::align::kMiddle, .disabled = kept.pending});
  return c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(checkbox),
      c::styled({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX, .shrink = scene::axes::kX,
          .alignSelf = scene::align::kMiddle}, elided(nodes::Text(alias, 14.0f, colours.text))),
      c::visible(published, address_badge(colours, "Published")));
}
template <class Box> struct choose_child {
  Box* box; std::vector<std::string> rooms;
  using Answer = std::optional<request::change_room>;
  Answer operator()(std::size_t index) const {
    return editable(box, power_need::change_settings{}) && index < rooms.size()
        ? submit(box, room_change::add_child{rooms[index]}) : std::nullopt;
  }
};
template <class Box> struct remove_child {
  Box* box; std::string room;
  using Answer = std::optional<request::change_room>;
  Answer operator()() const { return editable(box, power_need::change_settings{}) ? submit(box, room_change::remove_child{room}) : std::nullopt; }
};
template <class Box> auto space_children(Box* box, const room_settings_facts& facts) {
  namespace c = skiff::compose;
  const auto rooms = std::ranges::to<std::vector<std::string>>(std::views::transform(facts.addable, &room_settings_facts::named_room::id));
  const auto names = std::ranges::to<std::vector<std::string>>(std::views::transform(facts.addable, &room_settings_facts::named_room::name));
  return c::visible(facts.space, c::column(c::vbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      part_heading(*box->colours_, "Rooms in this Space"),
      c::many(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::ranges::to<std::vector>(
          std::views::transform(facts.children, [&](const auto& child) {
            return c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                nodes::Text(child.name, 14.0f, box->colours_->text), action(*box->colours_, "Remove", remove_child<Box>{box, child.id}, editable(box, power_need::change_settings{})));
          }))), c::styled({.disabled = !editable(box, power_need::change_settings{})},
          compact_choice(*box->colours_, "Add Room", names, rooms.size(), choose_child<Box>{box, rooms}))));
}
inline auto related_room(const palette& colours, std::string label, const std::optional<std::string>& room) {
  return skiff::compose::visible(room.has_value(), action(colours, std::move(label),
      sends<::mux::ui::request::open_url>{{"https://matrix.to/#/" + room.value_or("")}}));
}
template <class Draft> auto local_address_input(const palette& colours, std::string server, bool pending) {
  namespace c = skiff::compose;
  auto theme = colours.widgets;
  theme.fCorner = std::max(10.0f, theme.fCorner);
  theme.fSurface = colours.chosen;
  return c::row(c::hbox(4.0f, {.height = 44.0f, .relativeSize = scene::axes::kNone, .grow = scene::axes::kX,
      .padding = {0.0f, 8.0f, 0.0f, 8.0f}, .cornerRadius = theme.fCorner, .background = colours.chosen, .masking = true}),
      c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text("#", 12.0f, colours.dim)),
      c::bound<skiff::model::Field<&Draft::address>>(c::styled({.height = 36.0f, .relativeSize = scene::axes::kNone,
          .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle, .disabled = pending},
          widgets::TextField<std::string>(theme, "New address"))),
      c::styled({.shrink = scene::axes::kX, .maxWidth = 160.0f, .alignSelf = scene::align::kMiddle},
          elided(nodes::Text(std::move(server), 12.0f, colours.dim))));
}
template <class Box> auto general_page(Box* box, const room_settings_facts& facts) {
  namespace c = skiff::compose; using acts = general_actions<Box>; using draft = general_draft<Box>;
  const auto& colours = *box->colours_; const auto& kept = box->template part<state>();
  auto topic_input = multiline_input(colours, kept.profile && kept.profile->topic ? *kept.profile->topic : facts.topic, "");
  topic_input->apply({.disabled = !editable(box, power_need::retopic{})});
  auto published = facts.other_aliases;
  if (facts.alias && !std::ranges::contains(published, *facts.alias)) published.insert(published.begin(), *facts.alias);
  auto published_rows = c::many(c::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      std::ranges::to<std::vector>(std::views::transform(published, [&](const auto& alias) { return published_address_row(box, alias); })));
  auto local_rows = c::many(c::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      std::ranges::to<std::vector>(std::views::transform(kept.data.local_aliases, [&](const auto& alias) {
        return local_address_row(box, alias, std::ranges::contains(published, alias));
      })));
  using selection = selected_addresses<Box>;
  const bool selected_published = std::ranges::any_of(kept.selected_aliases, [&](const auto& alias) { return std::ranges::contains(published, alias); });
  const auto colon = facts.chat.account.address.find(':');
  const auto server = colon == std::string::npos ? std::string() : facts.chat.account.address.substr(colon);
  return c::local<draft>(general_events<Box>{box}, c::column(page_look(),
      c::visible(kept.pending || kept.loading || !kept.error.empty(), explained(colours, kept.pending ? "Saving…" : kept.loading ? "Loading…" : kept.error)),
      section(colours, "Profile", card(colours,
          c::visible(!kept.profile_editor, c::row(c::hbox(16.0f, {.fillX = true, .autoSize = scene::axes::kY}),
              c::column(c::vbox(12.0f, {.relativeSize = scene::axes::kNone, .autoSize = scene::axes::kY, .grow = scene::axes::kX}),
                  c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                      wrapped(nodes::Text(facts.name, 16.0f, colours.text)),
                      c::visible(!facts.topic.empty(), wrapped(nodes::Text(facts.topic, 12.0f, colours.text)))),
                  c::visible(editable(box, power_need::rename{}) || editable(box, power_need::retopic{}) || editable(box, power_need::change_avatar{}),
                      action(colours, "Edit", edit_section<Box>{box, editor::profile}))), avatar_mark(avatar_of::chat{}, facts.id, facts.name, 96.0f))),
          c::visible(kept.profile_editor, c::column(c::vbox(16.0f, {.fillX = true, .autoSize = scene::axes::kY}),
              c::row(c::hbox(16.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                  c::column(c::vbox(4.0f, {.relativeSize = scene::axes::kNone, .autoSize = scene::axes::kY, .grow = scene::axes::kX}),
                      settings_section_heading(colours, "Avatar"),
                      c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                          action(colours, "Upload", sends<::mux::ui::request::pick_room_avatar>{{facts.chat}}, editable(box, power_need::change_avatar{})),
                          action(colours, "Remove", sends<remove_avatar>{}, editable(box, power_need::change_avatar{})))),
                  avatar_mark(avatar_of::chat{}, facts.id, facts.name, 96.0f)),
              c::shown_for<skiff::model::Field<&draft::avatar>>([](const auto& avatar) { return avatar.has_value(); }, explained(colours, "Avatar change pending. Save to apply.")),
              c::styled({.disabled = !editable(box, power_need::rename{})}, model_field<&draft::name>(colours, "Name", "")),
              c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}), settings_section_heading(colours, "Topic"), topic_input),
              c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                  action(colours, "Save", sends<save_profile>{}, editable(box, power_need::rename{}) || editable(box, power_need::retopic{}) || editable(box, power_need::change_avatar{}), action_style::success),
                  action(colours, "Cancel", edit_section<Box>{box, editor::profile, false})))))),
      section(colours, "Options",
          tile(colours, facts.space ? "Space Access" : "Room Access", facts.space ? "Change how people can join the space." : "Change how people can join the room.", access_menu(box)),
          tile(colours, "Message History Visibility", "Changes to history visibility will only apply to future messages. The visibility of existing history will have no effect.",
              c::styled({.relativeSize = scene::axes::kNone, .disabled = !editable(box, power_need::change_history{})}, compact_choice(colours, "",
                  {"All Messages", "After Invite", "After Join", "All Messages (Guests)"}, rules_of(facts.theirs).history.index(), typename acts::choose_history{box}))),
          tile(colours, "Room Encryption", facts.encrypted ? "Messages in this room are protected by end-to-end encryption." : "Once enabled, encryption cannot be disabled!",
              action(colours, facts.encrypted ? "Enabled" : kept.confirming_encryption ? "Enable E2E Encryption" : "Enable", typename acts::encryption{box}, facts.encrypted || editable(box, power_need::encrypt{}), facts.encrypted ? action_style::success : action_style::primary)),
          c::visible(kept.confirming_encryption, card(colours,
              explained(colours, "Are you sure? Once enabled, encryption cannot be disabled!"),
              action(colours, "Cancel", typename acts::encryption{box, true}))),
          tile(colours, "Publish to Directory", facts.space ? "List the space in the public directory to make it discoverable by others." : "List the room in the public directory to make it discoverable by others.",
              switch_control(colours, kept.data.directory.value_or(false), typename acts::directory{box}, editable(box, power_need::change_address{}) && kept.data.directory.has_value()))),
      section(colours, "Addresses",
          card(colours, setting_tile(colours, "Published Addresses", "If access is Public, Published addresses will be used to join by anyone.", c::column()),
              cutout(colours, {12.0f, 12.0f, 12.0f, 12.0f}, c::visible(published.empty(), c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                  nodes::Text("No Addresses", 14.0f, colours.text),
                  wrapped(nodes::Text("To publish an address, it needs to be set as a local address first.", 12.0f, colours.dim)))), std::move(published_rows))),
          card(colours, setting_tile(colours, "Local Addresses", "Set local address so users can join through your homeserver.",
              action(colours, kept.local_addresses_expanded ? "Collapse" : "Expand", expand_addresses<Box>{box})),
              c::visible(kept.local_addresses_expanded, c::column(c::vbox(16.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                  cutout(colours, {12.0f, 12.0f, 12.0f, 12.0f},
                      c::visible(!kept.selected_aliases.empty(), c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                          c::styled({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, nodes::Text(std::format("{} Selected", kept.selected_aliases.size()), 12.0f, colours.text)),
                          c::visible(may(box->facts, power_need::change_address{}), action(colours, selected_published ? "Unpublish" : "Publish",
                              selection{box, selected_published ? selection::kind::unpublish : selection::kind::publish}, !kept.pending)),
                          action(colours, "Delete", selection{box, selection::kind::remove}, !kept.pending))),
                      c::visible(kept.data.local_aliases.empty(), nodes::Text(kept.loading ? "Loading…" : "No Addresses", 14.0f, colours.text)), std::move(local_rows)),
                  c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                      local_address_input<draft>(colours, server, kept.pending),
                      action(colours, "Save", sends<create_address>{}, !kept.pending, action_style::success)))))),
      space_children(box, facts),
      section(colours, "Advanced Options",
          tile(colours, facts.space ? "Upgrade Space" : "Upgrade Room", "Current version: " + rules_of(facts.theirs).version,
              action(colours, "Upgrade", sends<upgrade_room>{}, editable(box, power_need::upgrade{}) && !kept.data.versions.empty())),
          related_room(colours, "Previous Room", rules_of(facts.theirs).predecessor),
          related_room(colours, "Replacement Room", rules_of(facts.theirs).replaced_by),
          c::visible(kept.confirming_upgrade, card(colours,
              explained(colours, "Upgrading creates a replacement room. Members will need to move to the new room."),
              model_field<&draft::version>(colours, "Room Version", ""),
              model_field<&draft::creators>(colours, "Additional Creators (version 12 or newer)", "Space-separated Matrix IDs"),
              c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                  action(colours, "Upgrade", sends<upgrade_room>{}, editable(box, power_need::upgrade{})),
                  action(colours, "Cancel", sends<cancel_upgrade>{})))))),
      draft{box, kept.profile && kept.profile->name ? *kept.profile->name : facts.name,
          kept.profile && kept.profile->topic ? *kept.profile->topic : facts.topic, kept.local_address,
          kept.upgrade_version.empty() ? kept.data.default_version : kept.upgrade_version, kept.upgrade_creators,
          kept.profile ? kept.profile->avatar : std::nullopt, topic_input.get()});
}

// Members: the same membership filters and power-first ordering as Cinny.
// The query is kept by the page while only the displayed rows are replaced.
struct member_filter { std::size_t value; };
struct member_order { std::size_t value; };
struct select_filter { using Answer = member_filter; member_filter operator()(std::size_t value) const { return {value}; } };
struct select_order { using Answer = member_order; member_order operator()(std::size_t value) const { return {value}; } };
template <class Box> struct member_draft { Box* box; std::string query; std::size_t filter = 0, order = 0; };
template <class Box> struct member_events {
  auto on(member_filter event, const member_draft<Box>&) const {
    return skiff::model::over<skiff::model::Field<&member_draft<Box>::filter>>(skiff::model::setTo(event.value));
  }
  auto on(member_order event, const member_draft<Box>&) const {
    return skiff::model::over<skiff::model::Field<&member_draft<Box>::order>>(skiff::model::setTo(event.value));
  }
};
template <class Box> void preserve_draft(const member_draft<Box>& draft) {
  auto& kept = draft.box->template part<state>();
  kept.member_query = draft.query; kept.member_filter = draft.filter; kept.member_order = draft.order;
}
struct member_listing { std::vector<managed_member> rows; std::string empty; };
[[nodiscard]] inline std::vector<managed_member> available_members(const room_management& data, const room_settings_facts& facts) {
  if (!data.members.empty()) return data.members;
  return std::ranges::to<std::vector<managed_member>>(std::views::transform(facts.members, [](const auto& member) {
    return managed_member{member.id, member.name, member.avatar, membership_view::joined{}, 0};
  }));
}
template <class Box> struct choose_member_power {
  Box* box; std::string user; std::vector<std::int64_t> values;
  using Answer = std::optional<request::change_room>;
  Answer operator()(std::size_t index) const {
    if (!editable(box, power_need::change_permissions{}) || index >= values.size()) return std::nullopt;
    const auto current = level_of(rules_of(box->facts.theirs), user);
    if (current == kCreatorPower || (current >= box->facts.mine && user != box->facts.chat.account.address) || values[index] > box->facts.mine) return std::nullopt;
    return submit(box, room_change::set_power{user, values[index]});
  }
};
template <class Box> auto managed_member_row(Box* box, const managed_member& member) {
  namespace c = skiff::compose;
  const auto& rules = rules_of(box->facts.theirs); const auto& kept = box->template part<state>();
  const auto level = level_of(rules, member.id);
  auto values = power_values(rules, kept.data);
  if (level != kCreatorPower && !std::ranges::contains(values, level)) values.push_back(level);
  const auto selected = static_cast<std::size_t>(std::ranges::find(values, level) - values.begin());
  auto names = std::ranges::to<std::vector<std::string>>(std::views::transform(values, [&](auto value) { return power_name(value, kept.data); }));
  return c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::styled({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX}, member_row(*box->colours_, mux::member{member.id, member.name, std::nullopt, member.avatar}, power_name(level, kept.data))),
      c::styled({.relativeSize = scene::axes::kNone, .disabled = !editable(box, power_need::change_permissions{}) || level == kCreatorPower || (level >= box->facts.mine && member.id != box->facts.chat.account.address)},
          compact_choice(*box->colours_, "", std::move(names), selected, choose_member_power<Box>{box, member.id, std::move(values)})));
}
template <class Box> auto member_rows(Box* box, const member_listing& listing) {
  namespace c = skiff::compose;
  return c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          std::ranges::to<std::vector>(std::views::transform(listing.rows, [&](const auto& member) {
            return managed_member_row(box, member);
          }))), c::visible(listing.rows.empty(), explained(*box->colours_, listing.empty)));
}
template <class Box> auto members_page(Box* box, const room_settings_facts& facts) {
  namespace c = skiff::compose; using draft = member_draft<Box>;
  const auto& kept = box->template part<state>();
  auto listing = [box](const draft& now) {
    const auto& kept = box->template part<state>();
    const auto& rules = rules_of(box->facts.theirs);
    const auto wanted = logic::folded(now.query);
    auto rows = std::ranges::to<std::vector<managed_member>>(std::views::filter(available_members(kept.data, box->facts), [&](const auto& member) {
      return member.membership.index() == now.filter && (wanted.empty() || logic::folded(member.name).contains(wanted) || logic::folded(member.id).contains(wanted));
    }));
    std::ranges::stable_sort(rows, [&](const auto& a, const auto& b) {
      const auto ap = level_of(rules, a.id), bp = level_of(rules, b.id);
      if (ap != bp) return ap > bp;
      switch (now.order) {
        case 1: return logic::folded(a.name) > logic::folded(b.name);
        case 2: return a.timestamp > b.timestamp;
        case 3: return a.timestamp < b.timestamp;
        default: return logic::folded(a.name) < logic::folded(b.name);
      }
    });
    static const std::array<std::string_view, 5> labels{"Joined", "Invited", "Left", "Kicked", "Banned"};
    return member_listing{std::move(rows), kept.loading ? "Loading…" : std::format("No {} Members", labels.at(now.filter))};
  };
  using rows_t = decltype(member_rows(box, member_listing{}));
  auto rows = c::mount<rows_t, member_listing>([box](const member_listing& data) {
    return std::tuple{member_rows(box, data)};
  }, {.fillX = true, .autoSize = scene::axes::kY});
  const draft initial{box, kept.member_query, kept.member_filter, kept.member_order};
  // Protocol metadata can replace this page without changing the outer model.
  // Populate the mount now, rather than waiting for another binding revision.
  rows.read(std::optional{listing(initial)});
  return c::local<draft>(member_events<Box>{}, c::column(page_look(),
      c::visible(!kept.error.empty(), explained(*box->colours_, kept.error)),
      model_field<&draft::query>(*box->colours_, "", "Search"),
      c::row(c::justified(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}), nodes::justify::end{}),
          compact_choice(*box->colours_, "", {"Joined", "Invited", "Left", "Kicked", "Banned"}, kept.member_filter, select_filter{}),
          compact_choice(*box->colours_, "", {"A to Z", "Z to A", "Newest", "Oldest"}, kept.member_order, select_order{})),
      c::derived<draft>(listing, std::move(rows))), initial);
}

// Permissions: named powers, then Users, Messages, Calls, Moderation,
// Room Overview, Settings and Other. Changes are staged until Apply.
struct permission_item { std::string title; permission_t location; };
struct permission_group { std::string title; std::vector<permission_item> items; };
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
template <class Box> struct permission_actions {
  struct choose {
    Box* box; permission_t target; std::vector<std::int64_t> values;
    void operator()(std::size_t index) const {
      if (!editable(box, power_need::change_permissions{}) || index >= values.size()) return;
      const auto& original = rules_of(box->facts.theirs).needs;
      if (required(original, target) > box->facts.mine || values[index] > box->facts.mine) return;
      auto& draft = box->template part<state>().permissions;
      if (!draft) draft = original;
      assign_permission(*draft, target, values[index]);
      if (*draft == original) draft.reset();
      box->show_again();
    }
  };
  struct apply {
    Box* box; bool reset = false;
    using Answer = std::optional<request::change_room>;
    Answer operator()() const {
      auto& kept = box->template part<state>();
      if (kept.pending) return std::nullopt;
      if (reset) { kept.permissions.reset(); box->show_again(); return std::nullopt; }
      if (!editable(box, power_need::change_permissions{}) || !kept.permissions) return std::nullopt;
      return submit(box, room_change::set_permissions{rules_of(box->facts.theirs).needs, *kept.permissions});
    }
  };
};
struct add_custom_permission {};
template <class Box> struct power_draft { Box* box; std::string power, name, color, icon, event, level; };
template <class Box> struct power_events {
  Box* box;
  auto on(add_custom_permission, const power_draft<Box>& draft) const {
    const auto level = integer(draft.level);
    const auto& original = rules_of(box->facts.theirs).needs;
    if (!editable(box, power_need::change_permissions{}) || !level || *level > box->facts.mine ||
        draft.event.empty() || draft.event.find_first_of(" \t\n\r") != std::string::npos ||
        required(original, permission_t{event_permission{draft.event}}) > box->facts.mine)
      return skiff::model::Nothing{};
    box->show_again();
    auto& kept = box->template part<state>();
    if (!kept.permissions) kept.permissions = original;
    kept.permissions->events.insert_or_assign(draft.event, *level);
    if (*kept.permissions == original) kept.permissions.reset();
    kept.custom_event.clear(); kept.custom_level.clear();
    return skiff::model::Nothing{};
  }
  auto on(save_power, const power_draft<Box>& draft) const {
      if (box->template part<state>().pending) return skiff::model::Up{std::optional<request::change_room>{}};
      const auto number = integer(draft.power);
      const auto name = draft.name;
      const auto& rules = rules_of(box->facts.theirs);
      const auto event = rules.needs.events.find("in.cinny.room.power_level_tags");
      const auto need = event == rules.needs.events.end() ? rules.needs.state_default : event->second;
      if (!number || name.empty() || box->facts.mine < need) return skiff::model::Up{std::optional<request::change_room>{}};
      if (box->facts.mine != kCreatorPower && *number > box->facts.mine) return skiff::model::Up{std::optional<request::change_room>{}};
      auto tags = box->template part<state>().tag_draft.value_or(box->template part<state>().data.tags);
      auto& tag = tags[std::to_string(*number)];
      tag.name = name;
      const auto color = draft.color;
      tag.color = color.empty() ? std::nullopt : std::optional(color);
      const auto icon = draft.icon;
      if (!icon.empty()) { if (!tag.icon) tag.icon.emplace(); tag.icon->key = icon; } else tag.icon.reset();
      box->show_again();
      box->template part<state>().tag_draft = std::move(tags);
      box->template part<state>().power_form = false;
      return skiff::model::Up{std::optional<request::change_room>{}};
    }
  auto on(delete_power, const power_draft<Box>& draft) const {
    const auto number = integer(draft.power);
    const auto& rules = rules_of(box->facts.theirs);
    const auto found = rules.needs.events.find("in.cinny.room.power_level_tags");
    const auto need = found == rules.needs.events.end() ? rules.needs.state_default : found->second;
    if (!number || box->template part<state>().pending || box->facts.mine < need ||
        std::ranges::contains(power_values(rules, room_management{}), *number))
      return skiff::model::Up{std::optional<request::change_room>{}};
    auto tags = box->template part<state>().tag_draft.value_or(box->template part<state>().data.tags);
    if (!tags.erase(std::to_string(*number))) return skiff::model::Up{std::optional<request::change_room>{}};
    box->show_again();
      box->template part<state>().tag_draft = std::move(tags);
      box->template part<state>().power_form = false;
      return skiff::model::Up{std::optional<request::change_room>{}};
  }
};
template <class Box> void preserve_draft(const power_draft<Box>& draft) {
  auto& kept = draft.box->template part<state>();
  kept.tag_power = draft.power; kept.tag_name = draft.name;
  kept.tag_color = draft.color; kept.tag_icon = draft.icon;
  kept.custom_event = draft.event; kept.custom_level = draft.level;
}
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
  return c::row(c::hbox(4.0f, {.height = 24.0f, .relativeSize = scene::axes::kNone, .autoSize = scene::axes::kX, .padding = {0.0f, 8.0f, 0.0f, 8.0f}, .cornerRadius = colours.widgets.fCorner, .background = colours.chosen}),
      c::styled({.width = 14.0f, .height = 14.0f, .alignSelf = scene::align::kMiddle}, nodes::Icon(shape_of(icon::dot{colour}), colour)),
      c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(std::move(name), 14.0f, colours.text)));
}
template <class Box> auto power_chips(Box* box) {
  namespace c = skiff::compose;
  const auto& rules = rules_of(box->facts.theirs); const auto& kept = box->template part<state>();
  auto chips = c::many(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::ranges::to<std::vector>(
      std::views::transform(power_values(rules, kept.data), [&](auto value) {
        return power_chip(*box->colours_, power_name(value, kept.data), power_colour(value, kept.data));
      })));
  chips.fStack.wrap = true;
  return chips;
}
template <class Box> auto permission_row(Box* box, const permission_item& item) {
  const auto& rules = rules_of(box->facts.theirs);
  const auto& kept = box->template part<state>();
  const auto& draft = kept.permissions ? *kept.permissions : rules.needs;
  const auto current = required(draft, item.location);
  const bool user = spl::visit(spl::overloaded{
      [](const power_need_t& need) { return spl::visit(spl::overloaded{[](power_need::default_role) { return true; }, [](auto) { return false; }}, need); },
      [](const event_permission&) { return false; }}, item.location);
  auto values = power_values(rules, kept.data);
  if (!std::ranges::contains(values, current)) values.push_back(current);
  std::ranges::sort(values, std::greater{});
  const auto names = std::ranges::to<std::vector<std::string>>(std::views::transform(values, [&](auto value) { return power_name(value, kept.data) + (user ? "" : " & Above"); }));
  const auto selected = static_cast<std::size_t>(std::ranges::find(values, current) - values.begin());
  return setting_tile(*box->colours_, item.title, user ? "Default power level for all users." : "",
      skiff::compose::styled({.relativeSize = scene::axes::kNone, .disabled = !editable(box, power_need::change_permissions{}) || required(rules.needs, item.location) > box->facts.mine},
          compact_choice(*box->colours_, "", names, selected, typename permission_actions<Box>::choose{box, item.location, std::move(values)})));
}
template <class Box> auto permission_section(Box* box, const permission_group& group) {
  namespace c = skiff::compose;
  auto rows = c::many(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY, .cornerRadius = std::max(10.0f, box->colours_->widgets.fCorner),
                                   .background = box->colours_->tile}), std::ranges::to<std::vector>(
      std::views::transform(group.items, [&](const auto& item) {
        return c::styled({.padding = {12.0f, 12.0f, 12.0f, 12.0f}}, permission_row(box, item));
      })));
  return section(*box->colours_, group.title, std::move(rows));
}
[[nodiscard]] inline std::set<std::string> standard_permission_events() {
  std::set<std::string> names;
  std::ranges::for_each(permission_groups(), [&](const auto& group) {
    std::ranges::for_each(group.items, [&](const auto& item) {
      spl::visit(spl::overloaded{
          [&](const event_permission& event) { names.insert(event.type); },
          [&](const power_need_t& need) { spl::visit(spl::overloaded{
              [&]<sends_state Need>(Need) { names.insert(std::string(Need::event)); },
              [](auto) {}}, need); }}, item.location);
    });
  });
  return names;
}
template <class Box> struct remove_custom_permission {
  using Answer = skiff::model::Nothing;
  Box* box; std::string event;
  Answer operator()() const {
    const auto& original = rules_of(box->facts.theirs).needs;
    if (!editable(box, power_need::change_permissions{}) ||
        required(original, permission_t{event_permission{event}}) > box->facts.mine) return {};
    auto& kept = box->template part<state>();
    if (!kept.permissions) kept.permissions = original;
    kept.permissions->events.erase(event);
    if (*kept.permissions == original) kept.permissions.reset();
    box->show_again();
    return {};
  }
};
template <class Box> auto custom_permissions(Box* box) {
  namespace c = skiff::compose; using draft = power_draft<Box>;
  const auto& colours = *box->colours_; const auto& kept = box->template part<state>();
  const auto& needs = kept.permissions ? *kept.permissions : rules_of(box->facts.theirs).needs;
  const auto known = standard_permission_events();
  return section(colours, "Custom Event Permissions", card(colours,
      c::many(c::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::ranges::to<std::vector>(
          std::views::transform(std::views::filter(needs.events, [&](const auto& entry) { return !known.contains(entry.first); }), [&](const auto& entry) {
            return c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                c::styled({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX},
                    permission_row(box, permission_item{entry.first, event_permission{entry.first}})),
                action(colours, "Remove", remove_custom_permission<Box>{box, entry.first},
                    editable(box, power_need::change_permissions{}) && required(rules_of(box->facts.theirs).needs, permission_t{event_permission{entry.first}}) <= box->facts.mine));
          }))),
      explained(colours, "Set a power level for an additional event type. Removing an override restores its default permission."),
      c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          c::styled({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX}, model_field<&draft::event>(colours, "Event type", "org.example.event")),
          c::styled({.width = 96.0f, .relativeSize = scene::axes::kNone}, model_field<&draft::level>(colours, "Power level", "50")),
          action(colours, "Add", sends<add_custom_permission>{}, editable(box, power_need::change_permissions{})))));
}
template <class Box> struct edit_power {
  using Answer = skiff::model::Nothing;
  Box* box; std::int64_t power;
  skiff::model::Nothing operator()() const {
    box->show_again();
    auto& kept = box->template part<state>();
    const auto& tags = kept.tag_draft ? *kept.tag_draft : kept.data.tags;
    const auto found = tags.find(std::to_string(power));
    kept.tag_power = std::to_string(power); kept.tag_name = power_name(power, kept.data);
    kept.tag_color = found != tags.end() ? found->second.color.value_or("") : "";
    kept.tag_icon = found != tags.end() && found->second.icon ? found->second.icon->key.value_or("") : "";
    kept.power_form = true; kept.editing_power = power;
    return {};
  }
};
template <class Box> struct delete_tag {
  using Answer = skiff::model::Nothing;
  Box* box; std::int64_t power;
  skiff::model::Nothing operator()() const {
    auto& kept = box->template part<state>();
    if (kept.pending || std::ranges::contains(power_values(rules_of(box->facts.theirs), room_management{}), power)) return {};
    if (!kept.deleted_powers.erase(power)) kept.deleted_powers.insert(power);
    box->show_again();
    return {};
  }
};
template <class Box> struct apply_tags {
  using Answer = std::optional<request::change_room>;
  Box* box; bool reset = false;
  std::optional<request::change_room> operator()() const {
    auto& kept = box->template part<state>();
    if (kept.pending) return {};
    if (reset) { kept.tag_draft.reset(); kept.deleted_powers.clear(); box->show_again(); return {}; }
    if (!kept.tag_draft && kept.deleted_powers.empty()) return {};
    auto tags = kept.tag_draft.value_or(kept.data.tags);
    std::ranges::for_each(kept.deleted_powers, [&](auto value) { tags.erase(std::to_string(value)); });
    return submit(box, room_change::set_tags{std::move(tags)});
  }
};
template <class Box> auto power_form_view(Box* box) {
  namespace c = skiff::compose; using draft = power_draft<Box>;
  const auto& colours = *box->colours_; const auto& kept = box->template part<state>();
  return c::column(c::vbox(16.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                  c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                      c::styled({.width = 80.0f, .relativeSize = scene::axes::kNone}, model_field<&draft::color>(colours, "Color", "#91cfdf")),
                      c::styled({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX}, model_field<&draft::name>(colours, "Name", "Bot")),
                      c::styled({.width = 80.0f, .relativeSize = scene::axes::kNone, .disabled = kept.editing_power.has_value()}, model_field<&draft::power>(colours, "Power", "75"))),
                  model_field<&draft::icon>(colours, "Icon", "Emoji or mxc:// image"),
                  c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                      action(colours, "Save", sends<save_power>{}, !kept.pending),
                      action(colours, "Cancel", edit_section<Box>{box, editor::power, false})));
}
template <class Box> auto permissions_page(Box* box, const room_settings_facts& facts) {
  namespace c = skiff::compose; using draft = power_draft<Box>; using acts = permission_actions<Box>;
  const auto& colours = *box->colours_; const auto& kept = box->template part<state>(); const auto& rules = rules_of(facts.theirs);
  const auto permission = rules.needs.events.find("in.cinny.room.power_level_tags");
  const bool tags_allowed = facts.mine >= (permission == rules.needs.events.end() ? rules.needs.state_default : permission->second);
  const bool founders = std::ranges::any_of(rules.powers, [](const auto& user) { return user.second == kCreatorPower; });
  auto effective = kept.data;
  if (kept.tag_draft) effective.tags = *kept.tag_draft;
  return c::local<draft>(power_events<Box>{box}, c::column(page_look(),
      c::visible(kept.pending || !kept.error.empty(), explained(colours, kept.pending ? "Saving…" : kept.error)),
      c::visible(!kept.powers_editor, c::column(c::vbox(32.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
              c::visible(founders, card(colours,
                  setting_tile(colours, "Founders", "Founding members has all permissions and can only be changed during upgrade.", c::column()),
                  power_chip(colours, "Founder", 0xff0000ffu))),
              card(colours, setting_tile(colours, "Power Levels", "Manage and customize incremental power levels for users.",
                  c::visible(tags_allowed, action(colours, "Edit", edit_section<Box>{box, editor::powers}))), power_chips(box))),
          c::many(c::vbox(32.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::ranges::to<std::vector>(
              std::views::transform(permission_groups(), [&](const auto& group) { return permission_section(box, group); }))),
          custom_permissions(box),
          c::visible(kept.permissions.has_value(), card(colours,
              explained(colours, "You have unsaved changes."),
              c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                  action(colours, "Reset", typename acts::apply{box, true}, !kept.pending),
                  action(colours, "Apply Changes", typename acts::apply{box}, editable(box, power_need::change_permissions{}), action_style::success)))))),
      c::visible(kept.powers_editor, section(colours, "Power Levels",
          card(colours, setting_tile(colours, "New Power Level", "Create a new power level.",
              c::visible(!kept.power_form, action(colours, "Create", edit_section<Box>{box, editor::power}))),
              c::visible(kept.power_form && !kept.editing_power, power_form_view(box))),
          c::many(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::ranges::to<std::vector>(
              std::views::transform(power_values(rules, effective), [&](auto value) {
                return card(colours, setting_tile(colours, power_name(value, effective), std::format("({})", value),
                    c::row(c::hbox(8.0f, {.autoSize = scene::axes::kBoth}),
                        action(colours, kept.deleted_powers.contains(value) ? "Undo" : "Delete", delete_tag<Box>{box, value},
                            !kept.pending && !std::ranges::contains(power_values(rules, room_management{}), value)),
                        action(colours, "Edit", edit_power<Box>{box, value}, !kept.pending && !kept.deleted_powers.contains(value)))),
                    c::visible(kept.power_form && kept.editing_power == value, power_form_view(box)));
              }))),
          c::visible(kept.tag_draft.has_value() || !kept.deleted_powers.empty(), card(colours,
              explained(colours, "Changes saved! Apply when ready."),
              c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                  action(colours, "Reset", apply_tags<Box>{box, true}, !kept.pending),
                  action(colours, "Apply Changes", apply_tags<Box>{box}, !kept.pending, action_style::success)))),
          action(colours, "Permissions", edit_section<Box>{box, editor::powers, false})))),
      draft{box, kept.tag_power, kept.tag_name, kept.tag_color, kept.tag_icon, kept.custom_event, kept.custom_level});
}

struct create_pack {};
template <class Box> struct pack_draft { Box* box; std::string name; };
template <class Box> struct pack_events {
  auto on(create_pack, const pack_draft<Box>& draft) const {
    const auto& rules = rules_of(draft.box->facts.theirs);
    const auto found = rules.needs.events.find("im.ponies.room_emotes");
    const auto need = found == rules.needs.events.end() ? rules.needs.state_default : found->second;
    std::optional<request::save_room_pack> answer;
    if (!draft.name.empty() && draft.box->facts.mine >= need) {
      emote_pack pack;
      pack.chat = draft.box->facts.id; pack.name = draft.name; pack.key = draft.name;
      std::ranges::replace(pack.key, ' ', '-');
      const auto& packs = draft.box->template part<state>().packs;
      const auto stem = pack.key;
      for (std::size_t suffix = 1; std::ranges::any_of(packs, [&](const auto& existing) { return existing.key == pack.key; }); ++suffix)
        pack.key = std::format("{}-{}", stem, suffix);
      answer = request::save_room_pack{std::move(pack)};
    }
    return skiff::model::Up{std::move(answer)};
  }
};
template <class Box> void preserve_draft(const pack_draft<Box>& draft) { draft.box->template part<state>().pack_name = draft.name; }
template <class Box> auto packs_page(Box* box, const room_settings_facts& facts) {
  namespace c = skiff::compose; using draft = pack_draft<Box>;
  const auto& colours = *box->colours_; const auto& kept = box->template part<state>();
  const auto& rules = rules_of(facts.theirs);
  const auto need = rules.needs.events.find("im.ponies.room_emotes");
  const bool editable = facts.mine >= (need == rules.needs.events.end() ? rules.needs.state_default : need->second);
  return c::local<draft>(pack_events<Box>{}, c::column(page_look(), section(colours, "Room Packs",
      c::visible(editable, card(colours,
          setting_tile(colours, "New Pack", "Add your own emoji and sticker pack to use in room.", c::column()),
          c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
              c::styled({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX}, model_field<&draft::name>(colours, "Name", "Pack name")),
              action(colours, "Create", sends<create_pack>{})))),
      c::many(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::ranges::to<std::vector>(
          std::views::transform(kept.packs, [&](const auto& pack) {
            return card(colours, setting_tile(colours, pack.name.empty() ? "Unnamed Pack" : pack.name,
                std::format("{} images", pack.pictures.size()), action(colours, "View", sends<::mux::ui::request::open_room_packs>{{pack.key}})));
          }))), c::visible(kept.packs.empty(), explained(colours, "No packs found.")))), draft{box, kept.pack_name});
}
struct save_account_data {};
struct save_state_event {};
template <class Box> struct state_navigation {
  using Answer = skiff::model::Nothing;
  Box* box; state_screen target; std::string type, key;
  Answer operator()() const {
    auto& kept = box->template part<state>();
    if (kept.pending) return {};
    box->show_again();
    if (target == state_screen::view && !type.empty()) {
      const auto event = std::ranges::find_if(kept.data.state_events, [&](const auto& one) { return one.type == type && one.key == key; });
      if (event == kept.data.state_events.end()) return {};
      kept.state_type = event->type; kept.state_key = event->key;
      kept.state_source = event->json; kept.state_json = event->content_json;
    }
    if (target == state_screen::compose) { kept.state_type = type; kept.state_key = key; kept.state_json = "{}"; }
    kept.state_page = target;
    kept.error.clear();
    return {};
  }
};
template <class Box> struct expand_state_type {
  using Answer = skiff::model::Nothing;
  Box* box; std::string type;
  Answer operator()() const {
    auto& kept = box->template part<state>();
    kept.expanded_state_type = kept.expanded_state_type == type ? std::string() : type;
    box->show_again();
    return {};
  }
};
[[nodiscard]] inline auto grouped_state_events(const room_management& data) {
  std::map<std::string, std::vector<managed_state_event>> groups;
  std::ranges::for_each(data.state_events, [&](const auto& event) { groups[event.type].push_back(event); });
  std::ranges::for_each(groups, [](auto& group) { std::ranges::sort(group.second, {}, &managed_state_event::key); });
  return groups;
}
struct open_state_event { state_screen target; std::string type, key; };
struct expand_state_event { std::string type; };
template <class Box> struct toggle_developer_tools {
  using Answer = ::mux::ui::request::set_developer_tools;
  Box* box;
  Answer operator()() const { return {!box->looks_->developer_tools}; }
};
inline auto state_event_source(const palette& colours, std::string json) {
  auto text = nodes::Text(std::move(json), 13.0f, colours.text);
  text.setMonospace(true);
  text.setSelectable(true);
  return text;
}
enum class state_row_kind { add, type, key };
template <class Act> auto state_list_row(const palette& colours, std::string label, std::string detail, Act act, state_row_kind kind = state_row_kind::type, bool expanded = false) {
  namespace c = skiff::compose;
  return c::onClick(std::move(act), c::row(c::hbox(8.0f, {.fillX = true, .height = 36.0f,
      .padding = {4.0f, 8.0f, 4.0f, 8.0f}, .cornerRadius = colours.widgets.fCorner,
      .hoverBackground = colours.chosen}),
      c::visible(kind != state_row_kind::key, c::styled({.width = 12.0f, .height = 12.0f, .alignSelf = scene::align::kMiddle}, nodes::Icon(shape_of(kind == state_row_kind::add ? icon_t{icon::plus{}} : expanded ? icon_t{icon::down{}} : icon_t{icon::right{}}), colours.dim))),
      c::styled({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX, .shrink = scene::axes::kX,
          .alignSelf = scene::align::kMiddle}, elided(nodes::Text(std::move(label), 12.0f, colours.text))),
      c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(std::move(detail), 12.0f, colours.dim)),
      c::visible(kind == state_row_kind::key, c::styled({.width = 12.0f, .height = 12.0f, .alignSelf = scene::align::kMiddle}, nodes::Icon(shape_of(icon::right{}), colours.dim)))));
}
template <class Box> auto state_event_list(Box* box) {
  namespace c = skiff::compose;
  const auto& colours = *box->colours_; const auto& kept = box->template part<state>();
  const auto groups = grouped_state_events(kept.data);
  return c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      setting_tile(colours, "Events", "", nodes::Text(std::format("Total: {}", groups.size()), 12.0f, colours.dim)),
      cutout(colours, {}, state_list_row(colours, "Add New", "", open_state_event{state_screen::compose, "", ""}, state_row_kind::add),
          c::many(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::ranges::to<std::vector>(
              std::views::transform(groups, [&](const auto& group) {
                return c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                    state_list_row(colours, group.first, std::to_string(group.second.size()), expand_state_event{group.first}, state_row_kind::type, kept.expanded_state_type == group.first),
                    c::visible(kept.expanded_state_type == group.first, c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY,
                        .margin = {0.0f, 0.0f, 0.0f, 16.0f}, .padding = {0.0f, 0.0f, 0.0f, 8.0f}, .border = scene::Border{colours.band, 1.0f}}),
                        state_list_row(colours, "Add New", "", open_state_event{state_screen::compose, group.first, ""}, state_row_kind::add),
                        c::many(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::ranges::to<std::vector>(
                            std::views::transform(group.second, [&](const auto& event) {
                              return state_list_row(colours, event.key.empty() ? "Default" : "\"" + event.key + "\"", "", open_state_event{state_screen::view, event.type, event.key}, state_row_kind::key);
                            }))))));
              })))));
}
struct choose_account_data { std::size_t index; };
struct select_account_data { using Answer = choose_account_data; choose_account_data operator()(std::size_t index) const { return {index}; } };
template <class Box> struct developer_draft {
  Box* box; std::string type, json; widgets::TextArea<>* content = nullptr;
  std::string state_type, state_key, state_json; widgets::TextArea<>* state_content = nullptr;
};
template <class Box> struct developer_events {
  Box* box;
  auto on(open_state_event event, const developer_draft<Box>&) const {
    return state_navigation<Box>{box, event.target, std::move(event.type), std::move(event.key)}();
  }
  auto on(expand_state_event event, const developer_draft<Box>&) const {
    return expand_state_type<Box>{box, std::move(event.type)}();
  }
  auto on(save_state_event, const developer_draft<Box>& draft) const {
    const auto& original = rules_of(box->facts.theirs).needs;
    const auto needed = required(original, permission_t{event_permission{draft.state_type}});
    const auto json = draft.state_content ? draft.state_content->text() : draft.state_json;
    return skiff::model::Up{draft.state_type.empty() || box->facts.mine < needed
        ? std::nullopt : submit(box, room_change::set_state_event{draft.state_type, draft.state_key, json})};
  }
  auto on(save_account_data, const developer_draft<Box>& draft) const {
    return skiff::model::Up{draft.type.empty() ? std::nullopt : submit(box, room_change::set_account_data{draft.type, draft.content ? draft.content->text() : draft.json})};
  }
  auto on(choose_account_data event, const developer_draft<Box>& draft) const {
    auto next = draft;
    const auto& entries = box->template part<state>().data.account_data;
    if (event.index < entries.size()) { next.type = entries[event.index].type; next.json = entries[event.index].json; }
    box->show_again();
    auto& kept = box->template part<state>();
    kept.account_editor = true; kept.account_type = next.type; kept.account_json = next.json;
    if (next.content) next.content->setText(next.json);
    return skiff::model::over<developer_draft<Box>>(skiff::model::setTo(std::move(next)));
  }
};
template <class Box> void preserve_draft(const developer_draft<Box>& draft) {
  auto& kept = draft.box->template part<state>(); kept.account_type = draft.type; kept.account_json = draft.content ? draft.content->text() : draft.json;
  kept.state_type = draft.state_type; kept.state_key = draft.state_key;
  kept.state_json = draft.state_content ? draft.state_content->text() : draft.state_json;
}
template <class Box> auto developer_page(Box* box, const room_settings_facts& facts) {
  namespace c = skiff::compose; using draft = developer_draft<Box>;
  const auto& kept = box->template part<state>(); const auto& colours = *box->colours_;
  auto content = multiline_input(colours, kept.account_json.empty() ? "{}" : kept.account_json, "{}", 240.0f);
  auto state_content = multiline_input(colours, kept.state_json.empty() ? "{}" : kept.state_json, "{}", 320.0f);
  state_content->setMaxLines(1000);
  state_content->apply({.disabled = kept.pending});
  const auto names = std::ranges::to<std::vector<std::string>>(std::views::transform(kept.data.account_data, &managed_account_data::type));
  auto source = state_event_source(colours, kept.state_source);
  const bool viewing = kept.state_page == state_screen::view;
  const bool composing = kept.state_page == state_screen::compose;
  return c::local<draft>(developer_events<Box>{box}, c::column(page_look(),
      c::visible(kept.pending || !kept.error.empty(), explained(colours, kept.pending ? "Saving…" : kept.error)),
      c::visible(!kept.account_editor && kept.state_page == state_screen::overview, c::column(c::vbox(32.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          section(colours, "Options",
              tile(colours, "Enable Developer Tools", "", switch_control(colours, box->looks_->developer_tools, toggle_developer_tools<Box>{box})),
              c::visible(box->looks_->developer_tools, tile(colours, "Room ID", "Copy room ID to clipboard. (\"" + facts.id + "\")",
                  action(colours, "Copy", sends<::mux::ui::request::copy_text>{{facts.id}})))),
          c::visible(box->looks_->developer_tools, section(colours, "Data",
              tile(colours, "New Message Event", "Create and send a new message event within the room.", action(colours, "Compose", sends<request::open_send_custom>{})),
              card(colours, setting_tile(colours, "Room State", "State events of the room.",
                  action(colours, kept.expanded_state ? "Collapse" : "Expand", edit_section<Box>{box, editor::state_events})),
                  c::visible(kept.expanded_state, state_event_list(box))),
              card(colours, setting_tile(colours, "Account Data", "Private personalization data stored within room.",
                  action(colours, kept.expanded_account_data ? "Collapse" : "Expand", edit_section<Box>{box, editor::account_data})),
                  c::visible(kept.expanded_account_data, c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                      action(colours, "Add New", edit_section<Box>{box, editor::account_form}),
                      compact_choice(colours, "", names, names.size(), select_account_data{})))))))),
      c::visible(kept.state_page != state_screen::overview, c::column(c::vbox(16.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          c::visible(viewing, c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
              setting_tile(colours, "State Event", "",
                  c::visible(box->facts.mine >= required(rules_of(facts.theirs).needs, permission_t{event_permission{kept.state_type}}),
                      action(colours, "Edit", state_navigation<Box>{box, state_screen::edit, "", ""}))),
              card(colours, wrapped(std::move(source))))),
          c::visible(!viewing, c::column(c::vbox(16.0f, {.fillX = true, .autoSize = scene::axes::kY}),
              c::visible(!composing, section(colours, "State Event", tile(colours, kept.state_type, kept.state_key,
                  c::row(c::hbox(8.0f, {.autoSize = scene::axes::kBoth}),
                      action(colours, "Save", sends<save_state_event>{}, !kept.pending, action_style::success),
                      action(colours, "Cancel", state_navigation<Box>{box, state_screen::view, kept.state_type, kept.state_key}, !kept.pending))))),
              c::visible(composing, c::column(c::vbox(16.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                  c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                      c::styled({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX, .disabled = kept.pending},
                          model_field<&draft::state_type>(colours, "State Event Type", "")),
                      action(colours, "Send", sends<save_state_event>{}, !kept.pending, action_style::success)),
                  c::styled({.disabled = kept.pending}, model_field<&draft::state_key>(colours, "State Key (Optional)", "")))),
              section(colours, "JSON Content", state_content))))),
      c::visible(kept.account_editor, section(colours, "Account Data",
          model_field<&draft::type>(colours, "Type", ""),
          c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}), settings_section_heading(colours, "Content"), content),
          c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
              action(colours, "Save", sends<save_account_data>{}, !kept.pending),
              action(colours, "Cancel", edit_section<Box>{box, editor::account_form, false}))))), draft{box, kept.account_type, kept.account_json, content.get(), kept.state_type, kept.state_key, kept.state_json, state_content.get()});
}
template <class Page> requires requires(Page& page) { preserve_draft(page.fModel.root()); }
void preserve_manage_page(Page& page) { preserve_draft(page.fModel.root()); }
} // namespace mux::proto::matrix::manage_detail

export namespace mux::proto::matrix {
namespace manage { struct room {}; struct members {}; struct roles {}; struct packs {}; struct advanced {}; }
constexpr ui::manage_tab_list<manage::room, manage::members, manage::roles, manage::packs, manage::advanced> manage_tabs(const state&) { return {}; }
constexpr type_tag<manage_detail::part> manage_part_type(const state&) { return {}; }
constexpr manage::room manage_initial_tab(const state&) { return {}; }
inline ui::icon_t manage_avatar_icon(const state&, const ui::room_settings_facts& facts) {
  return facts.encrypted ? ui::icon_t{ui::icon::lock{}} : ui::icon_t{ui::icon::none{}};
}
inline ui::room_manage_style manage_style(const state&, const ui::palette& original) {
  return {original, true, 222.0f};
}
constexpr std::string_view manage_preferences_title(const state&) { return "Preferences"; }
}
export namespace mux::proto::matrix::manage {
inline bool manage_close_editor(roles, manage_detail::part& kept) {
  if (!kept.powers_editor) return false;
  kept.power_form = false; kept.powers_editor = false;
  return true;
}
inline bool manage_editor_header(advanced, const manage_detail::part& kept) {
  return kept.state_page != manage_detail::state_screen::overview;
}
inline bool manage_close_editor(advanced, manage_detail::part& kept) {
  if (kept.pending) return true;
  if (kept.state_page != manage_detail::state_screen::overview) { kept.state_page = manage_detail::state_screen::overview; return true; }
  return std::exchange(kept.account_editor, false);
}
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
template <class Box> auto make_manage_page(room, Box* box, const ui::room_settings_facts& facts) { return manage_detail::general_page(box, facts); }
template <class Box> constexpr auto page_type(room tab, type_tag<Box>) { return type_tag<decltype(make_manage_page(tab, std::declval<Box*>(), std::declval<const ui::room_settings_facts&>()))>{}; }
template <class Box> auto make_manage_page(members, Box* box, const ui::room_settings_facts& facts) { return manage_detail::members_page(box, facts); }
template <class Box> constexpr auto page_type(members tab, type_tag<Box>) { return type_tag<decltype(make_manage_page(tab, std::declval<Box*>(), std::declval<const ui::room_settings_facts&>()))>{}; }
template <class Box> auto make_manage_page(roles, Box* box, const ui::room_settings_facts& facts) { return manage_detail::permissions_page(box, facts); }
template <class Box> constexpr auto page_type(roles tab, type_tag<Box>) { return type_tag<decltype(make_manage_page(tab, std::declval<Box*>(), std::declval<const ui::room_settings_facts&>()))>{}; }
template <class Box> auto make_manage_page(packs, Box* box, const ui::room_settings_facts& facts) { return manage_detail::packs_page(box, facts); }
template <class Box> constexpr auto page_type(packs tab, type_tag<Box>) { return type_tag<decltype(make_manage_page(tab, std::declval<Box*>(), std::declval<const ui::room_settings_facts&>()))>{}; }
template <class Box> auto make_manage_page(advanced, Box* box, const ui::room_settings_facts& facts) { return manage_detail::developer_page(box, facts); }
template <class Box> constexpr auto page_type(advanced tab, type_tag<Box>) { return type_tag<decltype(make_manage_page(tab, std::declval<Box*>(), std::declval<const ui::room_settings_facts&>()))>{}; }
}
