// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.room_admin.app:matrix -- What the program does with a Matrix
// room's admin pages: a press done to what the settings keep, Cinny's rules
// -- what one may, what is staged until Apply, what a press confirms first
// -- and the write it asks sent; what the server said of the room, of an
// avatar uploaded and of a write, taken in. Built where Matrix is.
export module mux.plugin.room_admin.app:matrix;

import std;
import splice;
import knot;
import mux.core;
import mux.config;
import mux.protocols;
import mux.proto.matrix;
import mux.proto.matrix.changes;
import mux.proto.matrix.requests;
import mux.ui;
import mux.app.network;
import mux.app.services;
import mux.plugin.room_settings.types;
import mux.plugin.room_settings.ui;
import mux.plugin.room_admin.types;

export namespace mux::proto::matrix::app_detail {
// What a change on the settings' pages asks of the server: a change of the
// room.
using manage_ask_t = spl::variant<room_change_t>;
[[nodiscard]] inline std::optional<std::int64_t> integer(std::string_view text) {
  std::int64_t value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  return error == std::errc{} && end == text.data() + text.size() ? std::optional(value) : std::nullopt;
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
// The levels a room names: its needs', its users', its tags'.
[[nodiscard]] inline std::vector<std::int64_t> power_values(const room_rules& rules, const room_management& data) {
  const auto& n = rules.needs;
  std::set<std::int64_t, std::greater<>> values{n.users_default, n.events_default, n.state_default, n.invite, n.kick, n.ban, n.redact, n.notify_room};
  for (const auto& entry : rules.powers) { if (entry.second != kCreatorPower) values.insert(entry.second); }
  for (const auto& entry : n.events) { values.insert(entry.second); }
  for (const auto& entry : data.tags) {
    if (const auto number = integer(entry.first); number && *number != kCreatorPower) values.insert(*number);
  }
  return values | std::ranges::to<std::vector<std::int64_t>>();
}
// The level a kind of state event asks.
[[nodiscard]] inline std::int64_t event_need(const room_rules& rules, std::string_view type) {
  const auto found = rules.needs.events.find(std::string(type));
  return found == rules.needs.events.end() ? rules.needs.state_default : found->second;
}

// A change on the settings' pages, done to what Matrix keeps while they are
// open: Cinny's rules -- what one may, what is staged until Apply, what a
// press confirms first -- and the write it asks, where it asks one. A write
// on its way makes the rest wait.
struct manage_step {
  ::mux::plugins::room_settings::room_settings_facts& facts;
  manage_state& kept;
  using asked = std::optional<manage_ask_t>;
  [[nodiscard]] bool editable(const power_need_t& need) const { return !kept.pending && may(facts, need); }
  asked submit(room_change_t change) {
    if (kept.pending)
      return std::nullopt;
    kept.pending = true;
    kept.error.clear();
    return manage_ask_t{std::move(change)};
  }
  [[nodiscard]] const room_rules& rules() const { return rules_of(facts.theirs); }

  asked operator()(const manage_change::edit& one) {
    switch (one.target) {
      case editor::profile:
        kept.profile_editor = one.open;
        if (!one.open)
          kept.avatar.reset();
        ++kept.profile_round;
        break;
      case editor::powers: kept.powers_editor = one.open; break;
      case editor::power:
        kept.power_form = one.open;
        if (one.open)
          kept.editing_power.reset();
        break;
    }
    return std::nullopt;
  }
  asked operator()(const manage_change::encryption& one) {
    if (one.cancel) {
      kept.confirming_encryption = false;
      return std::nullopt;
    }
    if (!editable(power_need::encrypt{}) || facts.encrypted)
      return std::nullopt;
    // The first press asks for a second: encryption cannot be undone.
    if (!std::exchange(kept.confirming_encryption, true))
      return std::nullopt;
    kept.confirming_encryption = false;
    return submit(room_change::encrypt{});
  }
  asked operator()(const manage_change::directory&) {
    const bool joinable = spl::visit(spl::overloaded{[](join_rule::open) { return true; }, [](join_rule::knock) { return true; },
                                                     [](const join_rule::knock_restricted&) { return true; }, [](const auto&) { return false; }},
                                     rules().join_rule);
    if (!editable(power_need::change_address{}) || !joinable || !kept.data.directory)
      return std::nullopt;
    return submit(room_change::set_directory{!*kept.data.directory});
  }
  // What is published: the main address among them.
  [[nodiscard]] std::vector<std::string> published() const {
    auto out = facts.other_aliases;
    if (facts.alias && !std::ranges::contains(out, *facts.alias))
      out.push_back(*facts.alias);
    return out;
  }
  asked operator()(const manage_change::address& one) {
    using kind = manage_change::address_kind;
    if (kept.pending)
      return std::nullopt;
    if (one.what == kind::remove)
      return submit(room_change::delete_alias{one.alias});
    if (!editable(power_need::change_address{}))
      return std::nullopt;
    auto main = facts.alias;
    auto listed = published();
    switch (one.what) {
      case kind::main: main = main == one.alias ? std::nullopt : std::optional(one.alias); break;
      case kind::publish: if (!std::ranges::contains(listed, one.alias)) listed.push_back(one.alias); break;
      case kind::unpublish: std::erase(listed, one.alias); break;
      case kind::remove: break;
    }
    return submit(room_change::set_addresses{main, std::move(listed)});
  }
  asked operator()(const manage_change::expand_addresses&) {
    kept.local_addresses_expanded = !kept.local_addresses_expanded;
    return std::nullopt;
  }
  asked operator()(const manage_change::select_address& one) {
    if (!kept.pending && !kept.selected_aliases.erase(one.alias))
      kept.selected_aliases.insert(one.alias);
    return std::nullopt;
  }
  asked operator()(const manage_change::selected& one) {
    using kind = manage_change::selection_kind;
    if (kept.pending || kept.selected_aliases.empty())
      return std::nullopt;
    if (one.what == kind::remove)
      return submit(room_change::delete_aliases{kept.selected_aliases | std::ranges::to<std::vector<std::string>>()});
    if (!editable(power_need::change_address{}))
      return std::nullopt;
    auto listed = facts.other_aliases;
    for (const std::string& alias : kept.selected_aliases) {
      if (one.what == kind::unpublish)
        std::erase(listed, alias);
      else if (!std::ranges::contains(listed, alias))
        listed.push_back(alias);
    }
    return submit(room_change::set_addresses{facts.alias, std::move(listed)});
  }
  asked operator()(const manage_change::access& one) {
    return editable(power_need::change_access{}) ? submit(room_change::set_join_rule{one.rule}) : std::nullopt;
  }
  asked operator()(const manage_change::history& one) {
    return editable(power_need::change_history{}) ? submit(room_change::set_history{one.rule}) : std::nullopt;
  }
  asked operator()(const manage_change::add_child& one) {
    return editable(power_need::change_settings{}) ? submit(room_change::add_child{one.room}) : std::nullopt;
  }
  asked operator()(const manage_change::remove_child& one) {
    return editable(power_need::change_settings{}) ? submit(room_change::remove_child{one.room}) : std::nullopt;
  }
  // The profile saved: what changed of it, as far as one may change each.
  asked operator()(const manage_change::save_profile& one) {
    room_change::set_profile change;
    if (editable(power_need::rename{}) && one.name != facts.name)
      change.name = one.name;
    if (editable(power_need::retopic{}) && one.topic != facts.topic)
      change.topic = one.topic;
    if (editable(power_need::change_avatar{}) && kept.avatar && *kept.avatar != facts.avatar.value_or(""))
      change.avatar = kept.avatar;
    return change.name || change.topic || change.avatar ? submit(std::move(change)) : std::nullopt;
  }
  asked operator()(const manage_change::remove_avatar&) {
    if (editable(power_need::change_avatar{}))
      kept.avatar = std::string();
    return std::nullopt;
  }
  // A local address made: its spaces dashes, on the account's server.
  asked operator()(const manage_change::create_address& one) {
    const auto first = one.local.find_first_not_of(" \t\r\n");
    const auto last = one.local.find_last_not_of(" \t\r\n");
    auto local = first == std::string::npos ? std::string() : one.local.substr(first, last - first + 1);
    std::ranges::replace(local, ' ', '-');
    const auto colon = facts.chat.account.address.find(':');
    if (local.empty() || colon == std::string::npos || local.find_first_of("#:\n\r") != std::string::npos)
      return std::nullopt;
    return submit(room_change::create_alias{"#" + local + facts.chat.account.address.substr(colon)});
  }
  // Upgraded: the first press asks to confirm; the second, with a version
  // the server has, upgrades.
  asked operator()(const manage_change::upgrade& one) {
    if (!editable(power_need::upgrade{}))
      return std::nullopt;
    if (!std::exchange(kept.confirming_upgrade, true))
      return std::nullopt;
    if (!std::ranges::contains(kept.data.versions, one.version)) {
      kept.error = "Choose a room version supported by your server.";
      return std::nullopt;
    }
    kept.confirming_upgrade = false;
    const auto creators = one.creators | std::views::split(' ') | std::views::filter([](auto id) { return !id.empty(); }) | std::views::transform([](auto id) { return std::string(id.begin(), id.end()); }) | std::ranges::to<std::vector>();
    return submit(room_change::upgrade{one.version, integer(one.version).value_or(0) >= 12 ? std::optional(creators) : std::nullopt});
  }
  asked operator()(const manage_change::cancel_upgrade&) {
    kept.confirming_upgrade = false;
    return std::nullopt;
  }
  // A member's level: not a creator's, not one at or over one's own (but
  // one's own), and not over one's own.
  asked operator()(const manage_change::member_power& one) {
    if (!editable(power_need::change_permissions{}))
      return std::nullopt;
    const auto current = level_of(rules(), one.user);
    if (current == kCreatorPower || (current >= facts.mine && one.user != facts.chat.account.address) || one.level > facts.mine)
      return std::nullopt;
    return submit(room_change::set_power{one.user, one.level});
  }
  // A permission staged: kept until Apply, gone where it is as it was.
  void stage(const permission_t& target, std::optional<std::int64_t> level) {
    const auto& original = rules().needs;
    if (!kept.permissions)
      kept.permissions = original;
    spl::visit(spl::overloaded{[&](const event_permission& event) {
                                 if (!level)
                                   kept.permissions->events.erase(event.type);
                               },
                               [](const power_need_t&) {}},
               target);
    if (level)
      assign_permission(*kept.permissions, target, *level);
    if (*kept.permissions == original)
      kept.permissions.reset();
  }
  asked operator()(const manage_change::permission& one) {
    if (!editable(power_need::change_permissions{}) || required(rules().needs, one.target) > facts.mine || one.level > facts.mine)
      return std::nullopt;
    stage(one.target, one.level);
    return std::nullopt;
  }
  asked operator()(const manage_change::apply_permissions& one) {
    if (kept.pending)
      return std::nullopt;
    if (one.reset) {
      kept.permissions.reset();
      return std::nullopt;
    }
    if (!editable(power_need::change_permissions{}) || !kept.permissions)
      return std::nullopt;
    return submit(room_change::set_permissions{rules().needs, *kept.permissions});
  }
  asked operator()(const manage_change::add_custom_permission& one) {
    const auto level = integer(one.level);
    if (!editable(power_need::change_permissions{}) || !level || *level > facts.mine || one.event.empty() ||
        one.event.find_first_of(" \t\n\r") != std::string::npos || required(rules().needs, permission_t{event_permission{one.event}}) > facts.mine)
      return std::nullopt;
    stage(permission_t{event_permission{one.event}}, *level);
    ++kept.custom_round;
    return std::nullopt;
  }
  asked operator()(const manage_change::remove_custom_permission& one) {
    if (!editable(power_need::change_permissions{}) || required(rules().needs, permission_t{event_permission{one.event}}) > facts.mine)
      return std::nullopt;
    stage(permission_t{event_permission{one.event}}, std::nullopt);
    return std::nullopt;
  }
  // A power level's tag: staged until Apply, as one may edit tags.
  asked operator()(const manage_change::save_power& one) {
    const auto number = integer(one.power);
    if (kept.pending || !number || one.name.empty() || facts.mine < event_need(rules(), "in.cinny.room.power_level_tags") ||
        (facts.mine != kCreatorPower && *number > facts.mine))
      return std::nullopt;
    auto tags = kept.tag_draft.value_or(kept.data.tags);
    auto& tag = tags[std::to_string(*number)];
    tag.name = one.name;
    tag.color = one.color.empty() ? std::nullopt : std::optional(one.color);
    if (one.icon.empty())
      tag.icon.reset();
    else {
      if (!tag.icon)
        tag.icon.emplace();
      tag.icon->key = one.icon;
    }
    kept.tag_draft = std::move(tags);
    kept.power_form = false;
    return std::nullopt;
  }
  asked operator()(const manage_change::delete_power& one) {
    const auto number = integer(one.power);
    if (!number || kept.pending || facts.mine < event_need(rules(), "in.cinny.room.power_level_tags") ||
        std::ranges::contains(power_values(rules(), room_management{}), *number))
      return std::nullopt;
    auto tags = kept.tag_draft.value_or(kept.data.tags);
    if (tags.erase(std::to_string(*number))) {
      kept.tag_draft = std::move(tags);
      kept.power_form = false;
    }
    return std::nullopt;
  }
  asked operator()(const manage_change::edit_power& one) {
    kept.power_form = true;
    kept.editing_power = one.power;
    return std::nullopt;
  }
  asked operator()(const manage_change::delete_tag& one) {
    if (!kept.pending && !std::ranges::contains(power_values(rules(), room_management{}), one.power) && !kept.deleted_powers.erase(one.power))
      kept.deleted_powers.insert(one.power);
    return std::nullopt;
  }
  asked operator()(const manage_change::apply_tags& one) {
    if (kept.pending)
      return std::nullopt;
    if (one.reset) {
      kept.tag_draft.reset();
      kept.deleted_powers.clear();
      return std::nullopt;
    }
    if (!kept.tag_draft && kept.deleted_powers.empty())
      return std::nullopt;
    auto tags = kept.tag_draft.value_or(kept.data.tags);
    for (std::int64_t value : kept.deleted_powers) { tags.erase(std::to_string(value)); }
    return submit(room_change::set_tags{std::move(tags)});
  }
};

// What Matrix keeps of the settings open for a room, changed as `change`
// says -- with the facts -- where they are up, for it.
template <class Change>
void change_manage(ui::shown_model& showing, const mux::conversation_id& in, Change change) {
  const auto& open = *showing.look<std::optional<::mux::plugins::room_settings::room_settings_facts>>();
  if (!open || open->chat != in)
    return;
  auto now = *open;
  spl::visit(spl::overloaded{[&](manage_state& kept) { change(now, kept); }, [](auto&) {}}, now.manage);
  mux::ui::show(showing, std::optional(std::move(now)));
}
// A write done: what it changed of the room, in the facts; the drafts it
// settles let go.
inline void saved_in(::mux::plugins::room_settings::room_settings_facts& facts, manage_state& kept, const management_saved& one) {
  kept.pending = false;
  kept.error = one.error;
  if (!one.error.empty())
    return;
  auto& rules = rules_in(facts.theirs);
  spl::visit(spl::overloaded{
      [&](const room_change::set_join_rule& change) { rules.join_rule = change.rule; },
      [&](const room_change::set_history& change) { rules.history = change.rule; },
      [&](room_change::encrypt) { facts.encrypted = true; },
      [&](const room_change::set_permissions& change) {
        rules.needs = change.after;
        rules.power_default = change.after.users_default;
        kept.permissions.reset();
      },
      [&](const room_change::set_tags& change) {
        kept.data.tags = change.tags;
        kept.tag_draft.reset();
        kept.deleted_powers.clear();
      },
      [&](const room_change::set_profile& change) {
        if (change.name) facts.name = *change.name;
        if (change.topic) facts.topic = *change.topic;
        if (change.avatar) facts.avatar = change.avatar->empty() ? std::nullopt : change.avatar;
        kept.avatar.reset();
        kept.profile_editor = false;
        ++kept.profile_round;
      },
      [&](const room_change::set_addresses& change) {
        facts.alias = change.main;
        facts.other_aliases = change.published | std::views::filter([&](const std::string& alias) { return !change.main || alias != *change.main; }) | std::ranges::to<std::vector>();
        kept.selected_aliases.clear();
      },
      [&](const room_change::set_directory& change) { kept.data.directory = change.published; },
      [&](const room_change::set_power& change) {
        rules.powers.insert_or_assign(change.user, change.level);
        if (change.user == facts.chat.account.address) facts.mine = change.level;
      },
      [&](const room_change::set_need& change) { assign_need(rules.needs, change.need, change.level); },
      [&](const room_change::set_event_need& change) { rules.needs.events.insert_or_assign(change.event, change.level); },
      [&](const room_change::add_child& change) {
        if (const auto child = std::ranges::find(facts.addable, change.room, &::mux::plugins::room_settings::room_settings_facts::named_room::id); child != facts.addable.end()) {
          facts.children.push_back(*child);
          facts.addable.erase(child);
        }
      },
      [&](const room_change::remove_child& change) {
        if (const auto child = std::ranges::find(facts.children, change.room, &::mux::plugins::room_settings::room_settings_facts::named_room::id); child != facts.children.end()) {
          facts.addable.push_back(*child);
          facts.children.erase(child);
        }
      },
      [&](const room_change::create_alias&) { ++kept.address_round; },
      [&](const room_change::delete_aliases&) { kept.selected_aliases.clear(); },
      [](const auto&) {}},
             one.change);
}
}  // namespace mux::proto::matrix::app_detail

export namespace mux::plugins::room_admin {

struct matrix_glue {
  app::services* s_;

  // Only the room and account that asked for the read take it.
  void take(const proto::matrix::management_loaded& one) {
    proto::matrix::app_detail::change_manage(*s_->showing, one.in, [&](room_settings::room_settings_facts&, proto::matrix::manage_state& kept) {
      kept.data = one.data;
      kept.loading = false;
      kept.error = one.error;
    });
  }
  // A room avatar uploaded: kept to be saved with the profile.
  void take(const proto::matrix::room_avatar_uploaded& one) {
    proto::matrix::app_detail::change_manage(*s_->showing, one.in, [&](room_settings::room_settings_facts&, proto::matrix::manage_state& kept) {
      kept.pending = false;
      kept.error = one.error;
      if (one.error.empty())
        kept.avatar = one.uri;
    });
  }
  void take(const proto::matrix::management_saved& one) {
    proto::matrix::app_detail::change_manage(*s_->showing, one.in, [&](room_settings::room_settings_facts& facts, proto::matrix::manage_state& kept) {
      proto::matrix::app_detail::saved_in(facts, kept, one);
    });
  }
  // A press on the pages: what the settings keep changed, and the write it
  // asks sent -- for the room being managed, by its account.
  void apply(const proto::matrix::request::manage_room& one) {
    const auto& open = *s_->showing->look<std::optional<room_settings::room_settings_facts>>();
    if (!open)
      return;
    auto now = *open;
    std::optional<proto::matrix::app_detail::manage_ask_t> asked;
    spl::visit(spl::overloaded{[&](proto::matrix::manage_state& kept) { asked = spl::visit(proto::matrix::app_detail::manage_step{now, kept}, one.change); },
                               [](auto&) {}},
               now.manage);
    ui::show(*s_->showing, std::optional(std::move(now)));
    if (!asked)
      return;
    const auto chosen = s_->managed();
    if (!chosen || s_->demo())
      return;
    spl::visit(spl::overloaded{[&](proto::matrix::room_change_t& change) {
                 s_->net->on_account_of(*chosen, [room = chosen->id, change](auto& account)
                                                     -> decltype(void(account.change_room(room, change))) { account.change_room(room, change); });
               }},
               *asked);
  }
};

}  // namespace mux::plugins::room_admin
