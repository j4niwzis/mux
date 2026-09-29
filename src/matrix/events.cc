// SPDX-License-Identifier: AGPL-3.0-only
// mux.matrix:events -- A room's timeline events read into messages, edits, reactions and redactions.
export module mux.matrix:events;

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
void account<Sink>::event(const conversation_id& in, const loom::ev::timeline_event& one, placement_t where) {
  const auto at = std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::milliseconds(one.origin_server_ts));
  if (one.content.template is<loom::ev::m_room_message_content_t>()) {
    const auto& content = one.content.template as<loom::ev::m_room_message_content_t>();
    const knot::value* relates = extra(content.rest, one.content, "m.relates_to");
    // An edit: the event it replaces takes its new content.
    if (std::visit([](auto of) { return of.edit; },
                   relation_of(text(member(relates ? *relates : knot::value(), "rel_type"))))) {
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
    const auto [carries, picture, emote] = std::visit(
        [](auto of) { return std::tuple(of.carries, of.picture, of.is_emote); }, msgtype_of(content.msgtype));
    if (emote)
      made.body.plain = "* " + made.body.plain;
    // A picture or a file: where it is kept, its name, what it is; its
    // body a caption where a file name is given apart from it.
    if (carries) {
      mux::attachment carried;
      if (picture)
        carried.kind = attachment_kind::image{};
      carried.source = text(extra(content.rest, one.content, "url")).value_or("");
      const auto file_name = text(extra(content.rest, one.content, "filename"));
      carried.name = file_name.value_or(content.body);
      if (const knot::value* info = extra(content.rest, one.content, "info")) {
        const auto number = [&](std::string_view key) -> std::int64_t {
          const knot::value* got = member(*info, key);
          return got && got->is<std::int64_t>() ? got->as<std::int64_t>() : 0;
        };
        carried.mimetype = text(member(*info, "mimetype")).value_or("");
        carried.size = number("size");
        carried.width = static_cast<int>(number("w"));
        carried.height = static_cast<int>(number("h"));
        carried.blurhash = text(member(*info, "xyz.amorgan.blurhash"));
      }
      if (!carried.source.empty()) {
        made.attachment = std::move(carried);
        if (!file_name || *file_name == content.body)
          made.body = {};  // no caption: the body was the file's name
      }
    }
    if (relates)
      if (const knot::value* reply = member(*relates, "m.in_reply_to"))
        made.replies_to = text(member(*reply, "event_id"));
    sink_(change::message_added{std::move(made), where});
  } else if (one.content.template is<loom::ev::m_reaction_content_t>()) {
    const auto& content = one.content.template as<loom::ev::m_reaction_content_t>();
    if (content.m_relates_to && content.m_relates_to->event_id && content.m_relates_to->key) {
      reactions_[one.event_id] = {*content.m_relates_to->event_id, *content.m_relates_to->key, one.sender};
      sink_(change::reaction_changed{in, *content.m_relates_to->event_id, *content.m_relates_to->key, one.sender,
                                     true});
    }
  } else {
    // The rest, by its type: loom's timeline union does not have their
    // content yet.
    std::visit(overloaded{[&](event_type::encrypted) { encrypted(in, one, at, where); },
                          [&](event_type::redaction) { redaction(in, one); },
                          [](event_type::receipt) {},
                          [](event_type::other) {}},
               event_type_of(one.type));
  }
}

template <class Sink>
void account<Sink>::encrypted(const conversation_id& in, const loom::ev::timeline_event& one,
                 std::chrono::sys_time<std::chrono::milliseconds> at, placement_t where) {
  // By its type: loom's timeline union does not have its content yet.
  sink_(change::message_added{message{.in = in,
                                      .id = one.event_id,
                                      .sender = one.sender,
                                      .at = at,
                                      .body = {"🔒 an encrypted message (not yet readable here)", std::nullopt},
                                      .outgoing = one.sender == id_.address},
                              where});
}

template <class Sink>
void account<Sink>::redaction(const conversation_id& in, const loom::ev::timeline_event& one) {
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

template <class Sink>
auto account<Sink>::body_of(std::string plain, const knot::value& content) -> body {
  body made{std::move(plain), std::nullopt};
  if (std::visit([](auto of) { return of.html_given; }, body_format_of(text(member(content, "format")))))
    made.html = text(member(content, "formatted_body"));
  return made;
}

}  // namespace mux::matrix
