// SPDX-License-Identifier: AGPL-3.0-only
// mux.matrix:sync -- The sync: logging in, /sync long-polled, kept on disk, and what it brought told as changes.
export module mux.matrix:sync;

import std;
import mux.variant;
import knot;
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
      if (why.server && mux::visit([](auto code) { return code.gone; }, errcode_of(why.server->errcode))) {
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
    // The rooms whose news the sync cut short, since the last run: where
    // each had got to, to catch up on the gap between.
    std::vector<std::tuple<std::string, std::string, std::string>> gaps;
    if (!first && got->rooms && got->rooms->join)
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
  std::string name;
  for (const char c : id_.address)
    name += std::isalnum(static_cast<unsigned char>(c)) || c == '@' || c == '.' || c == '-' ? c : '_';
  return config::state_path(name + ".sync.json");
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
  return {mux::visit(overloaded{[](values::online) -> mux::availability_t { return mux::availability::online{}; },
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
        mux::visit(overloaded{[&](const loom::ev::m_presence_content_t& content) {
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
        for (const auto& event : *part.ephemeral->events) {
          const bool receipt = mux::visit(overloaded{[](event_type::receipt) { return true; },
                                                     // Every other type an ephemeral event can have.
                                                     [](const auto&) { return false; }},
                                          event_type_of(event.type));
          if (!receipt)
            continue;
          const knot::value tree = knot::to_value(event);
          const knot::value* content = member(tree, "content");
          if (!content || !content->is<knot::value::object>())
            continue;
          for (const auto& [event_id, kinds] : content->as<knot::value::object>()) {
            if (!kinds.is<knot::value::object>())
              continue;
            for (const auto& [kind, users] : kinds.as<knot::value::object>())
              if (mux::visit([](auto of) { return of.read_up_to; }, receipt_kind_of(kind)) &&
                  users.is<knot::value::object>())
                for (const auto& [user, when] : users.as<knot::value::object>()) {
                  read_by.insert_or_assign(user, event_id);
                  if (const knot::value* ts = member(when, "ts"); ts && ts->is<std::int64_t>())
                    read_at.insert_or_assign(user, std::chrono::sys_time<std::chrono::milliseconds>(
                                                       std::chrono::milliseconds(ts->as<std::int64_t>())));
                }
          }
        }
        if (!read_by.empty())
          sink_(change::receipts_changed{in, std::move(read_by), std::move(read_at)});
      }
    }
  if (rooms.invite)
    for (const auto& [room, part] : *rooms.invite)
      sink_(change::conversation_updated{.id = {id_, room}, .kind = conversation_kind::group{}, .name = room});
  if (rooms.leave)
    for (const auto& [room, part] : *rooms.leave)
      sink_(change::conversation_removed{{id_, room}});
}

// Each one's say in a room, as its power levels list them.
inline std::map<std::string, std::int64_t> powers_of(const knot::value& content) {
  std::map<std::string, std::int64_t> out;
  if (const knot::value* users = member(content, "users"); users && users->is<knot::value::object>())
    for (const auto& [user, level] : users->as<knot::value::object>())
      if (level.is<std::int64_t>())
        out.emplace(user, level.as<std::int64_t>());
  return out;
}
// What each thing done in a room asks, as its power levels say: read here,
// where they come in, into what the rest keeps.
inline power_needs needs_of(const knot::value& content) {
  power_needs out;
  const auto level = [&](const knot::value* at, std::int64_t& into) {
    if (at && at->is<std::int64_t>())
      into = at->as<std::int64_t>();
  };
  level(member(content, "users_default"), out.users_default);
  level(member(content, "events_default"), out.events_default);
  level(member(content, "state_default"), out.state_default);
  level(member(content, "invite"), out.invite);
  level(member(content, "kick"), out.kick);
  level(member(content, "ban"), out.ban);
  level(member(content, "redact"), out.redact);
  if (const knot::value* told = member(content, "notifications"))
    level(member(*told, "room"), out.notify_room);
  if (const knot::value* events = member(content, "events"); events && events->is<knot::value::object>())
    for (const auto& [kind, needed] : events->as<knot::value::object>())
      if (needed.is<std::int64_t>())
        out.events.emplace(kind, needed.as<std::int64_t>());
  return out;
}
// The other addresses a room publishes, besides its canonical one.
inline std::vector<std::string> other_aliases_of(const knot::value& content) {
  std::vector<std::string> out;
  if (const knot::value* alt = member(content, "alt_aliases"); alt && alt->is<knot::value::array>())
    for (const knot::value& one : alt->as<knot::value::array>())
      if (one.is<std::string>())
        out.push_back(one.as<std::string>());
  return out;
}
inline std::int64_t power_default_of(const knot::value& content) {
  const knot::value* level = member(content, "users_default");
  return level && level->is<std::int64_t>() ? level->as<std::int64_t>() : 0;
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
                                     .join_rule = join_rule_of(text(member(state_content(kept, "m.room.join_rules"), "join_rule"))),
                                     .history = history_rule_of(text(member(state_content(kept, "m.room.history_visibility"),
                                                                            "history_visibility"))),
                                     .powers = powers_of(state_content(kept, "m.room.power_levels")),
                                     .power_default = power_default_of(state_content(kept, "m.room.power_levels")),
                                     .needs = needs_of(state_content(kept, "m.room.power_levels")),
                                     .version = text(member(state_content(kept, "m.room.create"), "room_version")).value_or("1"),
                                     .other_aliases = other_aliases_of(state_content(kept, "m.room.canonical_alias"))});
}

template <class Sink>
auto account<Sink>::state_content(const loom::client::joined_room& kept, std::string_view type) -> knot::value {
  const auto* said = kept.state.find(std::string(type));
  if (!said)
    return knot::value();
  const knot::value tree = knot::to_value(*said);
  const knot::value* content = member(tree, "content");
  return content ? *content : knot::value();
}



// Whether a usage list lets an image be an emoji: MSC2545's "usage", on the
// image or else on its pack -- "emoticon", "sticker", or both where it is
// missing or empty.
inline bool usable_as_emoji(const knot::value* usage) {
  if (!usage || !usage->is<knot::value::array>() || usage->as<knot::value::array>().empty())
    return true;
  for (const auto& one : usage->as<knot::value::array>())
    if (one.is<std::string>() && mux::visit([](auto of) { return of.as_emoji; }, image_usage_of(one.as<std::string>())))
      return true;
  return false;
}
inline bool usable_as_sticker(const knot::value* usage) {
  if (!usage || !usage->is<knot::value::array>() || usage->as<knot::value::array>().empty())
    return true;
  for (const auto& one : usage->as<knot::value::array>())
    if (one.is<std::string>() && mux::visit([](auto of) { return of.as_sticker; }, image_usage_of(one.as<std::string>())))
      return true;
  return false;
}

// A pack's images, as MSC2545 has them: "images", shortcode to {"url"},
// those that may be emoji -- or, asked for stickers, those that may be those.
inline void emotes_from(const knot::value* content, std::vector<mux::emote>& into, bool stickers = false) {
  const knot::value* images = content ? member(*content, "images") : nullptr;
  if (!images || !images->is<knot::value::object>())
    return;
  const knot::value* pack = member(*content, "pack");
  const knot::value* pack_usage = pack ? member(*pack, "usage") : nullptr;
  for (const auto& [shortcode, image] : images->as<knot::value::object>()) {
    const knot::value* own_usage = member(image, "usage");
    const knot::value* usage = own_usage ? own_usage : pack_usage;
    if (!(stickers ? usable_as_sticker(usage) : usable_as_emoji(usage)))
      continue;
    const auto url = text(member(image, "url"));
    if (url && url->starts_with("mxc://") &&
        std::ranges::find(into, shortcode, &mux::emote::shortcode) == into.end())
      into.push_back({shortcode, *url});
  }
}

template <class Sink>
auto account<Sink>::emotes_of(const loom::client::joined_room& kept, bool stickers) const -> std::vector<mux::emote> {
  std::vector<mux::emote> out;
  if (const auto own = state_.account_data.find("im.ponies.user_emotes"); own != state_.account_data.end()) {
    const knot::value tree = knot::to_value(own->second);
    emotes_from(member(tree, "content"), out, stickers);
  }
  for (const auto& [key, one] : kept.state.events) {
    if (!mux::visit([](auto of) { return of.emotes; }, state_type_of(key.first)))
      continue;
    const knot::value tree = knot::to_value(one);
    emotes_from(member(tree, "content"), out, stickers);
  }
  // And the packs of other rooms the user made usable everywhere, as Cinny
  // and Sable do: im.ponies.emote_rooms, room to the state keys of its packs.
  if (const auto chosen = state_.account_data.find("im.ponies.emote_rooms"); chosen != state_.account_data.end()) {
    const knot::value tree = knot::to_value(chosen->second);
    const knot::value* content = member(tree, "content");
    const knot::value* rooms = content ? member(*content, "rooms") : nullptr;
    if (rooms && rooms->is<knot::value::object>())
      for (const auto& [room, packs] : rooms->as<knot::value::object>()) {
        const auto joined = state_.joined.find(room);
        if (joined == state_.joined.end() || !packs.is<knot::value::object>())
          continue;
        for (const auto& [state_key, ignored] : packs.as<knot::value::object>())
          for (const auto& [key, one] : joined->second.state.events)
            if (key.second == state_key && mux::visit([](auto of) { return of.emotes; }, state_type_of(key.first))) {
              const knot::value pack = knot::to_value(one);
              emotes_from(member(pack, "content"), out, stickers);
            }
      }
  }
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
  const auto* said = kept.state.find("m.room.pinned_events");
  if (!said)
    return out;
  const knot::value tree = knot::to_value(*said);
  const knot::value* content = member(tree, "content");
  const knot::value* pinned = content ? member(*content, "pinned") : nullptr;
  if (pinned && pinned->is<knot::value::array>())
    for (const auto& one : pinned->as<knot::value::array>())
      if (one.is<std::string>())
        out.push_back(one.as<std::string>());
  return out;
}

template <class Sink>
auto account<Sink>::space(const loom::client::joined_room& kept) -> bool {
  const auto* created = kept.state.find("m.room.create");
  if (!created)
    return false;
  const knot::value tree = knot::to_value(*created);
  const knot::value* content = member(tree, "content");
  const knot::value* type = content ? member(*content, "type") : nullptr;
  return mux::visit([](auto of) { return of.is_space; }, room_type_of(text(type)));
}

template <class Sink>
auto account<Sink>::children_of(const loom::client::joined_room& kept) -> std::vector<std::string> {
  std::vector<std::string> out;
  for (const auto& [key, one] : kept.state.events) {
    if (!mux::visit([](auto of) { return of.child; }, state_type_of(key.first)))
      continue;
    const knot::value tree = knot::to_value(one);
    const knot::value* content = member(tree, "content");
    if (content && content->is<knot::value::object>() && !content->as<knot::value::object>().empty())
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
  for (const char* gone : {"leave", "ban"})
    for (const std::string& user : kept.state.members(gone))
      who.erase(user);
  std::vector<mux::member> out;
  out.reserve(who.size());
  for (auto& [id, one] : who)
    out.push_back(std::move(one));
  sink_(change::members_changed{in, std::move(out)});
}

}  // namespace mux::matrix
