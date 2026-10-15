// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix -- What Matrix says when the client asks (mux.proto):
// its overloads, on its tag, found by ADL.
export module mux.proto.matrix;

import std;
import splice;
import mux.core;
import mux.proto;
import mux.proto.matrix.links;
import mux.proto.matrix.requests;

export namespace mux::proto::matrix {
constexpr bool offers(const state&, feature::server_previews) { return true; }


inline std::string directory_server(const state&, std::string_view address) {
  return std::string(address.substr(address.find(':') + 1));
}
constexpr bool offers(const state&, feature::people_directory) { return true; }
constexpr bool offers(const state&, feature::room_directory) { return true; }
constexpr bool offers(const state&, feature::room_creation) { return true; }
constexpr bool offers(const state&, feature::sticker_packs) { return true; }
constexpr bool offers(const state&, feature::history_context) { return true; }
constexpr bool offers(const state&, feature::identity_verification) { return true; }

// matrix.to, for a person, a room and a message in it.
inline std::optional<std::string> share_link(const state&, std::string_view address) {
  return "https://matrix.to/#/" + std::string(address);
}
inline std::optional<std::string> person_link(const state&, std::string_view user) { return "https://matrix.to/#/" + std::string(user); }
inline std::optional<std::string> room_link(const state&, const conversation& chat) { return room_link_to(chat); }
// Only an event the server named ($...): one still being sent has no link.
inline std::optional<std::string> message_link(const state&, const conversation& chat, std::string_view event) {
  if (!event.starts_with('$'))
    return std::nullopt;
  return message_link_to(chat, event);
}
// Its own dialogs, each by what it shows, as the program shows it: the
// developer tools, as Element's -- some JSON to read and copy, a room's
// state, an event of any type sent -- and a step of interactive auth done
// in the browser.
constexpr dialog_list<tool::devtools_text, tool::devtools_state, tool::send_custom_form, tool::uia_step> dialogs(const state&) { return {}; }
// Its own account pages: its encryption, and its sessions under
// cross-signing and the key backup.
namespace settings {
struct profile {};
struct encryption {};
struct sessions {};
}  // namespace settings
constexpr account_page_list<settings::profile, settings::encryption, settings::sessions> account_pages(const state&) { return {}; }
// What it keeps of an account while they are open: its sessions as the
// server listed them (none yet: not heard), this one's among them; whether
// this session has the cross-signing keys and the room keys are backed up;
// the session being renamed; the sessions being signed out, and what the
// server refused -- whether it wants the password for them.
struct account_security {
  bool cross_signing = false, backup = false;
};
struct account_state {
  std::optional<std::vector<session_info>> sessions;
  std::string current;
  std::optional<account_security> security;
  std::optional<std::string> renaming;
  std::vector<std::string> signing;
  bool signing_out = false;
  std::string refused;
  bool needs_password = false;
};
constexpr account_part_list<account_state> account_parts(const state&) { return {}; }

// A direct chat is a room: whom it is with, the member who is not the account.
inline std::string direct_contact(const state&, const conversation& one) {
  const auto other = std::ranges::find_if(one.members, [&](const member& each) { return each.id != one.id.account.address; });
  return other == one.members.end() ? std::string() : other->id;
}
// Its media: mxc://, the content repository's.
constexpr bool owns_media(const state&, std::string_view uri) { return uri.starts_with("mxc://"); }
// A room's !id is no name for anyone; an alias and a user ID are.
constexpr bool id_reads_as_name(const state&, std::string_view id) { return !id.starts_with('!'); }
// An ID's local part: after its sigil (@, #, !), before its server.
inline std::string local_part(const state&, std::string_view address) {
  if (address.starts_with('@') || address.starts_with('#') || address.starts_with('!'))
    address.remove_prefix(1);
  return std::string(address.substr(0, address.find(':')));
}
// A room's rules and levels, as Matrix keeps them in a chat's part: none
// kept, the defaults.
[[nodiscard]] inline const room_rules& rules_of(const room_part_t& part) {
  static const room_rules none{};
  return spl::visit(spl::overloaded{[](const room_rules& kept) -> const room_rules& { return kept; },
                                          [](const auto&) -> const room_rules& { return none; }},
                       part);
}
[[nodiscard]] inline const room_rules& rules_of(const conversation& in) { return rules_of(in.theirs); }
// And to change: the part made Matrix's where it was none.
[[nodiscard]] inline room_rules& rules_in(room_part_t& part) {
  return spl::visit(spl::overloaded{[](room_rules& kept) -> room_rules& { return kept; },
                                          [&](auto&) -> room_rules& { return part.template emplace<room_rules>(); }},
                       part);
}
// A room upgraded: the one it continues in, by its tombstone; and the one it
// continues, by its creation.
inline std::optional<std::string> successor_of(const state&, const conversation& in) { return rules_of(in).replaced_by; }
inline std::optional<std::string> predecessor_of(const state&, const conversation& in) { return rules_of(in).predecessor; }
// What the account may do in a room, as its power levels allow: write
// in it, let in those who knock; and to someone in it, only above them.
[[nodiscard]] inline std::int64_t level_of(const room_rules& rules, std::string_view who) {
  const auto level = rules.powers.find(std::string(who));
  return level == rules.powers.end() ? rules.power_default : level->second;
}
[[nodiscard]] inline std::int64_t level_of(const conversation& in, std::string_view who) { return level_of(rules_of(in), who); }
// Whether what a room's Manage facts say of one lets one do a thing.
[[nodiscard]] inline bool may(const auto& facts, const power_need_t& need) {
  return facts.mine >= rules_of(facts.theirs).needs.of(need);
}
// The Manage dialog's facts, as Matrix fills them: one's own level, and
// those the power levels name with one of their own (Element's privileged
// users), the highest first.
template <class Facts>
void manage_facts(const state&, const conversation& chat, Facts& facts) {
  const room_rules& rules = rules_of(chat);
  facts.mine = level_of(rules, chat.id.account.address);
  facts.privileged = std::ranges::to<decltype(facts.privileged)>(std::views::transform(std::views::filter(rules.powers, [&](const auto& each) { return each.second != rules.needs.users_default; }), [&](const auto& each) {
                       const auto member = std::ranges::find(chat.members, each.first, &mux::member::id);
                       return typename decltype(facts.privileged)::value_type{
                           each.first, member != chat.members.end() && !member->name.empty() ? member->name : each.first,
                           each.second};
                     }));
  std::ranges::stable_sort(facts.privileged, std::greater{}, &decltype(facts.privileged)::value_type::level);
}
inline part::chat_rights chat_rights(const state&, const conversation& in) {
  const std::int64_t mine = level_of(in, in.id.account.address);
  const auto asked = rules_of(in).needs.events.find("m.room.message");
  const std::int64_t needs = asked != rules_of(in).needs.events.end() ? asked->second : rules_of(in).needs.events_default;
  // Its packs (MSC2545's im.ponies.room_emotes), as a state event's level asks.
  const auto packs = rules_of(in).needs.events.find("im.ponies.room_emotes");
  const std::int64_t packs_need = packs != rules_of(in).needs.events.end() ? packs->second : rules_of(in).needs.state_default;
  return {mine >= needs, mine >= rules_of(in).needs.invite, mine >= packs_need};
}
inline part::person_rights person_rights(const state&, const conversation& in, std::string_view who) {
  const std::int64_t mine = level_of(in, in.id.account.address), theirs = level_of(in, who);
  const bool above = who != in.id.account.address && mine > theirs;
  return {above && mine >= rules_of(in).needs.of(power_need::kick{}), above && mine >= rules_of(in).needs.of(power_need::ban{})};
}
// What is known of someone's identity: on their card, and beside them in an
// encrypted room's members.
inline std::vector<part::badge> person_badges(const state&, const conversation* in, const model& known, const account_id& by,
                                              std::string_view who) {
  if (in && !in->encrypted)
    return {};
  const auto trust = known.trust_of(by, std::string(who));
  if (!trust)
    return {};
  return {spl::visit(spl::overloaded{[](trust::verified) { return part::badge{"Verified", part::tone::accent{}}; },
                                           [](trust::unverified) { return part::badge{"Not verified"}; },
                                           [](trust::changed) { return part::badge{"Identity reset", part::tone::danger{}}; }},
                        *trust)};
}
// A sender's role, as the room's power levels give it: 100 and 50.
inline std::string sender_role(const state&, const conversation& in, std::string_view who) {
  const std::int64_t power = level_of(in, who);
  return power >= 100 ? std::string("admin") : power >= 50 ? std::string("mod") : std::string();
}
// What is known of the other person's identity, in a direct encrypted chat.
[[nodiscard]] inline std::optional<trust_t> other_trust(const state& now, const conversation& one, const model& known) {
  const bool direct = spl::visit(
      spl::overloaded{[](const conversation_kind::direct&) { return true; }, [](const auto&) { return false; }}, one.kind);
  if (!one.encrypted || !direct)
    return std::nullopt;
  return known.trust_of(one.id.account, direct_contact(now, one));
}
// Encrypted, said after the chat's status, as Element's shield on its
// header: and in a direct chat, what is known of the other's identity.
inline std::vector<part::badge> header_badges(const state& now, const conversation& one, const model& known) {
  if (!one.encrypted || !one.typing.empty())
    return {};
  const auto trust = other_trust(now, one, known);
  const std::string after = !trust ? std::string()
                                   : spl::visit(spl::overloaded{[](trust::verified) { return std::string(" \u00b7 Verified"); },
                                                                      [](trust::unverified) { return std::string(" \u00b7 Not verified"); },
                                                                      [](trust::changed) { return std::string(" \u00b7 Identity reset"); }},
                                                   *trust);
  return {part::badge{"\U0001F512 Encrypted" + after}};
}
// Element's warning over the composer, in a direct encrypted chat: the
// other not verified -- messages are still encrypted to them -- or their
// identity reset, to be verified again or withdrawn on their card.
using banner = part::banner_of<request::verify_them>;
inline std::vector<banner> composer_banners(const state& now, const conversation& one, const model& known) {
  const auto trust = other_trust(now, one, known);
  if (!trust)
    return {};
  const std::string& name = one.name.empty() ? one.id.id : one.name;
  const request::verify_them them{one.id.account, direct_contact(now, one)};
  return spl::visit(
      spl::overloaded{
          [](trust::verified) { return std::vector<banner>{}; },
          [&](trust::unverified) {
            return std::vector<banner>{{std::format("Verify {} to confirm their identity.", name),
                                        part::tone::accent{}, "Verify", them}};
          },
          [&](trust::changed) {
            return std::vector<banner>{{std::format("{}'s identity changed. Verify them again.", name),
                                        part::tone::danger{}, "Verify again", them}};
          }},
      *trust);
}
constexpr bool can_pin(const state&, std::string_view event) { return event.starts_with('$'); }

// Annotation targets are events, including state and reaction events. Local
// transaction IDs cannot be related to until acknowledged by the server.
inline bool may_react(const state&, const conversation& chat, const message& event) {
  const auto& rules = rules_of(chat);
  const auto asked = rules.needs.events.find("m.reaction");
  const auto required = asked == rules.needs.events.end() ? rules.needs.events_default : asked->second;
  return event.id.starts_with('$') && !event.redacted && level_of(chat, chat.id.account.address) >= required;
}

// As the room's power levels allow it: one's own where one may send a
// redaction; another's where one may also redact.
inline bool may_delete(const state&, const conversation& chat, bool outgoing) {
  const auto mine = rules_of(chat).powers.find(chat.id.account.address);
  const std::int64_t level = mine != rules_of(chat).powers.end() ? mine->second : rules_of(chat).power_default;
  const auto redaction = rules_of(chat).needs.events.find("m.room.redaction");
  const std::int64_t send = redaction != rules_of(chat).needs.events.end() ? redaction->second : rules_of(chat).needs.events_default;
  return level >= send && (outgoing || level >= rules_of(chat).needs.redact);
}

// ---- a room's settings (Manage), as Cinny's -------------------------------------
// Its tabs beside the client's: General, Members, Permissions, Emojis &
// Stickers, Developer Tools.
namespace manage {
struct room {};
struct members {};
struct roles {};
struct packs {};
struct advanced {};
}  // namespace manage
constexpr manage_tab_list<manage::room, manage::members, manage::roles, manage::packs, manage::advanced> manage_tabs(const state&) { return {}; }
constexpr manage::room manage_initial_tab(const state&) { return {}; }
// The developer tools' state events: their list, one shown, one edited, a
// new one written.
enum class state_screen { overview, view, edit, compose };
// What a page has open: an editor, or a list unfolded.
enum class editor { profile, powers, power, state_events, account_data, account_form };
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
  state_screen state_page = state_screen::overview;
  std::string expanded_state_type, state_type, state_key, state_json, state_source;
  bool expanded_state = false, expanded_account_data = false, account_editor = false;
  // The account data chosen to edit, its draft made from it.
  std::string account_type, account_json;
  std::vector<emote_pack> packs;
  std::uint32_t profile_round = 0, address_round = 0, custom_round = 0, pack_round = 0, state_round = 0, account_round = 0;
};
constexpr manage_part_list<manage_state> manage_parts(const state&) { return {}; }
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
struct create_pack { std::string name; };
struct state_open { state_screen target = state_screen::overview; std::string type, key; };
struct state_expand { std::string type; };
struct save_state { std::string type, key, json; };
struct save_account_data { std::string type, json; };
struct choose_account_data { std::size_t index = 0; };
}  // namespace manage_change
using manage_change_t = spl::variant<
    manage_change::edit, manage_change::encryption, manage_change::directory, manage_change::address, manage_change::expand_addresses,
    manage_change::select_address, manage_change::selected, manage_change::access, manage_change::history, manage_change::add_child,
    manage_change::remove_child, manage_change::save_profile, manage_change::remove_avatar, manage_change::create_address,
    manage_change::upgrade, manage_change::cancel_upgrade, manage_change::member_power, manage_change::permission,
    manage_change::apply_permissions, manage_change::add_custom_permission, manage_change::remove_custom_permission,
    manage_change::save_power, manage_change::delete_power, manage_change::edit_power, manage_change::delete_tag,
    manage_change::apply_tags, manage_change::create_pack, manage_change::state_open, manage_change::state_expand,
    manage_change::save_state, manage_change::save_account_data, manage_change::choose_account_data>;
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
inline bool manage_close_editor(advanced, manage_state& kept) {
  if (kept.pending)
    return true;
  if (kept.state_page != state_screen::overview) {
    kept.state_page = state_screen::overview;
    return true;
  }
  return std::exchange(kept.account_editor, false);
}
// Whether the tab's page shows an editor, with a way back over it.
inline bool manage_editor_header(advanced, const manage_state& kept) { return kept.state_page != state_screen::overview; }
}  // namespace manage

}  // namespace mux::proto::matrix
