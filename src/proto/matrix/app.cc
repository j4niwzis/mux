// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.proto.matrix -- What the program does with Matrix's own changes:
// what its developer tools found, its sticker packs, an emoji verification,
// the account's sessions and its security. Templates on the program, which
// calls program_told(app, change) where it takes changes (mux.app.proto).
export module mux.app.proto.matrix;

import std;
import splice;
import mux.core;
import mux.config;
import mux.protocols;
import mux.proto.kept;
import mux.proto.matrix.changes;
import mux.proto.matrix.requests;
import knot;
import mux.ui;
import mux.ui.proto.matrix;

// What Matrix's glue asks of an account, by the program's network: each
// where the account's client has the call (ask_if_able, in on_account).
namespace mux::proto::matrix::ops {

template <class Net>
void list_sessions(Net& net, const account_id& by) {
  net.on_account(by, [](auto& account) -> decltype(void(account.list_sessions())) { account.list_sessions(); });
}

template <class Net>
void sign_out_sessions(Net& net, const account_id& by, std::vector<std::string> devices, std::string password) {
  net.on_account(by, [devices = std::move(devices), password = std::move(password)](auto& account) -> decltype(void(account.sign_out_sessions(devices, password))) { account.sign_out_sessions(devices, password); });
}

template <class Net>
void rename_session(Net& net, const account_id& by, std::string device, std::string name) {
  net.on_account(by, [device = std::move(device), name = std::move(name)](auto& account) -> decltype(void(account.rename_session(device, name))) { account.rename_session(device, name); });
}

template <class Net>
void continue_uia(Net& net, const account_id& by) {
  net.on_account(by, [](auto& account) -> decltype(void(account.continue_uia())) { account.continue_uia(); });
}
template <class Net>
void cancel_uia(Net& net, const account_id& by) {
  net.on_account(by, [](auto& account) -> decltype(void(account.cancel_uia())) { account.cancel_uia(); });
}

}  // namespace mux::proto::matrix::ops

namespace mux::proto::matrix::app_detail {
// The user's own member event in a room, as /myroomnick sends it: joined,
// under a name of its own there.
struct member_named {
  std::string membership = "join";
  std::string displayname;
  friend consteval auto json_schema(knot::type<member_named>) { return knot::schema<member_named>(); }
};
}  // namespace mux::proto::matrix::app_detail


export namespace mux::proto::matrix {

// A session the server gave at login: kept with the account, as its kept
// type keeps one (take_session).
template <class App>
void program_told(App& app, const session_given& given) {
  app.change_account(given.account.address, [&](mux::config::account_t& account) {
    spl::visit([&](auto& one) {
      using kept_defaults::take_session;
      take_session(one, given);
    }, account.own);
  });
}
// A registration stage done on the server's own page: opened in the browser,
// and said -- the registration carries on by itself once it is done there.
template <class App>
void program_told(App& app, const registration_page& page) {
  app.take(mux::ui::request::open_url{page.url});
  app.shared.notice("Finish registering in your browser",
                          "The server asks for a step it does on its own page, now open in your browser. Once it is done "
                          "there, mux carries on with the registration by itself.");
}
// The server's own sign-in page (OAuth 2.0): opened in the browser, and
// said -- the account carries on by itself once the browser comes back.
template <class App>
void program_told(App& app, const sign_in_page& page) {
  app.take(mux::ui::request::open_url{page.url});
  app.shared.notice("Sign in in your browser",
                          "The server's sign-in page is open in your browser. Once you have signed in there, the "
                          "browser comes back to mux, and the account carries on by itself.");
}
// A step of interactive auth for the browser: its page opened there, and a
// box to say when it is done.
template <class App>
void program_told(App& app, const uia_in_browser& asked) {
  app.take(mux::ui::request::open_url{asked.url});
  mux::ui::show(app.showing, std::optional(tool::uia_step{asked.what, asked.url}));
}
// What Matrix keeps of an account whose pages are open, changed: where
// they are open for that account.
template <class App, class Change>
void change_account_state(App& app, const account_id& by, Change change) {
  using facts_t = std::optional<mux::ui::account_page_facts>;
  if (const facts_t& open = *app.showing.template look<facts_t>(); !open || open->by != by)
    return;
  mux::ui::change_shown<facts_t>(app.showing, [&](facts_t& open) {
    spl::visit(spl::overloaded{[&](account_state& kept) { change(kept); }, [](auto&) {}}, open->kept);
  });
}
// The sessions as the server listed them: a sign-out on its way is done.
template <class App>
void program_told(App& app, const sessions_listed& listed) {
  change_account_state(app, listed.by, [&](account_state& kept) {
    kept.sessions = listed.sessions;
    kept.current = listed.current;
    kept.signing.clear();
    kept.signing_out = false;
    kept.refused.clear();
    kept.needs_password = false;
  });
}
template <class App>
void program_told(App& app, const security_state& state) {
  change_account_state(app, state.by, [&](account_state& kept) { kept.security = account_security{state.cross_signing, state.backup}; });
}
// The server said no: why; and, where it wants the password, the sessions
// kept to be signed out again with it.
template <class App>
void program_told(App& app, const sessions_refused& said) {
  change_account_state(app, said.by, [&](account_state& kept) {
    kept.signing_out = false;
    kept.refused = said.why;
    kept.needs_password = said.needs_password;
    if (!said.needs_password)
      kept.signing.clear();
  });
}

}  // namespace mux::proto::matrix

// What Matrix's UI asks, done: each for the account whose pages are open.
export namespace mux::proto::matrix::request {

// The sessions: one verified by emoji, some signed out, one renamed, listed.
template <class App>
void program_asked(App& app, const verify_session& one) {
  app.shared.with_chosen_account([&](config::account_t& account) {
    app.net->verify_start(App::id_of(account), config::address_of(account), one.device);
  });
}
// A press on the account's pages: what Matrix keeps of it changed, and
// the server asked -- a session renamed, some signed out.
template <class Net>
struct account_step {
  Net& net;
  const account_id& by;
  account_state& kept;
  // Signed out, with the password where the server asked for it.
  void sign_out(std::vector<std::string> devices, std::string password) {
    if (devices.empty())
      return;
    kept.signing = devices;
    kept.signing_out = true;
    kept.refused.clear();
    ops::sign_out_sessions(net, by, std::move(devices), std::move(password));
  }
  void operator()(const account_change::rename& one) {
    kept.renaming = one.open ? std::optional(one.device) : std::nullopt;
  }
  void operator()(const account_change::save_name& one) {
    kept.renaming.reset();
    ops::rename_session(net, by, one.device, one.name);
  }
  void operator()(const account_change::sign_out& one) { this->sign_out(one.devices, {}); }
  void operator()(const account_change::sign_out_others&) {
    this->sign_out(kept.sessions.value_or(std::vector<session_info>{}) | std::views::filter([&](const session_info& one) { return one.id != kept.current; }) | std::views::transform(&session_info::id) | std::ranges::to<std::vector>(),
                   {});
  }
  void operator()(const account_change::sign_out_again& one) {
    kept.needs_password = false;
    this->sign_out(kept.signing, one.password);
  }
};
template <class App>
void program_asked(App& app, const change_account& one) {
  using facts_t = std::optional<mux::ui::account_page_facts>;
  const facts_t& open = *app.showing.template look<facts_t>();
  if (!open || app.shared.demo())
    return;
  const account_id by = open->by;
  change_account_state(app, by, [&](account_state& kept) {
    spl::visit(account_step<std::remove_reference_t<decltype(*app.net)>>{*app.net, by, kept}, one.change);
  });
}
// A room changed as Matrix changes one: the room being managed, by its account.
template <class App>
void program_asked(App& app, const change_room& one) {
  const auto chosen = app.shared.managed();
  if (!chosen || app.shared.demo())
    return;
  app.net->on_account_of(*chosen, [room = chosen->id, change = one.change](auto& account)
                                      -> decltype(void(account.change_room(room, change))) { account.change_room(room, change); });
}
// A slash command: done where it was typed -- a message sent as it makes
// it, a room joined or left, something done to the room or to someone in
// it -- by the account of that chat.
template <class App>
void program_asked(App& app, const slash& one) {
  if (app.shared.demo())
    return;
  const mux::conversation_id to = one.to;
  spl::visit(spl::overloaded{
                 [&](const proto::matrix::slash::said& said) {
                   // An answer where the field answers a message, as any message sent
                   // from it is -- and the field let go of it after.
                   const auto reply_to = spl::visit(spl::overloaded{[](const mux::ui::compose::reply& r) { return std::optional<std::string>(r.id); },
                                                                    [](const auto&) { return std::optional<std::string>(); }},
                                                    app.showing.template look<mux::ui::chat_compose>()->now);
                   if (reply_to)
                     mux::ui::show(app.showing, mux::ui::chat_compose{});
                   app.net->on_account_of(to, [room = to.id, said, reply_to](auto& account)
                                                  -> decltype(void(account.send_as(room, said.body, said.html, said.emote, reply_to))) {
                     account.send_as(room, said.body, said.html, said.emote, reply_to);
                   });
                 },
                 [&](const proto::matrix::slash::name_mine& mine) {
                   app.net->on_account_of(to, [name = mine.name](auto& account)
                                                  -> decltype(void(account.set_display_name(name))) { account.set_display_name(name); });
                 },
                 [&](const proto::matrix::slash::join& join) { app.net->join(to.account, join.room, {}); },
                 [&](proto::matrix::slash::leave) { app.net->leave(to); },
                 [&](const proto::matrix::slash::act& act) { app.net->manage(to, act.action); },
                 [&](const proto::matrix::slash::power& power) {
                   const proto::matrix::room_change_t change = proto::matrix::room_change::set_power{power.user, power.level};
                   app.net->on_account_of(to, [room = to.id, change](auto& account)
                                                  -> decltype(void(account.change_room(room, change))) { account.change_room(room, change); });
                 },
                 [&](const proto::matrix::slash::room_name_mine& mine) {
                   // The user's own member event in the room, its name its own there.
                   const proto::matrix::room_change_t change = proto::matrix::room_change::set_state_event{
                       "m.room.member", to.account.address,
                       knot::to_json_string(app_detail::member_named{.displayname = mine.name})};
                   app.net->on_account_of(to, [room = to.id, change](auto& account)
                                                  -> decltype(void(account.change_room(room, change))) { account.change_room(room, change); });
                 }},
             one.what);
}
template <class App>
void program_asked(App& app, const verify_them& one) {
  app.net->verify_start(one.by, one.user, std::nullopt);
}
template <class App>
void program_asked(App& app, const refresh_sessions&) {
  app.shared.with_chosen_account([&](config::account_t& account) { ops::list_sessions(*app.net, App::id_of(account)); });
}

// A step of interactive auth done in the browser: the request sent again,
// or let go; the box closed either way.
template <class App>
void program_asked(App& app, const continue_uia&) {
  mux::ui::show<tool::uia_step>(app.showing, std::nullopt);
  app.shared.with_chosen_account([&](config::account_t& account) { ops::continue_uia(*app.net, App::id_of(account)); });
}
template <class App>
void program_asked(App& app, const cancel_uia&) {
  mux::ui::show<tool::uia_step>(app.showing, std::nullopt);
  app.shared.with_chosen_account([&](config::account_t& account) { ops::cancel_uia(*app.net, App::id_of(account)); });
}
// Its own dialogs closed: what each shows let go.
template <class App, class Facts>
void program_asked(App& app, const close_tool<Facts>&) {
  mux::ui::show<Facts>(app.showing, std::nullopt);
}
}  // namespace mux::proto::matrix::request

