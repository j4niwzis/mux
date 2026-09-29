// SPDX-License-Identifier: AGPL-3.0-only
// mux.matrix: a Matrix account, run by fibers on mux.net's loop -- loom's
// typed requests over mux.http, /sync long-polled into loom::client::state,
// and what each sync brought said as mux.core's changes.
export module mux.matrix;

import std;
import knot;
import loom.api;
import loom.ev;
import loom.state;
import loom.cs.leaving;
import loom.cs.login;
import loom.cs.message_pagination;
import loom.cs.receipts;
import loom.cs.room_send;
import loom.cs.sync;
import loom.cs.wellknown;
import mux.core;
import mux.http;
import mux.net;

export namespace mux::matrix {

struct settings {
  std::string user_id;  // @user:example.org
  std::string password;
  // The client-server API's base URL, instead of what the server name's
  // .well-known says.
  std::optional<std::string> homeserver;
  std::string device_name = "mux";
  // A proxy to connect through, where there is one.
  std::optional<net::proxy> proxy;
  // The session kept from before: logged in with, rather than logging in
  // again -- a new device each time -- where it is still good.
  std::optional<std::string> access_token;
  std::optional<std::string> device_id;
  // How long a sync waits on the server for something to happen.
  std::chrono::milliseconds sync_timeout = std::chrono::seconds(30);
};

// A value of an object, by key; nothing where it is not an object or has no
// such key.
inline const knot::value* member(const knot::value& of, std::string_view key) {
  if (!of.is<knot::value::object>())
    return nullptr;
  const auto& all = of.as<knot::value::object>();
  const auto found = all.find(key);
  return found == all.end() ? nullptr : &found->second;
}

// A key of an event's content that its type does not name: in the content's
// rest, or where knot::tagged keeps what the alternative did not type.
template <class Tagged>
const knot::value* extra(const knot::value& rest, const Tagged& content, std::string_view key) {
  if (const knot::value* found = member(rest, key))
    return found;
  return member(content.unknown, key);
}

inline std::optional<std::string> text(const knot::value* of) {
  if (of && of->is<std::string>())
    return of->as<std::string>();
  return std::nullopt;
}

// What a request failed with: the server's error, or the network's.
struct failure {
  std::optional<loom::error> server;
  std::string network;
  std::string said() const {
    if (server)
      return server->errcode + (server->message.empty() ? "" : ": " + server->message);
    return network;
  }
};

template <class Sink>
class account {
 public:
  account(net::loop& loop, net::tls& tls, settings how, Sink sink)
      : loop_(&loop), tls_(&tls), how_(std::move(how)), sink_(std::move(sink)) {
    id_ = account_id{protocol::matrix{}, how_.user_id};
    const auto colon = how_.user_id.find(':');
    localpart_ = how_.user_id.substr(how_.user_id.starts_with('@') ? 1 : 0,
                                     colon == std::string::npos ? std::string::npos : colon - 1);
    server_name_ = colon == std::string::npos ? std::string() : how_.user_id.substr(colon + 1);
  }
  account(const account&) = delete;
  account& operator=(const account&) = delete;

  const account_id& id() const noexcept { return id_; }

  void start() {
    loop_->spawn([this] { run(); });
  }

  void stop() { stopping_ = true; }

  // A read receipt for an event of a room: the people in it see how far this
  // account has read.
  void mark_read(std::string room, std::string event) {
    loop_->spawn([this, room = std::move(room), event = std::move(event)] {
      if (api_)
        (void)perform(*api_, loom::cs::post_receipt{.room_id = room,
                                                    .receipt_type = loom::cs::post_receipt::receipt_type_values::m_read{},
                                                    .event_id = event});
    });
  }

  // Older messages of a room, paged back from `from`: before the rest, and
  // where to page back from next -- nothing where the beginning is reached.
  void load_older(std::string room, std::string from) {
    loop_->spawn([this, room = std::move(room), from = std::move(from)] {
      if (!api_)
        return;
      auto got = perform(*api_, loom::cs::get_room_events{.room_id = room,
                                                          .from = from,
                                                          .dir = loom::cs::get_room_events::dir_values::b{},
                                                          .limit = 40});
      if (!got)
        return;
      const conversation_id in{id_, room};
      for (const auto& one : got->chunk)  // newest first: each goes before the rest
        event(in, one, true);
      sink_(change::history_position{in, got->end});
    });
  }

  // The account leaves a room; the next sync says it has, and the room goes.
  void leave(std::string room) {
    loop_->spawn([this, room = std::move(room)] {
      if (api_)
        (void)perform(*api_, loom::cs::leave_room{.room_id = room});
    });
  }

  // A text message sent to a room, from a fiber of its own. It is in the
  // conversation at once, under its transaction id; the server's answer
  // gives it its event id, and the echo in the next sync is the same message.
  void send(std::string room, std::string body) {
    loop_->spawn([this, room = std::move(room), body = std::move(body)] {
      const std::string txn = "mux" + std::to_string(++transactions_);
      const conversation_id in{id_, room};
      sink_(change::message_added{message{
          .in = in,
          .id = txn,
          .sender = id_.address,
          .at = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()),
          .body = {body, std::nullopt},
          .outgoing = true,
          .delivery = delivery::sending{}}});
      if (!api_) {
        sink_(change::delivery_changed{in, txn, delivery::failed{}});
        return;
      }
      knot::value::object content;
      content.emplace("msgtype", knot::value(std::string("m.text")));
      content.emplace("body", knot::value(body));
      auto sent = perform(*api_, loom::cs::send_message{.room_id = room,
                                                        .event_type = "m.room.message",
                                                        .txn_id = txn,
                                                        .body = knot::value(std::move(content))});
      if (!sent) {
        sink_(change::delivery_changed{in, txn, delivery::failed{}});
        return;
      }
      sink_(change::message_acknowledged{in, txn, sent->event_id});
    });
  }

 private:
  void say(connection_t state) { sink_(change::connection_changed{id_, std::move(state)}); }

  // A request made and its answer read into its type.
  template <class Endpoint>
  std::expected<typename Endpoint::response, failure> perform(http::connection& over, const Endpoint& endpoint,
                                                              std::chrono::seconds timeout = std::chrono::seconds(60)) {
    const loom::request asked = endpoint.to_send();
    try {
      const auto got = over.request(asked.method_name(), asked.target, asked.body,
                                    asked.authenticated && token_ ? std::optional<std::string_view>(*token_)
                                                                  : std::nullopt,
                                    timeout);
      auto read = loom::read<Endpoint>(got.status, got.body);
      if (!read) {
        loom::error said = std::move(read).error();
        if (!said.retry_after_ms)
          said.retry_after_ms = got.retry_after_ms;
        return std::unexpected(failure{std::move(said), {}});
      }
      return std::move(*read);
    } catch (const net::failure& failed) {
      return std::unexpected(failure{std::nullopt, failed.what()});
    }
  }

  // The client-server API's base URL: the one given, or the one the server
  // name's .well-known says (the spec's server discovery), or the server
  // name itself.
  std::optional<http::url> homeserver() {
    if (how_.homeserver)
      return http::url::parse(*how_.homeserver);
    if (auto name = http::url::parse(server_name_)) {
      http::connection discovery(*loop_, *tls_, *name, how_.proxy);
      if (auto found = perform(discovery, loom::cs::get_wellknown{}, std::chrono::seconds(15)))
        if (auto base = http::url::parse(found->m_homeserver.base_url))
          return base;
      return name;
    }
    return std::nullopt;
  }

  void run() {
    say(connection::connecting{});
    const auto base = homeserver();
    if (!base) {
      say(connection::failed{"no homeserver for " + how_.user_id});
      return;
    }
    http::connection api(*loop_, *tls_, *base, how_.proxy);
    http::connection syncing(*loop_, *tls_, *base, how_.proxy);  // the long poll has one of its own
    api_ = &api;

    // Logged in: as the device kept, where there is one, and the session
    // told, to be kept.
    const auto log_in = [&]() -> bool {
      loom::cs::def::user_identifier_t who{.type = "m.id.user"};
      knot::value::object said_user;
      said_user.emplace("user", knot::value(localpart_));
      who.rest = knot::value(std::move(said_user));
      auto logged = perform(api, loom::cs::login{.body = {.type = "m.login.password",
                                                          .identifier = std::move(who),
                                                          .password = how_.password,
                                                          .device_id = how_.device_id,
                                                          .initial_device_display_name = how_.device_name}});
      if (!logged) {
        say(connection::failed{"login: " + logged.error().said()});
        return false;
      }
      token_ = logged->access_token;
      how_.device_id = logged->device_id;
      sink_(change::session_given{id_, logged->access_token, logged->device_id});
      return true;
    };
    bool kept = false;
    if (how_.access_token) {
      token_ = how_.access_token;
      kept = true;
    } else if (!log_in()) {
      api_ = nullptr;
      return;
    }
    say(connection::online{});

    std::chrono::seconds backoff(1);
    while (!stopping_) {
      auto got = perform(syncing,
                         loom::cs::sync{.since = state_.since,
                                        .timeout = state_.since ? how_.sync_timeout.count() : 0},
                         std::chrono::duration_cast<std::chrono::seconds>(how_.sync_timeout) + std::chrono::seconds(30));
      if (!got) {
        const failure& why = got.error();
        if (why.server && (why.server->errcode == "M_UNKNOWN_TOKEN" || why.server->errcode == "M_FORBIDDEN")) {
          // A kept session no longer good: logged in again, once.
          if (kept) {
            kept = false;
            if (log_in())
              continue;
            break;
          }
          say(connection::failed{why.said()});
          break;
        }
        say(connection::connecting{why.said()});
        const auto wait = why.server && why.server->retry_after_ms
                              ? std::chrono::milliseconds(*why.server->retry_after_ms)
                              : std::chrono::duration_cast<std::chrono::milliseconds>(backoff);
        loop_->sleep(wait);
        backoff = std::min(backoff * 2, std::chrono::seconds(60));
        continue;
      }
      if (backoff != std::chrono::seconds(1)) {
        backoff = std::chrono::seconds(1);
        say(connection::online{});
      }
      state_.apply(*got);
      tell(*got);
    }
    api_ = nullptr;
    say(connection::offline{});
  }

  // What the rooms a sync named look like now, and what their timelines
  // brought.
  void tell(const loom::cs::sync::response& got) {
    if (!got.rooms)
      return;
    const auto& rooms = *got.rooms;
    if (rooms.join)
      for (const auto& [room, part] : *rooms.join) {
        const auto found = state_.joined.find(room);
        if (found == state_.joined.end())
          continue;
        const conversation_id in{id_, room};
        conversation(in, found->second);
        members(in, found->second);
        // Where to page back from: the first time a room is seen.
        if (part.timeline && part.timeline->prev_batch && paged_.insert(room).second)
          sink_(change::history_position{in, *part.timeline->prev_batch});
        if (part.timeline)
          for (const auto& one : part.timeline->events)
            event(in, one);
        sink_(change::typing_changed{in, found->second.typing});
      }
    if (rooms.invite)
      for (const auto& [room, part] : *rooms.invite)
        sink_(change::conversation_updated{.id = {id_, room}, .kind = conversation_kind::group{}, .name = room});
    if (rooms.leave)
      for (const auto& [room, part] : *rooms.leave)
        sink_(change::conversation_removed{{id_, room}});
  }

  // A room's name as the spec says a client works it out: m.room.name, the
  // canonical alias, the heroes, the room's id.
  static std::string name_of(const std::string& room, const loom::client::joined_room& kept) {
    if (auto name = kept.state.name(); name && !name->empty())
      return *name;
    if (auto alias = kept.state.canonical_alias(); alias && !alias->empty())
      return *alias;
    std::string heroes;
    for (const auto& hero : kept.summary.heroes) {
      if (!heroes.empty())
        heroes += ", ";
      heroes += kept.state.display_name(hero).value_or(hero);
    }
    return heroes.empty() ? room : heroes;
  }

  bool direct(const std::string& room) const {
    const auto found = state_.account_data.find("m.direct");
    if (found == state_.account_data.end())
      return false;
    const knot::value tree = knot::to_value(found->second);
    const knot::value* content = member(tree, "content");
    if (!content || !content->is<knot::value::object>())
      return false;
    for (const auto& [user, rooms] : content->as<knot::value::object>())
      if (rooms.is<knot::value::array>())
        for (const auto& one : rooms.as<knot::value::array>())
          if (one.is<std::string>() && one.as<std::string>() == room)
            return true;
    return false;
  }

  void conversation(const conversation_id& in, const loom::client::joined_room& kept) {
    sink_(change::conversation_updated{.id = in,
                                       .kind = direct(in.id) ? conversation_kind_t{conversation_kind::direct{}} : conversation_kind_t{conversation_kind::group{}},
                                       .name = name_of(in.id, kept),
                                       .avatar = kept.state.avatar_url(),
                                       .topic = kept.state.topic(),
                                       .encrypted = kept.state.encrypted(),
                                       .unread = kept.unread.notification,
                                       .highlights = kept.unread.highlight});
  }

  // Who is in a room, as its state says: those joined, by their names there.
  void members(const conversation_id& in, const loom::client::joined_room& kept) {
    std::vector<mux::member> who;
    for (const std::string& user : kept.state.members("join"))
      who.push_back({user, kept.state.display_name(user).value_or(user), std::nullopt});
    sink_(change::members_changed{in, std::move(who)});
  }

  // An event of a room's timeline, as changes: at the end, or before the
  // rest where it is history paged back to.
  void event(const conversation_id& in, const loom::ev::timeline_event& one, bool history = false) {
    const auto at = std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::milliseconds(one.origin_server_ts));
    if (one.content.template is<loom::ev::m_room_message_content_t>()) {
      const auto& content = one.content.template as<loom::ev::m_room_message_content_t>();
      const knot::value* relates = extra(content.rest, one.content, "m.relates_to");
      // An edit: the event it replaces takes its new content.
      if (text(member(relates ? *relates : knot::value(), "rel_type")) == "m.replace") {
        const auto target = text(member(*relates, "event_id"));
        const knot::value* now = extra(content.rest, one.content, "m.new_content");
        if (target && now)
          sink_(change::message_edited{in, *target, body_of(text(member(*now, "body")).value_or(""), *now)});
        return;
      }
      message made{.in = in,
                   .id = one.event_id,
                   .sender = one.sender,
                   .at = at,
                   .body = body_of(content.body, content.rest, one.content),
                   .outgoing = one.sender == id_.address};
      if (content.msgtype == "m.emote")
        made.body.plain = "* " + made.body.plain;
      if (relates)
        if (const knot::value* reply = member(*relates, "m.in_reply_to"))
          made.replies_to = text(member(*reply, "event_id"));
      sink_(change::message_added{std::move(made), history});
    } else if (one.type == "m.room.encrypted") {
      // By its type: loom's timeline union does not have its content yet.
      sink_(change::message_added{message{.in = in,
                                          .id = one.event_id,
                                          .sender = one.sender,
                                          .at = at,
                                          .body = {"🔒 an encrypted message (not yet readable here)", std::nullopt},
                                          .outgoing = one.sender == id_.address},
                                  history});
    } else if (one.content.template is<loom::ev::m_reaction_content_t>()) {
      const auto& content = one.content.template as<loom::ev::m_reaction_content_t>();
      if (content.m_relates_to && content.m_relates_to->event_id && content.m_relates_to->key) {
        reactions_[one.event_id] = {*content.m_relates_to->event_id, *content.m_relates_to->key, one.sender};
        sink_(change::reaction_changed{in, *content.m_relates_to->event_id, *content.m_relates_to->key, one.sender,
                                       true});
      }
    } else if (one.type == "m.room.redaction") {
      std::optional<std::string> target = one.redacts;
      if (one.content.template is<loom::ev::m_room_redaction_content_t>())
        if (const auto& redacts = one.content.template as<loom::ev::m_room_redaction_content_t>().redacts)
          target = redacts;
      if (!target)
        return;
      // A reaction taken back, or a message removed.
      if (const auto reaction = reactions_.find(*target); reaction != reactions_.end()) {
        sink_(change::reaction_changed{in, reaction->second.target, reaction->second.key, reaction->second.who,
                                       false});
        reactions_.erase(reaction);
      } else {
        sink_(change::message_redacted{in, *target});
      }
    }
  }

  // A message's body: its plain text, and its HTML where it says it has
  // org.matrix.custom.html.
  static body body_of(std::string plain, const knot::value& content) {
    body made{std::move(plain), std::nullopt};
    if (text(member(content, "format")) == "org.matrix.custom.html")
      made.html = text(member(content, "formatted_body"));
    return made;
  }
  template <class Tagged>
  static body body_of(std::string plain, const knot::value& rest, const Tagged& content) {
    body made{std::move(plain), std::nullopt};
    if (text(extra(rest, content, "format")) == "org.matrix.custom.html")
      made.html = text(extra(rest, content, "formatted_body"));
    return made;
  }

  struct reaction {
    std::string target, key, who;
  };

  net::loop* loop_;
  net::tls* tls_;
  settings how_;
  Sink sink_;
  account_id id_;
  std::string localpart_, server_name_;
  std::optional<std::string> token_;
  // The rooms whose place to page back from was told.
  std::set<std::string> paged_;
  http::connection* api_ = nullptr;
  loom::client::state state_;
  std::map<std::string, reaction> reactions_;
  std::uint64_t transactions_ = 0;
  bool stopping_ = false;
};

}  // namespace mux::matrix
