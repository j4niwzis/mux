// SPDX-License-Identifier: AGPL-3.0-only
// mux.matrix:events -- A room's timeline events read into messages, edits, reactions and redactions.
export module mux.matrix:events;

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

// A picture's or a file's facts, as an attachment keeps them.
// Of a message's own info or a gallery item's: loom reads both alike.
inline void carry_info(mux::attachment& carried, const auto& info) {
  carried.mimetype = info.mimetype.value_or("");
  carried.size = info.size.value_or(0);
  carried.width = static_cast<int>(info.w.value_or(0));
  carried.height = static_cast<int>(info.h.value_or(0));
  carried.blurhash = info.xyz_amorgan_blurhash;
}

using member_content = loom::ev::m_room_member_content_t;
// A membership as loom reads it, as mux's.
inline membership_t membership_from(const member_content::membership_t& said) {
  using values = member_content::membership_values;
  return splice::visit(splice::overloaded{[](values::join) -> membership_t { return membership::join{}; },
                               [](values::leave) -> membership_t { return membership::leave{}; },
                               [](values::invite) -> membership_t { return membership::invite{}; },
                               [](values::ban) -> membership_t { return membership::ban{}; },
                               [](values::knock) -> membership_t { return membership::knock{}; },
                               [](const std::string&) -> membership_t { return membership::other{}; }},
                    said);
}

// Whether a relation replaces what it relates to: an edit.
inline bool replaces(const loom::ev::m_room_message_content_t::m_relates_to_t& relates) {
  using values = loom::ev::m_room_message_content_t::m_relates_to_t::rel_type_values;
  return relates.rel_type &&
         splice::visit(splice::overloaded{[](values::m_replace) { return true; }, [](const auto&) { return false; }}, *relates.rel_type);
}

template <class Sink>
void account<Sink>::event(const conversation_id& in, const loom::ev::timeline_event& one, placement_t where) {
  const auto at = std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::milliseconds(one.origin_server_ts));
  // By the content's type: a message, a reaction, or the rest by the type
  // it says.
  splice::visit(splice::overloaded{[&](const loom::ev::m_room_message_content_t& content) {
    const auto& relates = content.m_relates_to;
    // An edit: the event it replaces takes its new content.
    if (relates && replaces(*relates)) {
      if (relates->event_id && content.m_new_content)
        sink_(change::message_edited{in, *relates->event_id,
                                     body_of(content.m_new_content->body.value_or(""), content.m_new_content->format,
                                             content.m_new_content->formatted_body)});
      return;
    }
    message made{.in = in,
                 .id = one.event_id,
                 .sender = one.sender,
                 .at = at,
                 .body = body_of(content.body, content.format, content.formatted_body),
                 .outgoing = one.sender == id_.address};
    const auto [carries, picture, emote] = splice::visit(
        [](auto of) { return std::tuple(of.carries, of.picture, of.is_emote); }, msgtype_of(content.msgtype));
    if (emote)
      made.body.plain = "* " + made.body.plain;
    // A picture or a file: where it is kept, its name, what it is; its
    // body a caption where a file name is given apart from it.
    if (carries) {
      mux::attachment carried;
      if (picture)
        carried.kind = attachment_kind::image{};
      carried.source = content.url.value_or("");
      carried.name = content.filename.value_or(content.body);
      if (content.info) {
        carry_info(carried, *content.info);
        // A video: shown by its thumbnail, as a picture, until it can be
        // played here; its own size where the video gives none.
        const bool video = splice::visit(splice::overloaded{[](msgtype::video) { return true; }, [](const auto&) { return false; }},
                                      msgtype_of(content.msgtype));
        if (video && content.info->thumbnail_url) {
          carried.video = carried.source;
          carried.source = *content.info->thumbnail_url;
          carried.duration_ms = content.info->duration.value_or(0);
          carried.kind = attachment_kind::image{};
          if (const auto& thumb = content.info->thumbnail_info; thumb && (carried.width == 0 || carried.height == 0)) {
            carried.width = static_cast<int>(thumb->w.value_or(0));
            carried.height = static_cast<int>(thumb->h.value_or(0));
          }
        }
      }
      if (picture)
        carried.kind = attachment_kind::image{.moves = moving_type(carried.mimetype)};
      if (!carried.source.empty()) {
        made.attachment = std::move(carried);
        if (!content.filename || *content.filename == content.body)
          made.body = {};  // no caption: the body was the file's name
      }
    }
    // A gallery (MSC4274): each of its itemtypes read as a picture or a file
    // alone is, its body the caption.
    if (splice::visit(splice::overloaded{[](msgtype::gallery) { return true; }, [](const auto&) { return false; }},
                   msgtype_of(content.msgtype)) &&
        content.itemtypes)
      for (const auto& item : *content.itemtypes) {
        const bool is_picture_item = splice::visit([](auto of) { return of.picture; }, msgtype_of(item.itemtype));
        mux::attachment carried;
        carried.source = item.url.value_or("");
        carried.name = item.filename.value_or(item.body.value_or(""));
        if (item.info)
          carry_info(carried, *item.info);
        if (is_picture_item)
          carried.kind = attachment_kind::image{.moves = moving_type(carried.mimetype)};
        if (!carried.source.empty())
          made.album.push_back(std::move(carried));
      }
    // Nothing mux can show of it: said so, so that it is there to be looked
    // at (View Source) rather than an empty space.
    if (made.body.plain.empty() && !made.body.html && !made.attachment && made.album.empty())
      made.body.plain = "Unsupported message (" + (content.msgtype.empty() ? std::string("no msgtype") : content.msgtype) + ")";
    if (relates && relates->m_in_reply_to)
      made.replies_to = relates->m_in_reply_to->event_id;
    // A message for the user, come as it happened: listed, as Telegram's @.
    // Who it mentions, as m.mentions says; before that, the user's ID in it.
    const bool live = splice::visit(splice::overloaded{[](placement::at_end) { return true; }, [](const auto&) { return false; }}, where);
    const auto mentions_me = [&] { return loom::client::mentions(content, id_.address); };
    if (live && !made.outgoing && mentions_me())
      sink_(change::mentioned{in, made.id, made.at});
    // The user's own, sent from here: the echo shown under its transaction
    // id is this one. Acknowledged first -- the sync can bring it before the
    // send's answer does, and both were shown until then.
    if (one.unsigned_ && one.unsigned_->transaction_id)
      sink_(change::message_acknowledged{in, *one.unsigned_->transaction_id, one.event_id});
    sink_(change::message_added{std::move(made), where});
  }, [&](const loom::ev::m_reaction_content_t& content) {
    if (content.m_relates_to && content.m_relates_to->event_id && content.m_relates_to->key) {
      reactions_[one.event_id] = {*content.m_relates_to->event_id, *content.m_relates_to->key, one.sender};
      const bool live =
          splice::visit(splice::overloaded{[](placement::at_end) { return true; }, [](const auto&) { return false; }}, where);
      sink_(change::reaction_changed{in, *content.m_relates_to->event_id, *content.m_relates_to->key, one.sender,
                                     true, one.event_id, at, live});
      // Fetched on its own, as what a reply quotes: a message of its own for
      // the quote, whether reactions are shown as events or not -- "Reacted
      // with" its key -- pointing at what it reacted to.
      const std::string& key = *content.m_relates_to->key;
      // A custom emoji's key is its picture: shown as the picture, in HTML,
      // as a message carries one -- not said to be "a custom emoji".
      const bool pictured = key.starts_with("mxc://");
      const auto escaped = [](std::string_view text) {
        std::string out;
        for (const char c : text) {
          switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
          }
        }
        return out;
      };
      const std::string emote = std::format(R"(<img data-mx-emoticon src="{}" alt=":emoji:" height="32">)", escaped(key));
      splice::visit(splice::overloaded{[&](placement::aside) {
                              message made{.in = in,
                                           .id = one.event_id,
                                           .sender = one.sender,
                                           .at = at,
                                           .body = pictured ? mux::body{"Reacted with :emoji:", "Reacted with " + emote}
                                                            : mux::body{std::format("Reacted with {}", key), std::nullopt},
                                           .replies_to = content.m_relates_to->event_id,
                                           .outgoing = one.sender == id_.address,
                                           .reaction = true};
                              sink_(change::message_added{std::move(made), where});
                            },
                            // Else a line of its own too, quoting what it is on: shown
                            // where the chat's settings show reactions so.
                            [&](const auto&) {
                              message made{.in = in,
                                           .id = one.event_id,
                                           .sender = one.sender,
                                           .at = at,
                                           .body = pictured
                                                       ? mux::body{std::format("{} reacted :emoji:", name_in(in.id, one.sender)),
                                                              escaped(name_in(in.id, one.sender)) + " reacted " + emote}
                                                       : mux::body{std::format("{} reacted {}", name_in(in.id, one.sender), key),
                                                              std::nullopt},
                                           .replies_to = content.m_relates_to->event_id,
                                           .outgoing = one.sender == id_.address,
                                           .service = true,
                                           .event_kind = room_event::reactions{}};
                              sink_(change::message_added{std::move(made), where});
                            }},
                 where);
    }
  }, [&](const auto&) {
    // The rest, by its type: loom's timeline union does not have their
    // content yet.
    const event_type_t type = event_type_of(one.type);
    splice::visit(splice::overloaded{[&](event_type::encrypted) { encrypted(in, one, at, where); },
                          [&](event_type::redaction) { redaction(in, one); },
                          [](event_type::receipt) {},
                          [&](const auto&) { done(in, one, type, at, where); }},
               type);
  }}, one.content.data());
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
                            std::chrono::sys_time<std::chrono::milliseconds> at, placement_t where, std::string said,
                            room_event_t kind) {
  sink_(change::message_added{message{.in = in,
                                      .id = one.event_id,
                                      .sender = one.sender,
                                      .at = at,
                                      .body = {std::move(said), std::nullopt},
                                      .outgoing = one.sender == id_.address,
                                      .service = true,
                                      .event_kind = kind},
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
void account<Sink>::done(const conversation_id& in, const loom::ev::timeline_event& one, event_type_t,
                         std::chrono::sys_time<std::chrono::milliseconds> at, placement_t where) {
  const std::string who = name_in(in.id, one.sender);
  const auto say = [&](room_event_t kind, std::string said) { service(in, one, at, where, std::move(said), kind); };
  splice::visit(
      splice::overloaded{
          [&](const member_content& content) {
            const std::string target_id = one.state_key.value_or(one.sender);
            const std::string target = content.displayname.value_or(name_in(in.id, target_id));
            // What it was: the content before, as the server gives it beside.
            std::optional<member_content> before;
            if (one.unsigned_ && one.unsigned_->prev_content)
              if (auto got = knot::try_read<member_content>(one.unsigned_->prev_content->text))
                before = std::move(*got);
            const membership_t now = membership_from(content.membership);
            const membership_t was = before ? membership_from(before->membership) : membership_t{membership::other{}};
            const bool was_in = splice::visit([](auto of) { return of.in; }, was);
            const bool self = one.sender == target_id;
            splice::visit(splice::overloaded{[&](membership::join) {
                                    if (!was_in) {
                                      say(room_event::joins{}, std::format("{} joined", target));
                                    } else if (const auto old = before ? before->displayname : std::nullopt; old && *old != target) {
                                      say(room_event::names{}, std::format("{} changed their name to {}", *old, target));
                                    } else {
                                      say(room_event::avatars{}, std::format("{} changed their picture", target));
                                    }
                                  },
                                  [&](membership::leave) {
                                    splice::visit(splice::overloaded{[&](membership::ban) { say(room_event::invites{}, std::format("{} unbanned {}", who, target)); },
                                                          [&](membership::invite) {
                                                            say(room_event::invites{}, self ? std::format("{} declined the invitation", target)
                                                                     : std::format("{} withdrew {}'s invitation", who, target));
                                                          },
                                                          [&](const auto&) {
                                                            say(self ? room_event_t{room_event::joins{}} : room_event_t{room_event::invites{}}, self ? std::format("{} left", target)
                                                                     : std::format("{} removed {}", who, target));
                                                          }},
                                               was);
                                  },
                                  [&](membership::invite) { say(room_event::invites{}, std::format("{} invited {}", who, target)); },
                                  [&](membership::ban) { say(room_event::invites{}, std::format("{} banned {}", who, target)); },
                                  [&](membership::knock) { say(room_event::invites{}, std::format("{} asked to join", target)); },
                                  [&](membership::other) { say(room_event::invites{}, std::format("{} changed {}'s membership", who, target)); }},
                       now);
          },
          [&](const loom::ev::m_room_name_content_t& content) {
            say(room_event::room_name{}, content.name.empty() ? std::format("{} removed the room's name", who)
                                                              : std::format("{} renamed the room to “{}”", who, content.name));
          },
          [&](const loom::ev::m_room_topic_content_t& content) {
            say(room_event::topic{}, content.topic.empty() ? std::format("{} removed the topic", who)
                                                           : std::format("{} changed the topic to “{}”", who, content.topic));
          },
          [&](const loom::ev::m_room_avatar_content_t&) { say(room_event::room_avatar{}, std::format("{} changed the room's picture", who)); },
          [&](const loom::ev::m_room_create_content_t&) { say(room_event::other{}, std::format("{} created the room", who)); },
          [&](const loom::ev::m_room_power_levels_content_t&) { say(room_event::permissions{}, std::format("{} changed who may do what here", who)); },
          [&](const loom::ev::m_room_pinned_events_content_t&) { say(room_event::pins{}, std::format("{} changed the pinned messages", who)); },
          [&](const loom::ev::m_room_join_rules_content_t& content) {
            say(room_event::access{}, std::format("{} set who may join to “{}”", who, loom::client::choice_text(content.join_rule)));
          },
          [&](const loom::ev::m_room_history_visibility_content_t& content) {
            say(room_event::access{}, std::format("{} set who may read the history to “{}”", who,
                                                  loom::client::choice_text(content.history_visibility)));
          },
          [&](const loom::ev::m_room_canonical_alias_content_t& content) {
            const std::string alias = content.alias.value_or("");
            say(room_event::address{}, alias.empty() ? std::format("{} removed the room's address", who)
                                                     : std::format("{} set the room's address to {}", who, alias));
          },
          // A sticker: a picture, as a message with one is shown.
          [&](const loom::ev::m_sticker_content_t& content) {
            if (content.url.empty()) {
              say(room_event::other{}, std::format("{} sent a sticker", who));
              return;
            }
            mux::attachment carried;
            carried.source = content.url;
            carried.name = content.body.empty() ? std::string("sticker") : content.body;
            carried.mimetype = content.info.mimetype.value_or("");
            carried.width = static_cast<int>(content.info.w.value_or(0));
            carried.height = static_cast<int>(content.info.h.value_or(0));
            carried.kind = attachment_kind::image{.moves = moving_type(carried.mimetype)};
            message made{.in = in,
                         .id = one.event_id,
                         .sender = one.sender,
                         .at = at,
                         .body = {},
                         .outgoing = one.sender == id_.address};
            made.attachment = std::move(carried);
            sink_(change::message_added{std::move(made), where});
          },
          // Any other: said by its type's name.
          [&](const auto&) { say(room_event::other{}, std::format("{} sent {}", who, one.type)); }},
      one.content.data());
}

template <class Sink>
void account<Sink>::redaction(const conversation_id& in, const loom::ev::timeline_event& one) {
  std::optional<std::string> target = one.redacts;
  // From room version 11, in its content.
  splice::visit(splice::overloaded{[&](const loom::ev::m_room_redaction_content_t& content) {
                                     if (content.redacts)
                                       target = content.redacts;
                                   },
                                   [](const auto&) {}},
                one.content.data());
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
auto account<Sink>::body_of(std::string plain, const std::optional<std::string>& format,
                            const std::optional<std::string>& formatted_body) -> body {
  body made{std::move(plain), std::nullopt};
  if (splice::visit([](auto of) { return of.html_given; }, body_format_of(format)))
    made.html = formatted_body;
  return made;
}

}  // namespace mux::matrix
