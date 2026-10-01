// SPDX-License-Identifier: AGPL-3.0-only
// mux.matrix:sync -- The sync: logging in, /sync long-polled, kept on disk, and what it brought told as changes.
export module mux.matrix:sync;

import std;
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
      auto slid = perform(syncing, ask,
                          sliding_first ? std::chrono::seconds(300)
                                        : std::chrono::duration_cast<std::chrono::seconds>(how_.sync_timeout) + std::chrono::seconds(30));
      if (!slid)
        return std::unexpected(slid.error());
      sliding_pos_ = slid->pos;
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

template <class Sink>
void account<Sink>::load_kept() {
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
      // Where to page back from: the first time a room is seen.
      if (part.timeline && part.timeline->prev_batch && paged_.insert(room).second)
        sink_(change::history_position{in, *part.timeline->prev_batch});
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
                                     .encrypted = kept.state.encrypted(),
                                     .unread = kept.unread.notification,
                                     .highlights = kept.unread.highlight,
                                     .space = space(kept),
                                     .children = children_of(kept),
                                     .member_count = kept.summary.joined_members,
                                     .alias = kept.state.canonical_alias(),
                                     .pinned = pinned_of(kept),
                                     .emotes = emotes_of(kept),
                                     .stickers = emotes_of(kept, true),
                                     .join_rule = join_rule_of(rule_text(kept.state.template content<loom::ev::m_room_join_rules_content_t>("m.room.join_rules"),
                                                                         &loom::ev::m_room_join_rules_content_t::join_rule)),
                                     .history = history_rule_of(rule_text(kept.state.template content<loom::ev::m_room_history_visibility_content_t>("m.room.history_visibility"),
                                                                          &loom::ev::m_room_history_visibility_content_t::history_visibility)),
                                     .powers = powers_in(kept),
                                     .power_default = power_default_of(kept.state.template content<power_levels_content>("m.room.power_levels")),
                                     .needs = needs_of(kept.state.template content<power_levels_content>("m.room.power_levels")),
                                     .version = kept.state.room_version(),
                                     .replaced_by = replaced_by_of(kept),
                                     .replaced_why = replaced_why_of(kept),
                                     .predecessor = predecessor_of(kept),
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

}  // namespace mux::matrix
