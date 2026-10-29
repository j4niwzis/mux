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
// The user's own name in the room (/myroomnick), and everywhere (/nick).
struct room_name_mine { std::string name; };
struct name_mine { std::string name; };
}  // namespace slash
using slash_t = spl::variant<slash::said, slash::join, slash::leave, slash::act, slash::power, slash::room_name_mine, slash::name_mine>;
namespace request {
// A slash command, for the chat it was typed in.
struct slash {
  mux::conversation_id to;
  slash_t what;
};
// A pack created or edited from the room's settings.
// A room changed, as Matrix changes one: the room being managed.
struct change_room {
  room_change_t change;
};
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
// The other person of a direct chat verified by emoji, from the banner
// that says they are not.
struct verify_them {
  account_id by;
  std::string user;
};
}  // namespace request


namespace tool {
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
inline request::close_tool<uia_step> closing_of(std::type_identity<uia_step>) { return {}; }
}  // namespace tool


}  // namespace mux::proto::matrix
