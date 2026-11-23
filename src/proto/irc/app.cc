// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.proto.irc -- What the program does with IRC's own changes -- the
// message of the day, shown in the server's chat -- and its own requests: a
// command typed in a chat, done by that chat's account. Templates on the
// program, which calls program_told(app, change) where it takes changes and
// program_asked(app, request) where it takes requests (mux.app.proto).
export module mux.app.proto.irc;

import std;
import splice;
import mux.core;
import mux.config;
import mux.proto.kept;
import mux.proto.irc.changes;
import mux.ui;
import mux.proto.irc;
import mux.app.network;

export namespace mux::proto::irc {

// The message of the day: nothing more is done with it than to keep it in
// the log; the server's chat has it as its notices.
template <class App>
void program_told(App&, const motd_read& one) {
  log(one.account, "the message of the day: {} line{}", std::ranges::count(one.text, '\n'), std::ranges::count(one.text, '\n') == 1 ? "" : "s");
}

}  // namespace mux::proto::irc

export namespace mux::proto::irc::request {

// A command typed in a chat: done by the account of that chat, where it has
// the call.
template <class App>
void program_asked(App& app, const command& one) {
  if (app.shared.demo())
    return;
  app.net->on_account_of(one.to, [room = one.to.id, what = one.what](auto& account)
                                     -> decltype(void(account.command(room, what))) { account.command(room, what); });
}

}  // namespace mux::proto::irc::request
