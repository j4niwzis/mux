// SPDX-License-Identifier: AGPL-3.0-only
// mux.matrix:sync -- The sync: logging in, /sync long-polled, kept on disk, and what it brought told as changes.
export module mux.matrix:sync;

import mux.vault;
import std;
import mux.matrix.crypto;
import splice;
import knot;
import loom.cs.sliding_sync;
import loom.cs.versions;
import loom.api;
import loom.ev;
import loom.state;
import loom.cs.joining;
import loom.cs.leaving;
import loom.cs.login;
import loom.cs.message_pagination;
import loom.cs.receipts;
import loom.cs.redaction;
import loom.cs.room_send;
import loom.cs.rooms;
import loom.cs.sync;
import loom.cs.typing;
import loom.cs.wellknown;
import mux.config;
import mux.core;
import mux.http;
import mux.net;
import :account;

// The members defined here are declared in :account, and exported there.
namespace mux::matrix {

// What the password login says of who: the user's local part.
struct user_field {
  std::string user;
  friend consteval auto json_schema(knot::type<user_field>) { return knot::schema<user_field>(); }
};

// A rule's word, as loom reads it: its text, for mux's own table.
template <class Content, class Rule>
std::optional<std::string> rule_text(const Content* content, Rule Content::* rule) {
  return content ? std::optional<std::string>(loom::client::choice_text(content->*rule)) : std::nullopt;
}

template <class Sink>
void account<Sink>::say(connection_t state) { sink_(change::connection_changed{id_, std::move(state)}); }

template <class Sink>
auto account<Sink>::homeserver() -> std::optional<http::url> {
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

// A sliding sync answer in the shape of a /sync one: what reads and keeps
// /sync's reads it as it is. A plain function from the one type to the
// other. Receipts, typing and account data of rooms the answer does not
// carry are left out: a room comes in with them once it is in the window.
inline loom::cs::sync::response legacy_of(const loom::cs::sliding_sync::response& got, std::string_view user) {
  using response = loom::cs::sync::response;
  using joined_t = response::rooms_t::joined_room_t;
  using invited_t = response::rooms_t::invited_room_t;
  using left_t = response::rooms_t::left_room_t;
  response out;
  out.next_batch = got.pos;
  std::map<std::string, joined_t> join;
  std::map<std::string, invited_t> invite;
  std::map<std::string, left_t> leave;
  // Whether the user's own membership in what a room brings says they are
  // gone from it: left, or banned.
  const auto gone = [&](const auto& room) {
    bool out_of_it = false;
    const auto look = [&](const std::optional<std::vector<loom::ev::timeline_event>>& events) {
      if (events)
        for (const auto& one : *events)
          if (one.state_key && *one.state_key == user)
            splice::visit(splice::overloaded{[&](const loom::ev::m_room_member_content_t& member) {
                                               using values = loom::ev::m_room_member_content_t::membership_values;
                                               out_of_it = splice::visit(splice::overloaded{[](values::leave) { return true; },
                                                                                            [](values::ban) { return true; },
                                                                                            [](const auto&) { return false; }},
                                                                         member.membership);
                                             },
                                             [](const auto&) {}},
                          one.content.data());
    };
    look(room.required_state);
    look(room.timeline);
    return out_of_it;
  };
  if (got.rooms)
    for (const auto& [id, room] : *got.rooms) {
      if (gone(room)) {
        leave.emplace(id, left_t{});
        continue;
      }
      if (room.invite_state) {
        invited_t one;
        one.invite_state = invited_t::invite_state_t{.events = *room.invite_state};
        invite.emplace(id, std::move(one));
        continue;
      }
      joined_t one;
      if (room.required_state)
        one.state = joined_t::state_t{.events = *room.required_state};
      if (room.timeline || room.limited || room.prev_batch)
        one.timeline = joined_t::timeline_t{.limited = room.limited,
                                            .prev_batch = room.prev_batch,
                                            .events = room.timeline.value_or(std::vector<loom::ev::timeline_event>{})};
      if (room.heroes || room.joined_count || room.invited_count)
        one.summary = joined_t::room_summary_t{
            .m_heroes = room.heroes ? std::optional(*room.heroes | std::views::transform([](const auto& hero) { return hero.user_id; }) |
                                                    std::ranges::to<std::vector>())
                                    : std::nullopt,
            .m_joined_member_count = room.joined_count,
            .m_invited_member_count = room.invited_count};
      if (room.notification_count || room.highlight_count)
        one.unread_notifications =
            joined_t::unread_notification_counts_t{.highlight_count = room.highlight_count, .notification_count = room.notification_count};
      join.emplace(id, std::move(one));
    }
  if (got.extensions) {
    const auto& extensions = *got.extensions;
    if (extensions.account_data) {
      if (extensions.account_data->global)
        out.account_data = response::account_data_t{.events = *extensions.account_data->global};
      if (extensions.account_data->rooms)
        for (const auto& [id, events] : *extensions.account_data->rooms)
          if (const auto in = join.find(id); in != join.end())
            in->second.account_data = joined_t::account_data_t{.events = events};
    }
    const auto ephemeral = [&](const auto& part) {
      if (part && part->rooms)
        for (const auto& [id, event] : *part->rooms)
          if (const auto in = join.find(id); in != join.end()) {
            auto& kept = in->second.ephemeral;
            if (!kept)
              kept.emplace();
            if (!kept->events)
              kept->events.emplace();
            kept->events->push_back(event);
          }
    };
    ephemeral(extensions.receipts);
    ephemeral(extensions.typing);
  }
  out.rooms = response::rooms_t{.join = std::move(join), .invite = std::move(invite), .leave = std::move(leave)};
  return out;
}

template <class Sink>
void account<Sink>::run() {
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

  // The machine woken from sleep -- a phone's screen turned on again: the
  // long poll it slept in is on a connection that is likely gone, and was
  // waited for until its timeout, then the back-off; messages came minutes
  // late. Seen as the wall clock gaining on the steady one, which stands
  // still while the machine sleeps (a wall clock set by hand reads the
  // same, and only syncs once more). The long poll is stopped then, and
  // the sync goes again at once.
  waking_ = std::make_shared<waking>();
  long_poll_ = &syncing;
  const auto wake = waking_;
  const struct wake_ends {
    std::shared_ptr<waking> of;
    http::connection** long_poll;
    ~wake_ends() {
      of->alive = false;  // the sync gone, and its connection with it
      *long_poll = nullptr;
    }
  } wake_guard{wake, &long_poll_};
  this->spawn_guarded([this, wake] {
    static constexpr auto kEvery = std::chrono::seconds(5);
    static constexpr auto kSlept = std::chrono::seconds(10);
    auto steady = std::chrono::steady_clock::now();
    auto wall = std::chrono::system_clock::now();
    while (wake->alive && !stopping_) {
      loop_->sleep(kEvery);
      if (!wake->alive || stopping_)
        return;  // the sync gone, and the connection with it
      const auto steady_now = std::chrono::steady_clock::now();
      const auto wall_now = std::chrono::system_clock::now();
      const auto gained = (wall_now - wall) - (steady_now - steady);
      steady = steady_now;
      wall = wall_now;
      if (gained > kSlept) {
        log(id_, "woken after {}s asleep: syncing again now",
            std::chrono::duration_cast<std::chrono::seconds>(gained).count());
        this->cut_long_poll();
      }
    }
  });

  // Logged in: as the device kept, where there is one, and the session
  // told, to be kept.
  const auto log_in = [&]() -> bool {
    loom::cs::def::user_identifier_t who{.type = "m.id.user"};
    who.rest = as_body(user_field{localpart_});
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
    sink_(proto::matrix::session_given{id_, logged->access_token, logged->device_id});
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
  this->start_crypto();
  // Simplified sliding sync, where the server says it has it: the rooms by
  // their activity, a window of them -- not every room at the first sync.
  if (auto versions = perform(api, loom::cs::get_versions{}); versions && versions->unstable_features)
    if (const auto found = versions->unstable_features->find("org.matrix.simplified_msc3575");
        found != versions->unstable_features->end() && found->second)
      sliding_ = true;
  log(id_, "syncing by {}", sliding_ ? "simplified sliding sync (MSC4186)" : "/sync");
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
    // The state a room of the sliding list comes with: what the chat list
    // and a room's head show, its spaces and its emoji -- members only those
    // who speak, and the user.
    static const std::vector<std::vector<std::string>> kRequiredState{
        {"m.room.create", ""},           {"m.room.tombstone", ""},           {"m.room.name", ""},          {"m.room.avatar", ""},
        {"m.room.topic", ""},            {"m.room.encryption", ""},    {"m.room.canonical_alias", ""},
        {"m.room.join_rules", ""},       {"m.room.history_visibility", ""}, {"m.room.power_levels", ""},
        {"m.room.pinned_events", ""},    {"m.room.tombstone", ""},     {"m.space.child", "*"},
        {"m.space.parent", "*"},         {"im.ponies.room_emotes", "*"}, {"m.room.member", "$LAZY"},
        {"m.room.member", "$ME"}};
    const bool sliding_first = sliding_ && !sliding_pos_;
    using sync_result = decltype(perform(syncing, loom::cs::sync{}, std::chrono::seconds(1)));
    auto got = [&]() -> sync_result {
      if (!sliding_)
        return perform(syncing,
                       loom::cs::sync{.filter = std::string(kFilter),
                                      .since = state_.since,
                                      .timeout = first ? 0 : how_.sync_timeout.count()},
                       first ? std::chrono::seconds(600)
                             : std::chrono::duration_cast<std::chrono::seconds>(how_.sync_timeout) +
                                   std::chrono::seconds(30));
      loom::cs::sliding_sync ask{.pos = sliding_pos_, .timeout = sliding_first ? 0 : how_.sync_timeout.count()};
      ask.body.lists.emplace("all", loom::cs::sliding_sync::body_t::list_t{.ranges = {{0, sliding_range_ - 1}},
                                                                             .required_state = kRequiredState,
                                                                             .timeline_limit = 20});
      // The room being read: more of its newest, wherever it is in the list.
      if (followed_room_)
        ask.body.room_subscriptions = std::map<std::string, loom::cs::sliding_sync::body_t::subscription_t>{
            {*followed_room_, {.required_state = kRequiredState, .timeline_limit = 50}}};
      using extension = loom::cs::sliding_sync::body_t::extension_t;
      ask.body.extensions = loom::cs::sliding_sync::body_t::extensions_t{
          .account_data = extension{.enabled = true}, .receipts = extension{.enabled = true}, .typing = extension{.enabled = true}};
      // What comes for this device, and how many of its one-time keys are
      // left, where it has encryption.
      if (crypto_) {
        ask.body.extensions->to_device = extension{.enabled = true, .since = crypto_->to_device_since()};
        ask.body.extensions->e2ee = extension{.enabled = true};
      }
      auto slid = perform(syncing, ask,
                          sliding_first ? std::chrono::seconds(300)
                                        : std::chrono::duration_cast<std::chrono::seconds>(how_.sync_timeout) + std::chrono::seconds(30));
      if (!slid)
        return std::unexpected(slid.error());
      sliding_pos_ = slid->pos;
      this->crypto_answer(*slid);
      // Fewer rooms in the window than the account has: two hundred more at
      // the next, until all are -- every room, a window at a time.
      if (slid->lists)
        if (const auto all = slid->lists->find("all"); all != slid->lists->end() && all->second.count &&
                                                       *all->second.count > sliding_range_)
          sliding_range_ = std::min(*all->second.count, sliding_range_ + 200);
      return legacy_of(*slid, id_.address);
    }();
    if (!got) {
      const failure& why = got.error();
      if (why.server && splice::visit([](auto code) { return code.gone; }, errcode_of(why.server->errcode))) {
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
      if (sliding_ && sliding_pos_ && why.server) {
        log(id_, "the sliding sync's position refused ({}): beginning it again", why.said());
        sliding_pos_.reset();
        continue;
      }
      // Stopped because the machine woke: straight on, from the first step
      // of the back-off.
      if (std::exchange(wake->woke, false)) {
        backoff = std::chrono::seconds(1);
        continue;
      }
      log(id_, "{} failed: {}; trying again", state_.since ? "sync" : "the first sync", why.said());
      say(connection::connecting{why.said()});
      const auto wait = why.server && why.server->retry_after_ms
                            ? std::chrono::milliseconds(*why.server->retry_after_ms)
                            : std::chrono::duration_cast<std::chrono::milliseconds>(backoff);
      // A second at a time, so that waking cuts it short: a minute of
      // back-off slept through was a minute more once the screen came on.
      for (auto left = wait; left > std::chrono::milliseconds(0) && !wake->woke && !stopping_;
           left -= std::chrono::seconds(1))
        loop_->sleep(std::min<std::chrono::milliseconds>(left, std::chrono::seconds(1)));
      if (std::exchange(wake->woke, false))
        backoff = std::chrono::seconds(1);
      else
        backoff = std::min(backoff * 2, std::chrono::seconds(60));
      continue;
    }
    if (backoff != std::chrono::seconds(1)) {
      backoff = std::chrono::seconds(1);
      log(id_, "syncing again");
      say(connection::online{});
    }
    // Logged in and syncing: UnifiedPush's endpoint given to the server,
    // where there is one it has not been given.
    if (push_endpoint_ && pushed_to_ != push_endpoint_)
      this->register_pusher();
    if (!state_.since) {
      const std::size_t joined = got->rooms && got->rooms->join ? got->rooms->join->size() : 0;
      log(id_, "first sync: {} room{}", joined, joined == 1 ? "" : "s");
    }
    // The rooms whose news the sync cut short, since the last run: where
    // each had got to, to catch up on the gap between.
    std::vector<std::tuple<std::string, std::string, std::string>> gaps;
    if (!first && !sliding_first && got->rooms && got->rooms->join)
      for (const auto& [room, part] : *got->rooms->join)
        if (part.timeline && part.timeline->limited.value_or(false) && part.timeline->prev_batch)
          if (const auto had = state_.joined.find(room); had != state_.joined.end() && !had->second.timeline.empty())
            gaps.emplace_back(room, *part.timeline->prev_batch, had->second.timeline.back().event_id);
    state_.apply(*got);
    tell(*got);
    for (auto& [room, from, until] : gaps)
      this->catch_up(std::move(room), std::move(from), std::move(until));
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

template <class Sink>
auto account<Sink>::kept_file() const -> std::filesystem::path {
  // Named one to one by the account; kept under its name before, moved.
  return config::moved_from(config::state_path(config::file_name_of(id_.address) + ".sync.json"),
                            config::state_path(config::old_file_name_of(id_.address) + ".sync.json"));
}

template <class Sink>
void account<Sink>::save_kept() const {
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
  // Through the vault: the rooms' events and state, sealed where local data
  // is encrypted.
  const std::filesystem::path where = this->kept_file();
  if (!mux::vault::the().write_file(where, knot::to_json_string(out)))
    log(id_, "the sync could not be kept in {}", where.string());
}

template <class Sink>
void account<Sink>::load_kept() {
  this->load_encrypted();
  const auto opened = mux::vault::the().read_file(this->kept_file());
  if (!opened)
    return;
  auto saved = knot::try_read<loom::cs::sync::response>(std::string_view(*opened));
  if (!saved) {
    log(id_, "the sync kept could not be read: starting afresh");
    return;
  }
  state_.apply(*saved);
  tell(*saved);
  log(id_, "the sync kept: {} rooms, going on from there", state_.joined.size());
}

// What an m.presence says, as mux's presence: unavailable is away, and a
// value the spec does not name is taken as offline.
[[nodiscard]] inline mux::presence presence_from(const loom::ev::m_presence_content_t& content) {
  using values = loom::ev::m_presence_content_t::presence_values;
  return {splice::visit(splice::overloaded{[](values::online) -> mux::availability_t { return mux::availability::online{}; },
                                [](values::unavailable) -> mux::availability_t { return mux::availability::away{}; },
                                [](values::offline) -> mux::availability_t { return mux::availability::offline{}; },
                                [](const std::string&) -> mux::availability_t { return mux::availability::offline{}; }},
                     content.presence),
          content.status_msg};
}

template <class Sink>
void account<Sink>::tell(const loom::cs::sync::response& got) {
  // Presence: each m.presence is sent by the user it is about.
  if (got.presence && got.presence->events)
    for (const auto& event : *got.presence->events)
      if (event.sender)
        splice::visit(splice::overloaded{[&](const loom::ev::m_presence_content_t& content) {
                                sink_(change::presence_changed{id_, *event.sender, presence_from(content)});
                              },
                              [](const auto&) {}},
                   event.content.data());
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
      // Where to page back from: the first time a room is seen -- and every
      // time its timeline comes cut short (limited: more was sent than the
      // sync gives), for between what came before and this is a gap the
      // history on disk has to know of (the app marks it).
      if (part.timeline && part.timeline->prev_batch) {
        const bool first = paged_.insert(room).second;
        if (first || part.timeline->limited.value_or(false))
          sink_(change::history_position{in, *part.timeline->prev_batch});
      }
      if (part.timeline)
        for (const auto& one : part.timeline->events)
          event(in, one);
      // Who is typing, but the account itself: its own typing, from this or
      // another device, is not news to it (as in Telegram).
      std::vector<std::string> typing;
      std::ranges::copy_if(found->second.typing, std::back_inserter(typing),
                           [&](const std::string& who) { return who != id_.address; });
      sink_(change::typing_changed{in, std::move(typing)});
      // Receipts: m.receipt's content is event -> kind -> user; the public
      // and the private m.read both say how far someone has read.
      if (part.ephemeral && part.ephemeral->events) {
        std::map<std::string, std::string> read_by;
        std::map<std::string, std::chrono::sys_time<std::chrono::milliseconds>> read_at;
        for (const auto& event : *part.ephemeral->events)
          for (const loom::client::read_receipt& one : loom::client::receipts_of(event)) {
            read_by.insert_or_assign(one.user, one.event_id);
            if (one.ts)
              read_at.insert_or_assign(one.user, std::chrono::sys_time<std::chrono::milliseconds>(
                                                     std::chrono::milliseconds(*one.ts)));
          }
        if (!read_by.empty())
          sink_(change::receipts_changed{in, std::move(read_by), std::move(read_at)});
      }
    }
  // Invites: the room as its stripped state tells of it -- its name,
  // picture, topic, address, whether it is a space -- and who asked, by the
  // user's own m.room.member: its sender, and whether it is a direct chat.
  if (rooms.invite)
    for (const auto& [room, part] : *rooms.invite) {
      change::conversation_updated made{.id = {id_, room}, .kind = conversation_kind::group{}, .name = room};
      mux::invite_info invite;
      std::map<std::string, std::string> names;
      if (const auto kept = state_.invited.find(room); kept != state_.invited.end())
        for (const auto& [key, one] : kept->second)
          splice::visit(
              splice::overloaded{
                  [&](const loom::ev::m_room_name_content_t& c) {
                    if (!c.name.empty())
                      made.name = c.name;
                  },
                  [&](const loom::ev::m_room_avatar_content_t& c) { made.avatar = c.url; },
                  [&](const loom::ev::m_room_topic_content_t& c) {
                    if (!c.topic.empty())
                      made.topic = c.topic;
                  },
                  [&](const loom::ev::m_room_canonical_alias_content_t& c) { made.alias = c.alias; },
                  [&](const loom::ev::m_room_create_content_t& c) {
                    made.space = splice::visit([](auto of) { return of.is_space; }, room_type_of(c.type));
                  },
                  [&](const loom::ev::m_room_member_content_t& c) {
                    if (c.displayname)
                      names.insert_or_assign(key.second, *c.displayname);
                    if (key.second == id_.address) {
                      invite.from = one.sender;
                      invite.direct = c.is_direct.value_or(false);
                    }
                  },
                  [](const auto&) {}},
              one.content.data());
      if (const auto found = names.find(invite.from); found != names.end())
        invite.from_name = found->second;
      if (invite.direct) {
        made.kind = conversation_kind::direct{};
        if (made.name == room)
          made.name = invite.from_name.empty() ? invite.from : invite.from_name;
      }
      made.invite = std::move(invite);
      sink_(std::move(made));
    }
  if (rooms.leave)
    for (const auto& [room, part] : *rooms.leave)
      sink_(change::conversation_removed{{id_, room}});
}

using power_levels_content = loom::ev::m_room_power_levels_content_t;
// Each one's say in a room, as its power levels list them.
inline std::map<std::string, std::int64_t> powers_of(const power_levels_content* content) {
  return content && content->users ? *content->users : std::map<std::string, std::int64_t>{};
}
// From room version 12 on ("hydra", MSC4289) a room's creators -- who sent
// its create event, and those it names besides -- outrank every level, and
// the power levels do not list them: read as the default, the creator of a
// new room had every permission greyed.
inline bool creators_outrank(std::string_view version) {
  if (version.contains("hydra"))
    return true;
  int number = 0;
  const auto [end, failed] = std::from_chars(version.data(), version.data() + version.size(), number);
  return failed == std::errc{} && end == version.data() + version.size() && number >= 12;
}
// When the room's encryption was turned on: its m.room.encryption's time.
inline std::optional<std::chrono::sys_time<std::chrono::milliseconds>> encrypted_since_of(const loom::client::joined_room& kept) {
  const auto found = kept.state.events.find(std::pair<std::string, std::string>{"m.room.encryption", ""});
  if (found == kept.state.events.end())
    return std::nullopt;
  return std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::milliseconds(found->second.origin_server_ts));
}
// Upgraded away, and what the tombstone said; the room it continues.
inline std::optional<std::string> replaced_by_of(const loom::client::joined_room& kept) {
  const auto* stone = kept.state.content<loom::ev::m_room_tombstone_content_t>("m.room.tombstone");
  return stone && !stone->replacement_room.empty() ? std::optional<std::string>(stone->replacement_room) : std::nullopt;
}
inline std::string replaced_why_of(const loom::client::joined_room& kept) {
  const auto* stone = kept.state.content<loom::ev::m_room_tombstone_content_t>("m.room.tombstone");
  return stone ? stone->body : std::string();
}
inline std::optional<std::string> predecessor_of(const loom::client::joined_room& kept) {
  const auto* created = kept.state.content<loom::ev::m_room_create_content_t>("m.room.create");
  return created && created->predecessor ? std::optional<std::string>(created->predecessor->room_id) : std::nullopt;
}
inline std::map<std::string, std::int64_t> powers_in(const loom::client::joined_room& kept) {
  auto out = powers_of(kept.state.content<power_levels_content>("m.room.power_levels"));
  if (!creators_outrank(kept.state.room_version()))
    return out;
  if (const auto created = kept.state.events.find(std::pair<std::string, std::string>{"m.room.create", ""});
      created != kept.state.events.end())
    out.insert_or_assign(created->second.sender, kCreatorPower);
  if (const auto* created = kept.state.content<loom::ev::m_room_create_content_t>("m.room.create");
      created && created->additional_creators)
    for (const std::string& one : *created->additional_creators)
      out.insert_or_assign(one, kCreatorPower);
  return out;
}
// What each thing done in a room asks, as its power levels say: read here,
// where they come in, into what the rest keeps.
inline power_needs needs_of(const power_levels_content* content) {
  power_needs out;
  if (!content)
    return out;
  const auto level = [](const std::optional<std::int64_t>& said, std::int64_t& into) {
    if (said)
      into = *said;
  };
  level(content->users_default, out.users_default);
  level(content->events_default, out.events_default);
  level(content->state_default, out.state_default);
  level(content->invite, out.invite);
  level(content->kick, out.kick);
  level(content->ban, out.ban);
  level(content->redact, out.redact);
  if (content->notifications)
    level(content->notifications->room, out.notify_room);
  if (content->events)
    for (const auto& [kind, needed] : *content->events)
      out.events.emplace(kind, needed);
  return out;
}
// The other addresses a room publishes, besides its canonical one.
inline std::vector<std::string> other_aliases_of(const loom::ev::m_room_canonical_alias_content_t* content) {
  return content && content->alt_aliases ? *content->alt_aliases : std::vector<std::string>{};
}
inline std::int64_t power_default_of(const power_levels_content* content) {
  return content ? content->users_default.value_or(0) : 0;
}

template <class Sink>
auto account<Sink>::avatar_of(const std::string& room, const loom::client::joined_room& kept) const -> std::optional<std::string> {
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

template <class Sink>
auto account<Sink>::name_of(const std::string& room, const loom::client::joined_room& kept) -> std::string {
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

template <class Sink>
auto account<Sink>::direct(const std::string& room) const -> bool {
  // m.direct read once a sync, not once a room: every room of a sync asks.
  if (direct_rooms_since_ != state_.since) {
    direct_rooms_.clear();
    for (const auto& [user, rooms] : loom::client::direct_rooms(state_))
      direct_rooms_.insert(rooms.begin(), rooms.end());
    direct_rooms_since_ = state_.since;
  }
  return direct_rooms_.contains(room);
}

template <class Sink>
void account<Sink>::conversation(const conversation_id& in, const loom::client::joined_room& kept) {
  sink_(change::conversation_updated{.id = in,
                                     .kind = direct(in.id) ? conversation_kind_t{conversation_kind::direct{}} : conversation_kind_t{conversation_kind::group{}},
                                     .name = name_of(in.id, kept),
                                     .avatar = avatar_of(in.id, kept),
                                     .topic = kept.state.topic(),
                                     .encrypted = this->encrypted_room(in.id) || kept.state.encrypted(),
                                     .unread = kept.unread.notification,
                                     .encrypted_since = this->encrypted_by(in.id, encrypted_since_of(kept)),
                                     .highlights = kept.unread.highlight,
                                     .space = space(kept),
                                     .children = children_of(kept),
                                     .member_count = kept.summary.joined_members,
                                     .alias = kept.state.canonical_alias(),
                                     .pinned = pinned_of(kept),
                                     .emotes = emotes_of(kept),
                                     .stickers = emotes_of(kept, true),
                                     .theirs = proto::matrix::room_rules{.join_rule = join_rule_of(rule_text(kept.state.template content<loom::ev::m_room_join_rules_content_t>("m.room.join_rules"),
                                                                         &loom::ev::m_room_join_rules_content_t::join_rule)),
                                     .history = history_rule_of(rule_text(kept.state.template content<loom::ev::m_room_history_visibility_content_t>("m.room.history_visibility"),
                                                                          &loom::ev::m_room_history_visibility_content_t::history_visibility)),
                                     .powers = powers_in(kept),
                                     .power_default = power_default_of(kept.state.template content<power_levels_content>("m.room.power_levels")),
                                     .needs = needs_of(kept.state.template content<power_levels_content>("m.room.power_levels")),
                                     .version = kept.state.room_version(),
                                     .replaced_by = replaced_by_of(kept),
                                     .replaced_why = replaced_why_of(kept),
                                     .predecessor = predecessor_of(kept)},
                                     .other_aliases = other_aliases_of(kept.state.template content<loom::ev::m_room_canonical_alias_content_t>("m.room.canonical_alias"))});
}




// The images of the packs the room offers (MSC2545), as loom finds them --
// the user's own, the room's, and those taken everywhere -- as emoji, or
// asked for stickers, as those.
template <class Sink>
auto account<Sink>::emotes_of(const loom::client::joined_room& kept, bool stickers) const -> std::vector<mux::emote> {
  const loom::client::image_use_t use = stickers ? loom::client::image_use_t{loom::client::image_use::sticker{}}
                                                 : loom::client::image_use_t{loom::client::image_use::emoticon{}};
  std::vector<mux::emote> out;
  for (loom::client::pack_image& one : loom::client::images(state_, kept, use))
    out.push_back({.shortcode = std::move(one.shortcode),
                   .url = std::move(one.url),
                   .body = std::move(one.body),
                   .w = one.w,
                   .h = one.h,
                   .size = one.size,
                   .mimetype = std::move(one.mimetype),
                   .pack = std::move(one.pack),
                   .pack_avatar = std::move(one.pack_avatar)});
  return out;
}

template <class Sink>
auto account<Sink>::emotes_in(const std::string& room) const -> std::vector<mux::emote> {
  if (const auto kept = state_.joined.find(room); kept != state_.joined.end())
    return emotes_of(kept->second);
  return {};
}

// The room's pinned messages: m.room.pinned_events' "pinned", as it says.
template <class Sink>
auto account<Sink>::pinned_of(const loom::client::joined_room& kept) -> std::vector<std::string> {
  std::vector<std::string> out;
  if (const auto* said = kept.state.template content<loom::ev::m_room_pinned_events_content_t>("m.room.pinned_events"))
    out = said->pinned;
  return out;
}

template <class Sink>
auto account<Sink>::space(const loom::client::joined_room& kept) -> bool {
  const auto* created = kept.state.template content<loom::ev::m_room_create_content_t>("m.room.create");
  return created && splice::visit([](auto of) { return of.is_space; }, room_type_of(created->type));
}

template <class Sink>
auto account<Sink>::children_of(const loom::client::joined_room& kept) -> std::vector<std::string> {
  std::vector<std::string> out;
  for (const auto& [key, one] : kept.state.events) {
    if (!splice::visit([](auto of) { return of.child; }, state_type_of(key.first)))
      continue;
    // A child taken out has its content emptied: then it is no longer read
    // as a child's content, which says the servers to reach it by.
    (void)one;
    if (kept.state.template content<loom::ev::m_space_child_content_t>("m.space.child", key.second))
      out.push_back(key.second);
  }
  return out;
}

template <class Sink>
void account<Sink>::members(const conversation_id& in, const loom::client::joined_room& kept) {
  std::map<std::string, mux::member> who;
  if (const auto full = full_members_.find(in.id); full != full_members_.end())
    who = full->second;
  for (const std::string& user : kept.state.members("join")) {
    const auto* joined = kept.state.content<loom::ev::m_room_member_content_t>("m.room.member", user);
    who.insert_or_assign(user, mux::member{user, kept.state.display_name(user).value_or(user), std::nullopt,
                                           joined ? joined->avatar_url : std::nullopt});
  }
  for (const char* gone : {"leave", "ban", "knock"})
    for (const std::string& user : kept.state.members(gone))
      who.erase(user);
  std::vector<mux::member> out;
  out.reserve(who.size());
  for (auto& [id, one] : who)
    out.push_back(std::move(one));
  // Those knocking: their name and their reason, as their member event says.
  std::vector<mux::knock_request> knocking;
  for (const std::string& user : kept.state.members("knock")) {
    const auto* asked = kept.state.content<loom::ev::m_room_member_content_t>("m.room.member", user);
    knocking.push_back({user, kept.state.display_name(user).value_or(user), asked && asked->reason ? *asked->reason : std::string()});
  }
  sink_(change::members_changed{in, std::move(out), std::move(knocking)});
}

// End-to-end encryption.
//
// The machine of this device, once it has one: read from its store, else
// made, and its keys put on the server -- the device's identity keys once,
// signed, and one-time keys up to half of what it may hold.
template <class Sink>
void account<Sink>::start_crypto() {
  if (crypto_ || !how_.device_id || how_.crypto_store.empty())
    return;
  try {
    crypto_.emplace(crypto::olm_machine::open(how_.crypto_store, id_.address, *how_.device_id));
  } catch (const std::exception& failed) {
    log(id_, "encryption not started: {}", failed.what());
    return;
  }
  log(id_, "encryption: this device's curve25519 key is {}", crypto_->curve25519());
  sink_(change::own_session{id_, crypto_->device_id(), crypto_->ed25519()});
  this->upload_keys(0);
  this->check_own_sessions();
}

// Keys put on the server: the device's, the first time; one-time keys where
// it has fewer than half of what it may hold.
template <class Sink>
void account<Sink>::upload_keys(std::int64_t on_server) {
  if (!crypto_ || !api_)
    return;
  const bool first = !crypto_->device_keys_uploaded();
  const auto now = std::chrono::steady_clock::now();
  if (!first && keys_uploaded_at_ && now - *keys_uploaded_at_ < std::chrono::minutes(1))
    return;
  keys_uploaded_at_ = now;
  loom::cs::upload_keys ask;
  if (first)
    if (auto signed_keys = crypto_->signed_device_keys())
      ask.body.device_keys = loom::cs::upload_keys::body_t::device_keys_t{
          .user_id = signed_keys->keys.user_id,
          .device_id = signed_keys->keys.device_id,
          .algorithms = signed_keys->keys.algorithms,
          .keys = signed_keys->keys.keys,
          .signatures = {{id_.address, {{"ed25519:" + signed_keys->keys.device_id, signed_keys->signature}}}}};
  if (auto made = crypto_->one_time_keys(on_server); !made.empty())
    ask.body.one_time_keys = std::move(made);
  if (!ask.body.device_keys && !ask.body.one_time_keys)
    return;
  auto done = perform(*api_, ask);
  if (!done) {
    log(id_, "keys not uploaded: {}", done.error().said());
    return;
  }
  try {
    crypto_->published(ask.body.device_keys.has_value());
  } catch (const std::exception& failed) {
    log(id_, "encryption stopped: {}", failed.what());
    crypto_.reset();
    return;
  }
  log(id_, "keys uploaded{}", ask.body.device_keys ? ", the device's with them" : "");
}

// ---- Emoji verification (SAS) ----------------------------------------------
//
// Over to-device messages, as Element does between devices: request, ready,
// start, accept, key both ways, the emoji compared by the user on both
// sides, MACs of each side's keys, done. A device is taken as verified only
// once the user said the emoji match and the other side's MAC of its own
// ed25519 key checks against the key /keys/query gives: a server that
// swapped keys is caught there (and could not have made the emoji match).
inline constexpr std::string_view kSasInfo = "MATRIX_KEY_VERIFICATION_SAS|";
inline constexpr std::string_view kMacInfo = "MATRIX_KEY_VERIFICATION_MAC";

template <class Sink>
bool account<Sink>::send_plain(std::string type, const std::string& user, const std::string& device, knot::raw content) {
  if (!api_)
    return false;
  std::map<std::string, std::map<std::string, knot::raw>> messages;
  messages[user][device] = std::move(content);
  return perform(*api_, loom::cs::send_to_device{.event_type = std::move(type),
                                                 .txn_id = this->transaction(),
                                                 .body = {.messages = std::move(messages)}})
      .has_value();
}
template <class Sink>
void account<Sink>::verification_said(const crypto::sas_state& state, verification_step_t step) {
  sink_(proto::matrix::verification_changed{id_, state.txn, state.their_user, state.their_device, std::move(step)});
}
template <class Sink>
void account<Sink>::cancel_verification(const std::string& txn, std::string code, std::string reason) {
  const auto found = verifications_.find(txn);
  if (found == verifications_.end())
    return;
  const crypto::sas_state& state = found->second;
  this->send_step(state, "m.key.verification.cancel",
                  loom::ev::m_key_verification_cancel_content_t{.reason = reason, .code = std::move(code)});
  this->verification_said(state, verification_step::cancelled{std::move(reason)});
  verifications_.erase(found);
}

template <class Sink>
void account<Sink>::verify_start(std::string user, std::optional<std::string> device) {
  this->spawn_guarded([this, user = std::move(user), device = std::move(device)] {
    if (!crypto_)
      return;
    crypto::sas_state state{.txn = this->transaction(), .their_user = user, .we_requested = true};
    const auto now =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const loom::ev::m_key_verification_request_content_t asked{
        .from_device = crypto_->device_id(), .transaction_id = state.txn, .methods = {"m.sas.v1"}, .timestamp = now};
    if (!this->send_plain("m.key.verification.request", user, device.value_or("*"), knot::raw{knot::to_json_string(asked)})) {
      this->verification_said(state, verification_step::cancelled{"The request could not be sent."});
      return;
    }
    this->verification_said(state, verification_step::waiting{});
    verifications_.insert_or_assign(state.txn, std::move(state));
  });
}
template <class Sink>
void account<Sink>::verification_in(const std::string& sender, const loom::ev::m_key_verification_request_content_t& content) {
  if (!crypto_ || !content.transaction_id || (sender == id_.address && content.from_device == crypto_->device_id()) ||
      !std::ranges::contains(content.methods, std::string_view("m.sas.v1")))
    return;
  // One asked of this side at a time: anyone may send requests, and each
  // would put the dialog up again.
  if (std::ranges::any_of(verifications_, [](const auto& one) { return !one.second.we_requested; }))
    return;
  crypto::sas_state state{.txn = *content.transaction_id, .their_user = sender, .their_device = content.from_device};
  this->verification_said(state, verification_step::asked{});
  verifications_.insert_or_assign(state.txn, std::move(state));
}
template <class Sink>
void account<Sink>::verify_accept(std::string txn) {
  this->spawn_guarded([this, txn = std::move(txn)] {
    const auto found = verifications_.find(txn);
    if (found == verifications_.end() || !crypto_)
      return;
    const loom::ev::m_key_verification_ready_content_t ready{
        .from_device = crypto_->device_id(), .transaction_id = txn, .methods = {"m.sas.v1"}};
    this->send_step(found->second, "m.key.verification.ready", ready);
    this->verification_said(found->second, verification_step::waiting{});
  });
}
template <class Sink>
void account<Sink>::verification_in(const std::string& sender, const loom::ev::m_key_verification_ready_content_t& content) {
  const auto found = content.transaction_id ? verifications_.find(*content.transaction_id) : verifications_.end();
  if (found == verifications_.end() || !found->second.we_requested || found->second.their_user != sender ||
      !found->second.their_device.empty())
    return;
  found->second.their_device = content.from_device;
  this->sas_start(found->second);
}
template <class Sink>
void account<Sink>::sas_start(crypto::sas_state& state) {
  using start_t = loom::ev::m_key_verification_start_m_sas_v1_content_t;
  using sas_kind = start_t::short_authentication_string_item_values;
  state.begin();
  const start_t start{.from_device = crypto_->device_id(),
                      .transaction_id = state.txn,
                      .method = start_t::method_values::m_sas_v1{},
                      .key_agreement_protocols = {"curve25519-hkdf-sha256"},
                      .hashes = {"sha256"},
                      .message_authentication_codes = {"hkdf-hmac-sha256.v2"},
                      .short_authentication_string = {sas_kind::decimal{}, sas_kind::emoji{}}};
  // Committed to as it is sent: with its transport's stamp.
  const auto canonical = knot::to_canonical_json(this->stamped(state, start));
  if (!canonical)
    return this->cancel_verification(state.txn, "m.unexpected_message", "The start could not be written.");
  state.start_canonical = *canonical;
  state.we_started = true;
  this->send_step(state, "m.key.verification.start", start);
}
template <class Sink>
void account<Sink>::verification_in(const std::string& sender, const loom::ev::m_key_verification_start_content_t& content) {
  const auto found = content.transaction_id ? verifications_.find(*content.transaction_id) : verifications_.end();
  if (found == verifications_.end() || found->second.their_user != sender || found->second.their_device != content.from_device)
    return;
  crypto::sas_state& state = found->second;
  // Both sides started: the one whose user and device sort first keeps its.
  if (state.we_started && std::pair(id_.address, crypto_->device_id()) < std::pair(sender, content.from_device))
    return;
  const auto offer = knot::try_read<crypto::sas_offer>(content.rest.text);
  if (content.method != "m.sas.v1" || !offer || !crypto::speaks(*offer))
    return this->cancel_verification(state.txn, "m.unknown_method", "Only emoji verification is spoken here.");
  // Committed to as its sender wrote it: in a room, with its reference (out
  // of the ciphertext where the room is encrypted) and no transaction ID.
  auto as_sent = content;
  if (state.room) {
    as_sent.transaction_id.reset();
    if (!as_sent.m_relates_to)
      as_sent.m_relates_to = loom::ev::def::verification_relates_to_t{
          .rel_type = loom::ev::def::verification_relates_to_t::rel_type_values::m_reference{}, .event_id = state.txn};
  }
  const auto canonical = knot::to_canonical_json(as_sent);
  if (!canonical)
    return this->cancel_verification(state.txn, "m.unexpected_message", "The start could not be read.");
  state.start_canonical = *canonical;
  state.we_started = false;
  state.begin();
  using accept_t = loom::ev::m_key_verification_accept_content_t;
  using sas_kind = accept_t::short_authentication_string_item_values;
  const accept_t accept{.transaction_id = state.txn,
                        .key_agreement_protocol = "curve25519-hkdf-sha256",
                        .hash = "sha256",
                        .message_authentication_code = "hkdf-hmac-sha256.v2",
                        .short_authentication_string = {sas_kind::decimal{}, sas_kind::emoji{}},
                        .commitment = crypto::commitment_of(state.our_key, state.start_canonical)};
  this->send_step(state, "m.key.verification.accept", accept);
  this->verification_said(state, verification_step::waiting{});
}
template <class Sink>
void account<Sink>::verification_in(const std::string& sender, const loom::ev::m_key_verification_accept_content_t& content) {
  const auto found = content.transaction_id ? verifications_.find(*content.transaction_id) : verifications_.end();
  if (found == verifications_.end() || found->second.their_user != sender || !found->second.we_started)
    return;
  found->second.commitment = content.commitment;
  this->send_step(found->second, "m.key.verification.key", loom::ev::m_key_verification_key_content_t{.key = found->second.our_key});
}
template <class Sink>
void account<Sink>::verification_in(const std::string& sender, const loom::ev::m_key_verification_key_content_t& content) {
  const auto found = content.transaction_id ? verifications_.find(*content.transaction_id) : verifications_.end();
  if (found == verifications_.end() || found->second.their_user != sender || found->second.their_key)
    return;
  crypto::sas_state& state = found->second;
  // Where this side started: their key is the one they committed to before
  // seeing ours -- else they chose it to make the emoji come out.
  if (state.we_started && (!state.commitment || crypto::commitment_of(content.key, state.start_canonical) != *state.commitment))
    return this->cancel_verification(state.txn, "m.mismatched_commitment", "Their key is not the one they committed to.");
  if (!state.establish(content.key))
    return this->cancel_verification(state.txn, "m.key_mismatch", "Their key is not a key.");
  if (!state.we_started)
    this->send_step(state, "m.key.verification.key", loom::ev::m_key_verification_key_content_t{.key = state.our_key});
  this->sas_show(state);
}
template <class Sink>
void account<Sink>::sas_show(crypto::sas_state& state) {
  const std::string ours = std::format("{}|{}|{}", id_.address, crypto_->device_id(), state.our_key);
  const std::string theirs = std::format("{}|{}|{}", state.their_user, state.their_device, *state.their_key);
  const std::string info = std::string(kSasInfo) + (state.we_started ? ours + "|" + theirs : theirs + "|" + ours) + "|" + state.txn;
  this->verification_said(state, verification_step::compare{state.emoji(info)});
}
template <class Sink>
void account<Sink>::verify_confirm(std::string txn, bool match) {
  this->spawn_guarded([this, txn = std::move(txn), match] {
    const auto found = verifications_.find(txn);
    if (found == verifications_.end() || !found->second.established)
      return;
    if (!match)
      return this->cancel_verification(txn, "m.mismatched_sas", "The emoji did not match.");
    found->second.we_confirmed = true;
    this->sas_send_mac(found->second);
    if (found->second.their_mac)
      this->sas_check_mac(found->second);
    else
      this->verification_said(found->second, verification_step::waiting{});
  });
}
template <class Sink>
void account<Sink>::sas_send_mac(crypto::sas_state& state) {
  const std::string base = std::string(kMacInfo) + id_.address + crypto_->device_id() + state.their_user + state.their_device + state.txn;
  std::map<std::string, std::string> macs;
  const std::string device_key = "ed25519:" + crypto_->device_id();
  macs.emplace(device_key, state.mac(crypto_->ed25519(), base + device_key));
  if (const auto master = crypto_->pinned_master(id_.address)) {
    const std::string master_key = "ed25519:" + *master;
    macs.emplace(master_key, state.mac(*master, base + master_key));
  }
  const std::string ids = macs | std::views::keys | std::views::join_with(',') | std::ranges::to<std::string>();
  this->send_step(state, "m.key.verification.mac",
                  loom::ev::m_key_verification_mac_content_t{.mac = std::move(macs), .keys = state.mac(ids, base + "KEY_IDS")});
}
template <class Sink>
void account<Sink>::verification_in(const std::string& sender, const loom::ev::m_key_verification_mac_content_t& content) {
  const auto found = content.transaction_id ? verifications_.find(*content.transaction_id) : verifications_.end();
  if (found == verifications_.end() || found->second.their_user != sender || !found->second.established)
    return;
  found->second.their_mac = content.mac;
  found->second.their_keys_mac = content.keys;
  if (found->second.we_confirmed)
    this->sas_check_mac(found->second);
}
template <class Sink>
void account<Sink>::sas_check_mac(crypto::sas_state& state) {
  const std::string txn = state.txn;
  const std::string base = std::string(kMacInfo) + state.their_user + state.their_device + id_.address + crypto_->device_id() + txn;
  const std::string ids = *state.their_mac | std::views::keys | std::views::join_with(',') | std::ranges::to<std::string>();
  if (!state.mac_ok(ids, base + "KEY_IDS", state.their_keys_mac))
    return this->cancel_verification(txn, "m.key_mismatch", "The keys they listed are not the ones they sent.");
  if (!api_)
    return this->cancel_verification(txn, "m.key_mismatch", "Not connected.");
  loom::cs::query_keys ask;
  ask.body.device_keys.emplace(state.their_user, std::vector<std::string>{});
  auto got = perform(*api_, ask);
  if (!got || !got->device_keys)
    return this->cancel_verification(txn, "m.key_mismatch", "Their keys could not be fetched.");
  const auto user = got->device_keys->find(state.their_user);
  if (user == got->device_keys->end())
    return this->cancel_verification(txn, "m.key_mismatch", "Their devices are not listed.");
  const auto device = user->second.find(state.their_device);
  if (device == user->second.end())
    return this->cancel_verification(txn, "m.key_mismatch", "Their device is not listed.");
  const auto ed25519 = device->second.keys.find("ed25519:" + state.their_device);
  if (ed25519 == device->second.keys.end())
    return this->cancel_verification(txn, "m.key_mismatch", "Their device has no signing key.");
  const auto master = crypto::master_of(*got, state.their_user);
  bool device_ok = false, master_ok = false;
  for (const auto& [key_id, mac] : *state.their_mac) {
    if (key_id == "ed25519:" + state.their_device)
      device_ok = state.mac_ok(ed25519->second, base + key_id, mac);
    else if (master && key_id == "ed25519:" + *master)
      master_ok = state.mac_ok(*master, base + key_id, mac);
  }
  if (!device_ok)
    return this->cancel_verification(txn, "m.key_mismatch", "Their device's key is not the one the server lists.");
  crypto_->mark_verified(state.their_user, ed25519->second);
  if (master_ok)
    crypto_->verify_master(state.their_user, *master);
  // Signed, where this device has the cross-signing keys: one's own device
  // with the self-signing key, another's master key with the user-signing one.
  if (state.their_user == id_.address)
    this->cross_sign_device(device->second);
  else if (master_ok && got->master_keys)
    if (const auto theirs = got->master_keys->find(state.their_user); theirs != got->master_keys->end())
      this->cross_sign_user(state.their_user, theirs->second);
  this->send_step(state, "m.key.verification.done", loom::ev::m_key_verification_done_content_t{});
  this->verification_said(state, verification_step::done{});
  this->tell_trust(state.their_user);
  // One's own other session, verified: the cross-signing keys asked of it,
  // where this device has none -- with them it signs itself.
  if (state.their_user == id_.address && !crypto_->cross_signing_keys())
    this->request_secrets(state.their_device);
  verifications_.erase(txn);
}
template <class Sink>
void account<Sink>::verification_in(const std::string& sender, const loom::ev::m_key_verification_cancel_content_t& content) {
  const auto found = content.transaction_id ? verifications_.find(*content.transaction_id) : verifications_.end();
  if (found == verifications_.end() || found->second.their_user != sender)
    return;
  this->verification_said(found->second, verification_step::cancelled{content.reason.empty() ? content.code : content.reason});
  verifications_.erase(found);
}
// In a room: the request, a message to this user from a device of theirs.
template <class Sink>
void account<Sink>::verification_request_in_room(const conversation_id& in, const loom::ev::timeline_event& one,
                                                 const crypto::room_request_fields& fields) {
  if (!crypto_ || fields.to != id_.address || one.sender == id_.address ||
      !std::ranges::contains(fields.methods, std::string_view("m.sas.v1")) ||
      std::ranges::any_of(verifications_, [](const auto& each) { return !each.second.we_requested; }))
    return;
  crypto::sas_state state{.txn = one.event_id, .room = in.id, .their_user = one.sender, .their_device = fields.from_device};
  this->verification_said(state, verification_step::asked{});
  verifications_.insert_or_assign(state.txn, std::move(state));
}
// Its steps: read by their type as the to-device ones are, the request they
// refer to standing for the transaction. Shown nowhere in the timeline.
template <class Sink>
bool account<Sink>::verification_in_room(const conversation_id& in, const loom::ev::timeline_event& one, const knot::raw& raw,
                                         placement_t where) {
  const auto kind = verification_kind_of(one.type);
  const bool step = splice::visit(splice::overloaded{[](verification_kind::none) { return false; }, [](const auto&) { return true; }}, kind);
  if (!step)
    return false;
  const bool live = splice::visit(splice::overloaded{[](placement::at_end) { return true; }, [](const auto&) { return false; }}, where);
  if (!live || one.sender == id_.address || !crypto_)
    return true;
  // The step, read as its type's content, its reference made its transaction
  // -- for the room it came in only.
  const auto take = [&]<class Content>(type_tag<Content>) {
    auto content = knot::try_read<Content>(raw.text);
    if (!content)
      return;
    const auto reference = content->m_relates_to && content->m_relates_to->event_id ? content->m_relates_to->event_id
                                                                                     : outer_reference_;
    if (!reference)
      return;
    const auto found = verifications_.find(*reference);
    if (found == verifications_.end() || found->second.room != in.id)
      return;
    content->transaction_id = found->first;
    this->verification_in(one.sender, *content);
  };
  splice::visit(splice::overloaded{[](verification_kind::none) {},
                                   [&](verification_kind::ready) { take(type_tag<loom::ev::m_key_verification_ready_content_t>{}); },
                                   [&](verification_kind::start) { take(type_tag<loom::ev::m_key_verification_start_content_t>{}); },
                                   [&](verification_kind::accept) { take(type_tag<loom::ev::m_key_verification_accept_content_t>{}); },
                                   [&](verification_kind::key) { take(type_tag<loom::ev::m_key_verification_key_content_t>{}); },
                                   [&](verification_kind::mac) { take(type_tag<loom::ev::m_key_verification_mac_content_t>{}); },
                                   [&](verification_kind::cancel) { take(type_tag<loom::ev::m_key_verification_cancel_content_t>{}); },
                                   [](verification_kind::done) {}},
                kind);
  return true;
}

template <class Sink>
void account<Sink>::verify_cancel(std::string txn) {
  this->spawn_guarded([this, txn = std::move(txn)] { this->cancel_verification(txn, "m.user", "Cancelled."); });
}

template <class Sink>
void account<Sink>::mend_session(const std::string& user, const std::string& curve25519) {
  if (!api_ || !crypto_)
    return;
  const auto now = std::chrono::steady_clock::now();
  if (const auto last = mended_at_.find(curve25519); last != mended_at_.end() && now - last->second < std::chrono::hours(1))
    return;
  mended_at_.insert_or_assign(curve25519, now);
  loom::cs::query_keys ask;
  ask.body.device_keys.emplace(user, std::vector<std::string>{});
  auto got = perform(*api_, ask);
  if (!got)
    return;
  const auto theirs = crypto::recipients_of(*got, user, crypto_->pinned_master(user), std::string_view(), crypto_->verified_keys(user));
  const auto device = std::ranges::find(theirs, curve25519, &crypto::recipient::curve25519);
  if (device == theirs.end())
    return;  // not a device of theirs that passes the checks: nothing sent to it
  loom::cs::claim_keys claim;
  claim.body.one_time_keys[user][device->device_id] = "signed_curve25519";
  auto claimed = perform(*api_, claim);
  if (!claimed)
    return;
  std::optional<std::string> key;
  if (const auto by_user = claimed->one_time_keys.find(user); by_user != claimed->one_time_keys.end())
    if (const auto by_device = by_user->second.find(device->device_id); by_device != by_user->second.end())
      for (const auto& [id, raw] : by_device->second)
        if (!key)
          key = crypto::one_time_key_of(raw, *device);
  if (!key)
    return;
  const auto sealed = crypto_->mend(*device, *key);
  if (!sealed)
    return;
  std::map<std::string, std::map<std::string, knot::raw>> messages;
  messages[user][device->device_id] = knot::raw{
      knot::to_json_string(crypto::olm_content{.sender_key = crypto_->curve25519(), .ciphertext = {{curve25519, *sealed}}})};
  if (perform(*api_, loom::cs::send_to_device{.event_type = "m.room.encrypted", .txn_id = this->transaction(),
                                              .body = {.messages = std::move(messages)}}))
    log(id_, "a broken session with {}'s device {} mended", user, device->device_id);
}

template <class Sink>
void account<Sink>::upload_fallback_key() {
  if (!crypto_ || !api_)
    return;
  const auto now = std::chrono::steady_clock::now();
  if (fallback_uploaded_at_ && now - *fallback_uploaded_at_ < std::chrono::hours(1))
    return;
  fallback_uploaded_at_ = now;
  loom::cs::upload_keys ask;
  ask.body.fallback_keys = crypto_->fresh_fallback_key();
  if (ask.body.fallback_keys->empty())
    return;
  if (auto done = perform(*api_, ask); !done) {
    log(id_, "fallback key not uploaded: {}", done.error().said());
    return;
  }
  crypto_->published(false);
  log(id_, "fallback key uploaded");
}

// What a sliding sync's answer brought for this device: to-device messages
// -- Olm, carrying room keys -- and how many one-time keys the server holds.
template <class Sink>
void account<Sink>::crypto_answer(const loom::cs::sliding_sync::response_t& got) {
  if (!crypto_ || !got.extensions)
    return;
  try {
    this->crypto_answer_now(got);
  } catch (const std::exception& failed) {
    log(id_, "encryption stopped: {}", failed.what());
    crypto_.reset();
  }
}
template <class Sink>
void account<Sink>::crypto_answer_now(const loom::cs::sliding_sync::response_t& got) {
  // What the last answer's messages left unsaved, saved first.
  crypto_->flush();
  const auto& extensions = *got.extensions;
  if (extensions.to_device) {
    if (extensions.to_device->events)
      for (const auto& one : *extensions.to_device->events)
        splice::visit(splice::overloaded{[&](const loom::ev::m_room_encrypted_content_t& content) {
                                           if (auto said = crypto_->to_device(one.sender.value_or(""), content))
                                             splice::visit(splice::overloaded{[&](const crypto::room_key_offer& offer) { this->vet_room_key(offer); },
                                                                              [&](const crypto::secret_got& got) { this->secret_in(got); }},
                                                           *said);
                                         },
                                         [&](const loom::ev::m_room_key_withheld_content_t& content) { this->withheld_in(content); },
                                         [&](const loom::ev::m_secret_request_content_t& content) {
                                           this->secret_request_in(one.sender.value_or(""), content);
                                         },
                                         [&](const loom::ev::m_key_verification_request_content_t& content) {
                                           this->verification_in(one.sender.value_or(""), content);
                                         },
                                         [&](const loom::ev::m_key_verification_ready_content_t& content) {
                                           this->verification_in(one.sender.value_or(""), content);
                                         },
                                         [&](const loom::ev::m_key_verification_start_content_t& content) {
                                           this->verification_in(one.sender.value_or(""), content);
                                         },
                                         [&](const loom::ev::m_key_verification_accept_content_t& content) {
                                           this->verification_in(one.sender.value_or(""), content);
                                         },
                                         [&](const loom::ev::m_key_verification_key_content_t& content) {
                                           this->verification_in(one.sender.value_or(""), content);
                                         },
                                         [&](const loom::ev::m_key_verification_mac_content_t& content) {
                                           this->verification_in(one.sender.value_or(""), content);
                                         },
                                         [&](const loom::ev::m_key_verification_cancel_content_t& content) {
                                           this->verification_in(one.sender.value_or(""), content);
                                         },
                                         [](const auto&) {}},
                      one.content.data());
    for (const auto& [user, curve] : crypto_->take_wedged())
      this->mend_session(user, curve);
    crypto_->went_on_to(extensions.to_device->next_batch);
    // Room keys that came, into the key backup.
    this->upload_backup();
  }
  // No unused fallback key on the server (none, or one used): a new one.
  if (extensions.e2ee && extensions.e2ee->device_unused_fallback_key_types &&
      !std::ranges::contains(*extensions.e2ee->device_unused_fallback_key_types, std::string_view("signed_curve25519")))
    this->upload_fallback_key();
  if (extensions.e2ee && extensions.e2ee->device_one_time_keys_count)
    if (const auto left = extensions.e2ee->device_one_time_keys_count->find("signed_curve25519");
        left != extensions.e2ee->device_one_time_keys_count->end())
      this->upload_keys(left->second);
}

template <class Sink>
void account<Sink>::export_room_keys(std::string path, std::string passphrase) {
  this->spawn_guarded([this, path = std::move(path), passphrase = std::move(passphrase)] {
    if (!crypto_) {
      sink_(change::refused{id_, "Not exported: encryption is not running for this account."});
      return;
    }
    const auto sessions = crypto_->export_sessions();
    const std::string text = crypto::export_file(sessions, passphrase);
    std::error_code failed;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), failed);
    { std::ofstream(path, std::ios::binary | std::ios::trunc); }
    std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace, failed);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    if (!out.flush()) {
      sink_(change::refused{id_, std::format("Not exported: {} could not be written.", path)});
      return;
    }
    sink_(change::notice{id_, "Room keys exported",
                         std::format("{} room keys written to {}, sealed under the passphrase.", sessions.size(), path)});
  });
}
template <class Sink>
void account<Sink>::import_room_keys(std::string path, std::string passphrase) {
  this->spawn_guarded([this, path = std::move(path), passphrase = std::move(passphrase)] {
    if (!crypto_) {
      sink_(change::refused{id_, "Not imported: encryption is not running for this account."});
      return;
    }
    std::ifstream in(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!in && text.empty()) {
      sink_(change::refused{id_, std::format("Not imported: {} could not be read.", path)});
      return;
    }
    const auto sessions = crypto::import_file(text, passphrase);
    if (!sessions) {
      sink_(change::refused{id_, "Not imported: not a key file, the passphrase is another, or the file was changed."});
      return;
    }
    const std::size_t taken = crypto_->import_sessions(*sessions);
    sink_(change::notice{id_, "Room keys imported",
                         std::format("{} of {} room keys taken (the rest were held already). Messages read with them are "
                                     "marked as from an unverified device: the file is only as good as where it came from.",
                                     taken, sessions->size())});
  });
}

template <class Sink>
bool account<Sink>::owns_key(const std::string& user, const std::string& curve25519) {
  const auto key = std::pair(user, curve25519);
  if (const auto known = owns_key_.find(key); known != owns_key_.end())
    return known->second;
  // Not connected (a kept sync read at the start): not known yet, so not
  // shown -- asked once there is a server to ask.
  if (!api_)
    return false;
  loom::cs::query_keys ask;
  ask.body.device_keys.emplace(user, std::vector<std::string>{});
  auto got = perform(*api_, ask);
  if (!got)
    return false;  // not known now: asked again next time
  const bool owns = crypto::device_of(*got, user, curve25519, crypto_ ? crypto_->pinned_master(user) : std::nullopt).has_value();
  owns_key_.insert_or_assign(key, owns);
  return owns;
}

// The room's readers now -- its joined members' devices that pass the
// checks (recipients_of) -- and its session given to those that have not
// got it: over an Olm session where there is one, else one made from a
// one-time key claimed for the device and signed by it.
template <class Sink>
std::optional<std::string> account<Sink>::share_room_key(const std::string& room) {
  if (!api_ || !crypto_)
    return std::nullopt;
  auto members = perform(*api_, loom::cs::get_joined_members_by_room{.room_id = room});
  if (!members || !members->joined) {
    log(id_, "{}: its members could not be fetched", room);
    return std::nullopt;
  }
  loom::cs::query_keys ask;
  for (const auto& [user, profile] : *members->joined)
    ask.body.device_keys.emplace(user, std::vector<std::string>{});
  auto got = perform(*api_, ask);
  if (!got) {
    log(id_, "{}: its members' devices could not be fetched: {}", room, got.error().said());
    return std::nullopt;
  }
  std::vector<crypto::recipient> readers;
  for (const auto& [user, profile] : *members->joined) {
    if (!crypto_->pinned_master(user))
      if (const auto master = crypto::master_of(*got, user))
        crypto_->pin_master(user, *master);
    auto theirs = crypto::recipients_of(*got, user, crypto_->pinned_master(user),
                                        user == id_.address ? std::string_view(crypto_->device_id()) : std::string_view(),
                                        crypto_->verified_keys(user), user == id_.address || how_.only_verified);
    readers.insert(readers.end(), std::make_move_iterator(theirs.begin()), std::make_move_iterator(theirs.end()));
  }
  crypto::rotation limits;
  if (const auto kept = state_.joined.find(room); kept != state_.joined.end())
    if (const auto* how = kept->second.state.template content<loom::ev::m_room_encryption_content_t>("m.room.encryption"))
      limits = crypto::rotation_of(how->rotation_period_ms, how->rotation_period_msgs);
  const auto now_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  const auto plan = crypto_->outbound_for(room, readers, limits, now_ms);
  if (plan.to_share.empty())
    return plan.session_id;
  // One-time keys for the devices there is no session with yet.
  loom::cs::claim_keys claim;
  for (const crypto::recipient& one : plan.to_share)
    if (!crypto_->has_session(one.curve25519))
      claim.body.one_time_keys[one.user][one.device_id] = "signed_curve25519";
  std::map<std::string, std::string> keys_by_curve;
  if (!claim.body.one_time_keys.empty())
    if (auto claimed = perform(*api_, claim))
      for (const crypto::recipient& one : plan.to_share)
        if (const auto user = claimed->one_time_keys.find(one.user); user != claimed->one_time_keys.end())
          if (const auto device = user->second.find(one.device_id); device != user->second.end())
            for (const auto& [id, key] : device->second)
              if (auto checked = crypto::one_time_key_of(key, one))
                keys_by_curve.insert_or_assign(one.curve25519, std::move(*checked));
  std::map<std::string, std::map<std::string, knot::raw>> messages;
  std::vector<crypto::recipient> given;
  for (const crypto::recipient& one : plan.to_share) {
    const auto key = keys_by_curve.find(one.curve25519);
    auto sealed = crypto_->room_key_for(one, room, plan,
                                        key == keys_by_curve.end() ? std::nullopt : std::optional<std::string>(key->second));
    if (!sealed) {
      log(id_, "{}: no session with {}'s device {}, which does not get the key", room, one.user, one.device_id);
      continue;
    }
    messages[one.user][one.device_id] = knot::raw{
        knot::to_json_string(crypto::olm_content{.sender_key = crypto_->curve25519(), .ciphertext = {{one.curve25519, *sealed}}})};
    given.push_back(one);
  }
  if (messages.empty())
    return plan.session_id;
  auto sent = perform(*api_, loom::cs::send_to_device{.event_type = "m.room.encrypted",
                                                      .txn_id = this->transaction(),
                                                      .body = {.messages = std::move(messages)}});
  if (!sent) {
    log(id_, "{}: the room's key could not be sent: {}", room, sent.error().said());
    return std::nullopt;
  }
  for (const crypto::recipient& one : given)
    crypto_->shared(room, one);
  return plan.session_id;
}

// A room key offered: the sender's devices asked of the server, and the key
// taken only where the Olm session it came over is one of them, signed by
// itself, and the ed25519 key the payload claimed is that device's. The
// to-device sender is the server's word: a server (or a device it made up)
// otherwise hands over a session of its own as anyone's (review 4, H1).
template <class Sink>
void account<Sink>::vet_room_key(const crypto::room_key_offer& offer) {
  const std::string& user = offer.from.sender;
  auto got = perform(*api_, loom::cs::query_keys{.body = {.device_keys = {{user, {}}}}});
  if (!got) {
    log(id_, "room key from {} not taken: their devices could not be fetched: {}", user, got.error().said());
    return;
  }
  const auto device = crypto::device_of(*got, user, offer.from.sender_key, crypto_->pinned_master(user));
  if (!device) {
    log(id_, "room key from {} refused: no device of theirs, signed by itself, has the key it came with", user);
    return;
  }
  if (device->master && !crypto_->pinned_master(user))
    crypto_->pin_master(user, *device->master);
  // Their master key is not the one first seen: said once, plainly -- a
  // server that swaps it is making their devices its own (review 6).
  if (const auto pinned = crypto_->pinned_master(user);
      device->master && pinned && *pinned != *device->master && identity_changed_.emplace(user).second)
  {
    sink_(change::refused{id_, std::format("{}'s encryption identity changed. Their messages are marked as from an "
                                           "unverified device until it is verified.",
                                           user)});
    this->tell_trust(user);
  }
  // Cross-signed, or verified here by comparing emoji.
  crypto::device_identity trusted = *device;
  trusted.cross_signed = device->cross_signed || std::ranges::contains(crypto_->verified_keys(user), device->ed25519);
  if (!crypto_->accept_room_key(offer, trusted)) {
    log(id_, "room key from {} ({}) not taken", user, device->device_id);
    return;
  }
  log(id_, "room key from {} ({}){}", user, device->device_id, device->cross_signed ? "" : ", unverified device");
}

// Encrypted, once known so: by its state now, or by what was known before --
// kept beside the sync, and in the E2EE store where there is one, so that a
// server hiding m.room.encryption later, or at the next start, does not make
// it plain.
template <class Sink>
bool account<Sink>::encrypted_room(std::string_view room) {
  if (encrypted_rooms_.contains(room))
    return true;
  bool now = false;
  if (const auto kept = state_.joined.find(std::string(room)); kept != state_.joined.end()) {
    now = kept->second.state.encrypted();
    // The room an encrypted room was upgraded into: encrypted too, whatever
    // its state says -- a server dropping m.room.encryption from the new
    // room would otherwise have its first messages in the clear.
    if (const auto before = predecessor_of(kept->second); !now && before && encrypted_rooms_.contains(*before))
      now = true;
  }
  if (!now && crypto_)
    now = crypto_->was_encrypted(room);
  if (now)
    this->remember_encrypted(room);
  return now;
}
template <class Sink>
void account<Sink>::remember_encrypted(std::string_view room) {
  if (!encrypted_rooms_.emplace(room).second)
    return;
  this->save_encrypted();
  if (crypto_) {
    try {
      crypto_->remember_encrypted(room);
    } catch (const std::exception& failed) {
      log(id_, "encryption stopped: {}", failed.what());
      crypto_.reset();
    }
  }
}
// What the file keeps: the rooms, and when each was encrypted at the latest.
struct encrypted_kept {
  std::vector<std::string> rooms;
  std::optional<std::map<std::string, std::int64_t>> since;
  friend consteval auto json_schema(knot::type<encrypted_kept>) { return knot::schema<encrypted_kept>(); }
};
template <class Sink>
void account<Sink>::save_encrypted() {
  const encrypted_kept all{.rooms = std::vector<std::string>(encrypted_rooms_.begin(), encrypted_rooms_.end()),
                           .since = std::map<std::string, std::int64_t>(encrypted_since_.begin(), encrypted_since_.end())};
  if (!mux::vault::the().write_file(this->encrypted_rooms_file(), knot::to_json_string(all), true))
    log(id_, "the encrypted rooms could not be kept in {}", this->encrypted_rooms_file().string());
}
template <class Sink>
std::optional<typename account<Sink>::since_t> account<Sink>::encrypted_by(std::string_view room, std::optional<since_t> seen) {
  const auto kept = encrypted_since_.find(room);
  if (seen && (kept == encrypted_since_.end() || seen->time_since_epoch().count() < kept->second)) {
    encrypted_since_.insert_or_assign(std::string(room), seen->time_since_epoch().count());
    this->save_encrypted();
    return seen;
  }
  if (kept == encrypted_since_.end())
    return std::nullopt;
  return since_t(std::chrono::milliseconds(kept->second));
}
template <class Sink>
std::filesystem::path account<Sink>::encrypted_rooms_file() const {
  return std::filesystem::path(this->kept_file()).concat(".encrypted");
}
template <class Sink>
void account<Sink>::load_encrypted() {
  const auto opened = mux::vault::the().read_file(this->encrypted_rooms_file());
  if (!opened)
    return;
  if (auto read = knot::try_read<encrypted_kept>(std::string_view(*opened))) {
    encrypted_rooms_.insert(read->rooms.begin(), read->rooms.end());
    if (read->since)
      encrypted_since_.insert(read->since->begin(), read->since->end());
  } else if (auto old = knot::try_read<std::vector<std::string>>(std::string_view(*opened))) {
    encrypted_rooms_.insert(old->begin(), old->end());  // as kept before the times were
  }
}

}  // namespace mux::matrix

namespace mux::matrix {
// A changed identity first -- whatever was verified was the old one; then
// verified, by emoji (a device of theirs) or their master key; else not.
template <class Sink>
void account<Sink>::tell_trust(std::string user) {
  if (!crypto_)
    return;
  trust_t now = trust::unverified{};
  if (identity_changed_.contains(user))
    now = trust::changed{};
  else if (crypto_->master_verified(user) || !crypto_->verified_keys(user).empty())
    now = trust::verified{};
  sink_(change::trust_changed{id_, std::move(user), std::move(now)});
}
}  // namespace mux::matrix

namespace mux::matrix {

template <class Sink>
void account<Sink>::request_secrets(const std::string& device) {
  if (!api_ || !crypto_)
    return;
  for (const auto& [name, at] : crypto::kSecretNames) {
    const std::string request = this->transaction();
    secrets_asked_.insert_or_assign(request, crypto::secret_name_at(at));
    std::map<std::string, std::map<std::string, knot::raw>> messages;
    messages[id_.address][device] = knot::raw{knot::to_json_string(crypto::secret_request_part{
        .name = std::string(name), .requesting_device_id = crypto_->device_id(), .request_id = request})};
    (void)perform(*api_, loom::cs::send_to_device{.event_type = "m.secret.request", .txn_id = this->transaction(),
                                                  .body = {.messages = std::move(messages)}});
  }
  log(id_, "asked {} for the cross-signing keys and the backup key", device);
}

// A secret given: taken only as the answer to one asked here, from this
// user, from a device of theirs verified here by emoji. The three
// cross-signing keys, together, checked against the master key the server
// lists, kept, and this device signed with them; the backup's key, the
// backup restored.
template <class Sink>
void account<Sink>::secret_in(const crypto::secret_got& got) {
  if (!api_ || !crypto_)
    return;
  const auto asked = secrets_asked_.find(got.request_id);
  if (asked == secrets_asked_.end() || got.sender != id_.address)
    return;
  if (!std::ranges::contains(crypto_->verified_keys(id_.address), got.ed25519)) {
    log(id_, "a secret refused: not from a session of yours verified here");
    return;
  }
  const crypto::secret_name_t which = asked->second;
  secrets_asked_.erase(asked);
  splice::visit(splice::overloaded{[&](crypto::secret_name::master) { secrets_got_.master = got.secret; },
                                   [&](crypto::secret_name::self_signing) { secrets_got_.self_signing = got.secret; },
                                   [&](crypto::secret_name::user_signing) { secrets_got_.user_signing = got.secret; },
                                   [&](crypto::secret_name::backup) {
                                     const std::size_t restored = this->restore_backup(got.secret);
                                     log(id_, "the backup key given: {} room keys restored", restored);
                                   }},
                which);
  if (!secrets_got_.master || !secrets_got_.self_signing || !secrets_got_.user_signing)
    return;
  const crypto::cross_signing_secrets secrets{.master = *secrets_got_.master, .self_signing = *secrets_got_.self_signing,
                                              .user_signing = *secrets_got_.user_signing};
  loom::cs::query_keys ask;
  ask.body.device_keys.emplace(id_.address, std::vector<std::string>{});
  auto listed = perform(*api_, ask);
  const auto server_master = listed ? crypto::master_of(*listed, id_.address) : std::nullopt;
  if (!server_master || crypto::detail::ed25519_public(secrets.master) != server_master) {
    sink_(change::refused{id_, "The cross-signing keys your other session gave are not the ones your account has."});
    return;
  }
  crypto_->keep_cross_signing(secrets);
  crypto_->verify_master(id_.address, *server_master);
  if (listed->device_keys)
    if (const auto own = listed->device_keys->find(id_.address); own != listed->device_keys->end())
      if (const auto device = own->second.find(crypto_->device_id()); device != own->second.end())
        this->cross_sign_device(device->second);
  secrets_got_ = {};
  sink_(change::notice{id_, "Verified",
                       "Your other session gave this one your cross-signing keys: it has signed itself with them, and "
                       "the people you talk to see it as yours."});
}

// A secret asked by one of this user's own other sessions: given where this
// device has it and the asker is a device of theirs verified here by emoji
// -- over Olm, a session with it opened from its one-time key where none is.
template <class Sink>
void account<Sink>::secret_request_in(const std::string& sender, const loom::ev::m_secret_request_content_t& content) {
  if (!api_ || !crypto_ || sender != id_.address || content.requesting_device_id == crypto_->device_id() || !content.name)
    return;
  const bool asking = splice::visit(splice::overloaded{[](loom::ev::m_secret_request_content_t::action_values::request_) { return true; },
                                                       [](const auto&) { return false; }},
                                    content.action);
  if (!asking)
    return;
  const auto keys = crypto_->cross_signing_keys();
  const auto which = crypto::secret_name_of(*content.name);
  if (!keys || !which)
    return;
  const std::optional<std::string> secret =
      splice::visit(splice::overloaded{[&](crypto::secret_name::master) { return std::optional<std::string>(keys->master); },
                                       [&](crypto::secret_name::self_signing) { return std::optional<std::string>(keys->self_signing); },
                                       [&](crypto::secret_name::user_signing) { return std::optional<std::string>(keys->user_signing); },
                                       [](crypto::secret_name::backup) { return std::optional<std::string>(); }},
                    *which);
  if (!secret)
    return;
  loom::cs::query_keys ask;
  ask.body.device_keys.emplace(id_.address, std::vector<std::string>{});
  auto got = perform(*api_, ask);
  if (!got)
    return;
  const auto mine = crypto::recipients_of(*got, id_.address, crypto_->pinned_master(id_.address), std::string_view(),
                                          crypto_->verified_keys(id_.address));
  const auto device = std::ranges::find(mine, content.requesting_device_id, &crypto::recipient::device_id);
  if (device == mine.end() || !std::ranges::contains(crypto_->verified_keys(id_.address), device->ed25519)) {
    log(id_, "{} asked for {}: not a session of yours verified here, not given", content.requesting_device_id, *content.name);
    return;
  }
  std::optional<std::string> one_time_key;
  if (!crypto_->has_session(device->curve25519)) {
    loom::cs::claim_keys claim;
    claim.body.one_time_keys[id_.address][device->device_id] = "signed_curve25519";
    if (auto claimed = perform(*api_, claim))
      if (const auto by_user = claimed->one_time_keys.find(id_.address); by_user != claimed->one_time_keys.end())
        if (const auto by_device = by_user->second.find(device->device_id); by_device != by_user->second.end())
          for (const auto& [id, raw] : by_device->second)
            if (!one_time_key)
              one_time_key = crypto::one_time_key_of(raw, *device);
  }
  const auto sealed = crypto_->secret_for(*device, content.request_id, *secret, one_time_key);
  if (!sealed)
    return;
  std::map<std::string, std::map<std::string, knot::raw>> messages;
  messages[id_.address][device->device_id] = knot::raw{
      knot::to_json_string(crypto::olm_content{.sender_key = crypto_->curve25519(), .ciphertext = {{device->curve25519, *sealed}}})};
  if (perform(*api_, loom::cs::send_to_device{.event_type = "m.room.encrypted", .txn_id = this->transaction(),
                                              .body = {.messages = std::move(messages)}}))
    log(id_, "{} given to your session {}", *content.name, device->device_id);
}
}  // namespace mux::matrix

namespace mux::matrix {
template <class Sink>
void account<Sink>::tell_devices(std::string user) {
  if (!api_ || !crypto_)
    return;
  loom::cs::query_keys ask;
  ask.body.device_keys.emplace(user, std::vector<std::string>{});
  auto got = perform(*api_, ask);
  if (!got || !got->device_keys)
    return;
  const auto theirs = got->device_keys->find(user);
  if (theirs == got->device_keys->end())
    return;
  const auto verified_here = crypto_->verified_keys(user);
  std::vector<change::device_view> devices;
  for (const auto& [id, info] : theirs->second) {
    const auto curve = info.keys.find("curve25519:" + id);
    const auto identity = curve == info.keys.end() ? std::nullopt
                                                   : crypto::device_of(*got, user, curve->second, crypto_->pinned_master(user));
    devices.push_back({.id = id,
                       .name = info.unsigned_ && info.unsigned_->device_display_name ? *info.unsigned_->device_display_name : std::string(),
                       .verified = identity && (identity->cross_signed || std::ranges::contains(verified_here, identity->ed25519))});
  }
  sink_(change::devices_listed{id_, std::move(user), std::move(devices)});
}
}  // namespace mux::matrix

namespace mux::matrix {
template <class Sink>
void account<Sink>::check_own_sessions() {
  if (!api_ || !crypto_)
    return;
  loom::cs::query_keys ask;
  ask.body.device_keys.emplace(id_.address, std::vector<std::string>{});
  auto got = perform(*api_, ask);
  if (!got || !got->device_keys)
    return;
  const auto mine = got->device_keys->find(id_.address);
  if (mine == got->device_keys->end())
    return;
  const auto verified_here = crypto_->verified_keys(id_.address);
  const auto trusted = [&](const std::string& id, const auto& info) {
    const auto curve = info.keys.find("curve25519:" + id);
    const auto identity = curve == info.keys.end()
                              ? std::nullopt
                              : crypto::device_of(*got, id_.address, curve->second, crypto_->pinned_master(id_.address));
    return identity && (identity->cross_signed || std::ranges::contains(verified_here, identity->ed25519));
  };
  std::vector<std::string> others;
  bool this_one = false;
  for (const auto& [id, info] : mine->second) {
    if (id == crypto_->device_id())
      this_one = trusted(id, info);
    else if (!trusted(id, info))
      others.push_back(info.unsigned_ && info.unsigned_->device_display_name ? *info.unsigned_->device_display_name : id);
  }
  if (!this_one)
    sink_(change::notice{id_, "Verify this session",
                         "Verify this session to allow it to read your message history, and so that others can trust "
                         "what it sends: verify it with emoji from another session of yours (Sessions), or restore "
                         "with your recovery key (Sessions, Device verification)."});
  if (!others.empty()) {
    std::string listed;
    for (const std::string& one : others)
      listed += (listed.empty() ? "" : ", ") + one;
    sink_(change::notice{id_, "New login. Was this you?",
                         std::format("Not verified: {}. Verify each from Sessions -- or sign it out, if it was not you.", listed)});
  }
}
}  // namespace mux::matrix

namespace mux::matrix {
template <class Sink>
void account<Sink>::withheld_in(const loom::ev::m_room_key_withheld_content_t& content) {
  if (!content.session_id)
    return;
  using codes = loom::ev::m_room_key_withheld_content_t::code_values;
  const std::string said = splice::visit(
      splice::overloaded{
          [](codes::m_unverified) { return std::string("🔒 You don't have access to this message: the sender does not trust this session (it is not verified)."); },
          [](codes::m_blacklisted) { return std::string("🔒 You don't have access to this message: the sender has blocked this session."); },
          [](codes::m_unauthorised) { return std::string("🔒 You don't have access to this message."); },
          [](codes::m_history_not_shared) { return std::string("🔒 You don't have access to this message: it was sent before you joined."); },
          [](codes::m_unavailable) { return std::string("🔒 Unable to decrypt message: its key could not be sent."); },
          [](codes::m_no_olm) { return std::string("🔒 Unable to decrypt message: the sender could not reach this session."); },
          [](const std::string&) { return std::string("🔒 Unable to decrypt message."); }},
      content.code);
  withheld_.insert_or_assign(*content.session_id, said);
  if (const auto kept = undecrypted_.find(*content.session_id); kept != undecrypted_.end())
    for (message one : kept->second) {
      one.body = {said, std::nullopt};
      sink_(change::message_added{std::move(one), placement::aside{}});
    }
}
}  // namespace mux::matrix

namespace mux::matrix {
template <class Sink>
void account<Sink>::set_only_verified(bool on) {
  how_.only_verified = on;
}
}  // namespace mux::matrix

namespace mux::matrix {
template <class Sink>
void account<Sink>::accept_identity(std::string user) {
  if (!api_ || !crypto_)
    return;
  loom::cs::query_keys ask;
  ask.body.device_keys.emplace(user, std::vector<std::string>{});
  auto got = perform(*api_, ask);
  const auto master = got ? crypto::master_of(*got, user) : std::nullopt;
  if (!master)
    return;
  crypto_->accept_master(user, *master);
  identity_changed_.erase(user);
  this->tell_trust(std::move(user));
}
}  // namespace mux::matrix
