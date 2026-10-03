// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix.requests -- What Matrix's own UI asks of the program: its
// account's sessions, cross-signing, the key backup and room keys. Listed by
// requests_of(state), done by program_asked (mux.app.proto.matrix).
export module mux.proto.matrix.requests;

import std;
import mux.proto.tags;
import mux.proto;

export namespace mux::proto::matrix {

namespace request {
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
struct sign_out_sessions {
  std::vector<std::string> devices;
  std::string password;
};
struct rename_session {
  std::string device;
  std::string name;
};
struct refresh_sessions {};
}  // namespace request

constexpr request_list<request::setup_cross_signing, request::restore_cross_signing, request::reset_identity,
                       request::sign_out_unverified, request::reset_backup, request::delete_backup, request::export_room_keys,
                       request::import_room_keys, request::verify_session, request::sign_out_sessions, request::rename_session,
                       request::refresh_sessions>
requests_of(const state&) {
  return {};
}

}  // namespace mux::proto::matrix
