// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix.requests -- What Matrix's own UI asks of the program: its
// account's sessions, cross-signing, the key backup and room keys. Listed by
// requests_of(state), done by program_asked (mux.app.proto.matrix).
export module mux.proto.matrix.requests;

import std;
import splice;
import mux.proto.tags;
import mux.core.ids;
import mux.proto;

export namespace mux::proto::matrix {

// What Matrix changes of a room beyond what every protocol does: who may
// join, who reads its history, who has a say and what a thing done asks,
// encryption, its version.
namespace room_change {
struct set_join_rule {
  join_rule_t rule;
};
struct set_history {
  history_rule_t rule;
};
struct set_power {
  std::string user;
  std::int64_t level = 0;
};
struct encrypt {};  // for good: it cannot be turned off
struct set_need {  // the level a thing done asks
  power_need_t need;
  std::int64_t level = 0;
};
// Upgraded to a room version: a new room made, this one tombstoned.
struct upgrade {
  std::string version;
};
// The level any kind of event asks, by its type -- one of the list's or not.
struct set_event_need {
  std::string event;
  std::int64_t level = 0;
};
// A space's room or space: listed in it (m.space.child), or no longer.
struct add_child {
  std::string room;
};
struct remove_child {
  std::string room;
};
}  // namespace room_change
using room_change_t = spl::variant<room_change::set_join_rule, room_change::set_history, room_change::set_power,
                                      room_change::encrypt, room_change::set_need, room_change::upgrade,
                                      room_change::set_event_need, room_change::add_child, room_change::remove_child>;

namespace request {
// A room changed, as Matrix changes one: the room being managed.
struct change_room {
  room_change_t change;
};
// Cross-signing for the chosen account: set up, or brought back with the
// recovery key; its identity reset; its unverified sessions signed out.
struct setup_cross_signing {};
struct restore_cross_signing {};
struct reset_identity {};
struct sign_out_unverified {};
// Element's Secure Backup: made anew, or deleted.
struct reset_backup {};
struct delete_backup {};
// Its room keys, written to a key file or read from one.
struct export_room_keys {};
struct import_room_keys {};
// Its sessions: one verified by emoji, some signed out (with the password
// typed, where one is), one renamed, the list asked again.
struct verify_session {
  std::string device;
};
// A step of interactive auth done in the browser: what it was for, done
// again; or let go.
struct continue_uia {};
struct cancel_uia {};
struct sign_out_sessions {
  std::vector<std::string> devices;
  std::string password;
};
struct rename_session {
  std::string device;
  std::string name;
};
struct refresh_sessions {};
// The developer tools: the room's state explored, an event of any type sent.
struct explore_state {};
struct open_send_custom {};
struct send_custom {
  std::string type;
  std::optional<std::string> state_key;
  std::string json;
};
// The other person of a direct chat verified by emoji, from the banner
// that says they are not.
struct verify_them {
  account_id by;
  std::string user;
};
}  // namespace request

namespace passphrase {
// Its room keys: written to a file under a new passphrase, or read from one
// under its own.
struct export_keys {
  friend bool operator==(export_keys, export_keys) = default;
};
struct import_keys {
  friend bool operator==(import_keys, import_keys) = default;
};
// Cross-signing set up: the account's password, which the server asks for.
struct cross_signing {
  friend bool operator==(cross_signing, cross_signing) = default;
};
// Its identity reset (Element's "Reset identity"): new cross-signing keys
// in place of the old, the password asked as for a setting up.
struct reset_identity {
  friend bool operator==(reset_identity, reset_identity) = default;
};
// One's own sessions not verified, signed out: the password, as the server
// asks for it.
struct sign_out_unverified {
  friend bool operator==(sign_out_unverified, sign_out_unverified) = default;
};
// Cross-signing taken back with the recovery key.
struct recovery {
  friend bool operator==(recovery, recovery) = default;
};
constexpr passphrase_words passphrase_text(export_keys) {
  return {"Export room keys",
          "This account's room keys are written to your Downloads folder, sealed under a new passphrase: with "
          "them and it, any client reads every encrypted message they open. Keep both safe.",
          "Export", false, true};
}
constexpr passphrase_words passphrase_text(import_keys) {
  return {"Import room keys", "Room keys from a key file Element or mux wrote, under its passphrase.", "Import", true, false, true};
}
constexpr passphrase_words passphrase_text(cross_signing) {
  return {"Set up cross-signing",
          "Three keys are made for your account and kept on this device: with them it signs your devices and "
          "the people you verify. Your server asks for your account's password to take them.",
          "Set up", true, false};
}
constexpr passphrase_words passphrase_text(reset_identity) {
  return {"Reset your identity",
          "New cross-signing keys replace your account's: everyone who verified you, and every session of yours, "
          "must verify again, and messages only your old keys could read stay unreadable here. Only if you have "
          "lost every verified session and the recovery key. Your server asks for your account's password.",
          "Reset", true, false};
}
constexpr passphrase_words passphrase_text(sign_out_unverified) {
  return {"Sign out unverified sessions",
          "Every session of yours that is not verified -- not cross-signed, nor verified by emoji here -- is "
          "signed out. Your server asks for your account's password.",
          "Sign out", true, false};
}
constexpr passphrase_words passphrase_text(recovery) {
  return {"Restore with the recovery key",
          "The recovery key written down when cross-signing was set up: with it, this device takes your "
          "cross-signing keys back from your server.",
          "Restore", true, false};
}
}  // namespace passphrase
constexpr passphrase_list<passphrase::export_keys, passphrase::import_keys, passphrase::cross_signing,
                          passphrase::reset_identity, passphrase::sign_out_unverified, passphrase::recovery>
passphrases(const state&) {
  return {};
}

constexpr request_list<request::setup_cross_signing, request::restore_cross_signing, request::reset_identity,
                       request::sign_out_unverified, request::reset_backup, request::delete_backup, request::export_room_keys,
                       request::import_room_keys, request::verify_session, request::sign_out_sessions, request::rename_session,
                       request::refresh_sessions, request::verify_them, request::explore_state,
                       request::open_send_custom, request::send_custom, request::change_room, request::continue_uia,
                       request::cancel_uia>
requests_of(const state&) {
  return {};
}

}  // namespace mux::proto::matrix
