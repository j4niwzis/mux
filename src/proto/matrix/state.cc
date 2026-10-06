// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix.state -- What a Matrix account is now, as Matrix's
// extension points decide by it.
export module mux.proto.matrix.state;

import std;
import splice;
import mux.proto.identity;

export namespace mux::proto::matrix {

struct state {
  bool online = false;  // syncing
  // This session, once its encryption has started: its device ID and its
  // ed25519 key, to be compared with what another session shows.
  std::string device_id;
  std::string ed25519;
  friend bool operator==(const state&, const state&) = default;
};
// The protocol is known by its state type itself (id<state>, in protocol_t);
// every overload of it takes its state.

// Who may join a room, as its join rule says.
namespace join_rule {
struct open {};      // anyone: "public"
struct invite {};    // those invited
struct knock {};     // those who ask, once let in
// Members of the spaces it names (Element's "Space members"): they join as
// they would a public room; anyone else, as one invited.
struct restricted {
  std::vector<std::string> spaces;
  friend bool operator==(const restricted&, const restricted&) = default;
};
// The same, and anyone else may ask to join (knock_restricted).
struct knock_restricted {
  std::vector<std::string> spaces;
  friend bool operator==(const knock_restricted&, const knock_restricted&) = default;
};
struct other {};     // private, or a rule unknown -- not offered here
}  // namespace join_rule
using join_rule_t = spl::variant<join_rule::open, join_rule::invite, join_rule::knock, join_rule::restricted,
                                 join_rule::knock_restricted, join_rule::other>;
// Who may read a room's history.
namespace history_rule {
struct shared {};          // members, all of it
struct invited {};         // members, from when they were invited
struct joined {};          // members, from when they joined
struct world_readable {};  // anyone
}  // namespace history_rule
using history_rule_t =
    spl::variant<history_rule::shared, history_rule::invited, history_rule::joined, history_rule::world_readable>;

// What a room asks of those who do something in it: the level each needs,
// as its power levels say (m.room.power_levels), Matrix's defaults where
// they say nothing. What is asked is a tag, one for each thing done; one
// that sends a kind of state event carries that kind's name, as Matrix
// writes it -- the key its level is kept by.
namespace power_need {
struct send_messages {};  // events_default
struct change_settings {};  // state_default: any state event not listed
struct default_role {};  // users_default
struct invite {};
struct kick {};
struct ban {};
struct redact {};  // remove what others sent
struct notify_everyone {};  // notifications.room: @room
struct rename {
  static constexpr std::string_view event = "m.room.name";
};
struct retopic {
  static constexpr std::string_view event = "m.room.topic";
};
struct change_avatar {
  static constexpr std::string_view event = "m.room.avatar";
};
struct change_address {
  static constexpr std::string_view event = "m.room.canonical_alias";
};
struct change_history {
  static constexpr std::string_view event = "m.room.history_visibility";
};
struct change_access {
  static constexpr std::string_view event = "m.room.join_rules";
};
struct change_permissions {
  static constexpr std::string_view event = "m.room.power_levels";
};
struct encrypt {
  static constexpr std::string_view event = "m.room.encryption";
};
struct upgrade {
  static constexpr std::string_view event = "m.room.tombstone";
};
struct change_acl {
  static constexpr std::string_view event = "m.room.server_acl";
};
struct pin {
  static constexpr std::string_view event = "m.room.pinned_events";
};
}  // namespace power_need
using power_need_t =
    spl::variant<power_need::default_role, power_need::send_messages, power_need::invite, power_need::change_settings,
                 power_need::kick, power_need::ban, power_need::redact, power_need::notify_everyone,
                 power_need::rename, power_need::retopic, power_need::change_avatar, power_need::change_address,
                 power_need::change_history, power_need::change_access, power_need::change_permissions,
                 power_need::encrypt, power_need::upgrade, power_need::change_acl, power_need::pin>;
template <class Need>
concept sends_state = requires { Need::event; };

struct power_needs {
  std::int64_t users_default = 0;
  std::int64_t events_default = 0;
  std::int64_t state_default = 50;
  std::int64_t invite = 0;
  std::int64_t kick = 50;
  std::int64_t ban = 50;
  std::int64_t redact = 50;
  std::int64_t notify_room = 50;
  std::map<std::string, std::int64_t, std::less<>> events;  // by the kind of event
  friend bool operator==(const power_needs&, const power_needs&) = default;

  [[nodiscard]] std::int64_t of(power_need::default_role) const { return users_default; }
  [[nodiscard]] std::int64_t of(power_need::send_messages) const { return events_default; }
  [[nodiscard]] std::int64_t of(power_need::change_settings) const { return state_default; }
  [[nodiscard]] std::int64_t of(power_need::invite) const { return invite; }
  [[nodiscard]] std::int64_t of(power_need::kick) const { return kick; }
  [[nodiscard]] std::int64_t of(power_need::ban) const { return ban; }
  [[nodiscard]] std::int64_t of(power_need::redact) const { return redact; }
  [[nodiscard]] std::int64_t of(power_need::notify_everyone) const { return notify_room; }
  template <sends_state Need>
  [[nodiscard]] std::int64_t of(Need) const {
    const auto found = events.find(Need::event);
    return found == events.end() ? state_default : found->second;
  }
  [[nodiscard]] std::int64_t of(const power_need_t& need) const {
    return spl::visit([this](auto one) { return this->of(one); }, need);
  }
};

// A room's creator, from room version 12 on: above every level, and not
// listed in the power levels (MSC4289).
inline constexpr std::int64_t kCreatorPower = std::numeric_limits<std::int64_t>::max();

// What a room of Matrix's is, beyond what every chat is: who may join it
// and read its history, each one's say in it (its power levels, those not
// listed having the default) and what each thing done asks, its version as
// it was made -- what an upgrade goes from.
struct room_rules {
  join_rule_t join_rule = join_rule::invite{};
  history_rule_t history = history_rule::shared{};
  std::map<std::string, std::int64_t> powers;
  std::int64_t power_default = 0;
  power_needs needs;
  std::string version;
  // Upgraded away: the room it continues in (m.room.tombstone), and what its
  // tombstone said; and the room this one continues, where it does.
  std::optional<std::string> replaced_by;
  std::string replaced_why;
  std::optional<std::string> predecessor;
};
constexpr room_part_list<room_rules> room_parts(const state&) { return {}; }

}  // namespace mux::proto::matrix
