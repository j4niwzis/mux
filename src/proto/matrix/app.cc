// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.proto.matrix -- What the program does with Matrix's own changes:
// what its developer tools found, its sticker packs, an emoji verification,
// the account's sessions and its security. Templates on the program, which
// calls program_told(app, change) where it takes changes (mux.app.proto).
export module mux.app.proto.matrix;

import std;
import splice;
import mux.core;
import mux.config;
import mux.protocols;
import mux.proto.kept;
import mux.proto.matrix.changes;
import mux.proto.matrix.requests;
import knot;
import mux.ui;
import mux.ui.proto.matrix;

// What Matrix's glue asks of an account, by the program's network: each
// where the account's client has the call (ask_if_able, in on_account).
namespace mux::proto::matrix::ops {

template <class Net>
void list_sessions(Net& net, const account_id& by) {
  net.on_account(by, [](auto& account) -> decltype(void(account.list_sessions())) { account.list_sessions(); });
}

template <class Net>
void sign_out_sessions(Net& net, const account_id& by, std::vector<std::string> devices, std::string password) {
  net.on_account(by, [devices = std::move(devices), password = std::move(password)](auto& account) -> decltype(void(account.sign_out_sessions(devices, password))) { account.sign_out_sessions(devices, password); });
}

template <class Net>
void rename_session(Net& net, const account_id& by, std::string device, std::string name) {
  net.on_account(by, [device = std::move(device), name = std::move(name)](auto& account) -> decltype(void(account.rename_session(device, name))) { account.rename_session(device, name); });
}

template <class Net>
void setup_cross_signing(Net& net, const account_id& by, std::string password, bool reset = false) {
  net.on_account(by, [password = std::move(password), reset](auto& account) -> decltype(void(account.setup_cross_signing(password, reset))) { account.setup_cross_signing(password, reset); });
}

template <class Net>
void restore_cross_signing(Net& net, const account_id& by, std::string recovery) {
  net.on_account(by, [recovery = std::move(recovery)](auto& account) -> decltype(void(account.restore_cross_signing(recovery))) { account.restore_cross_signing(recovery); });
}

template <class Net>
void reset_backup(Net& net, const account_id& by) {
  net.on_account(by, [](auto& account) -> decltype(void(account.reset_backup())) { account.reset_backup(); });
}

template <class Net>
void delete_backup(Net& net, const account_id& by) {
  net.on_account(by, [](auto& account) -> decltype(void(account.delete_backup())) { account.delete_backup(); });
}

template <class Net>
void continue_uia(Net& net, const account_id& by) {
  net.on_account(by, [](auto& account) -> decltype(void(account.continue_uia())) { account.continue_uia(); });
}
template <class Net>
void cancel_uia(Net& net, const account_id& by) {
  net.on_account(by, [](auto& account) -> decltype(void(account.cancel_uia())) { account.cancel_uia(); });
}

template <class Net>
void sign_out_unverified(Net& net, const account_id& by, std::string password) {
  net.on_account(by, [password = std::move(password)](auto& account) -> decltype(void(account.sign_out_unverified(password))) { account.sign_out_unverified(password); });
}

template <class Net>
void export_room_keys(Net& net, const account_id& by, std::string path, std::string passphrase) {
  net.on_account(by, [path = std::move(path), passphrase = std::move(passphrase)](auto& account) -> decltype(void(account.export_room_keys(path, passphrase))) { account.export_room_keys(path, passphrase); });
}

template <class Net>
void import_room_keys(Net& net, const account_id& by, std::string path, std::string passphrase) {
  net.on_account(by, [path = std::move(path), passphrase = std::move(passphrase)](auto& account) -> decltype(void(account.import_room_keys(path, passphrase))) { account.import_room_keys(path, passphrase); });
}


}  // namespace mux::proto::matrix::ops

namespace mux::proto::matrix::app_detail {
// The user's own member event in a room, as /myroomnick sends it: joined,
// under a name of its own there.
struct member_named {
  std::string membership = "join";
  std::string displayname;
  friend consteval auto json_schema(knot::type<member_named>) { return knot::schema<member_named>(); }
};
}  // namespace mux::proto::matrix::app_detail

namespace mux::proto::matrix::app_detail {
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
  ui::room_settings_facts& facts;
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
template <class App, class Change>
void change_manage(App& app, const mux::conversation_id& in, Change change) {
  const auto& open = *app.showing.template look<std::optional<mux::ui::room_settings_facts>>();
  if (!open || open->chat != in)
    return;
  auto now = *open;
  spl::visit(spl::overloaded{[&](manage_state& kept) { change(now, kept); }, [](auto&) {}}, now.manage);
  mux::ui::show(app.showing, std::optional(std::move(now)));
}
// A write done: what it changed of the room, in the facts; the drafts it
// settles let go.
inline void saved_in(ui::room_settings_facts& facts, manage_state& kept, const management_saved& one) {
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
        if (const auto child = std::ranges::find(facts.addable, change.room, &ui::room_settings_facts::named_room::id); child != facts.addable.end()) {
          facts.children.push_back(*child);
          facts.addable.erase(child);
        }
      },
      [&](const room_change::remove_child& change) {
        if (const auto child = std::ranges::find(facts.children, change.room, &ui::room_settings_facts::named_room::id); child != facts.children.end()) {
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

export namespace mux::proto::matrix {

// A session the server gave at login: kept with the account, as its kept
// type keeps one (take_session).
template <class App>
void program_told(App& app, const session_given& given) {
  app.change_account(given.account.address, [&](mux::config::account_t& account) {
    spl::visit([&](auto& one) {
      using kept_defaults::take_session;
      take_session(one, given);
    }, account.own);
  });
}
// A registration stage done on the server's own page: opened in the browser,
// and said -- the registration carries on by itself once it is done there.
template <class App>
void program_told(App& app, const registration_page& page) {
  app.take(mux::ui::request::open_url{page.url});
  app.shared.notice("Finish registering in your browser",
                          "The server asks for a step it does on its own page, now open in your browser. Once it is done "
                          "there, mux carries on with the registration by itself.");
}
// The server's own sign-in page (OAuth 2.0): opened in the browser, and
// said -- the account carries on by itself once the browser comes back.
template <class App>
void program_told(App& app, const sign_in_page& page) {
  app.take(mux::ui::request::open_url{page.url});
  app.shared.notice("Sign in in your browser",
                          "The server's sign-in page is open in your browser. Once you have signed in there, the "
                          "browser comes back to mux, and the account carries on by itself.");
}
// A step of interactive auth for the browser: its page opened there, and a
// box to say when it is done.
template <class App>
void program_told(App& app, const uia_in_browser& asked) {
  app.take(mux::ui::request::open_url{asked.url});
  mux::ui::show(app.showing, std::optional(tool::uia_step{asked.what, asked.url}));
}
// Only the room and account that requested the read may receive it.
template <class App>
void program_told(App& app, const management_loaded& one) {
  app_detail::change_manage(app, one.in, [&](ui::room_settings_facts&, manage_state& kept) {
    kept.data = one.data;
    kept.loading = false;
    kept.error = one.error;
  });
}
// A room avatar uploaded: kept to be saved with the profile.
template <class App>
void program_told(App& app, const room_avatar_uploaded& one) {
  app_detail::change_manage(app, one.in, [&](ui::room_settings_facts&, manage_state& kept) {
    kept.pending = false;
    kept.error = one.error;
    if (one.error.empty())
      kept.avatar = one.uri;
  });
}
template <class App>
void program_told(App& app, const management_saved& one) {
  app_detail::change_manage(app, one.in, [&](ui::room_settings_facts& facts, manage_state& kept) { app_detail::saved_in(facts, kept, one); });
}
// What Matrix keeps of an account whose pages are open, changed: where
// they are open for that account.
template <class App, class Change>
void change_account_state(App& app, const account_id& by, Change change) {
  using facts_t = std::optional<mux::ui::account_page_facts>;
  if (const facts_t& open = *app.showing.template look<facts_t>(); !open || open->by != by)
    return;
  mux::ui::change_shown<facts_t>(app.showing, [&](facts_t& open) {
    spl::visit(spl::overloaded{[&](account_state& kept) { change(kept); }, [](auto&) {}}, open->kept);
  });
}
// The sessions as the server listed them: a sign-out on its way is done.
template <class App>
void program_told(App& app, const sessions_listed& listed) {
  change_account_state(app, listed.by, [&](account_state& kept) {
    kept.sessions = listed.sessions;
    kept.current = listed.current;
    kept.signing.clear();
    kept.signing_out = false;
    kept.refused.clear();
    kept.needs_password = false;
  });
}
template <class App>
void program_told(App& app, const security_state& state) {
  change_account_state(app, state.by, [&](account_state& kept) { kept.security = account_security{state.cross_signing, state.backup}; });
}
// The server said no: why; and, where it wants the password, the sessions
// kept to be signed out again with it.
template <class App>
void program_told(App& app, const sessions_refused& said) {
  change_account_state(app, said.by, [&](account_state& kept) {
    kept.signing_out = false;
    kept.refused = said.why;
    kept.needs_password = said.needs_password;
    if (!said.needs_password)
      kept.signing.clear();
  });
}

}  // namespace mux::proto::matrix

// What Matrix's UI asks, done: each for the account whose pages are open.
export namespace mux::proto::matrix::request {

// Asked with a passphrase (or the account's password) first: its dialog up,
// for that account.
template <class App, class Purpose>
void with_passphrase(App& app, Purpose purpose) {
  app.shared.with_chosen_account([&](config::account_t& account) {
    app.keys_of = App::id_of(account);
    mux::ui::show(app.showing, std::optional(mux::ui::passphrase_facts{purpose, std::nullopt}));
  });
}
template <class App>
void program_asked(App& app, const setup_cross_signing&) {
  with_passphrase(app, passphrase::cross_signing{});
}
template <class App>
void program_asked(App& app, const restore_cross_signing&) {
  with_passphrase(app, passphrase::recovery{});
}
template <class App>
void program_asked(App& app, const reset_identity&) {
  with_passphrase(app, passphrase::reset_identity{});
}
template <class App>
void program_asked(App& app, const sign_out_unverified&) {
  with_passphrase(app, passphrase::sign_out_unverified{});
}
template <class App>
void program_asked(App& app, const export_room_keys&) {
  with_passphrase(app, passphrase::export_keys{});
}
template <class App>
void program_asked(App& app, const import_room_keys&) {
  with_passphrase(app, passphrase::import_keys{});
}
// Element's Secure Backup: made anew, or deleted.
template <class App>
void program_asked(App& app, const reset_backup&) {
  app.shared.with_chosen_account([&](config::account_t& account) {
    if (!app.shared.demo())
      ops::reset_backup(*app.net, App::id_of(account));
  });
}
template <class App>
void program_asked(App& app, const delete_backup&) {
  app.shared.with_chosen_account([&](config::account_t& account) {
    if (!app.shared.demo())
      ops::delete_backup(*app.net, App::id_of(account));
  });
}
// The sessions: one verified by emoji, some signed out, one renamed, listed.
template <class App>
void program_asked(App& app, const verify_session& one) {
  app.shared.with_chosen_account([&](config::account_t& account) {
    app.net->verify_start(App::id_of(account), config::address_of(account), one.device);
  });
}
// A press on the account's pages: what Matrix keeps of it changed, and
// the server asked -- a session renamed, some signed out.
template <class Net>
struct account_step {
  Net& net;
  const account_id& by;
  account_state& kept;
  // Signed out, with the password where the server asked for it.
  void sign_out(std::vector<std::string> devices, std::string password) {
    if (devices.empty())
      return;
    kept.signing = devices;
    kept.signing_out = true;
    kept.refused.clear();
    ops::sign_out_sessions(net, by, std::move(devices), std::move(password));
  }
  void operator()(const account_change::rename& one) {
    kept.renaming = one.open ? std::optional(one.device) : std::nullopt;
  }
  void operator()(const account_change::save_name& one) {
    kept.renaming.reset();
    ops::rename_session(net, by, one.device, one.name);
  }
  void operator()(const account_change::sign_out& one) { this->sign_out(one.devices, {}); }
  void operator()(const account_change::sign_out_others&) {
    this->sign_out(kept.sessions.value_or(std::vector<session_info>{}) | std::views::filter([&](const session_info& one) { return one.id != kept.current; }) | std::views::transform(&session_info::id) | std::ranges::to<std::vector>(),
                   {});
  }
  void operator()(const account_change::sign_out_again& one) {
    kept.needs_password = false;
    this->sign_out(kept.signing, one.password);
  }
};
template <class App>
void program_asked(App& app, const change_account& one) {
  using facts_t = std::optional<mux::ui::account_page_facts>;
  const facts_t& open = *app.showing.template look<facts_t>();
  if (!open || app.shared.demo())
    return;
  const account_id by = open->by;
  change_account_state(app, by, [&](account_state& kept) {
    spl::visit(account_step<std::remove_reference_t<decltype(*app.net)>>{*app.net, by, kept}, one.change);
  });
}
// A room changed as Matrix changes one: the room being managed, by its account.
template <class App>
void program_asked(App& app, const change_room& one) {
  const auto chosen = app.shared.managed();
  if (!chosen || app.shared.demo())
    return;
  app.net->on_account_of(*chosen, [room = chosen->id, change = one.change](auto& account)
                                      -> decltype(void(account.change_room(room, change))) { account.change_room(room, change); });
}
// A slash command: done where it was typed -- a message sent as it makes
// it, a room joined or left, something done to the room or to someone in
// it -- by the account of that chat.
template <class App>
void program_asked(App& app, const slash& one) {
  if (app.shared.demo())
    return;
  const mux::conversation_id to = one.to;
  spl::visit(spl::overloaded{
                 [&](const proto::matrix::slash::said& said) {
                   // An answer where the field answers a message, as any message sent
                   // from it is -- and the field let go of it after.
                   const auto reply_to = spl::visit(spl::overloaded{[](const mux::ui::compose::reply& r) { return std::optional<std::string>(r.id); },
                                                                    [](const auto&) { return std::optional<std::string>(); }},
                                                    app.showing.template look<mux::ui::chat_compose>()->now);
                   if (reply_to)
                     mux::ui::show(app.showing, mux::ui::chat_compose{});
                   app.net->on_account_of(to, [room = to.id, said, reply_to](auto& account)
                                                  -> decltype(void(account.send_as(room, said.body, said.html, said.emote, reply_to))) {
                     account.send_as(room, said.body, said.html, said.emote, reply_to);
                   });
                 },
                 [&](const proto::matrix::slash::name_mine& mine) {
                   app.net->on_account_of(to, [name = mine.name](auto& account)
                                                  -> decltype(void(account.set_display_name(name))) { account.set_display_name(name); });
                 },
                 [&](const proto::matrix::slash::join& join) { app.net->join(to.account, join.room, {}); },
                 [&](proto::matrix::slash::leave) { app.net->leave(to); },
                 [&](const proto::matrix::slash::act& act) { app.net->manage(to, act.action); },
                 [&](const proto::matrix::slash::power& power) {
                   const proto::matrix::room_change_t change = proto::matrix::room_change::set_power{power.user, power.level};
                   app.net->on_account_of(to, [room = to.id, change](auto& account)
                                                  -> decltype(void(account.change_room(room, change))) { account.change_room(room, change); });
                 },
                 [&](const proto::matrix::slash::room_name_mine& mine) {
                   // The user's own member event in the room, its name its own there.
                   const proto::matrix::room_change_t change = proto::matrix::room_change::set_state_event{
                       "m.room.member", to.account.address,
                       knot::to_json_string(app_detail::member_named{.displayname = mine.name})};
                   app.net->on_account_of(to, [room = to.id, change](auto& account)
                                                  -> decltype(void(account.change_room(room, change))) { account.change_room(room, change); });
                 }},
             one.what);
}
// A press on the settings' pages: what Matrix keeps of them changed, and
// the write it asks, asked.
template <class App>
void program_asked(App& app, const manage_room& one) {
  const auto& open = *app.showing.template look<std::optional<mux::ui::room_settings_facts>>();
  if (!open)
    return;
  auto now = *open;
  std::optional<app_detail::manage_ask_t> asked;
  spl::visit(spl::overloaded{[&](manage_state& kept) { asked = spl::visit(app_detail::manage_step{now, kept}, one.change); }, [](auto&) {}},
             now.manage);
  mux::ui::show(app.showing, std::optional(std::move(now)));
  if (asked)
    spl::visit(spl::overloaded{[&](room_change_t& change) { program_asked(app, change_room{std::move(change)}); }}, *asked);
}
template <class App>
void program_asked(App& app, const verify_them& one) {
  app.net->verify_start(one.by, one.user, std::nullopt);
}
template <class App>
void program_asked(App& app, const refresh_sessions&) {
  app.shared.with_chosen_account([&](config::account_t& account) { ops::list_sessions(*app.net, App::id_of(account)); });
}

// A step of interactive auth done in the browser: the request sent again,
// or let go; the box closed either way.
template <class App>
void program_asked(App& app, const continue_uia&) {
  mux::ui::show<tool::uia_step>(app.showing, std::nullopt);
  app.shared.with_chosen_account([&](config::account_t& account) { ops::continue_uia(*app.net, App::id_of(account)); });
}
template <class App>
void program_asked(App& app, const cancel_uia&) {
  mux::ui::show<tool::uia_step>(app.showing, std::nullopt);
  app.shared.with_chosen_account([&](config::account_t& account) { ops::cancel_uia(*app.net, App::id_of(account)); });
}
// Its own dialogs closed: what each shows let go.
template <class App, class Facts>
void program_asked(App& app, const close_tool<Facts>&) {
  mux::ui::show<Facts>(app.showing, std::nullopt);
}
}  // namespace mux::proto::matrix::request

// A passphrase or a password given, for what Matrix asked it for: for the
// account whose keys were asked of (keys_of).
export namespace mux::proto::matrix::passphrase {

template <class App, class Given>
void passphrase_given(App& app, const export_keys&, const Given& one) {
  if (auto refused = config::new_passphrase_refused(one.fresh, one.again))
    return app.shared.passphrase_refused(*refused);
  if (!app.keys_of)
    return mux::ui::show<mux::ui::passphrase_facts>(app.showing, std::nullopt);
  const char* home = std::getenv("HOME");
  const auto folder = home && *home ? std::filesystem::path(home) / "Downloads" : std::filesystem::current_path();
  // Joined, not formatted: clang 23 crashed on format strings first made in
  // these modules (see app/network.cc).
  const std::string file = "mux-room-keys-" + std::string(config::file_name_of(app.keys_of->address)) + ".txt";
  ops::export_room_keys(*app.net, *app.keys_of, (folder / file).string(), one.fresh);
  mux::ui::show<mux::ui::passphrase_facts>(app.showing, std::nullopt);
}
template <class App, class Given>
void passphrase_given(App& app, const import_keys&, const Given& one) {
  if (one.file.empty())
    return app.shared.passphrase_refused("Type the key file's path.");
  if (!app.keys_of)
    return mux::ui::show<mux::ui::passphrase_facts>(app.showing, std::nullopt);
  ops::import_room_keys(*app.net, *app.keys_of, one.file, one.current);
  mux::ui::show<mux::ui::passphrase_facts>(app.showing, std::nullopt);
}
template <class App, class Given>
void passphrase_given(App& app, const cross_signing&, const Given& one) {
  if (app.keys_of)
    ops::setup_cross_signing(*app.net, *app.keys_of, one.current);
  mux::ui::show<mux::ui::passphrase_facts>(app.showing, std::nullopt);
}
template <class App, class Given>
void passphrase_given(App& app, const sign_out_unverified&, const Given& one) {
  if (app.keys_of)
    ops::sign_out_unverified(*app.net, *app.keys_of, one.current);
  mux::ui::show<mux::ui::passphrase_facts>(app.showing, std::nullopt);
}
template <class App, class Given>
void passphrase_given(App& app, const reset_identity&, const Given& one) {
  if (app.keys_of)
    ops::setup_cross_signing(*app.net, *app.keys_of, one.current, true);
  mux::ui::show<mux::ui::passphrase_facts>(app.showing, std::nullopt);
}
template <class App, class Given>
void passphrase_given(App& app, const recovery&, const Given& one) {
  if (app.keys_of)
    ops::restore_cross_signing(*app.net, *app.keys_of, one.current);
  mux::ui::show<mux::ui::passphrase_facts>(app.showing, std::nullopt);
}

}  // namespace mux::proto::matrix::passphrase
