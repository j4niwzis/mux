// SPDX-License-Identifier: AGPL-3.0-only
// mux.matrix:requests -- What the window asks of an account: messages sent, edited, removed, reactions, reads, history, typing, joining, members.
export module mux.matrix:requests;

import std;
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
import loom.cs.room_summary;
import loom.cs.list_public_rooms;
import loom.cs.room_send;
import loom.cs.rooms;
import loom.cs.room_state;
import loom.cs.content_repo;
import loom.cs.authed_content_repo;
import loom.cs.create_room;
import loom.cs.account_data;
import loom.cs.kicking;
import loom.cs.banning;
import loom.cs.inviting;
import loom.cs.sync;
import loom.cs.typing;
import loom.cs.wellknown;
import mux.config;
import mux.core;
import mux.http;
import mux.net;
import mux.logic.markdown;
import :account;

// The members defined here are declared in :account, and exported there.
namespace mux::matrix {

template <class Sink>
auto account<Sink>::id() const noexcept -> const account_id& { return id_; }

template <class Sink>
void account<Sink>::start() {
  loop_->spawn([this] { run(); });
}

template <class Sink>
void account<Sink>::stop() { stopping_ = true; }

template <class Sink>
void account<Sink>::mark_read(std::string room, std::string event) {
  loop_->spawn([this, room = std::move(room), event = std::move(event)] {
    if (api_)
      (void)perform(*api_, loom::cs::post_receipt{.room_id = room,
                                                  .receipt_type = loom::cs::post_receipt::receipt_type_values::m_read{},
                                                  .event_id = event});
  });
}

template <class Sink>
void account<Sink>::load_older(std::string room, std::string from) {
  loop_->spawn([this, room = std::move(room), from = std::move(from)] {
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
    sink_(change::history_position{in, got->end});
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
  friend consteval auto json_schema(knot::type<link_facts>) {
    return knot::schema<link_facts>()
    .member<"site">(knot::key("og:site_name"))
    .member<"title">(knot::key("og:title"))
    .member<"description">(knot::key("og:description"))
    .member<"image">(knot::key("og:image"));
  }
};

using power_levels_content = loom::ev::m_room_power_levels_content_t;

template <class Sink>
void account<Sink>::manage(std::string room, room_action_t action) {
  loop_->spawn([this, room = std::move(room), action = std::move(action)] {
    if (!api_)
      return;
    // A state event of the room set, its content given.
    const auto set = [&](std::string type, const auto& content) {
      auto done = perform(*api_, loom::cs::set_room_state_with_key{
                                     .room_id = room, .event_type = type, .state_key = "", .body = as_body(content)});
      if (!done)
        log(id_, "could not set {} in {}: {}", type, room, done.error().said());
    };
    // The room's power levels as they are now: what a change is made on.
    const auto power_levels = [&] {
      if (const auto kept = state_.joined.find(room); kept != state_.joined.end())
        if (const auto* now = kept->second.state.template content<power_levels_content>("m.room.power_levels"))
          return *now;
      return power_levels_content{};
    };
    const auto told = [&](const char* what, auto done) {
      if (!done)
        log(id_, "could not {} in {}: {}", what, room, done.error().said());
    };
    splice::visit(
        splice::overloaded{
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
            [&](const room_action::set_join_rule& one) {
              loom::ev::m_room_join_rules_content_t content;
              content.join_rule = std::string(splice::visit([](auto of) { return word_of(of); }, one.rule));
              set("m.room.join_rules", content);
            },
            [&](const room_action::set_history& one) {
              loom::ev::m_room_history_visibility_content_t content;
              content.history_visibility = std::string(splice::visit([](auto of) { return word_of(of); }, one.rule));
              set("m.room.history_visibility", content);
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
            },
            // A say given: the room's power levels as they are, with it.
            [&](const room_action::set_power& one) {
              power_levels_content content = power_levels();
              if (!content.users)
                content.users.emplace();
              content.users->insert_or_assign(one.user, static_cast<std::int64_t>(one.level));
              set("m.room.power_levels", content);
            },
            // Encryption on, as Element turns it on.
            [&](const room_action::encrypt&) {
              loom::ev::m_room_encryption_content_t content;
              content.algorithm = loom::ev::m_room_encryption_content_t::algorithm_values::m_megolm_v1_aes_sha2{};
              set("m.room.encryption", content);
            },
            // What a thing done asks: the power levels as they are, with it.
            [&](const room_action::set_need& one) {
              power_levels_content content = power_levels();
              const auto top = [&](std::optional<std::int64_t> power_levels_content::* member) {
                content.*member = static_cast<std::int64_t>(one.level);
              };
              splice::visit(splice::overloaded{[&](power_need::default_role) { top(&power_levels_content::users_default); },
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
            }},
        action);
  });
}

template <class Sink>
void account<Sink>::create_direct(std::string user) {
  loop_->spawn([this, user = std::move(user)] {
    if (!api_)
      return;
    auto made = perform(*api_, loom::cs::create_room{.body = {.invite = std::vector<std::string>{user},
                                                              .preset = loom::cs::create_room::body_t::preset_values::trusted_private_chat{},
                                                              .is_direct = true}});
    if (!made) {
      log(id_, "could not start a chat with {}: {}", user, made.error().said());
      return;
    }
    // m.direct as it is, with the new room under its person.
    loom::client::direct_rooms_t direct = loom::client::direct_rooms(state_);
    direct[user].push_back(made->room_id);
    (void)perform(*api_, loom::cs::set_account_data{.user_id = id_.address, .type = "m.direct",
                                                    .body = as_body(direct)});
    sink_(change::room_created{{id_, made->room_id}});
  });
}

template <class Sink>
void account<Sink>::send_sticker(std::string room, mux::emote sticker) {
  loop_->spawn([this, room = std::move(room), sticker = std::move(sticker)] {
    if (!api_)
      return;
    loom::ev::m_sticker_content_t content;
    content.body = sticker.shortcode;
    content.url = sticker.url;
    auto sent = perform(*api_, loom::cs::send_message{.room_id = room,
                                                      .event_type = "m.sticker",
                                                      .txn_id = this->transaction(),
                                                      .body = as_body(content)});
    if (!sent)
      log(id_, "could not send a sticker to {}: {}", room, sent.error().said());
  });
}

template <class Sink>
void account<Sink>::view_source(std::string room, std::string event) {
  loop_->spawn([this, room = std::move(room), event = std::move(event)] {
    if (!api_)
      return;
    auto got = perform(*api_, loom::cs::get_one_room_event{.room_id = room, .event_id = event});
    if (!got) {
      sink_(change::devtools_text{"Source of " + event, "Not fetched: " + got.error().said()});
      return;
    }
    sink_(change::devtools_text{"Source of " + event, knot::to_pretty_json_string(*got)});
  });
}

template <class Sink>
void account<Sink>::list_state(std::string room) {
  loop_->spawn([this, room = std::move(room)] {
    std::vector<change::state_entry> entries;
    if (const auto kept = state_.joined.find(room); kept != state_.joined.end())
      for (const auto& [key, one] : kept->second.state.events)
        entries.push_back({key.first, key.second, knot::to_pretty_json_string(one)});
    sink_(change::state_listed{{id_, room}, std::move(entries)});
  });
}

template <class Sink>
void account<Sink>::send_custom(std::string room, std::string type, std::optional<std::string> state_key,
                                std::string json) {
  loop_->spawn([this, room = std::move(room), type = std::move(type), state_key = std::move(state_key),
                json = std::move(json)] {
    const std::string title = "Sent " + type;
    // Only an object is a content: read as one, its keys' values left as text.
    if (!knot::try_read<std::map<std::string, knot::raw>>(json)) {
      sink_(change::devtools_text{title, "Not sent: the content is not a JSON object."});
      return;
    }
    if (!api_) {
      sink_(change::devtools_text{title, "Not sent: not connected."});
      return;
    }
    if (state_key) {
      auto done = perform(*api_, loom::cs::set_room_state_with_key{
                                     .room_id = room, .event_type = type, .state_key = *state_key, .body = knot::raw{json}});
      sink_(change::devtools_text{title, done ? "Sent: " + done->event_id : "Not sent: " + done.error().said()});
    } else {
      auto done = perform(*api_, loom::cs::send_message{
                                     .room_id = room, .event_type = type, .txn_id = this->transaction(), .body = knot::raw{json}});
      sink_(change::devtools_text{title, done ? "Sent: " + done->event_id : "Not sent: " + done.error().said()});
    }
  });
}

template <class Sink>
void account<Sink>::fetch_preview(std::string url) {
  loop_->spawn([this, url = std::move(url)] {
    if (!api_)
      return;
    // The authenticated endpoint (Matrix 1.11), and the old one where the
    // server has not that.
    std::optional<link_facts> facts;
    // What the spec leaves open -- og:title and the rest -- is kept as the
    // answer's remainder, read here once; og:image is typed already.
    const auto facts_of = [](const auto& answer) {
      link_facts read = knot::try_read<link_facts>(answer.rest.text).value_or(link_facts{});
      if (!read.image)
        read.image = answer.og_image;
      return read;
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
    if (facts->image && facts->image->starts_with("mxc://"))
      made.image = std::move(facts->image);
    if (made.title.empty() && made.description.empty())
      return;
    sink_(change::preview_loaded{url, std::move(made)});
  });
}

template <class Sink>
void account<Sink>::search_directory(std::string server, std::string query) {
  loop_->spawn([this, server = std::move(server), query = std::move(query)] {
    if (!api_)
      return;
    using asked = loom::cs::query_public_rooms;
    auto got = perform(*api_, asked{.server = server.empty() ? std::nullopt : std::optional<std::string>(server),
                                    .body = {.limit = 50,
                                             .filter = query.empty() ? std::nullopt
                                                                     : std::optional<asked::body_t::filter_t>(
                                                                           asked::body_t::filter_t{.generic_search_term = query})}});
    if (!got) {
      log(id_, "the directory of {}: {}", server.empty() ? std::string("the home server") : server, got.error().said());
      sink_(change::directory_listed{id_, server, query, {}});
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
    sink_(change::directory_listed{id_, server, query, std::move(rooms)});
  });
}

template <class Sink>
void account<Sink>::search_people(std::string term) {
  loop_->spawn([this, term = std::move(term)] {
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
void account<Sink>::create_room(std::string name, std::string topic, bool open, std::string alias, bool federate) {
  loop_->spawn([this, name = std::move(name), topic = std::move(topic), open, alias = std::move(alias), federate] {
    if (!api_)
      return;
    using made_t = loom::cs::create_room::body_t;
    auto made = perform(
        *api_, loom::cs::create_room{
                   .body = {.visibility = open ? made_t::visibility_t{made_t::visibility_values::public_{}}
                                               : made_t::visibility_t{made_t::visibility_values::private_{}},
                            .room_alias_name = alias.empty() ? std::nullopt : std::optional<std::string>(alias),
                            .name = name,
                            .topic = topic.empty() ? std::nullopt : std::optional<std::string>(topic),
                            .preset = open ? made_t::preset_t{made_t::preset_values::public_chat{}}
                                           : made_t::preset_t{made_t::preset_values::private_chat{}},
                            // Element's "Block anyone not part of the server":
                            // the room's creation content, as the spec has it.
                            .creation_content = federate ? std::nullopt
                                                         : std::optional<knot::raw>(knot::raw{R"({"m.federate":false})"})}});
    if (!made) {
      log(id_, "could not make the room {}: {}", name, made.error().said());
      return;
    }
    sink_(change::room_created{{id_, made->room_id}});
  });
}

template <class Sink>
void account<Sink>::catch_up(std::string room, std::string from, std::string until) {
  loop_->spawn([this, room = std::move(room), from = std::move(from), until = std::move(until)] {
    const conversation_id in{id_, room};
    std::map<std::string, std::string> sender_of;  // of every event the pages held
    struct reaction_found {
      std::string event, target;
      std::chrono::sys_time<std::chrono::milliseconds> at;
    };
    std::vector<reaction_found> reactions;
    std::optional<std::string> token = from;
    std::size_t read = 0, mentions = 0;
    // At most ten pages of a hundred: a gap longer than that is the
    // history's, paged back to when read.
    for (int page = 0; page < 10 && api_ && token; ++page) {
      auto got = perform(*api_, loom::cs::get_room_events{.room_id = room,
                                                          .from = token,
                                                          .dir = loom::cs::get_room_events::dir_values::b{},
                                                          .limit = 100});
      if (!got || got->chunk.empty())
        break;
      bool reached = false;
      for (const auto& one : got->chunk) {
        if (one.event_id == until) {
          reached = true;
          break;
        }
        ++read;
        sender_of.emplace(one.event_id, one.sender);
        if (one.sender == id_.address)
          continue;
        const auto at = std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::milliseconds(one.origin_server_ts));
        splice::visit(
            splice::overloaded{
                [&](const loom::ev::m_room_message_content_t& content) {
                  if (loom::client::mentions(content, id_.address)) {
                    ++mentions;
                    sink_(change::mentioned{in, one.event_id, at});
                  }
                },
                [&](const loom::ev::m_reaction_content_t& content) {
                  if (content.m_relates_to && content.m_relates_to->event_id)
                    reactions.push_back({one.event_id, *content.m_relates_to->event_id, at});
                },
                [](const auto&) {}},
            one.content.data());
      }
      if (reached)
        break;
      token = got->end;
    }
    // The reactions to what the user sent: known by who sent it, where the
    // pages or the room's last events held it.
    const auto kept = state_.joined.find(room);
    const auto mine = [&](const std::string& target) {
      if (const auto found = sender_of.find(target); found != sender_of.end())
        return found->second == id_.address;
      if (kept != state_.joined.end())
        for (const auto& one : kept->second.timeline)
          if (one.event_id == target)
            return one.sender == id_.address;
      return false;
    };
    std::size_t to_mine = 0;
    for (const auto& one : reactions)
      if (mine(one.target)) {
        ++to_mine;
        sink_(change::reacted_to_mine{in, one.event, one.target, one.at});
      }
    log(id_, "caught up on {}: {} event{}, {} mention{}, {} reaction{} to yours", room, read, read == 1 ? "" : "s",
        mentions, mentions == 1 ? "" : "s", to_mine, to_mine == 1 ? "" : "s");
  });
}

template <class Sink>
void account<Sink>::preview_room(std::string room, std::vector<std::string> via) {
  loop_->spawn([this, room = std::move(room), via = std::move(via)] {
    if (!api_)
      return;
    auto got = perform(*api_, loom::cs::get_room_summary{
                                  .room_id_or_alias = room,
                                  .via = via.empty() ? std::nullopt : std::optional<std::vector<std::string>>(via)});
    if (!got) {
      sink_(change::room_previewed{id_, room, {.note = "Its server tells nothing of it: " + got.error().said()}});
      return;
    }
    sink_(change::room_previewed{id_, room,
                                 {.id = got->room_id,
                                  .name = got->name.value_or(""),
                                  .alias = got->canonical_alias.value_or(""),
                                  .topic = got->topic.value_or(""),
                                  .avatar = got->avatar_url,
                                  .members = got->num_joined_members}});
  });
}

template <class Sink>
void account<Sink>::create_group(std::string name) {
  loop_->spawn([this, name = std::move(name)] {
    if (!api_)
      return;
    auto made = perform(*api_, loom::cs::create_room{.body = {.name = name,
                                                              .preset = loom::cs::create_room::body_t::preset_values::private_chat{}}});
    if (!made) {
      log(id_, "could not make the room {}: {}", name, made.error().said());
      return;
    }
    sink_(change::room_created{{id_, made->room_id}});
  });
}

template <class Sink>
void account<Sink>::forward(std::string from, std::string event, std::string to) {
  loop_->spawn([this, from = std::move(from), event = std::move(event), to = std::move(to)] {
    if (!api_)
      return;
    auto got = perform(*api_, loom::cs::get_one_room_event{.room_id = from, .event_id = event});
    if (!got) {
      log(id_, "could not fetch {} to forward: {}", event, got.error().said());
      return;
    }
    // A message's content as it is typed, without what it answered or
    // replaced: sent on as a message of its own. Anything else is not
    // forwarded.
    std::optional<loom::ev::m_room_message_content_t> content;
    splice::visit(splice::overloaded{[&](const loom::ev::m_room_message_content_t& one) { content = one; },
                                     [](const auto&) {}},
                  got->content.data());
    if (!content) {
      log(id_, "{} is not a message: not forwarded", event);
      return;
    }
    content->m_relates_to.reset();
    content->m_new_content.reset();
    auto done = perform(*api_, loom::cs::send_message{.room_id = to,
                                                      .event_type = "m.room.message",
                                                      .txn_id = this->transaction(),
                                                      .body = as_body(*content)});
    if (!done)
      log(id_, "could not forward {} to {}: {}", event, to, done.error().said());
  });
}

template <class Sink>
void account<Sink>::fetch_quoted(std::string room, std::string target) {
  loop_->spawn([this, room = std::move(room), target = std::move(target)] {
    if (!api_)
      return;
    auto got = perform(*api_, loom::cs::get_one_room_event{.room_id = room, .event_id = target});
    if (!got) {
      log(id_, "could not fetch {} in {}: {}", target, room, got.error().said());
      return;
    }
    event(conversation_id{id_, room}, *got, placement::aside{});
  });
}

template <class Sink>
void account<Sink>::load_context(std::string room, std::string target) {
  loop_->spawn([this, room = std::move(room), target = std::move(target)] {
    if (!api_)
      return;
    auto got = perform(*api_, loom::cs::get_event_context{.room_id = room, .event_id = target, .limit = 60});
    if (!got) {
      log(id_, "context of {} in {}: {}", target, room, got.error().said());
      return;
    }
    const conversation_id in{id_, room};
    sink_(change::window_opened{in, got->start, got->end});
    // Before it, newest first: given oldest first.
    if (got->events_before)
      for (auto it = got->events_before->rbegin(); it != got->events_before->rend(); ++it)
        event(in, *it, placement::in_window{});
    // It, read as a timeline event from what the server gave.
    if (got->event)
      event(in, *got->event, placement::in_window{});
    if (got->events_after)
      for (const auto& one : *got->events_after)
        event(in, one, placement::in_window{});
    log(id_, "context of {} in {}: {} before, {} after", target, room,
        got->events_before ? got->events_before->size() : 0, got->events_after ? got->events_after->size() : 0);
  });
}

template <class Sink>
void account<Sink>::load_newer(std::string room, std::string from) {
  loop_->spawn([this, room = std::move(room), from = std::move(from)] {
    if (!api_)
      return;
    auto got = perform(*api_, loom::cs::get_room_events{.room_id = room,
                                                        .from = from,
                                                        .dir = loom::cs::get_room_events::dir_values::f{},
                                                        .limit = 40});
    if (!got) {
      log(id_, "newer in {}: {}", room, got.error().said());
      return;
    }
    const conversation_id in{id_, room};
    for (const auto& one : got->chunk)  // oldest first: each after the rest
      event(in, one, placement::in_window{});
    // Nothing more, or no token on: the newest is met, and it is live.
    sink_(change::window_extended{in, got->chunk.empty() ? std::nullopt : got->end});
  });
}

template <class Sink>
void account<Sink>::fetch_avatar(std::string source, std::string of) {
  this->fetch_media(std::move(source), media_use::avatar{std::move(of)}, 96, true);
}

// Defined further down, beside send: a message made HTML.
[[nodiscard]] inline std::optional<std::string> html_of(std::string_view body, const std::vector<mux::emote>& emotes);

template <class Sink>
void account<Sink>::edit(std::string room, std::string event, std::string text) {
  loop_->spawn([this, room = std::move(room), event = std::move(event), text = std::move(text)] {
    if (!api_)
      return;
    // Made HTML as a message sent is: its Markdown, the room's emoji.
    const auto html = html_of(text, emotes_in(room));
    const auto content = loom::client::edit_message(event, text, html);
    if (perform(*api_, loom::cs::send_message{.room_id = room,
                                              .event_type = "m.room.message",
                                              .txn_id = this->transaction(),
                                              .body = as_body(content)}))
      sink_(change::message_edited{{id_, room}, event, body{text, html}});
  });
}

template <class Sink>
void account<Sink>::remove(std::string room, std::string event) {
  loop_->spawn([this, room = std::move(room), event = std::move(event)] {
    if (!api_)
      return;
    if (perform(*api_, loom::cs::redact_event{.room_id = room,
                                              .event_id = event,
                                              .txn_id = this->transaction()}))
      sink_(change::message_redacted{{id_, room}, event});
  });
}

template <class Sink>
void account<Sink>::react(std::string room, std::string target, std::string key, bool on) {
  loop_->spawn([this, room = std::move(room), target = std::move(target), key = std::move(key), on] {
    if (!api_)
      return;
    if (on) {
      auto content = loom::client::reaction(target, key);
      if (key.starts_with("mxc://")) {
        const auto emotes = emotes_in(room);
        if (const auto found = std::ranges::find(emotes, key, &mux::emote::url); found != emotes.end())
          content.rest = as_body(reaction_shortcode{std::format(":{}:", found->shortcode)});
      }
      (void)perform(*api_, loom::cs::send_message{.room_id = room,
                                                  .event_type = "m.reaction",
                                                  .txn_id = this->transaction(),
                                                  .body = as_body(content)});
      return;
    }
    for (const auto& [event, one] : reactions_)
      if (one.target == target && one.key == key && one.who == id_.address) {
        (void)perform(*api_, loom::cs::redact_event{.room_id = room,
                                                    .event_id = event,
                                                    .txn_id = this->transaction()});
        return;
      }
  });
}

template <class Sink>
void account<Sink>::pin(std::string room, std::string target, bool on) {
  loop_->spawn([this, room = std::move(room), target = std::move(target), on] {
    if (!api_)
      return;
    std::vector<std::string> pinned;
    if (const auto kept = state_.joined.find(room); kept != state_.joined.end())
      pinned = pinned_of(kept->second);
    std::erase(pinned, target);
    if (on)
      pinned.push_back(target);
    loom::ev::m_room_pinned_events_content_t content;
    content.pinned = std::move(pinned);
    if (auto done = perform(*api_, loom::cs::set_room_state_with_key{.room_id = room,
                                                                     .event_type = "m.room.pinned_events",
                                                                     .state_key = "",
                                                                     .body = as_body(content)});
        !done)
      log(id_, "could not {} {} in {}: {}", on ? "pin" : "unpin", target, room, done.error().said());
  });
}

template <class Sink>
void account<Sink>::leave(std::string room) {
  loop_->spawn([this, room = std::move(room)] {
    if (api_)
      (void)perform(*api_, loom::cs::leave_room{.room_id = room});
  });
}

// A message's text as HTML where it names custom emoji: each :shortcode: the
// room knows an <img data-mx-emoticon>, as MSC2545 sends them, and the rest
// escaped. Nothing where it names none.
[[nodiscard]] inline std::optional<std::string> with_emotes(std::string_view body, const std::vector<mux::emote>& emotes) {
  if (emotes.empty())
    return std::nullopt;
  std::string html;
  bool any = false;
  const auto escaped = [&](char c) {
    switch (c) {
      case '&': html += "&amp;"; break;
      case '<': html += "&lt;"; break;
      case '>': html += "&gt;"; break;
      case '"': html += "&quot;"; break;
      case '\n': html += "<br>"; break;
      default: html += c;
    }
  };
  for (std::size_t at = 0; at < body.size();) {
    if (body[at] == ':') {
      const auto end = body.find(':', at + 1);
      if (end != std::string_view::npos && end > at + 1) {
        const std::string_view code = body.substr(at + 1, end - at - 1);
        if (const auto found = std::ranges::find(emotes, code, &mux::emote::shortcode); found != emotes.end()) {
          html += std::format(R"(<img data-mx-emoticon src="{}" alt=":{}:" title=":{}:" height="32">)", found->url,
                              code, code);
          any = true;
          at = end + 1;
          continue;
        }
      }
    }
    escaped(body[at]);
    ++at;
  }
  if (!any)
    return std::nullopt;
  return html;
}

// In HTML already -- Markdown made so -- the :shortcode:s of the room's
// custom emoji made <img>s, in the text and not inside a tag.
[[nodiscard]] inline std::string emotes_in_html(std::string_view html, const std::vector<mux::emote>& emotes) {
  std::string out;
  for (std::size_t at = 0; at < html.size();) {
    if (html[at] == '<') {
      const auto end = html.find('>', at);
      const auto stop = end == std::string_view::npos ? html.size() : end + 1;
      out.append(html.substr(at, stop - at));
      at = stop;
      continue;
    }
    if (html[at] == ':') {
      const auto end = html.find(':', at + 1);
      if (end != std::string_view::npos && end > at + 1) {
        const std::string_view code = html.substr(at + 1, end - at - 1);
        if (const auto found = std::ranges::find(emotes, code, &mux::emote::shortcode); found != emotes.end()) {
          out += std::format(R"(<img data-mx-emoticon src="{}" alt=":{}:" title=":{}:" height="32">)", found->url, code,
                             code);
          at = end + 1;
          continue;
        }
      }
    }
    out += html[at];
    ++at;
  }
  return out;
}

// What a message is sent as: its Markdown made HTML, as Element sends it,
// and the room's custom emoji in that; else the emoji alone, where it names
// any; else nothing -- the text as it is.
[[nodiscard]] inline std::optional<std::string> html_of(std::string_view body, const std::vector<mux::emote>& emotes) {
  if (auto marked = mux::logic::markdown_html(body))
    return emotes.empty() ? *marked : emotes_in_html(*marked, emotes);
  return with_emotes(body, emotes);
}

template <class Sink>
void account<Sink>::send(std::string room, std::string body, std::optional<std::string> reply_to,
                         std::vector<mention> mentions) {
  loop_->spawn([this, room = std::move(room), body = std::move(body), reply_to = std::move(reply_to),
                mentions = std::move(mentions)] {
    const std::string txn = this->transaction();
    const conversation_id in{id_, room};
    // Each mention a link to its person in the Markdown, as Element sends a
    // pill: the body keeps the name as written.
    std::string marked = body;
    std::size_t from = 0;
    for (const mention& one : mentions) {
      const auto at = marked.find(one.name, from);
      if (at == std::string::npos)
        continue;
      std::string label;
      for (const char c : one.name) {
        if (c == '[' || c == ']' || c == '\\')
          label += '\\';
        label += c;
      }
      const std::string link = std::format("[{}](https://matrix.to/#/{})", label, one.user);
      marked.replace(at, one.name.size(), link);
      from = at + link.size();
    }
    const auto html = html_of(marked, emotes_in(room));
    sink_(change::message_added{message{
        .in = in,
        .id = txn,
        .sender = id_.address,
        .at = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()),
        .body = {body, html},
        .replies_to = reply_to,
        .outgoing = true,
        .delivery = delivery::sending{}}});
    if (!api_) {
      sink_(change::delivery_changed{in, txn, delivery::failed{}});
      return;
    }
    loom::client::text_said said{.body = body, .html = html, .reply_to = reply_to};
    // Who is mentioned, as Matrix 1.7 says it: what their clients notify by.
    for (const mention& one : mentions)
      said.mentions.push_back(one.user);
    const auto content = loom::client::text_message(said);
    auto sent = perform(*api_, loom::cs::send_message{.room_id = room,
                                                      .event_type = "m.room.message",
                                                      .txn_id = txn,
                                                      .body = as_body(content)});
    if (!sent) {
      sink_(change::delivery_changed{in, txn, delivery::failed{}});
      return;
    }
    sink_(change::message_acknowledged{in, txn, sent->event_id});
  });
}

template <class Sink>
void account<Sink>::typing(std::string room, bool on) {
  loop_->spawn([this, room = std::move(room), on] {
    if (api_)
      (void)perform(*api_, loom::cs::set_typing{.user_id = id_.address,
                                                .room_id = room,
                                                .body = {.typing = on,
                                                         .timeout = on ? std::optional<std::int64_t>(30000)
                                                                       : std::nullopt}});
  });
}

template <class Sink>
void account<Sink>::join(std::string room, std::vector<std::string> via) {
  loop_->spawn([this, room = std::move(room), via = std::move(via)] {
    if (!api_)
      return;
    auto got = perform(*api_, loom::cs::join_room{.room_id_or_alias = room,
                                                  .via = via.empty() ? std::nullopt
                                                                     : std::optional<std::vector<std::string>>(via)});
    if (got)
      log(id_, "joined {} ({})", room, got->room_id);
    else
      log(id_, "could not join {}: {}", room, got.error().said());
  });
}

template <class Sink>
void account<Sink>::fetch_members(std::string room) {
  loop_->spawn([this, room = std::move(room)] {
    if (!api_)
      return;
    auto got = perform(*api_, loom::cs::get_joined_members_by_room{.room_id = room});
    if (!got || !got->joined)
      return;
    auto& all = full_members_[room];
    for (const auto& [user, one] : *got->joined)
      all.insert_or_assign(user, mux::member{user, one.display_name.value_or(user), std::nullopt, one.avatar_url});
    log(id_, "members of {}: {}", room, all.size());
    if (const auto kept = state_.joined.find(room); kept != state_.joined.end())
      members(conversation_id{id_, room}, kept->second);
  });
}

}  // namespace mux::matrix
