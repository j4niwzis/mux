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

// What the password login says of who: the user's local part.
struct user_field {
  std::string user;
};
consteval auto json_schema(knot::type<user_field>) { return knot::schema<user_field>(); }

// m.receipt's content: event, then kind, then user, to when.
struct receipt_event {
  struct at_t {
    std::optional<std::int64_t> ts;
  };
  std::map<std::string, std::map<std::string, std::map<std::string, at_t>>> content;
};
consteval auto json_schema(knot::type<receipt_event::at_t>) { return knot::schema<receipt_event::at_t>(); }
consteval auto json_schema(knot::type<receipt_event>) { return knot::schema<receipt_event>(); }

// A pack of images (MSC2545): as the room's state, or the user's own.
struct pack_event {
  struct image_t {
    std::optional<std::string> url;
    std::optional<std::vector<std::string>> usage;
  };
  struct pack_t {
    std::optional<std::vector<std::string>> usage;
  };
  struct content_t {
    std::optional<std::map<std::string, image_t>> images;
    std::optional<pack_t> pack;
  };
  content_t content;
};
consteval auto json_schema(knot::type<pack_event::image_t>) { return knot::schema<pack_event::image_t>(); }
consteval auto json_schema(knot::type<pack_event::pack_t>) { return knot::schema<pack_event::pack_t>(); }
consteval auto json_schema(knot::type<pack_event::content_t>) { return knot::schema<pack_event::content_t>(); }
consteval auto json_schema(knot::type<pack_event>) { return knot::schema<pack_event>(); }

// im.ponies.emote_rooms: room, then the state keys of its packs.
struct emote_rooms_event {
  struct content_t {
    std::optional<std::map<std::string, std::map<std::string, knot::raw>>> rooms;
  };
  content_t content;
};
consteval auto json_schema(knot::type<emote_rooms_event::content_t>) { return knot::schema<emote_rooms_event::content_t>(); }
consteval auto json_schema(knot::type<emote_rooms_event>) { return knot::schema<emote_rooms_event>(); }

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
          const auto receipts = read_as<receipt_event>(event);
          if (!receipts)
            continue;
          for (const auto& [event_id, kinds] : receipts->content)
            for (const auto& [kind, users] : kinds)
              if (mux::visit([](auto of) { return of.read_up_to; }, receipt_kind_of(kind)))
                for (const auto& [user, when] : users) {
                  read_by.insert_or_assign(user, event_id);
                  if (when.ts)
                    read_at.insert_or_assign(user, std::chrono::sys_time<std::chrono::milliseconds>(
                                                       std::chrono::milliseconds(*when.ts)));
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

using power_levels_content = loom::ev::m_room_power_levels_content_t;
// Each one's say in a room, as its power levels list them.
inline std::map<std::string, std::int64_t> powers_of(const power_levels_content* content) {
  return content && content->users ? *content->users : std::map<std::string, std::int64_t>{};
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
  const auto found = state_.account_data.find("m.direct");
  if (found == state_.account_data.end())
    return false;
  const auto said = read_as<direct_event>(found->second);
  return said && std::ranges::any_of(said->content, [&](const auto& one) { return std::ranges::contains(one.second, room); });
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
                                     .powers = powers_of(kept.state.template content<power_levels_content>("m.room.power_levels")),
                                     .power_default = power_default_of(kept.state.template content<power_levels_content>("m.room.power_levels")),
                                     .needs = needs_of(kept.state.template content<power_levels_content>("m.room.power_levels")),
                                     .version = kept.state.room_version(),
                                     .other_aliases = other_aliases_of(kept.state.template content<loom::ev::m_room_canonical_alias_content_t>("m.room.canonical_alias"))});
}




// Whether a usage list lets an image be an emoji: MSC2545's "usage", on the
// image or else on its pack -- "emoticon", "sticker", or both where it is
// missing or empty.
using usage_t = std::optional<std::vector<std::string>>;
inline bool usable_as_emoji(const usage_t& usage) {
  return !usage || usage->empty() || std::ranges::any_of(*usage, [](const std::string& one) {
    return mux::visit([](auto of) { return of.as_emoji; }, image_usage_of(one));
  });
}
inline bool usable_as_sticker(const usage_t& usage) {
  return !usage || usage->empty() || std::ranges::any_of(*usage, [](const std::string& one) {
    return mux::visit([](auto of) { return of.as_sticker; }, image_usage_of(one));
  });
}

// A pack's images, as MSC2545 has them: "images", shortcode to {"url"},
// those that may be emoji -- or, asked for stickers, those that may be those.
inline void emotes_from(const std::optional<pack_event>& said, std::vector<mux::emote>& into, bool stickers = false) {
  if (!said || !said->content.images)
    return;
  const usage_t no_usage;
  const usage_t& pack_usage = said->content.pack ? said->content.pack->usage : no_usage;
  for (const auto& [shortcode, image] : *said->content.images) {
    const usage_t& usage = image.usage ? image.usage : pack_usage;
    if (!(stickers ? usable_as_sticker(usage) : usable_as_emoji(usage)))
      continue;
    if (image.url && image.url->starts_with("mxc://") &&
        std::ranges::find(into, shortcode, &mux::emote::shortcode) == into.end())
      into.push_back({shortcode, *image.url});
  }
}

template <class Sink>
auto account<Sink>::emotes_of(const loom::client::joined_room& kept, bool stickers) const -> std::vector<mux::emote> {
  std::vector<mux::emote> out;
  if (const auto own = state_.account_data.find("im.ponies.user_emotes"); own != state_.account_data.end()) {
    emotes_from(read_as<pack_event>(own->second), out, stickers);
  }
  for (const auto& [key, one] : kept.state.events) {
    if (!mux::visit([](auto of) { return of.emotes; }, state_type_of(key.first)))
      continue;
    emotes_from(read_as<pack_event>(one), out, stickers);
  }
  // And the packs of other rooms the user made usable everywhere, as Cinny
  // and Sable do: im.ponies.emote_rooms, room to the state keys of its packs.
  if (const auto chosen = state_.account_data.find("im.ponies.emote_rooms"); chosen != state_.account_data.end()) {
    if (const auto said = read_as<emote_rooms_event>(chosen->second); said && said->content.rooms)
      for (const auto& [room, packs] : *said->content.rooms) {
        const auto joined = state_.joined.find(room);
        if (joined == state_.joined.end())
          continue;
        for (const auto& [state_key, ignored] : packs)
          for (const auto& [key, one] : joined->second.state.events)
            if (key.second == state_key && mux::visit([](auto of) { return of.emotes; }, state_type_of(key.first)))
              emotes_from(read_as<pack_event>(one), out, stickers);
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
  if (const auto* said = kept.state.template content<loom::ev::m_room_pinned_events_content_t>("m.room.pinned_events"))
    out = said->pinned;
  return out;
}

template <class Sink>
auto account<Sink>::space(const loom::client::joined_room& kept) -> bool {
  const auto* created = kept.state.template content<loom::ev::m_room_create_content_t>("m.room.create");
  return created && mux::visit([](auto of) { return of.is_space; }, room_type_of(created->type));
}

template <class Sink>
auto account<Sink>::children_of(const loom::client::joined_room& kept) -> std::vector<std::string> {
  std::vector<std::string> out;
  for (const auto& [key, one] : kept.state.events) {
    if (!mux::visit([](auto of) { return of.child; }, state_type_of(key.first)))
      continue;
    // A child taken out has its content emptied.
    if (const auto said = read_as<content_keys>(one); said && !said->content.empty())
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
