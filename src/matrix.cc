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
import loom.cs.redaction;
import loom.cs.room_send;
import loom.cs.sync;
import loom.cs.wellknown;
import mux.config;
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
      if (!got) {
        log(id_, "history of {}: {}", room, got.error().said());
        return;
      }
      log(id_, "history of {}: {} event{}", room, got->chunk.size(), got->chunk.size() == 1 ? "" : "s");
      const conversation_id in{id_, room};
      for (const auto& one : got->chunk)  // newest first: each goes before the rest
        event(in, one, true);
      sink_(change::history_position{in, got->end});
    });
  }

  // An avatar's picture: the server's thumbnail of an mxc:// URI, at the size
  // it is drawn at twice over, handed on for `key`. The authenticated media
  // API first (v1.11), the older one where the server has no such thing.
  void fetch_avatar(std::string source, std::string key) {
    loop_->spawn([this, source = std::move(source), key = std::move(key)] {
      if (!api_ || !source.starts_with("mxc://"))
        return;
      const std::string_view rest = std::string_view(source).substr(6);
      const auto slash = rest.find('/');
      if (slash == std::string_view::npos)
        return;
      const std::string server(rest.substr(0, slash));
      const std::string media(rest.substr(slash + 1));
      const std::string query = "?width=96&height=96&method=crop";
      for (const std::string& base : {std::string("/_matrix/client/v1/media/thumbnail/"),
                                      std::string("/_matrix/media/v3/thumbnail/")}) {
        try {
          const auto got = api_->request("GET", base + server + "/" + media + query, {},
                                         token_ ? std::optional<std::string_view>(*token_) : std::nullopt);
          if (got.status == 200 && !got.body.empty()) {
            sink_(change::avatar_loaded{key, source, got.body});
            return;
          }
        } catch (const net::failure&) {
          return;
        }
      }
    });
  }

  // A message of one's own edited (m.replace): the new text in its place.
  void edit(std::string room, std::string event, std::string text) {
    loop_->spawn([this, room = std::move(room), event = std::move(event), text = std::move(text)] {
      if (!api_)
        return;
      knot::value::object now;
      now.emplace("msgtype", knot::value(std::string("m.text")));
      now.emplace("body", knot::value(text));
      knot::value::object relates;
      relates.emplace("rel_type", knot::value(std::string("m.replace")));
      relates.emplace("event_id", knot::value(event));
      knot::value::object content;
      content.emplace("msgtype", knot::value(std::string("m.text")));
      content.emplace("body", knot::value("* " + text));
      content.emplace("m.new_content", knot::value(std::move(now)));
      content.emplace("m.relates_to", knot::value(std::move(relates)));
      if (perform(*api_, loom::cs::send_message{.room_id = room,
                                                .event_type = "m.room.message",
                                                .txn_id = "mux" + std::to_string(++transactions_),
                                                .body = knot::value(std::move(content))}))
        sink_(change::message_edited{{id_, room}, event, body{text, std::nullopt}});
    });
  }
  // A message removed (redacted).
  void remove(std::string room, std::string event) {
    loop_->spawn([this, room = std::move(room), event = std::move(event)] {
      if (!api_)
        return;
      if (perform(*api_, loom::cs::redact_event{.room_id = room,
                                                .event_id = event,
                                                .txn_id = "mux" + std::to_string(++transactions_)}))
        sink_(change::message_redacted{{id_, room}, event});
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
  void send(std::string room, std::string body, std::optional<std::string> reply_to = std::nullopt) {
    loop_->spawn([this, room = std::move(room), body = std::move(body), reply_to = std::move(reply_to)] {
      const std::string txn = "mux" + std::to_string(++transactions_);
      const conversation_id in{id_, room};
      sink_(change::message_added{message{
          .in = in,
          .id = txn,
          .sender = id_.address,
          .at = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()),
          .body = {body, std::nullopt},
          .replies_to = reply_to,
          .outgoing = true,
          .delivery = delivery::sending{}}});
      if (!api_) {
        sink_(change::delivery_changed{in, txn, delivery::failed{}});
        return;
      }
      knot::value::object content;
      content.emplace("msgtype", knot::value(std::string("m.text")));
      content.emplace("body", knot::value(body));
      if (reply_to) {
        knot::value::object target;
        target.emplace("event_id", knot::value(*reply_to));
        knot::value::object relates;
        relates.emplace("m.in_reply_to", knot::value(std::move(target)));
        content.emplace("m.relates_to", knot::value(std::move(relates)));
      }
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
  std::expected<typename Endpoint::response, failure> perform(auto& over, const Endpoint& endpoint,
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
    if (how_.proxy)
      log(id_, "through the proxy {}:{}", how_.proxy->host, how_.proxy->port);
    log(id_, "finding the homeserver");
    const auto base = homeserver();
    if (!base) {
      log(id_, "no homeserver found");
      say(connection::failed{"no homeserver for " + how_.user_id});
      return;
    }
    // Several at once for what is asked while the long poll waits.
    http::pool api(*loop_, *tls_, *base, how_.proxy);
    http::connection syncing(*loop_, *tls_, *base, how_.proxy);  // the long poll has one of its own
    api_ = &api;

    // Logged in: as the device kept, where there is one, and the session
    // told, to be kept.
    const auto log_in = [&]() -> bool {
      loom::cs::def::user_identifier_t who{.type = "m.id.user"};
      knot::value::object said_user;
      said_user.emplace("user", knot::value(localpart_));
      who.rest = knot::value(std::move(said_user));
      log(id_, "logging in, as the device {}", how_.device_id.value_or("the server makes"));
      auto logged = perform(api, loom::cs::login{.body = {.type = "m.login.password",
                                                          .identifier = std::move(who),
                                                          .password = how_.password,
                                                          .device_id = how_.device_id,
                                                          .initial_device_display_name = how_.device_name}});
      if (!logged) {
        log(id_, "login failed: {}", logged.error().said());
        say(connection::failed{"login: " + logged.error().said()});
        return false;
      }
      token_ = logged->access_token;
      how_.device_id = logged->device_id;
      log(id_, "logged in, as the device {}", how_.device_id.value_or("?"));
      sink_(change::session_given{id_, logged->access_token, logged->device_id});
      return true;
    };
    bool kept = false;
    if (how_.access_token) {
      token_ = how_.access_token;
      kept = true;
      log(id_, "going on with the session kept, as the device {}", how_.device_id.value_or("?"));
    } else if (!log_in()) {
      api_ = nullptr;
      return;
    }
    say(connection::online{});
    // Where the last run left the sync: its rooms at once, and the sync goes
    // on from there rather than asking for every room again.
    this->load_kept();
    auto saved_at = std::chrono::steady_clock::now();

    std::chrono::seconds backoff(1);
    while (!stopping_) {
      // Lighter than the server's whole state: a room's last messages (the
      // rest is paged back to) and only the members who speak in them. The
      // first sync still gathers every room, and on a large account the
      // server takes minutes over it -- it is given them, where a later
      // one is given the long poll and half a minute.
      static constexpr std::string_view kFilter =
          R"({"room":{"timeline":{"limit":20},"state":{"lazy_load_members":true}}})";
      const bool first = !state_.since.has_value();
      if (first)
        log(id_, "the first sync: asking for every room (on a large account, this takes a while)");
      auto got = perform(syncing,
                         loom::cs::sync{.filter = std::string(kFilter),
                                        .since = state_.since,
                                        .timeout = first ? 0 : how_.sync_timeout.count()},
                         first ? std::chrono::seconds(600)
                               : std::chrono::duration_cast<std::chrono::seconds>(how_.sync_timeout) +
                                     std::chrono::seconds(30));
      if (!got) {
        const failure& why = got.error();
        if (why.server && (why.server->errcode == "M_UNKNOWN_TOKEN" || why.server->errcode == "M_FORBIDDEN")) {
          // A kept session no longer good: logged in again, once.
          if (kept) {
            log(id_, "the session kept is no longer good: logging in again");
            kept = false;
            if (log_in())
              continue;
            break;
          }
          say(connection::failed{why.said()});
          break;
        }
        log(id_, "{} failed: {}; trying again", state_.since ? "sync" : "the first sync", why.said());
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
        log(id_, "syncing again");
        say(connection::online{});
      }
      if (!state_.since) {
        const std::size_t joined = got->rooms && got->rooms->join ? got->rooms->join->size() : 0;
        log(id_, "first sync: {} room{}", joined, joined == 1 ? "" : "s");
      }
      state_.apply(*got);
      tell(*got);
      // Written every half a minute, and at the end: a restart goes on from
      // at most that far back.
      if (first || std::chrono::steady_clock::now() - saved_at > std::chrono::seconds(30)) {
        this->save_kept();
        saved_at = std::chrono::steady_clock::now();
      }
    }
    this->save_kept();
    api_ = nullptr;
    log(id_, "disconnected");
    say(connection::offline{});
  }

  // The file the sync is kept in, for this account.
  std::filesystem::path kept_file() const {
    std::string name;
    for (const char c : id_.address)
      name += std::isalnum(static_cast<unsigned char>(c)) || c == '@' || c == '.' || c == '-' ? c : '_';
    return config::state_path(name + ".sync.json");
  }
  // The sync as it stands, as a sync's answer: every joined room's state,
  // its timeline since its last gap and where that pages back from, its
  // summary, unread counts and data; the account's data; and the token to
  // go on from. Read back, it is applied as an answer is.
  void save_kept() const {
    if (!state_.since)
      return;
    using response = loom::cs::sync::response;
    using joined_t = response::rooms_t::joined_room_t;
    response out;
    out.next_batch = *state_.since;
    std::map<std::string, joined_t> join;
    for (const auto& [room, kept] : state_.joined) {
      joined_t one;
      std::vector<loom::ev::timeline_event> state_events;
      for (const auto& [key, event] : kept.state.events)
        state_events.push_back(event);
      one.state = joined_t::state_t{.events = std::move(state_events)};
      one.timeline = joined_t::timeline_t{.limited = true, .prev_batch = kept.prev_batch, .events = kept.timeline};
      one.summary = joined_t::room_summary_t{.m_heroes = kept.summary.heroes,
                                             .m_joined_member_count = kept.summary.joined_members,
                                             .m_invited_member_count = kept.summary.invited_members};
      one.unread_notifications = joined_t::unread_notification_counts_t{.highlight_count = kept.unread.highlight,
                                                                        .notification_count = kept.unread.notification};
      std::vector<loom::client::other_event> data;
      for (const auto& [type, event] : kept.account_data)
        data.push_back(event);
      one.account_data = joined_t::account_data_t{.events = std::move(data)};
      join.emplace(room, std::move(one));
    }
    out.rooms = response::rooms_t{.join = std::move(join)};
    std::vector<loom::client::other_event> data;
    for (const auto& [type, event] : state_.account_data)
      data.push_back(event);
    out.account_data = response::account_data_t{.events = std::move(data)};
    const std::filesystem::path where = this->kept_file();
    std::error_code failed;
    std::filesystem::create_directories(where.parent_path(), failed);
    const std::filesystem::path fresh = where.string() + ".new";
    {
      std::ofstream file(fresh, std::ios::binary | std::ios::trunc);
      file << knot::to_json_string(out);
      if (!file) {
        log(id_, "the sync could not be kept in {}", where.string());
        return;
      }
    }
    std::filesystem::rename(fresh, where, failed);
  }
  void load_kept() {
    std::ifstream file(this->kept_file(), std::ios::binary);
    if (!file)
      return;
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto saved = knot::try_read<loom::cs::sync::response>(std::string_view(text));
    if (!saved) {
      log(id_, "the sync kept could not be read: starting afresh");
      return;
    }
    state_.apply(*saved);
    tell(*saved);
    log(id_, "the sync kept: {} rooms, going on from there", state_.joined.size());
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

  // A room's picture: its own, or, for a chat with one other person, theirs.
  std::optional<std::string> avatar_of(const std::string& room, const loom::client::joined_room& kept) const {
    if (auto own = kept.state.avatar_url(); own && !own->empty())
      return own;
    if (!direct(room))
      return std::nullopt;
    for (const std::string& hero : kept.summary.heroes)
      if (const auto* them = kept.state.content<loom::ev::m_room_member_content_t>("m.room.member", hero);
          them && them->avatar_url)
        return them->avatar_url;
    return std::nullopt;
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
                                       .avatar = avatar_of(in.id, kept),
                                       .topic = kept.state.topic(),
                                       .encrypted = kept.state.encrypted(),
                                       .unread = kept.unread.notification,
                                       .highlights = kept.unread.highlight,
                                       .space = space(kept),
                                       .children = children_of(kept)});
  }

  // Whether a room is a space: its creation says so, by its type.
  static bool space(const loom::client::joined_room& kept) {
    const auto* created = kept.state.find("m.room.create");
    if (!created)
      return false;
    const knot::value tree = knot::to_value(*created);
    const knot::value* content = member(tree, "content");
    const knot::value* type = content ? member(*content, "type") : nullptr;
    return type && type->is<std::string>() && type->as<std::string>() == "m.space";
  }
  // The rooms a space holds: an m.space.child for each, whose content is
  // not empty -- an emptied one is a child taken out.
  static std::vector<std::string> children_of(const loom::client::joined_room& kept) {
    std::vector<std::string> out;
    for (const auto& [key, one] : kept.state.events) {
      if (key.first != "m.space.child")
        continue;
      const knot::value tree = knot::to_value(one);
      const knot::value* content = member(tree, "content");
      if (content && content->is<knot::value::object>() && !content->as<knot::value::object>().empty())
        out.push_back(key.second);
    }
    return out;
  }

  // Who is in a room, as its state says: those joined, by their names there.
  void members(const conversation_id& in, const loom::client::joined_room& kept) {
    std::vector<mux::member> who;
    for (const std::string& user : kept.state.members("join")) {
      const auto* joined = kept.state.content<loom::ev::m_room_member_content_t>("m.room.member", user);
      who.push_back({user, kept.state.display_name(user).value_or(user), std::nullopt,
                     joined ? joined->avatar_url : std::nullopt});
    }
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
  http::pool* api_ = nullptr;
  loom::client::state state_;
  std::map<std::string, reaction> reactions_;
  std::uint64_t transactions_ = 0;
  bool stopping_ = false;
};

}  // namespace mux::matrix
