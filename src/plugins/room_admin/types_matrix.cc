// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.room_admin.types:matrix -- A Matrix room's settings' tabs, as
// Cinny's: General, Members, Permissions -- their types, what the settings
// keep while they are open (manage_state), what a press on them asks
// (manage_change, request::manage_room), and what is filled as they open.
// Built where Matrix is.
export module mux.plugin.room_admin.types:matrix;

import std;
import splice;
import mux.core;
import mux.composition;
import mux.proto;
import mux.proto.matrix;
import mux.proto.matrix.requests;
import mux.proto.matrix.changes;

export namespace mux::proto::matrix {

// ---- a room's settings (Manage), as Cinny's -------------------------------------
// Its tabs beside the client's: General, Members, Permissions.
namespace manage {
struct room {};
struct members {};
struct roles {};
}  // namespace manage
// The client's General shown under Matrix's own (the room's name, topic,
// address): one General tab, not two.
namespace manage {
constexpr bool hosts_general(room) { return true; }
constexpr std::string_view tab_title(room) { return "General"; }
constexpr std::string_view tab_title(members) { return "Members"; }
constexpr std::string_view tab_title(roles) { return "Permissions"; }
// Its tabs, a Matrix room's alone.
template <class Facts>
[[nodiscard]] bool matrix_room(const Facts& facts) {
  return spl::visit(spl::overloaded{[](const ::mux::protocol::matrix&) { return true; }, [](const auto&) { return false; }}, facts.speaks);
}
template <class Facts> bool tab_visible(room, const Facts& facts) { return matrix_room(facts); }
template <class Facts> bool tab_visible(members, const Facts& facts) { return matrix_room(facts); }
template <class Facts> bool tab_visible(roles, const Facts& facts) { return matrix_room(facts); }
}  // namespace manage
// What a page has open: an editor, or a list unfolded.
enum class editor { profile, powers, power };
// What Matrix keeps while the settings are open: what the server said of the
// room beyond what the chat has (its addresses, its directory listing, its
// versions, power tags, members, account data and state), what is being
// written and what failed, what each page has open, the drafts that are the
// program's to keep (the permissions and tags staged until Apply), and for
// each draft a page keeps that the program resets, a round: the draft made
// again where it moves.
struct manage_state {
  room_management data;
  bool loading = true, pending = false;
  std::string error;
  bool confirming_encryption = false, confirming_upgrade = false;
  std::optional<power_needs> permissions;
  // An avatar uploaded (or "" to remove it), to be saved with the profile.
  std::optional<std::string> avatar;
  bool profile_editor = false, powers_editor = false, power_form = false;
  std::optional<std::int64_t> editing_power;
  std::optional<std::map<std::string, power_tag>> tag_draft;
  std::set<std::int64_t> deleted_powers;
  bool local_addresses_expanded = false;
  std::set<std::string> selected_aliases;
  std::uint32_t profile_round = 0, address_round = 0, custom_round = 0;
};
// A room avatar being sent: shown pending, the last error gone.
inline void avatar_sending(manage_state& kept) {
  kept.pending = true;
  kept.error.clear();
}
// A permission a page sets: a thing done (power_need), or an event's type --
// a state event's or a message event's, whose defaults differ though Matrix
// keeps their overrides together in power_levels.events.
struct event_permission {
  std::string type;
  bool state = true;
  friend bool operator==(const event_permission&, const event_permission&) = default;
};
using permission_t = spl::variant<power_need_t, event_permission>;
// What a press on the settings' pages asks: the program changes what it
// keeps (manage_state) and, where it is a write, asks the server.
namespace manage_change {
enum class address_kind { main, publish, unpublish, remove };
enum class selection_kind { publish, unpublish, remove };
struct edit { editor target; bool open = true; };
struct encryption { bool cancel = false; };
struct directory {};
struct address { std::string alias; address_kind what = address_kind::main; };
struct expand_addresses {};
struct select_address { std::string alias; };
struct selected { selection_kind what = selection_kind::publish; };
struct access { join_rule_t rule; };
struct history { history_rule_t rule; };
struct add_child { std::string room; };
struct remove_child { std::string room; };
struct save_profile { std::string name, topic; };
struct remove_avatar {};
struct create_address { std::string local; };
struct upgrade { std::string version, creators; };
struct cancel_upgrade {};
struct member_power { std::string user; std::int64_t level = 0; };
struct permission { permission_t target; std::int64_t level = 0; };
struct apply_permissions { bool reset = false; };
struct add_custom_permission { std::string event, level; };
struct remove_custom_permission { std::string event; };
struct save_power { std::string power, name, color, icon; };
struct delete_power { std::string power; };
struct edit_power { std::int64_t power = 0; };
struct delete_tag { std::int64_t power = 0; };
struct apply_tags { bool reset = false; };
}  // namespace manage_change
using manage_change_t = spl::variant<
    manage_change::edit, manage_change::encryption, manage_change::directory, manage_change::address, manage_change::expand_addresses,
    manage_change::select_address, manage_change::selected, manage_change::access, manage_change::history, manage_change::add_child,
    manage_change::remove_child, manage_change::save_profile, manage_change::remove_avatar, manage_change::create_address,
    manage_change::upgrade, manage_change::cancel_upgrade, manage_change::member_power, manage_change::permission,
    manage_change::apply_permissions, manage_change::add_custom_permission, manage_change::remove_custom_permission,
    manage_change::save_power, manage_change::delete_power, manage_change::edit_power, manage_change::delete_tag,
    manage_change::apply_tags>;
namespace request {
struct manage_room {
  manage_change_t change;
};
}  // namespace request
namespace manage {
// Back, or Esc: an editor open on the tab closed first.
inline bool manage_close_editor(roles, manage_state& kept) {
  if (!kept.powers_editor)
    return false;
  kept.power_form = false;
  kept.powers_editor = false;
  return true;
}
}  // namespace manage

// The Manage dialog's facts, as Matrix fills them: one's own level, and
// those the power levels name with one of their own (Element's privileged
// users), the highest first.
template <class Facts>
void manage_facts(const conversation& chat, Facts& facts) {
  const room_rules& rules = rules_of(chat);
  facts.mine = level_of(rules, chat.id.account.address);
  facts.privileged = rules.powers | std::views::filter([&](const auto& each) { return each.second != rules.needs.users_default; }) | std::views::transform([&](const auto& each) {
                       const auto member = std::ranges::find(chat.members, each.first, &mux::member::id);
                       return typename decltype(facts.privileged)::value_type{
                           each.first, member != chat.members.end() && !member->name.empty() ? member->name : each.first,
                           each.second};
                     }) | std::ranges::to<decltype(facts.privileged)>();
  std::ranges::stable_sort(facts.privileged, std::greater{}, &decltype(facts.privileged)::value_type::level);
}

}  // namespace mux::proto::matrix

export namespace mux::plugins::room_admin {

// Matrix's realization: its tabs, what it keeps, and the settings as they
// open for one of its rooms -- its General up first.
struct matrix_admin {
  using tabs = composition::page_list<proto::matrix::manage::room, proto::matrix::manage::members, proto::matrix::manage::roles>;
  using kept = composition::page_list<proto::matrix::manage_state>;
  template <class Facts>
  static void opened(Facts& facts, const conversation& chat) {
    if (!proto::matrix::manage::matrix_room(facts))
      return;
    proto::matrix::manage_facts(chat, facts);
    facts.tab = proto::matrix::manage::room{};
    facts.manage = proto::matrix::manage_state{};
  }
};

}  // namespace mux::plugins::room_admin
