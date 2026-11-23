// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.irc.changes -- What an IRC account says that no other protocol
// does: what the server said to the account itself rather than in a chat --
// its message of the day, a notice before registration -- shown as a notice.
// Its change list (changes_type(const state&), by ADL) is made part of
// mux.core's change_t.
export module mux.proto.irc.changes;

import std;
import splice;
import mux.core.ids;
import mux.proto.irc.kept;

export namespace mux::proto::irc {

// The server's message of the day, read whole (375, 372..., 376).
struct motd_read {
  account_id account;
  std::string text;
  friend bool operator==(const motd_read&, const motd_read&) = default;
};
inline std::string describe(const motd_read& one) { return one.account.address + ": the message of the day"; }

using changes = change_list<motd_read>;
constexpr type_tag<changes> changes_type(const state&) { return {}; }

}  // namespace mux::proto::irc
