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

// What a press on the account's pages asks: the program changes what it
// keeps of the account (account_state) and, where it is a write, asks the
// server.
namespace account_change {
// A session's name, to be edited or let be; and saved.
struct rename { std::string device; bool open = true; };
struct save_name { std::string device, name; };
// Sessions signed out: these, or all but this one; and again with the
// password the server asked for.
struct sign_out { std::vector<std::string> devices; };
struct sign_out_others {};
struct sign_out_again { std::string password; };
}  // namespace account_change
using account_change_t = spl::variant<account_change::rename, account_change::save_name, account_change::sign_out,
                                      account_change::sign_out_others, account_change::sign_out_again>;
// A line typed as Element's slash commands read it: what it does, typed
// once where the line is read (command_of, mux.proto.matrix).
namespace slash {
// A message as the command makes it: its body and HTML as they are, a text
// or an emote (/me, /shrug, /plain, /html, /rainbow, /spoiler...).
struct said {
  std::string body;
  std::optional<std::string> html;
  bool emote = false;
};
struct join { std::string room; };
struct leave {};
// Named, described, someone let in or sent out (/roomname, /topic,
// /invite, /kick, /ban, /unban).
struct act { mux::room_action_t action; };
// Someone's power in the room (/op user level, /deop user).
struct power { std::string user; std::int64_t level = 0; };
// The user's own name in the room (/myroomnick).
struct room_name_mine { std::string name; };
}  // namespace slash
using slash_t = spl::variant<slash::said, slash::join, slash::leave, slash::act, slash::power, slash::room_name_mine>;
namespace request {
// A slash command, for the chat it was typed in.
struct slash {
  mux::conversation_id to;
  slash_t what;
};
// A pack created or edited from the room's settings.
struct save_room_pack { mux::emote_pack pack; };
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
struct refresh_sessions {};
struct change_account {
  account_change_t change;
};
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

namespace tool {
struct devtools_text {
  std::string title, text;
  friend bool operator==(const devtools_text&, const devtools_text&) = default;
};
struct devtools_entry {
  std::string type, key, json;
  friend bool operator==(const devtools_entry&, const devtools_entry&) = default;
};
struct devtools_state {
  std::vector<devtools_entry> entries;
  friend bool operator==(const devtools_state&, const devtools_state&) = default;
};
struct send_custom_form {
  friend bool operator==(send_custom_form, send_custom_form) = default;
};
struct uia_step {
  std::string what, url;
  friend bool operator==(const uia_step&, const uia_step&) = default;
};
}  // namespace tool
namespace request {
// One of its own dialogs closed: what it shows let go.
template <class Facts>
struct close_tool {};
}  // namespace request
namespace tool {
// What Esc on each asks for.
inline request::close_tool<devtools_text> closing_of(std::type_identity<devtools_text>) { return {}; }
inline request::close_tool<devtools_state> closing_of(std::type_identity<devtools_state>) { return {}; }
inline request::close_tool<send_custom_form> closing_of(std::type_identity<send_custom_form>) { return {}; }
inline request::close_tool<uia_step> closing_of(std::type_identity<uia_step>) { return {}; }
}  // namespace tool


}  // namespace mux::proto::matrix
