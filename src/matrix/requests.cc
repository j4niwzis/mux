// SPDX-License-Identifier: AGPL-3.0-only
// mux.matrix:requests -- What the window asks of an account: messages sent, edited, removed, reactions, reads, history, typing, joining, members.
export module mux.matrix:requests;

import std;
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

template <class Sink>
void account<Sink>::manage(std::string room, room_action_t action) {
  loop_->spawn([this, room = std::move(room), action = std::move(action)] {
    if (!api_)
      return;
    // A state event of the room set, its content given.
    const auto set = [&](std::string type, knot::value::object content) {
      auto done = perform(*api_, loom::cs::set_room_state_with_key{
                                     .room_id = room, .event_type = type, .state_key = "", .body = knot::value(std::move(content))});
      if (!done)
        log(id_, "could not set {} in {}: {}", type, room, done.error().said());
    };
    const auto one_field = [](std::string_view key, std::string value) {
      knot::value::object content;
      content.emplace(std::string(key), knot::value(std::move(value)));
      return content;
    };
    const auto told = [&](const char* what, auto done) {
      if (!done)
        log(id_, "could not {} in {}: {}", what, room, done.error().said());
    };
    std::visit(
        overloaded{
            [&](const room_action::rename& one) { set("m.room.name", one_field("name", one.name)); },
            [&](const room_action::retopic& one) { set("m.room.topic", one_field("topic", one.topic)); },
            [&](const room_action::set_join_rule& one) {
              set("m.room.join_rules",
                  one_field("join_rule", std::string(std::visit([](auto of) { return word_of(of); }, one.rule))));
            },
            [&](const room_action::set_history& one) {
              set("m.room.history_visibility",
                  one_field("history_visibility", std::string(std::visit([](auto of) { return word_of(of); }, one.rule))));
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
              knot::value::object content;
              if (const auto kept = state_.joined.find(room); kept != state_.joined.end())
                if (const knot::value now = state_content(kept->second, "m.room.power_levels");
                    now.is<knot::value::object>())
                  content = now.as<knot::value::object>();
              knot::value::object users;
              if (const auto found = content.find("users");
                  found != content.end() && found->second.is<knot::value::object>())
                users = found->second.as<knot::value::object>();
              users.insert_or_assign(one.user, knot::value(one.level));
              content.insert_or_assign("users", knot::value(std::move(users)));
              set("m.room.power_levels", std::move(content));
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
    knot::value::object direct;
    if (const auto found = state_.account_data.find("m.direct"); found != state_.account_data.end()) {
      const knot::value tree = knot::to_value(found->second);
      if (const knot::value* content = member(tree, "content"); content && content->is<knot::value::object>())
        direct = content->as<knot::value::object>();
    }
    knot::value::array rooms;
    if (const auto theirs = direct.find(user); theirs != direct.end() && theirs->second.is<knot::value::array>())
      rooms = theirs->second.as<knot::value::array>();
    rooms.push_back(knot::value(made->room_id));
    direct.insert_or_assign(user, knot::value(std::move(rooms)));
    (void)perform(*api_, loom::cs::set_account_data{.user_id = id_.address, .type = "m.direct",
                                                    .body = knot::value(std::move(direct))});
    sink_(change::room_created{{id_, made->room_id}});
  });
}

template <class Sink>
void account<Sink>::send_sticker(std::string room, mux::emote sticker) {
  loop_->spawn([this, room = std::move(room), sticker = std::move(sticker)] {
    if (!api_)
      return;
    knot::value::object content;
    content.emplace("body", knot::value(sticker.shortcode));
    content.emplace("url", knot::value(sticker.url));
    content.emplace("info", knot::value(knot::value::object{}));
    auto sent = perform(*api_, loom::cs::send_message{.room_id = room,
                                                      .event_type = "m.sticker",
                                                      .txn_id = this->transaction(),
                                                      .body = knot::value(std::move(content))});
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
    sink_(change::devtools_text{"Source of " + event, knot::to_pretty_json_string(knot::to_value(*got))});
  });
}

template <class Sink>
void account<Sink>::list_state(std::string room) {
  loop_->spawn([this, room = std::move(room)] {
    std::vector<change::state_entry> entries;
    if (const auto kept = state_.joined.find(room); kept != state_.joined.end())
      for (const auto& [key, one] : kept->second.state.events)
        entries.push_back({key.first, key.second, knot::to_pretty_json_string(knot::to_value(one))});
    sink_(change::state_listed{{id_, room}, std::move(entries)});
  });
}

template <class Sink>
void account<Sink>::send_custom(std::string room, std::string type, std::optional<std::string> state_key,
                                std::string json) {
  loop_->spawn([this, room = std::move(room), type = std::move(type), state_key = std::move(state_key),
                json = std::move(json)] {
    const std::string title = "Sent " + type;
    auto body = knot::try_read<knot::value>(json);
    if (!body || !body->is<knot::value::object>()) {
      sink_(change::devtools_text{title, "Not sent: the content is not a JSON object."});
      return;
    }
    if (!api_) {
      sink_(change::devtools_text{title, "Not sent: not connected."});
      return;
    }
    if (state_key) {
      auto done = perform(*api_, loom::cs::set_room_state_with_key{
                                     .room_id = room, .event_type = type, .state_key = *state_key, .body = std::move(*body)});
      sink_(change::devtools_text{title, done ? "Sent: " + done->event_id : "Not sent: " + done.error().said()});
    } else {
      auto done = perform(*api_, loom::cs::send_message{
                                     .room_id = room, .event_type = type, .txn_id = this->transaction(), .body = std::move(*body)});
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
    knot::value tree;
    if (auto got = perform(*api_, loom::cs::get_url_preview_authed{.url = url}))
      tree = knot::to_value(*got);
    else if (auto old = perform(*api_, loom::cs::get_url_preview{.url = url}))
      tree = knot::to_value(*old);
    else
      return;
    const auto said = [&](std::string_view key) { return text(member(tree, key)).value_or(""); };
    link_preview made{.site = said("og:site_name"), .title = said("og:title"), .description = said("og:description")};
    if (auto image = text(member(tree, "og:image")); image && image->starts_with("mxc://"))
      made.image = std::move(image);
    if (made.title.empty() && made.description.empty())
      return;
    sink_(change::preview_loaded{url, std::move(made)});
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
    const knot::value tree = knot::to_value(*got);
    const knot::value* content = member(tree, "content");
    if (!content || !content->is<knot::value::object>())
      return;
    knot::value::object sent = content->as<knot::value::object>();
    sent.erase("m.relates_to");
    auto done = perform(*api_, loom::cs::send_message{.room_id = to,
                                                      .event_type = "m.room.message",
                                                      .txn_id = this->transaction(),
                                                      .body = knot::value(std::move(sent))});
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
      if (auto one = knot::try_read<loom::ev::timeline_event>(knot::to_json_string(knot::to_value(*got->event))))
        event(in, *one, placement::in_window{});
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

template <class Sink>
void account<Sink>::edit(std::string room, std::string event, std::string text) {
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
                                              .txn_id = this->transaction(),
                                              .body = knot::value(std::move(content))}))
      sink_(change::message_edited{{id_, room}, event, body{text, std::nullopt}});
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
      knot::value::object relates;
      relates.emplace("rel_type", knot::value(std::string("m.annotation")));
      relates.emplace("event_id", knot::value(target));
      relates.emplace("key", knot::value(key));
      knot::value::object content;
      content.emplace("m.relates_to", knot::value(std::move(relates)));
      if (key.starts_with("mxc://")) {
        const auto emotes = emotes_in(room);
        if (const auto found = std::ranges::find(emotes, key, &mux::emote::url); found != emotes.end())
          content.emplace("com.beeper.reaction.shortcode", knot::value(std::format(":{}:", found->shortcode)));
      }
      (void)perform(*api_, loom::cs::send_message{.room_id = room,
                                                  .event_type = "m.reaction",
                                                  .txn_id = this->transaction(),
                                                  .body = knot::value(std::move(content))});
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
    knot::value::array listed;
    for (auto& one : pinned)
      listed.push_back(knot::value(std::move(one)));
    knot::value::object content;
    content.emplace("pinned", knot::value(std::move(listed)));
    if (auto done = perform(*api_, loom::cs::set_room_state_with_key{.room_id = room,
                                                                     .event_type = "m.room.pinned_events",
                                                                     .state_key = "",
                                                                     .body = knot::value(std::move(content))});
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

template <class Sink>
void account<Sink>::send(std::string room, std::string body, std::optional<std::string> reply_to) {
  loop_->spawn([this, room = std::move(room), body = std::move(body), reply_to = std::move(reply_to)] {
    const std::string txn = this->transaction();
    const conversation_id in{id_, room};
    const auto html = with_emotes(body, emotes_in(room));
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
    knot::value::object content;
    content.emplace("msgtype", knot::value(std::string("m.text")));
    content.emplace("body", knot::value(body));
    if (html) {
      content.emplace("format", knot::value(std::string("org.matrix.custom.html")));
      content.emplace("formatted_body", knot::value(*html));
    }
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
