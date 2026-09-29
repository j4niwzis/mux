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
                                              .txn_id = "mux" + std::to_string(++transactions_),
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
                                              .txn_id = "mux" + std::to_string(++transactions_)}))
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
      (void)perform(*api_, loom::cs::send_message{.room_id = room,
                                                  .event_type = "m.reaction",
                                                  .txn_id = "mux" + std::to_string(++transactions_),
                                                  .body = knot::value(std::move(content))});
      return;
    }
    for (const auto& [event, one] : reactions_)
      if (one.target == target && one.key == key && one.who == id_.address) {
        (void)perform(*api_, loom::cs::redact_event{.room_id = room,
                                                    .event_id = event,
                                                    .txn_id = "mux" + std::to_string(++transactions_)});
        return;
      }
  });
}

template <class Sink>
void account<Sink>::leave(std::string room) {
  loop_->spawn([this, room = std::move(room)] {
    if (api_)
      (void)perform(*api_, loom::cs::leave_room{.room_id = room});
  });
}

template <class Sink>
void account<Sink>::send(std::string room, std::string body, std::optional<std::string> reply_to) {
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
