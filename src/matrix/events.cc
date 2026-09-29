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
      if (picture)
        carried.kind = attachment_kind::image{.moves = moving_type(carried.mimetype)};
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
                                     true, one.event_id, at});
    }
  } else {
    // The rest, by its type: loom's timeline union does not have their
    // content yet.
    const event_type_t type = event_type_of(one.type);
    std::visit(overloaded{[&](event_type::encrypted) { encrypted(in, one, at, where); },
                          [&](event_type::redaction) { redaction(in, one); },
                          [](event_type::receipt) {},
                          [&](const auto&) { done(in, one, type, at, where); }},
               type);
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
void account<Sink>::service(const conversation_id& in, const loom::ev::timeline_event& one,
                            std::chrono::sys_time<std::chrono::milliseconds> at, placement_t where, std::string said) {
  sink_(change::message_added{message{.in = in,
                                      .id = one.event_id,
                                      .sender = one.sender,
                                      .at = at,
                                      .body = {std::move(said), std::nullopt},
                                      .outgoing = one.sender == id_.address,
                                      .service = true},
                              where});
}

template <class Sink>
auto account<Sink>::name_in(const std::string& room, const std::string& user) const -> std::string {
  if (const auto kept = state_.joined.find(room); kept != state_.joined.end())
    return kept->second.state.display_name(user).value_or(user);
  return user;
}

// An event that is not a message, read out as tdesktop reads its service
// messages out: who did what. A sticker is a picture, and shown as one;
// a type nothing here reads is said by its name.
template <class Sink>
void account<Sink>::done(const conversation_id& in, const loom::ev::timeline_event& one, event_type_t type,
                         std::chrono::sys_time<std::chrono::milliseconds> at, placement_t where) {
  const knot::value tree = knot::to_value(one);
  const knot::value* content = member(tree, "content");
  const knot::value* extras = member(tree, "unsigned");
  const knot::value* before = extras ? member(*extras, "prev_content") : nullptr;
  const auto field = [](const knot::value* of, std::string_view key) {
    return of ? text(member(*of, key)) : std::optional<std::string>();
  };
  const std::string who = name_in(in.id, one.sender);
  const auto say = [&](std::string said) { service(in, one, at, where, std::move(said)); };
  std::visit(
      overloaded{
          [&](event_type::member) {
            const std::string target_id = one.state_key.value_or(one.sender);
            const std::string target = field(content, "displayname").value_or(name_in(in.id, target_id));
            const membership_t now = membership_of(field(content, "membership"));
            const membership_t was = membership_of(field(before, "membership"));
            const bool was_in = std::visit([](auto of) { return of.in; }, was);
            const bool self = one.sender == target_id;
            std::visit(overloaded{[&](membership::join) {
                                    if (!was_in) {
                                      say(std::format("{} joined", target));
                                    } else if (const auto old = field(before, "displayname"); old && *old != target) {
                                      say(std::format("{} changed their name to {}", *old, target));
                                    } else {
                                      say(std::format("{} changed their picture", target));
                                    }
                                  },
                                  [&](membership::leave) {
                                    std::visit(overloaded{[&](membership::ban) { say(std::format("{} unbanned {}", who, target)); },
                                                          [&](membership::invite) {
                                                            say(self ? std::format("{} declined the invitation", target)
                                                                     : std::format("{} withdrew {}'s invitation", who, target));
                                                          },
                                                          [&](const auto&) {
                                                            say(self ? std::format("{} left", target)
                                                                     : std::format("{} removed {}", who, target));
                                                          }},
                                               was);
                                  },
                                  [&](membership::invite) { say(std::format("{} invited {}", who, target)); },
                                  [&](membership::ban) { say(std::format("{} banned {}", who, target)); },
                                  [&](membership::knock) { say(std::format("{} asked to join", target)); },
                                  [&](membership::other) { say(std::format("{} changed {}'s membership", who, target)); }},
                       now);
          },
          [&](event_type::room_name) {
            const auto name = field(content, "name").value_or("");
            say(name.empty() ? std::format("{} removed the room's name", who)
                             : std::format("{} renamed the room to “{}”", who, name));
          },
          [&](event_type::topic) {
            const auto topic = field(content, "topic").value_or("");
            say(topic.empty() ? std::format("{} removed the topic", who)
                              : std::format("{} changed the topic to “{}”", who, topic));
          },
          [&](event_type::room_avatar) { say(std::format("{} changed the room's picture", who)); },
          [&](event_type::create) { say(std::format("{} created the room", who)); },
          [&](event_type::power_levels) { say(std::format("{} changed who may do what here", who)); },
          [&](event_type::pinned) { say(std::format("{} changed the pinned messages", who)); },
          [&](event_type::join_rules) {
            say(std::format("{} set who may join to “{}”", who, field(content, "join_rule").value_or("?")));
          },
          [&](event_type::history_visibility) {
            say(std::format("{} set who may read the history to “{}”", who,
                            field(content, "history_visibility").value_or("?")));
          },
          [&](event_type::canonical_alias) {
            const auto alias = field(content, "alias").value_or("");
            say(alias.empty() ? std::format("{} removed the room's address", who)
                              : std::format("{} set the room's address to {}", who, alias));
          },
          // A sticker: a picture, as a message with one is shown.
          [&](event_type::sticker) {
            message made{.in = in,
                         .id = one.event_id,
                         .sender = one.sender,
                         .at = at,
                         .body = {},
                         .outgoing = one.sender == id_.address};
            mux::attachment carried;
            carried.source = field(content, "url").value_or("");
            carried.name = field(content, "body").value_or("sticker");
            if (const knot::value* info = content ? member(*content, "info") : nullptr) {
              const auto number = [&](std::string_view key) -> std::int64_t {
                const knot::value* got = member(*info, key);
                return got && got->is<std::int64_t>() ? got->as<std::int64_t>() : 0;
              };
              carried.mimetype = text(member(*info, "mimetype")).value_or("");
              carried.width = static_cast<int>(number("w"));
              carried.height = static_cast<int>(number("h"));
            }
            carried.kind = attachment_kind::image{.moves = moving_type(carried.mimetype)};
            if (carried.source.empty()) {
              say(std::format("{} sent a sticker", who));
              return;
            }
            made.attachment = std::move(carried);
            sink_(change::message_added{std::move(made), where});
          },
          [&](const auto&) { say(std::format("{} sent {}", who, one.type)); }},
      type);
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
