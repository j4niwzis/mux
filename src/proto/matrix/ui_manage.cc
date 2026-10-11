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
import skiff.widgets.button;
import mux.core;
import mux.config;
import mux.proto.kept;
import mux.proto.matrix;
import mux.proto.matrix.requests;
import mux.proto.matrix.changes;
import mux.logic.text;
import mux.ui;

export namespace mux::proto::matrix::manage_detail {
using namespace ::mux::ui;

// Drafts belong to the settings window, so changing a page does not submit
// or discard them. Only an acknowledged write clears a permission draft.
struct part {
  room_management data;
  bool loading = true, pending = false;
  std::string error;
  bool confirming_encryption = false, confirming_upgrade = false;
  std::optional<power_needs> permissions;
  std::optional<room_change::set_profile> profile;
  std::string local_address, upgrade_version, upgrade_creators;
  std::string tag_power, tag_name, tag_color, tag_icon;
  std::string account_type, account_json;
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
  kept.data = one.data;
  kept.loading = false;
  kept.error = one.error;
  box->show_again();
}
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
        [&](const room_change::set_tags& change) { kept.data.tags = change.tags;  },
        [&](const room_change::set_profile& change) { if (change.name) facts.name = *change.name; if (change.topic) facts.topic = *change.topic; if (change.avatar) facts.avatar = change.avatar->empty() ? std::nullopt : change.avatar; kept.profile.reset(); },
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
}

inline skiff::compose::Look page_look() {
  return skiff::compose::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY,
      .padding = {8.0f, 24.0f, 24.0f, 24.0f}});
}
// A setting tile uses the same wrapped label node as the client's radio
// choices, with its control beside it and the description under its title.
template <class Control> auto tile(const palette& colours, std::string title, std::string description, Control control) {
  namespace c = skiff::compose;
  return c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY,
      .padding = {14.0f, 14.0f, 14.0f, 14.0f}, .cornerRadius = 8.0f, .background = colours.tile}),
      c::column(c::vbox(4.0f, {.autoSize = scene::axes::kY, .grow = scene::axes::kX}),
          wrapped(nodes::Text(std::move(title), 14.0f, colours.text)),
          wrapped(nodes::Text(std::move(description), 13.0f, colours.dim))), std::move(control));
}
template <class Act> auto action(const palette& colours, std::string label, Act act, bool allowed = true) {
  return skiff::compose::styled({.width = 130.0f, .height = 36.0f, .alignSelf = scene::align::kMiddle, .disabled = !allowed},
      widgets::Button<Act>(colours.widgets, std::move(label), std::move(act)));
}

// General: profile, options, addresses, and advanced options in Cinny's order.
struct remove_avatar {}, save_profile {}, create_address {}, upgrade_room {}, cancel_upgrade {}, save_power {}, delete_power {};
template <class Box> struct general_draft { Box* box; std::string name, topic, address, version, creators; std::optional<std::string> avatar; };
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
        case kind::unpublish: std::erase(published, alias); if (main == alias) main.reset(); break;
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
    return skiff::compose::styled({.width = 180.0f, .disabled = !editable(box, power_need::change_access{})},
        make_choice_menu(*box->colours_, "", std::move(names), index, typename general_actions<Box>::choose_access{box, std::move(rules)}));
  }

template <class Box> struct general_events {
  Box* box;
  auto on(save_profile, const general_draft<Box>& draft) const {
    room_change::set_profile change;
    if (editable(box, power_need::rename{}) && draft.name != box->facts.name) change.name = draft.name;
    if (editable(box, power_need::retopic{}) && draft.topic != box->facts.topic) change.topic = draft.topic;
    if (editable(box, power_need::change_avatar{}) && draft.avatar && *draft.avatar != box->facts.avatar.value_or("")) change.avatar = draft.avatar;
    return skiff::model::Up{change.name || change.topic || change.avatar ? submit(box, std::move(change)) : std::nullopt};
  }
  auto on(remove_avatar, const general_draft<Box>& draft) const {
    auto avatar = editable(box, power_need::change_avatar{}) ? std::optional<std::string>("") : draft.avatar;
    return skiff::model::over<skiff::model::Field<&general_draft<Box>::avatar>>(skiff::model::setTo(std::move(avatar)));
  }
  auto on(create_address, const general_draft<Box>& draft) const {
    auto local = draft.address;
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
  if (draft.topic != draft.box->facts.topic) profile.topic = draft.topic;
  if (draft.avatar && *draft.avatar != draft.box->facts.avatar.value_or("")) profile.avatar = draft.avatar;
  kept.profile = profile.name || profile.topic || profile.avatar ? std::optional(profile) : std::nullopt;
  kept.local_address = draft.address; kept.upgrade_version = draft.version; kept.upgrade_creators = draft.creators;
}
template <class Box> auto address_row(Box* box, std::string alias, bool published, bool local) {
  namespace c = skiff::compose; using act = typename general_actions<Box>::address_action;
  return c::column(c::vbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      id_line(*box->colours_, alias, "", box->facts.alias == alias ? "Main" : published ? "Published" : "Local Address"),
      action(*box->colours_, local ? (published ? "Unpublish" : "Publish") : (box->facts.alias == alias ? "Unset Main" : "Set Main"),
          act{box, alias, local ? (published ? act::kind::unpublish : act::kind::publish) : act::kind::main}, editable(box, power_need::change_address{})),
      c::visible(local, action(*box->colours_, "Delete", act{box, alias, act::kind::remove}, !box->template part<state>().pending)));
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
          make_choice_menu(*box->colours_, "Add Room", names, rooms.size(), choose_child<Box>{box, rooms}))));
}
inline auto related_room(const palette& colours, std::string label, const std::optional<std::string>& room) {
  return skiff::compose::visible(room.has_value(), action(colours, std::move(label),
      sends<::mux::ui::request::open_url>{{"https://matrix.to/#/" + room.value_or("")}}));
}
template <class Box> auto general_page(Box* box, const room_settings_facts& facts) {
  namespace c = skiff::compose; using acts = general_actions<Box>; using draft = general_draft<Box>;
  const auto& colours = *box->colours_; const auto& kept = box->template part<state>();
  auto published = facts.other_aliases;
  if (facts.alias && !std::ranges::contains(published, *facts.alias)) published.insert(published.begin(), *facts.alias);
  auto rows = [&](const auto& aliases, bool local) {
    return c::many(c::vbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::ranges::to<std::vector>(
        std::views::transform(aliases, [&](const auto& alias) { return address_row(box, alias, std::ranges::contains(published, alias), local); })));
  };
  return c::local<draft>(general_events<Box>{box}, c::column(page_look(),
      tab_heading(colours, "General"), explained(colours, kept.pending ? "Saving…" : kept.loading ? "Loading…" : kept.error),
      c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}), avatar_mark(facts.id, facts.name, 88.0f),
          c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          action(colours, "Upload Avatar", sends<::mux::ui::request::pick_room_avatar>{{facts.chat}}, editable(box, power_need::change_avatar{})),
          action(colours, "Remove Avatar", sends<remove_avatar>{}, editable(box, power_need::change_avatar{})))),
      c::shown_for<skiff::model::Field<&draft::avatar>>([](const auto& avatar) { return avatar.has_value(); }, explained(colours, "Avatar change pending. Save to apply.")),
      c::styled({.disabled = !editable(box, power_need::rename{})}, model_field<&draft::name>(colours, "Name", "")),
      c::styled({.disabled = !editable(box, power_need::retopic{})}, model_field<&draft::topic>(colours, "Topic", "")),
      action(colours, "Save", sends<save_profile>{}, editable(box, power_need::rename{}) || editable(box, power_need::retopic{}) || editable(box, power_need::change_avatar{})),
      part_heading(colours, "Options"),
      tile(colours, facts.space ? "Space Access" : "Room Access", "Change how people can join the room.", access_menu(box)),
      tile(colours, "Message History Visibility", "Changes to history visibility will only apply to future messages.",
          c::styled({.width = 180.0f, .disabled = !editable(box, power_need::change_history{})}, make_choice_menu(colours, "",
              {"All Messages", "After Invite", "After Join", "All Messages (Guests)"}, rules_of(facts.theirs).history.index(), typename acts::choose_history{box}))),
      tile(colours, "Room Encryption", "Once enabled, encryption cannot be disabled!",
          action(colours, facts.encrypted ? "Enabled" : kept.confirming_encryption ? "Enable E2E Encryption" : "Enable", typename acts::encryption{box}, !facts.encrypted && editable(box, power_need::encrypt{}))),
      c::visible(kept.confirming_encryption, explained(colours, "Are you sure? Once enabled, encryption cannot be disabled!")),
      c::visible(kept.confirming_encryption, action(colours, "Cancel", typename acts::encryption{box, true})),
      tile(colours, "Publish to Directory", "List the room in the public directory to make it discoverable by others.",
          action(colours, kept.data.directory.value_or(false) ? "Published" : "Publish", typename acts::directory{box}, editable(box, power_need::change_address{}) && kept.data.directory.has_value())),
      part_heading(colours, "Addresses"), part_heading(colours, "Published Addresses"), rows(published, false),
      part_heading(colours, "Local Addresses"), rows(kept.data.local_aliases, true),
      model_field<&draft::address>(colours, "New Local Address", "address"), action(colours, "Save", sends<create_address>{}, !kept.pending),
      space_children(box, facts), part_heading(colours, "Advanced Options"), explained(colours, "Current version: " + rules_of(facts.theirs).version),
      related_room(colours, "Previous Room", rules_of(facts.theirs).predecessor),
      related_room(colours, "Replacement Room", rules_of(facts.theirs).replaced_by),
      c::visible(kept.confirming_upgrade, explained(colours, "Upgrading creates a replacement room. Members will need to move to the new room.")),
      c::visible(kept.confirming_upgrade, model_field<&draft::version>(colours, "Room Version", "")),
      c::visible(kept.confirming_upgrade, model_field<&draft::creators>(colours, "Additional Creators (version 12 or newer)", "Space-separated Matrix IDs")),
      action(colours, "Upgrade", sends<upgrade_room>{}, editable(box, power_need::upgrade{}) && !kept.data.versions.empty()),
      c::visible(kept.confirming_upgrade, action(colours, "Cancel", sends<cancel_upgrade>{}))),
      draft{box, kept.profile && kept.profile->name ? *kept.profile->name : facts.name,
          kept.profile && kept.profile->topic ? *kept.profile->topic : facts.topic, kept.local_address,
          kept.upgrade_version.empty() ? kept.data.default_version : kept.upgrade_version, kept.upgrade_creators,
          kept.profile ? kept.profile->avatar : std::nullopt});
}

// Members: the same membership filters and power-first ordering as Cinny.
// The query is kept by the page while only the displayed rows are replaced.
struct member_filter { std::size_t value; };
struct member_order { std::size_t value; };
struct select_filter { member_filter operator()(std::size_t value) const { return {value}; } };
struct select_order { member_order operator()(std::size_t value) const { return {value}; } };
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
      c::styled({.width = 160.0f, .disabled = !editable(box, power_need::change_permissions{}) || level == kCreatorPower || (level >= box->facts.mine && member.id != box->facts.chat.account.address)},
          make_choice_menu(*box->colours_, "", std::move(names), selected, choose_member_power<Box>{box, member.id, std::move(values)})));
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
    auto rows = std::ranges::to<std::vector<managed_member>>(std::views::filter(kept.data.members, [&](const auto& member) {
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
  return c::local<draft>(member_events<Box>{}, c::column(page_look(),
      tab_heading(*box->colours_, "Members"), explained(*box->colours_, kept.error),
      model_field<&draft::query>(*box->colours_, "", "Search"),
      make_choice_menu(*box->colours_, "", {"Joined", "Invited", "Left", "Kicked", "Banned"}, kept.member_filter, select_filter{}),
      make_choice_menu(*box->colours_, "", {"A to Z", "Z to A", "Newest", "Oldest"}, kept.member_order, select_order{}),
      c::visible(editable(box, power_need::invite{}), action(*box->colours_, "Invite", sends<::mux::ui::request::open_room_invite>{{facts.chat}})),
      c::derived<draft>(listing, c::mount<rows_t, member_listing>([box](const member_listing& data) {
        return std::tuple{member_rows(box, data)};
      }, {.fillX = true, .autoSize = scene::axes::kY}))), draft{box, kept.member_query, kept.member_filter, kept.member_order});
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
template <class Box> struct power_draft { Box* box; std::string power, name, color, icon; };
template <class Box> struct power_events {
  Box* box;
  auto on(save_power, const power_draft<Box>& draft) const {
      if (box->template part<state>().pending) return skiff::model::Up{std::optional<request::change_room>{}};
      const auto number = integer(draft.power);
      const auto name = draft.name;
      const auto& rules = rules_of(box->facts.theirs);
      const auto event = rules.needs.events.find("in.cinny.room.power_level_tags");
      const auto need = event == rules.needs.events.end() ? rules.needs.state_default : event->second;
      if (!number || name.empty() || box->facts.mine < need) return skiff::model::Up{std::optional<request::change_room>{}};
      if (box->facts.mine != kCreatorPower && *number > box->facts.mine) return skiff::model::Up{std::optional<request::change_room>{}};
      auto tags = box->template part<state>().data.tags;
      auto& tag = tags[std::to_string(*number)];
      tag.name = name;
      const auto color = draft.color;
      tag.color = color.empty() ? std::nullopt : std::optional(color);
      const auto icon = draft.icon;
      if (!icon.empty()) { if (!tag.icon) tag.icon.emplace(); tag.icon->key = icon; } else tag.icon.reset();
      return skiff::model::Up{submit(box, room_change::set_tags{std::move(tags)})};
    }
  auto on(delete_power, const power_draft<Box>& draft) const {
    const auto number = integer(draft.power);
    const auto& rules = rules_of(box->facts.theirs);
    const auto found = rules.needs.events.find("in.cinny.room.power_level_tags");
    const auto need = found == rules.needs.events.end() ? rules.needs.state_default : found->second;
    if (!number || box->template part<state>().pending || box->facts.mine < need ||
        std::ranges::contains(power_values(rules, room_management{}), *number))
      return skiff::model::Up{std::optional<request::change_room>{}};
    auto tags = box->template part<state>().data.tags;
    if (!tags.erase(std::to_string(*number))) return skiff::model::Up{std::optional<request::change_room>{}};
    return skiff::model::Up{submit(box, room_change::set_tags{std::move(tags)})};
  }
};
template <class Box> void preserve_draft(const power_draft<Box>& draft) {
  auto& kept = draft.box->template part<state>();
  kept.tag_power = draft.power; kept.tag_name = draft.name;
  kept.tag_color = draft.color; kept.tag_icon = draft.icon;
}
template <class Box> auto permission_row(Box* box, const permission_item& item) {
  const auto& rules = rules_of(box->facts.theirs);
  const auto& kept = box->template part<state>();
  const auto& draft = kept.permissions ? *kept.permissions : rules.needs;
  const auto current = required(draft, item.location);
  auto values = power_values(rules, kept.data);
  if (!std::ranges::contains(values, current)) values.push_back(current);
  std::ranges::sort(values, std::greater{});
  const auto names = std::ranges::to<std::vector<std::string>>(std::views::transform(values, [&](auto value) { return power_name(value, kept.data) + " & Above"; }));
  const auto selected = static_cast<std::size_t>(std::ranges::find(values, current) - values.begin());
  return tile(*box->colours_, item.title, current != required(rules.needs, item.location) ? "Unsaved change" : "",
      skiff::compose::styled({.width = 180.0f, .disabled = !editable(box, power_need::change_permissions{}) || required(rules.needs, item.location) > box->facts.mine},
          make_choice_menu(*box->colours_, "", names, selected, typename permission_actions<Box>::choose{box, item.location, std::move(values)})));
}
template <class Box> auto permission_section(Box* box, const permission_group& group) {
  namespace c = skiff::compose;
  return c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      part_heading(*box->colours_, group.title),
      c::many(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::ranges::to<std::vector>(
          std::views::transform(group.items, [&](const auto& item) { return permission_row(box, item); }))));
}
template <class Box> auto permissions_page(Box* box, const room_settings_facts& facts) {
  namespace c = skiff::compose; using draft = power_draft<Box>; using acts = permission_actions<Box>;
  const auto& colours = *box->colours_; const auto& kept = box->template part<state>(); const auto& rules = rules_of(facts.theirs);
  const auto permission = rules.needs.events.find("in.cinny.room.power_level_tags");
  const bool tags_allowed = facts.mine >= (permission == rules.needs.events.end() ? rules.needs.state_default : permission->second);
  return c::local<draft>(power_events<Box>{box}, c::column(page_look(),
      tab_heading(colours, "Permissions"), explained(colours, kept.pending ? "Saving…" : kept.error),
      part_heading(colours, "Power Levels"), explained(colours, "Manage and customize incremental power levels for users."),
      c::many(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::ranges::to<std::vector>(
          std::views::transform(power_values(rules, kept.data), [&](auto value) { return nodes::Text(std::format("{} · {}", power_name(value, kept.data), value), 14.0f, colours.accent); }))),
      c::visible(tags_allowed, c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          model_field<&draft::power>(colours, "Power", "75"), model_field<&draft::name>(colours, "Name", "Bot"),
          model_field<&draft::color>(colours, "Color", "#91cfdf"), model_field<&draft::icon>(colours, "Icon", "Emoji or mxc:// image"),
          action(colours, "Save", sends<save_power>{}, !kept.pending),
          action(colours, "Delete Unused", sends<delete_power>{}, !kept.pending))),
      c::many(c::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::ranges::to<std::vector>(
          std::views::transform(permission_groups(), [&](const auto& group) { return permission_section(box, group); }))),
      c::visible(kept.permissions.has_value(), explained(colours, "You have unsaved changes.")),
      c::visible(kept.permissions.has_value(), action(colours, "Apply", typename acts::apply{box}, editable(box, power_need::change_permissions{}))),
      c::visible(kept.permissions.has_value(), action(colours, "Reset", typename acts::apply{box, true}, !kept.pending))),
      draft{box, kept.tag_power, kept.tag_name, kept.tag_color, kept.tag_icon});
}

template <class Box> auto packs_page(Box* box, const room_settings_facts&) {
  return skiff::compose::column(page_look(), tab_heading(*box->colours_, "Emojis & Stickers"),
      explained(*box->colours_, "Manage this room's emoji and sticker packs."),
      action(*box->colours_, "Room Packs", sends<::mux::ui::request::open_room_packs>{}));
}
struct save_account_data {};
struct choose_account_data { std::size_t index; };
struct select_account_data { choose_account_data operator()(std::size_t index) const { return {index}; } };
template <class Box> struct developer_draft { Box* box; std::string type, json; };
template <class Box> struct developer_events {
  Box* box;
  auto on(save_account_data, const developer_draft<Box>& draft) const {
    return skiff::model::Up{draft.type.empty() ? std::nullopt : submit(box, room_change::set_account_data{draft.type, draft.json})};
  }
  auto on(choose_account_data event, const developer_draft<Box>& draft) const {
    auto next = draft;
    const auto& entries = box->template part<state>().data.account_data;
    if (event.index < entries.size()) { next.type = entries[event.index].type; next.json = entries[event.index].json; }
    return skiff::model::over<developer_draft<Box>>(skiff::model::setTo(std::move(next)));
  }
};
template <class Box> void preserve_draft(const developer_draft<Box>& draft) {
  auto& kept = draft.box->template part<state>(); kept.account_type = draft.type; kept.account_json = draft.json;
}
template <class Box> auto developer_page(Box* box, const room_settings_facts& facts) {
  namespace c = skiff::compose; using draft = developer_draft<Box>;
  const auto& kept = box->template part<state>(); const auto& colours = *box->colours_;
  const auto names = std::ranges::to<std::vector<std::string>>(std::views::transform(kept.data.account_data, &managed_account_data::type));
  return c::local<draft>(developer_events<Box>{box}, c::column(page_look(), tab_heading(colours, "Developer Tools"),
      explained(colours, kept.pending ? "Saving…" : kept.error), part_heading(colours, "Options"), id_line(colours, facts.id, "", "Room ID"),
      action(colours, "State Events", sends<request::explore_state>{}), action(colours, "Send Event", sends<request::open_send_custom>{}),
      part_heading(colours, "Account Data"), explained(colours, "Private personalization data stored within room."),
      make_choice_menu(colours, "Select Account Data", names, names.size(), select_account_data{}),
      model_field<&draft::type>(colours, "Type", ""), model_field<&draft::json>(colours, "Content", "{}"),
      action(colours, "Save", sends<save_account_data>{}, !kept.pending)), draft{box, kept.account_type, kept.account_json});
}
template <class Page> requires requires(Page& page) { preserve_draft(page.fModel.root()); }
void preserve_manage_page(Page& page) { preserve_draft(page.fModel.root()); }
} // namespace mux::proto::matrix::manage_detail

export namespace mux::proto::matrix {
namespace manage { struct room {}; struct members {}; struct roles {}; struct packs {}; struct advanced {}; }
constexpr ui::manage_tab_list<manage::room, manage::members, manage::roles, manage::packs, manage::advanced> manage_tabs(const state&) { return {}; }
constexpr type_tag<manage_detail::part> manage_part_type(const state&) { return {}; }
constexpr manage::room manage_initial_tab(const state&) { return {}; }
constexpr std::string_view manage_preferences_title(const state&) { return "Preferences"; }
}
export namespace mux::proto::matrix::manage {
constexpr std::string_view tab_title(room) { return "General"; }
constexpr std::string_view tab_title(members) { return "Members"; }
constexpr std::string_view tab_title(roles) { return "Permissions"; }
constexpr std::string_view tab_title(packs) { return "Emojis & Stickers"; }
constexpr std::string_view tab_title(advanced) { return "Developer Tools"; }
inline ui::icon_t tab_icon(room) { return ui::icon::gear{}; }
inline ui::icon_t tab_icon(members) { return ui::icon::people{}; }
inline ui::icon_t tab_icon(roles) { return ui::icon::eye{}; }
inline ui::icon_t tab_icon(packs) { return ui::icon::smile{}; }
inline ui::icon_t tab_icon(advanced) { return ui::icon::sliders{}; }
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
