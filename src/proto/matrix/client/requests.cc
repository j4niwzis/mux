// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix.client:requests -- What the window asks of an account: messages sent, edited, removed, reactions, reads, history, typing, joining, members.
export module mux.proto.matrix.client:requests;

import std;
import loom.media;
import splice.bytes;
import chevron.escape;
import splice;
import knot;
import loom.api;
import loom.ev;
import loom.state;
import loom.cs.joining;
import loom.cs.leaving;
import loom.cs.login;
import loom.cs.message_pagination;
import loom.cs.event_context;
import loom.cs.receipts;
import loom.cs.redaction;
import loom.cs.users;
import loom.cs.relations;
import loom.cs.threads_list;
import loom.cs.room_summary;
import loom.cs.list_public_rooms;
import loom.cs.space_hierarchy;
import loom.cs.room_send;
import loom.cs.rooms;
import loom.crypto;
import loom.cs.keys;
import loom.cs.cross_signing;
import loom.cs.room_state;
import loom.cs.content_repo;
import loom.cs.authed_content_repo;
import loom.cs.create_room;
import loom.cs.account_data;
import loom.cs.key_backup;
import loom.cs.kicking;
import loom.cs.banning;
import loom.cs.inviting;
import loom.cs.sync;
import loom.cs.typing;
import loom.cs.wellknown;
import loom.cs.profile;
import loom.cs.device_management;
import mux.config;
import mux.core;
import mux.http;
import mux.net;
import loom.cs.pusher;
import mux.logic.markdown;
import :account;

// The members defined here are declared in :account, and exported there.
namespace mux::proto::matrix::client {

// What a push server says of its Matrix gateway (UnifiedPush's gateway
// discovery): {"unifiedpush":{"gateway":"matrix"}} where it has one.
struct gateway_said {
  struct unifiedpush_t {
    std::optional<std::string> gateway;
    friend consteval auto json_schema(knot::type<unifiedpush_t>) { return knot::schema<unifiedpush_t>(); }
  };
  std::optional<unifiedpush_t> unifiedpush;
  friend consteval auto json_schema(knot::type<gateway_said>) { return knot::schema<gateway_said>(); }
};

template <class Sink>
void account<Sink>::cut_long_poll() {
  if (!waking_ || !waking_->alive || !long_poll_)
    return;
  waking_->woke = true;
  long_poll_->abort();
}

template <class Sink>
void account<Sink>::sync_now() {
  this->cut_long_poll();
}

template <class Sink>
void account<Sink>::set_pusher(std::optional<std::string> endpoint) {
  push_endpoint_ = std::move(endpoint);
  if (!push_endpoint_) {
    pushed_to_.reset();
    return;
  }
  if (api_ && token_ && pushed_to_ != push_endpoint_)
    this->register_pusher();
}

template <class Sink>
void account<Sink>::register_pusher() {
  if (!push_endpoint_ || !api_)
    return;
  pushed_to_ = push_endpoint_;
  this->spawn_guarded([this, endpoint = *push_endpoint_] {
    // The endpoint's own gateway, where its push server says it has one --
    // ntfy's does; else UnifiedPush's public one.
    static constexpr std::string_view kPath = "/_matrix/push/v1/notify";
    std::string gateway = std::string("https://matrix.gateway.unifiedpush.org") + std::string(kPath);
    if (const auto where = http::url::parse(endpoint)) {
      try {
        http::connection asking(*loop_, *tls_, http::url{.host = where->host, .port = where->port, .path = {}}, how_.proxy);
        const auto got = asking.request("GET", kPath, {}, std::nullopt, std::chrono::seconds(20));
        std::optional<gateway_said> said;
        if (got.status == 200)
          if (auto read = knot::try_read<gateway_said>(got.body))
            said = std::move(*read);
        // Read once, here: whether it is a Matrix gateway.
        const bool own = said && said->unifiedpush && said->unifiedpush->gateway == std::optional<std::string>("matrix");
        if (own)
          gateway = "https://" + where->host + (where->port == 443 ? std::string() : std::format(":{}", where->port)) +
                    std::string(kPath);
      } catch (const net::failure& failed) {
        log(id_, "push: the endpoint's server not asked for its gateway ({}); the public one used", failed.what());
      }
    }
    using data_t = loom::cs::post_pusher::body_t::pusher_data_t;
    const auto set = perform(*api_, loom::cs::post_pusher{
                                        .body = {.pushkey = endpoint,
                                                 .kind = "http",
                                                 // The name mux owns on the session bus (mux.dbus's kAppId).
                                                 .app_id = "io.github.j4niwzis.mux",
                                                 .app_display_name = "mux",
                                                 .device_display_name = "mux",
                                                 .lang = "en",
                                                 .data = data_t{.url = gateway,
                                                                .format = data_t::format_t{data_t::format_values::event_id_only{}}},
                                                 // Beside this account's others, and other accounts' on one endpoint.
                                                 .append = true}});
    if (!set) {
      log(id_, "push: the server did not take the pusher: {}", set.error().said());
      pushed_to_.reset();
      return;
    }
    log(id_, "push: the server pushes through {}", gateway);
  });
}

template <class Sink>
auto account<Sink>::id() const noexcept -> const account_id& { return id_; }

template <class Sink>
void account<Sink>::start() {
  this->spawn_guarded([this] { run(); });
}

template <class Sink>
void account<Sink>::stop() { stopping_ = true; }

template <class Sink>
void account<Sink>::mark_read(std::string room, std::string event) {
  this->spawn_guarded([this, room = std::move(room), event = std::move(event)] {
    if (api_)
      (void)perform(*api_, loom::cs::post_receipt{.room_id = room,
                                                  .receipt_type = loom::cs::post_receipt::receipt_type_values::m_read{},
                                                  .event_id = event});
  });
}

template <class Sink>
void account<Sink>::load_older(std::string room, std::string from) {
  this->spawn_guarded([this, room = std::move(room), from = std::move(from)] {
    if (!api_)
      return;
    // No token: from the room's newest, back.
    auto got = perform(*api_, loom::cs::get_room_events{.room_id = room,
                                                        .from = from.empty() ? std::nullopt
                                                                             : std::optional<std::string>(from),
                                                        .dir = loom::cs::get_room_events::dir_values::b{},
                                                        .limit = 40});
    if (!got) {
      log(id_, "history of {}: {}", room, got.error().said());
      return;
    }
    log(id_, "history of {}: {} event{}", room, got->chunk.size(), got->chunk.size() == 1 ? "" : "s");
    const conversation_id in{id_, room};
    for (const auto& one : got->chunk)  // newest first: each goes before the rest
      event(in, one, placement::at_start{});
    // Nothing came, or the token did not move: the room's start. A server
    // that hands out a token with an empty page each time had an empty chat
    // -- at its top all along -- page back again at every one, for ever.
    const bool start = got->chunk.empty() || !got->end || *got->end == from;
    sink_(change::history_position{in, start ? std::nullopt : got->end});
  });
}

// Beeper's name of a custom emoji reacted with, beside the relation.
struct reaction_shortcode {
  std::string shortcode;
  friend consteval auto json_schema(knot::type<reaction_shortcode>) {
    return knot::schema<reaction_shortcode>().member<"shortcode">(knot::key("com.beeper.reaction.shortcode"));
  }
};

// What a link preview says of the page (Open Graph).
struct link_facts {
  std::optional<std::string> site;
  std::optional<std::string> title;
  std::optional<std::string> description;
  std::optional<std::string> image;
};

using power_levels_content = loom::ev::m_room_power_levels_content_t;

// The server a room is reached through: the one its ID names (rooms before
// version 12 have one), else this account's own.
template <class Sink>
std::string account<Sink>::server_of(const std::string& room) const {
  const auto colon = room.find(':');
  if (colon != std::string::npos)
    return room.substr(colon + 1);
  const auto own = id_.address.find(':');
  return own == std::string::npos ? std::string() : id_.address.substr(own + 1);
}

template <class Sink>
void account<Sink>::set_room_state(const std::string& room, std::string type, const auto& content, std::string key) {
  auto done = this->perform(*api_, loom::cs::set_room_state_with_key{.room_id = room, .event_type = type, .state_key = std::move(key), .body = as_body(content)});
  if (!done)
    log(id_, "could not set {} in {}: {}", type, room, done.error().said());
}

template <class Sink>
void account<Sink>::manage(std::string room, room_action_t action) {
  this->spawn_guarded([this, room = std::move(room), action = std::move(action)] {
    if (!api_)
      return;
    const auto set = [&](std::string type, const auto& content) { this->set_room_state(room, std::move(type), content); };
    const auto told = [&](const char* what, auto done) { this->told_failing(room, what, done); };
    spl::visit(
        spl::overloaded{
            [&](const room_action::rename& one) {
              loom::ev::m_room_name_content_t content;
              content.name = one.name;
              set("m.room.name", content);
            },
            [&](const room_action::retopic& one) {
              loom::ev::m_room_topic_content_t content;
              content.topic = one.topic;
              set("m.room.topic", content);
            },
            [&](const room_action::invite& one) {
              told("invite", perform(*api_, loom::cs::invite_user{.room_id = room, .body = {.user_id = one.user}}));
            },
            [&](const room_action::kick& one) {
              told("remove", perform(*api_, loom::cs::kick{.room_id = room, .body = {.user_id = one.user}}));
            },
            [&](const room_action::ban& one) {
              told("ban", perform(*api_, loom::cs::ban{.room_id = room, .body = {.user_id = one.user}}));
            },
            [&](const room_action::unban& one) {
              told("unban", perform(*api_, loom::cs::unban{.room_id = room, .body = {.user_id = one.user}}));
            }},
        action);
  });
}

// What Matrix changes of a room beyond what every protocol does.
template <class Sink>
void account<Sink>::change_room(std::string room, proto::matrix::room_change_t change) {
  this->spawn_guarded([this, room = std::move(room), change = std::move(change)] {
    if (!api_)
      return;
    const auto set = [&](std::string type, const auto& content) { this->set_room_state(room, std::move(type), content); };
    // The room's power levels as they are now: what a change is made on.
    const auto power_levels = [&] {
      if (const auto kept = state_.joined.find(room); kept != state_.joined.end())
        if (const auto* now = kept->second.state.template content<power_levels_content>("m.room.power_levels"))
          return *now;
      return power_levels_content{};
    };
    const auto told = [&](const char* what, auto done) { this->told_failing(room, what, done); };
    spl::visit(
        spl::overloaded{
            [&](const proto::matrix::room_change::set_join_rule& one) {
              loom::ev::m_room_join_rules_content_t content;
              content.join_rule = std::string(spl::visit([](const auto& of) { return word_of(of); }, one.rule));
              content.allow = allow_of(one.rule);
              set("m.room.join_rules", content);
            },
            [&](const proto::matrix::room_change::set_history& one) {
              loom::ev::m_room_history_visibility_content_t content;
              content.history_visibility = std::string(spl::visit([](auto of) { return word_of(of); }, one.rule));
              set("m.room.history_visibility", content);
            },
            // A say given: the room's power levels as they are, with it.
            [&](const proto::matrix::room_change::set_power& one) {
              power_levels_content content = power_levels();
              if (!content.users)
                content.users.emplace();
              content.users->insert_or_assign(one.user, static_cast<std::int64_t>(one.level));
              set("m.room.power_levels", content);
            },
            // Encryption on, as Element turns it on.
            [&](const proto::matrix::room_change::encrypt&) {
              loom::ev::m_room_encryption_content_t content;
              content.algorithm = loom::ev::m_room_encryption_content_t::algorithm_values::m_megolm_v1_aes_sha2{};
              set("m.room.encryption", content);
            },
            // What a thing done asks: the power levels as they are, with it.
            [&](const proto::matrix::room_change::set_need& one) {
              power_levels_content content = power_levels();
              const auto top = [&](std::optional<std::int64_t> power_levels_content::* member) {
                content.*member = static_cast<std::int64_t>(one.level);
              };
              spl::visit(spl::overloaded{[&](power_need::default_role) { top(&power_levels_content::users_default); },
                                    [&](power_need::send_messages) { top(&power_levels_content::events_default); },
                                    [&](power_need::change_settings) { top(&power_levels_content::state_default); },
                                    [&](power_need::invite) { top(&power_levels_content::invite); },
                                    [&](power_need::kick) { top(&power_levels_content::kick); },
                                    [&](power_need::ban) { top(&power_levels_content::ban); },
                                    [&](power_need::redact) { top(&power_levels_content::redact); },
                                    [&](power_need::notify_everyone) {
                                      if (!content.notifications)
                                        content.notifications.emplace();
                                      content.notifications->room = static_cast<std::int64_t>(one.level);
                                    },
                                    [&]<sends_state Need>(Need) {
                                      if (!content.events)
                                        content.events.emplace();
                                      content.events->insert_or_assign(std::string(Need::event),
                                                                       static_cast<std::int64_t>(one.level));
                                    }},
                         one.need);
              set("m.room.power_levels", content);
            },
            // Upgraded: the server makes the new room and tombstones this one.
            [&](const proto::matrix::room_change::upgrade& one) {
              told("upgrade", perform(*api_, loom::cs::upgrade_room{.room_id = room, .body = {.new_version = one.version}}));
            },
            // Any kind of event's: by its type, in the power levels' events.
            [&](const proto::matrix::room_change::set_event_need& one) {
              power_levels_content content = power_levels();
              if (!content.events)
                content.events.emplace();
              content.events->insert_or_assign(one.event, static_cast<std::int64_t>(one.level));
              set("m.room.power_levels", content);
            },
            // Listed in the space, keyed by the room: through its own
            // server where its ID names one, else through this account's.
            [&](const proto::matrix::room_change::add_child& one) {
              loom::ev::m_space_child_content_t content;
              content.via = {this->server_of(one.room)};
              this->set_room_state(room, "m.space.child", content, one.room);
            },
            // No longer: its entry with no via, which the spec reads as none.
            [&](const proto::matrix::room_change::remove_child& one) {
              this->set_room_state(room, "m.space.child", loom::ev::m_space_child_content_t{}, one.room);
            }},
        change);
  });
}

// A room encrypted from its first event: m.room.encryption in its initial
// state, as Element makes direct chats and private rooms -- never a first
// message in the clear while the state catches up.
// An account data request whose answer is read as the type it is kept as:
// once, from the response's body.
template <class Content>
struct account_data_as : loom::cs::get_account_data {
  using response = Content;
};

inline std::vector<loom::cs::create_room::body_t::state_event_t> encrypted_from_the_start() {
  loom::ev::m_room_encryption_content_t content;
  content.algorithm = loom::ev::m_room_encryption_content_t::algorithm_values::m_megolm_v1_aes_sha2{};
  return {{.type = "m.room.encryption", .state_key = "", .content = knot::raw{knot::to_json_string(content)}}};
}

template <class Sink>
void account<Sink>::create_direct(std::string user) {
  this->spawn_guarded([this, user = std::move(user)] {
    if (!api_)
      return;
    auto made = perform(*api_, loom::cs::create_room{.body = {.invite = std::vector<std::string>{user},
                                                              .initial_state = encrypted_from_the_start(),
                                                              .preset = loom::cs::create_room::body_t::preset_values::trusted_private_chat{},
                                                              .is_direct = true}});
    if (!made) {
      log(id_, "could not start a chat with {}: {}", user, made.error().said());
      return;
    }
    this->remember_encrypted(made->room_id);
    // m.direct as it is, with the new room under its person.
    loom::client::direct_rooms_t direct = loom::client::direct_rooms(state_);
    direct[user].push_back(made->room_id);
    (void)perform(*api_, loom::cs::set_account_data{.user_id = id_.address, .type = "m.direct",
                                                    .body = as_body(direct)});
    sink_(change::room_created{{id_, made->room_id}});
  });
}

template <class Sink>
void account<Sink>::send_sticker(std::string room, mux::emote sticker, std::optional<std::string> reply_to) {
  this->spawn_sending([this, room = std::move(room), sticker = std::move(sticker), reply_to = std::move(reply_to)] {
    if (!api_)
      return;
    // As the spec has it: its words, and its info -- its size and type, by
    // which other clients size it before it comes.
    loom::ev::m_sticker_content_t content;
    content.body = sticker.body.empty() ? sticker.shortcode : sticker.body;
    content.url = sticker.url;
    content.info.w = sticker.w;
    content.info.h = sticker.h;
    content.info.size = sticker.size;
    content.info.mimetype = sticker.mimetype;
    if (reply_to)
      content.m_relates_to = loom::ev::m_sticker_content_t::m_relates_to_t{
          .m_in_reply_to = loom::ev::m_sticker_content_t::m_relates_to_t::m_in_reply_to_t{.event_id = *reply_to}};
    auto sent = this->send_room_event(loom::cs::send_message{.room_id = room,
                                                      .event_type = "m.sticker",
                                                      .txn_id = this->transaction(),
                                                      .body = as_body(content)},
                                      relates_to_of(content));
    if (!sent)
      log(id_, "could not send a sticker to {}: {}", room, sent.error().said());
  });
}

template <class Sink>
void account<Sink>::view_source(std::string room, std::string event) {
  this->spawn_guarded([this, room = std::move(room), event = std::move(event)] {
    if (!api_)
      return;
    auto got = perform(*api_, loom::cs::get_one_room_event{.room_id = room, .event_id = event});
    if (!got) {
      sink_(proto::matrix::devtools_text{"Source of " + event, "Not fetched: " + got.error().said()});
      return;
    }
    sink_(proto::matrix::devtools_text{"Source of " + event, knot::to_pretty_json_string(*got)});
  });
}

template <class Sink>
void account<Sink>::list_state(std::string room) {
  this->spawn_guarded([this, room = std::move(room)] {
    std::vector<proto::matrix::state_entry> entries;
    if (const auto kept = state_.joined.find(room); kept != state_.joined.end())
      for (const auto& [key, one] : kept->second.state.events)
        entries.push_back({key.first, key.second, knot::to_pretty_json_string(one)});
    sink_(proto::matrix::state_listed{{id_, room}, std::move(entries)});
  });
}

template <class Sink>
void account<Sink>::send_custom(std::string room, std::string type, std::optional<std::string> state_key,
                                std::string json) {
  this->spawn_sending([this, room = std::move(room), type = std::move(type), state_key = std::move(state_key),
                json = std::move(json)] {
    const std::string title = "Sent " + type;
    // Only an object is a content: read as one, its keys' values left as text.
    if (!knot::try_read<std::map<std::string, knot::raw>>(json)) {
      sink_(proto::matrix::devtools_text{title, "Not sent: the content is not a JSON object."});
      return;
    }
    if (!api_) {
      sink_(proto::matrix::devtools_text{title, "Not sent: not connected."});
      return;
    }
    if (state_key) {
      auto done = perform(*api_, loom::cs::set_room_state_with_key{
                                     .room_id = room, .event_type = type, .state_key = *state_key, .body = knot::raw{json}});
      sink_(proto::matrix::devtools_text{title, done ? "Sent: " + done->event_id : "Not sent: " + done.error().said()});
    } else {
      auto done = this->send_room_event(loom::cs::send_message{
                                     .room_id = room, .event_type = type, .txn_id = this->transaction(), .body = knot::raw{json}},
                                     [&] -> std::optional<knot::raw> {
                                       const auto typed = knot::try_read<crypto::relation_part>(json);
                                       return typed ? typed->relates_to : std::nullopt;
                                     }());
      sink_(proto::matrix::devtools_text{title, done ? "Sent: " + done->event_id : "Not sent: " + done.error().said()});
    }
  });
}

template <class Sink>
void account<Sink>::fetch_preview(std::string url) {
  this->spawn_guarded([this, url = std::move(url)] {
    if (!api_)
      return;
    // The authenticated endpoint (Matrix 1.11), and the old one where the
    // server has not that.
    std::optional<link_facts> facts;
    // The page's Open Graph facts, as loom read them with the answer.
    const auto facts_of = [](const auto& answer) {
      return link_facts{.site = answer.og_site_name, .title = answer.og_title, .description = answer.og_description,
                        .image = answer.og_image};
    };
    if (auto got = perform(*api_, loom::cs::get_url_preview_authed{.url = url}))
      facts = facts_of(*got);
    else if (auto old = perform(*api_, loom::cs::get_url_preview{.url = url}))
      facts = facts_of(*old);
    else
      return;
    link_preview made{.site = facts->site.value_or(""),
                      .title = facts->title.value_or(""),
                      .description = facts->description.value_or("")};
    if (facts->image && loom::media::mxc_of(*facts->image))
      made.image = std::move(facts->image);
    if (made.title.empty() && made.description.empty())
      return;
    sink_(change::preview_loaded{url, std::move(made)});
  });
}

template <class Sink>
void account<Sink>::search_directory(std::string server, std::string query, std::optional<std::string> since) {
  this->spawn_guarded([this, server = std::move(server), query = std::move(query), since = std::move(since)] {
    if (!api_)
      return;
    using asked = loom::cs::query_public_rooms;
    auto got = perform(*api_, asked{.server = server.empty() ? std::nullopt : std::optional<std::string>(server),
                                    .body = {.limit = 50,
                                             .since = since,
                                             .filter = query.empty() ? std::nullopt
                                                                     : std::optional<asked::body_t::filter_t>(
                                                                           asked::body_t::filter_t{.generic_search_term = query})}});
    if (!got) {
      log(id_, "the directory of {}: {}", server.empty() ? std::string("the home server") : server, got.error().said());
      sink_(change::directory_listed{.by = id_, .server = server, .query = query, .more = since.has_value()});
      return;
    }
    std::vector<directory_room> rooms;
    for (const auto& one : got->chunk)
      rooms.push_back({.id = one.room_id,
                       .name = one.name.value_or(""),
                       .alias = one.canonical_alias.value_or(""),
                       .topic = one.topic.value_or(""),
                       .avatar = one.avatar_url,
                       .members = one.num_joined_members});
    sink_(change::directory_listed{.by = id_,
                                   .server = server,
                                   .query = query,
                                   .rooms = std::move(rooms),
                                   .next = got->next_batch,
                                   .more = since.has_value()});
  });
}

template <class Sink>
void account<Sink>::explore_space(std::string room) {
  this->spawn_guarded([this, room = std::move(room)] {
    if (!api_)
      return;
    auto got = perform(*api_, loom::cs::get_space_hierarchy{.room_id = room, .limit = 100, .max_depth = 1});
    if (!got) {
      log(id_, "the rooms of {}: {}", room, got.error().said());
      sink_(change::directory_listed{id_, "", "", {}, room});
      return;
    }
    // The space itself first in what the server says: its rooms after it.
    std::vector<directory_room> rooms;
    for (const auto& one : got->rooms)
      if (one.room_id != room)
        rooms.push_back({.id = one.room_id,
                         .name = one.name.value_or(""),
                         .alias = one.canonical_alias.value_or(""),
                         .topic = one.topic.value_or(""),
                         .avatar = one.avatar_url,
                         .members = one.num_joined_members,
                         .space = !one.children_state.empty() ||
                                  spl::visit([](auto of) { return of.is_space; },
                                                room_type_of(one.room_type ? std::optional<std::string_view>(*one.room_type) : std::nullopt))});
    log(id_, "the rooms of {}: {} listed, {} of them spaces", room, rooms.size(),
        std::ranges::count_if(rooms, [](const directory_room& one) { return one.space; }));
    sink_(change::directory_listed{id_, "", "", std::move(rooms), room});
  });
}

namespace packs {
// A pack's or an image's use, as its usage list says: missing or empty, any.
inline void use_of(const auto& usage, bool& emoji, bool& sticker) {
  emoji = loom::client::detail::allows(usage, loom::client::image_use::emoticon{});
  sticker = loom::client::detail::allows(usage, loom::client::image_use::sticker{});
}
// A usage list, as it is written: of the item and value types of the kind
// of content it goes in.
template <class Item, class Values>
[[nodiscard]] std::vector<Item> usage_list(bool emoji, bool sticker) {
  std::vector<Item> out;
  if (emoji)
    out.push_back(Item{typename Values::emoticon{}});
  if (sticker)
    out.push_back(Item{typename Values::sticker{}});
  return out;
}
// A pack read from its content -- a room's or one's own, the same shape.
template <class Content>
[[nodiscard]] emote_pack pack_of(const Content& content, std::optional<std::string> room, std::string key) {
  emote_pack pack{.chat = std::move(room), .key = std::move(key)};
  if (content.pack) {
    pack.name = content.pack->display_name.value_or("");
    pack.avatar = content.pack->avatar_url;
    pack.attribution = content.pack->attribution.value_or("");
    use_of(content.pack->usage, pack.emoji, pack.sticker);
  }
  for (const auto& [shortcode, image] : content.images) {
    pack_picture one{.shortcode = shortcode, .url = image.url, .body = image.body.value_or("")};
    if (image.usage && !image.usage->empty())
      use_of(image.usage, one.emoji, one.sticker);
    else {
      one.emoji = pack.emoji;
      one.sticker = pack.sticker;
    }
    if (image.info) {
      one.mimetype = image.info->mimetype.value_or("");
      one.width = image.info->w.value_or(0);
      one.height = image.info->h.value_or(0);
      one.size = image.info->size.value_or(0);
    }
    pack.pictures.push_back(std::move(one));
  }
  return pack;
}
// And written: an image's usage only where it is not the pack's.
template <class Content>
[[nodiscard]] Content content_of(const emote_pack& pack) {
  using image_t = typename Content::image_pack_image_t;
  using meta_t = typename Content::image_pack_meta_t;
  Content content;
  auto& meta = content.pack.emplace();
  if (!pack.name.empty())
    meta.display_name = pack.name;
  meta.avatar_url = pack.avatar;
  if (!pack.attribution.empty())
    meta.attribution = pack.attribution;
  meta.usage = usage_list<typename meta_t::usage_item_t, typename meta_t::usage_item_values>(pack.emoji, pack.sticker);
  for (const pack_picture& one : pack.pictures) {
    image_t image;
    image.url = one.url;
    if (!one.body.empty())
      image.body = one.body;
    if (one.emoji != pack.emoji || one.sticker != pack.sticker)
      image.usage = usage_list<typename image_t::usage_item_t, typename image_t::usage_item_values>(one.emoji, one.sticker);
    if (!one.mimetype.empty() || one.width > 0 || one.height > 0) {
      auto& info = image.info.emplace();
      if (!one.mimetype.empty())
        info.mimetype = one.mimetype;
      if (one.width > 0)
        info.w = one.width;
      if (one.height > 0)
        info.h = one.height;
      if (one.size > 0)
        info.size = one.size;
    }
    content.images.insert_or_assign(one.shortcode, std::move(image));
  }
  return content;
}
}  // namespace packs

template <class Sink>
void account<Sink>::list_packs(std::optional<std::string> room) {
  this->spawn_guarded([this, room = std::move(room)] {
    std::vector<emote_pack> found;
    if (!room) {
      if (const auto own = state_.account_data.find("im.ponies.user_emotes"); own != state_.account_data.end())
        spl::visit(spl::overloaded{[&](const loom::ev::im_ponies_user_emotes_content_t& content) {
                                           found.push_back(packs::pack_of(content, std::nullopt, std::string()));
                                         },
                                         [](const auto&) {}},
                      own->second.content.data());
      // One's own pack, even empty: to be filled.
      if (found.empty())
        found.push_back(emote_pack{});
    } else if (const auto joined = state_.joined.find(*room); joined != state_.joined.end()) {
      for (const auto& [key, one] : joined->second.state.events)
        spl::visit(spl::overloaded{[&](const loom::ev::im_ponies_room_emotes_content_t& content) {
                                           // An emptied one is a pack taken away.
                                           if (!content.images.empty() || content.pack)
                                             found.push_back(packs::pack_of(content, room, key.second));
                                         },
                                         [](const auto&) {}},
                      one.content.data());
    }
    sink_(proto::matrix::packs_listed{id_, room, std::move(found)});
  });
}

template <class Sink>
void account<Sink>::save_pack(emote_pack pack) {
  this->spawn_guarded([this, pack = std::move(pack)]() mutable {
    // A new room pack: its state key made of its name.
    if (pack.chat && pack.key.empty()) {
      pack.key = spl::bytes::key_text(pack.name);
      if (pack.key.empty())
        pack.key = "pack";
    }
    bool done = false;
    if (api_) {
      if (pack.chat)
        done = static_cast<bool>(perform(
            *api_, loom::cs::set_room_state_with_key{
                       .room_id = *pack.chat,
                       .event_type = "im.ponies.room_emotes",
                       .state_key = pack.key,
                       .body = as_body(packs::content_of<loom::ev::im_ponies_room_emotes_content_t>(pack))}));
      else
        done = static_cast<bool>(perform(
            *api_, loom::cs::set_account_data{
                       .user_id = id_.address,
                       .type = "im.ponies.user_emotes",
                       .body = as_body(packs::content_of<loom::ev::im_ponies_user_emotes_content_t>(pack))}));
    }
    if (!done)
      log(id_, "the pack {} was not saved", pack.name);
    sink_(proto::matrix::pack_saved{.by = id_, .pack = pack, .done = done});
  });
}

template <class Sink>
void account<Sink>::delete_pack(emote_pack pack) {
  this->spawn_guarded([this, pack = std::move(pack)] {
    // A room's taken away as the MSC has it: its state emptied. One's own is
    // one pack, emptied by saving it so, not deleted.
    const bool done = api_ && pack.chat &&
                      static_cast<bool>(perform(*api_, loom::cs::set_room_state_with_key{.room_id = *pack.chat,
                                                                                         .event_type = "im.ponies.room_emotes",
                                                                                         .state_key = pack.key,
                                                                                         .body = knot::raw{"{}"}}));
    sink_(proto::matrix::pack_saved{.by = id_, .pack = pack, .removed = true, .done = done});
  });
}

template <class Sink>
void account<Sink>::search_people(std::string term) {
  this->spawn_guarded([this, term = std::move(term)] {
    if (!api_)
      return;
    auto got = perform(*api_, loom::cs::search_user_directory{.body = {.search_term = term, .limit = 30}});
    std::vector<found_person> people;
    if (got)
      for (const auto& one : got->results)
        people.push_back({.id = one.user_id, .name = one.display_name.value_or(""), .avatar = one.avatar_url});
    else
      log(id_, "the user directory, for {}: {}", term, got.error().said());
    sink_(change::people_found{id_, term, std::move(people)});
  });
}

template <class Sink>
void account<Sink>::create_room(std::string name, std::string topic, bool open, std::string alias, bool federate,
                                bool encrypted, mux::room_place place) {
  this->spawn_guarded([this, name = std::move(name), topic = std::move(topic), open, alias = std::move(alias), federate,
                       encrypted, place = std::move(place)] {
    if (!api_)
      return;
    using made_t = loom::cs::create_room::body_t;
    // What it is made as: a space, where asked; one of this server's only,
    // where others are blocked -- Element's two switches, in its creation.
    loom::ev::m_room_create_content_t creation;
    if (!federate)
      creation.m_federate = false;
    if (place.make_space)
      creation.type = "m.space";
    // Its first state: encrypted, where asked; in a space, the space its
    // parent and, where the space's members may join it, the rule saying so.
    std::vector<made_t::state_event_t> first = encrypted && !place.make_space ? encrypted_from_the_start()
                                                                              : std::vector<made_t::state_event_t>{};
    if (place.space) {
      loom::ev::m_space_parent_content_t parent;
      parent.via = {this->server_of(*place.space)};
      parent.canonical = true;
      first.push_back({.type = "m.space.parent", .state_key = *place.space, .content = knot::raw{knot::to_json_string(parent)}});
      if (place.space_members && !open) {
        loom::ev::m_room_join_rules_content_t rule;
        rule.join_rule = loom::ev::m_room_join_rules_content_t::join_rule_values::restricted{};
        rule.allow = allow_of(proto::matrix::join_rule::restricted{{*place.space}});
        first.push_back({.type = "m.room.join_rules", .state_key = "", .content = knot::raw{knot::to_json_string(rule)}});
      }
    }
    auto made = perform(
        *api_, loom::cs::create_room{
                   .body = {.visibility = open ? made_t::visibility_t{made_t::visibility_values::public_{}}
                                               : made_t::visibility_t{made_t::visibility_values::private_{}},
                            .room_alias_name = alias.empty() ? std::nullopt : std::optional<std::string>(alias),
                            .name = name,
                            .topic = topic.empty() ? std::nullopt : std::optional<std::string>(topic),
                            // Element's "Block anyone not part of the server":
                            // the room's creation content, as the spec has it.
                            .creation_content = federate && !place.make_space
                                                    ? std::nullopt
                                                    : std::optional<knot::raw>(knot::raw{knot::to_json_string(creation)}),
                            .initial_state = first.empty() ? std::nullopt : std::optional(std::move(first)),
                            .preset = open ? made_t::preset_t{made_t::preset_values::public_chat{}}
                                           : made_t::preset_t{made_t::preset_values::private_chat{}}}});
    if (!made) {
      log(id_, "could not make the room {}: {}", name, made.error().said());
      return;
    }
    if (encrypted && !place.make_space)
      this->remember_encrypted(made->room_id);
    // Listed in its space, as Element lists one made there.
    if (place.space) {
      loom::ev::m_space_child_content_t child;
      child.via = {this->server_of(made->room_id)};
      this->set_room_state(*place.space, "m.space.child", child, made->room_id);
    }
    sink_(change::room_created{{id_, made->room_id}});
  });
}

}  // namespace mux::proto::matrix::client
