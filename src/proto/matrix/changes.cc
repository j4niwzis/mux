// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix.changes -- What a Matrix account says that no other
// protocol does: an emoji verification as it goes, cross-signing and the key
// backup, its sessions, the developer tools, sticker packs. Its change list
// (changes_type(const state&), by ADL) is made part of mux.core's change_t; each is
// the window's, and said on the command line by describe().
export module mux.proto.matrix.changes;

import std;
import splice;
import mux.core.ids;
import mux.proto.matrix.state;

export namespace mux::proto::matrix {

// Room management reads and writes report back to the page that requested
// them. A failed write leaves its draft available for another attempt.
struct management_loaded {
  conversation_id in;
  room_management data;
  std::string error;
};
struct room_avatar_uploaded { conversation_id in; std::string uri, error; };
struct management_saved {
  conversation_id in;
  room_change_t change;
  std::string error;
  std::optional<managed_state_event> saved_state;
};
struct verification_changed {
  account_id by;
  std::string txn;
  std::string user;
  std::string device;
  verification_step_t step;
};
// This session, as Element's Cryptography section names it: its ID, and
// its key (ed25519), to be compared with what another session shows.
// Element's Security section: whether this session has the cross-signing
// keys, and whether the room keys are backed up.
struct security_state {
  account_id by;
  bool cross_signing = false;
  bool backup = false;
};
// The account's sessions, this one's ID among them.
struct sessions_listed {
  account_id by;
  std::string current;
  std::vector<session_info> sessions;
};
// Sessions not signed out, or not renamed: why -- and whether the password
// is what was missing.
struct sessions_refused {
  account_id by;
  std::string why;
  bool needs_password = false;
};

// A step of interactive auth to be done in the browser -- an SSO login
// again, an OIDC server's page: where, and for what.
struct uia_in_browser {
  account_id by;
  std::string url;
  std::string what;
};

// A session the server gave at login: kept with the account (take_session).
struct session_given {
  account_id account;
  std::string access_token;
  std::string device_id;
  // Signed in in the browser (OAuth 2.0): the token that renews the session,
  // and the client the server knows mux as.
  std::optional<std::string> refresh_token = {};
  std::optional<std::string> oauth_client_id = {};
};
// A registration stage done on the server's own page -- a CAPTCHA,
// whichever the server uses; an email confirmed -- to be opened in the
// browser: the registration carries on by itself once it is done there.
struct registration_page {
  account_id account;
  std::string url;
};

// The server's own sign-in page (OAuth 2.0), to be opened in the browser:
// the account goes on by itself once the browser comes back to mux.
struct sign_in_page {
  account_id account;
  std::string url;
};

using changes = change_list<management_loaded, management_saved, room_avatar_uploaded, session_given, registration_page, sign_in_page, verification_changed, security_state, sessions_listed, sessions_refused, uia_in_browser>;
constexpr type_tag<changes> changes_type(const state&) { return {}; }

// As the command line says them.
inline std::string describe(const room_avatar_uploaded& one) { return one.error.empty() ? "Room avatar uploaded: " + one.in.id : one.error; }
inline std::string describe(const management_loaded& one) { return "Room settings: " + one.in.id; }
inline std::string describe(const management_saved& one) { return one.error.empty() ? "Room settings saved: " + one.in.id : one.error; }
inline std::string describe(const session_given& one) { return one.account.address + " was given a session"; }
inline std::string describe(const registration_page& one) { return one.account.address + ": registering on " + one.url; }
inline std::string describe(const sign_in_page& one) { return one.account.address + ": signing in on " + one.url; }
inline std::string describe(const security_state& one) {
  return std::format("cross-signing {}, backup {}", one.cross_signing ? "set up" : "not set up", one.backup ? "on" : "off");
}
inline std::string describe(const verification_changed& one) {
  return std::format("verifying {} {}: {}", one.user, one.device, one.txn);
}
inline std::string describe(const session_info& one) { return one.id + " " + one.name; }
inline std::string describe(const sessions_listed& one) { return std::format("{} sessions", one.sessions.size()); }
inline std::string describe(const sessions_refused& one) { return one.why; }
inline std::string describe(const uia_in_browser& one) { return one.what + ": " + one.url; }

}  // namespace mux::proto::matrix
