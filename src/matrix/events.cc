// SPDX-License-Identifier: AGPL-3.0-only
// mux.matrix:events -- A room's timeline events read into messages, edits, reactions and redactions.
export module mux.matrix:events;

import std;
import mux.matrix.crypto;
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
// Extera's mark of a forwarded message, as it comes beside the content.
struct forward_mark_in {
  struct attribution_t {
    std::optional<std::string> attribution;
    knot::raw rest;
    friend consteval auto json_schema(knot::type<attribution_t>) { return knot::schema<attribution_t>().member<"rest">(knot::rest); }
  };
  // MSC2723's: the original's event, room and sender -- by its stable name
  // or its unstable one.
  struct where_t {
    std::string event_id;
    std::string room_id;
    std::string sender;
    knot::raw rest;
    friend consteval auto json_schema(knot::type<where_t>) { return knot::schema<where_t>().member<"rest">(knot::rest); }
  };
  std::optional<attribution_t> forward;
  std::optional<where_t> forwarded;
  std::optional<where_t> forwarded_unstable;
  knot::raw rest;
  friend consteval auto json_schema(knot::type<forward_mark_in>) {
    return knot::schema<forward_mark_in>()
        .member<"forward">(knot::key("xyz.extera.forward"))
        .member<"forwarded">(knot::key("m.forwarded"))
        .member<"forwarded_unstable">(knot::key("com.famedly.app.forwarded"))
        .member<"rest">(knot::rest);
  }
};
// The links of an attribution, in order: where each goes, and its words.
inline std::vector<std::pair<std::string, std::string>> links_in(std::string_view html) {
  std::vector<std::pair<std::string, std::string>> out;
  constexpr std::string_view open = "<a href=\"";
  for (std::size_t at = html.find(open); at != std::string_view::npos; at = html.find(open, at)) {
    const std::size_t from = at + open.size();
    const std::size_t quote = html.find('"', from);
    const std::size_t words = quote == std::string_view::npos ? quote : html.find('>', quote);
    const std::size_t close = words == std::string_view::npos ? words : html.find("</a>", words);
    if (close == std::string_view::npos)
      break;
    out.emplace_back(std::string(html.substr(from, quote - from)), std::string(html.substr(words + 1, close - words - 1)));
    at = close;
  }
  return out;
}
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

// The thread a message is in: its relation's root, where it is m.thread.
inline std::optional<std::string> thread_of(const loom::ev::m_room_message_content_t::m_relates_to_t& relates) {
  using values = loom::ev::m_room_message_content_t::m_relates_to_t::rel_type_values;
  if (!relates.rel_type || !relates.event_id)
    return std::nullopt;
  return splice::visit(splice::overloaded{[&](values::m_thread) { return relates.event_id; },
                                          [](const auto&) { return std::optional<std::string>(); }},
                       *relates.rel_type);
}

template <class Sink>
void account<Sink>::event(const conversation_id& in, const loom::ev::timeline_event& one, placement_t where, bool sealed) {
  const auto at = std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::milliseconds(one.origin_server_ts));
  // A verification step in the room: taken by the verification, not shown.
  const bool verification = splice::visit(
      splice::overloaded{[&](const knot::raw& raw) { return this->verification_in_room(in, one, raw, where); },
                         [](const auto&) { return false; }},
      one.content.data());
  if (verification)
    return;
  // By the content's type: a message, a reaction, or the rest by the type
  // it says.
  splice::visit(splice::overloaded{[&](const loom::ev::m_room_message_content_t& content) {
    // A verification request, to this user: asked of them (and shown as
    // the message it is).
    if (verification_request_of(content.msgtype) &&
        splice::visit(splice::overloaded{[](placement::at_end) { return true; }, [](const auto&) { return false; }}, where))
      if (auto fields = knot::try_read<crypto::room_request_fields>(content.rest.text))
        this->verification_request_in_room(in, one, *fields);
    const auto& relates = content.m_relates_to;
    // An edit: the event it replaces takes its new content.
    if (relates && replaces(*relates)) {
      if (relates->event_id && content.m_new_content)
        sink_(change::message_edited{in, *relates->event_id,
                                     body_of(content.m_new_content->body.value_or(""), content.m_new_content->format,
                                             content.m_new_content->formatted_body),
                                     one.sender, !sealed});
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
      // Encrypted: its ciphertext's URI, and what opens it kept for when it
      // is downloaded.
      if (carried.source.empty())
        if (auto sealed = knot::try_read<crypto::file_part>(content.rest.text)) {
          carried.source = sealed->file.url;
          encrypted_media_.insert_or_assign(sealed->file.url, std::move(sealed->file));
        }
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
    // Forwarded, as Extera marks it: who from and where, read from the mark;
    // the attribution its text was given dropped from what is shown -- the
    // bubble says it, as Telegram's does.
    auto mark = knot::try_read<forward_mark_in>(content.rest.text);
    // Forwarded as MSC2723 says: the content as it was, where it is from beside.
    if (mark && (mark->forwarded || mark->forwarded_unstable)) {
      const auto& where = mark->forwarded ? *mark->forwarded : *mark->forwarded_unstable;
      made.forwarded = forward_info{.from = where.sender,
                                    .name = where.sender,
                                    .link = std::format("https://matrix.to/#/{}/{}", where.room_id, where.event_id)};
    } else if (mark && mark->forward && mark->forward->attribution) {
      // Who from, as an attribution's links say: the person, then where.
      const auto from_links = [](const std::string& attribution) -> std::optional<forward_info> {
        const auto links = links_in(attribution);
        if (links.empty())
          return std::nullopt;
        constexpr std::string_view person = "https://matrix.to/#/";
        const std::string& to = links.front().first;
        return forward_info{.from = to.starts_with(person) ? to.substr(person.size()) : to,
                            .name = links.front().second,
                            .link = links.size() > 1 ? links[1].first : std::string()};
      };
      made.forwarded = from_links(*mark->forward->attribution);
      // Its attribution in bold, the message quoted under it -- and, forwarded
      // again, the same inside, as many times over: each taken off, and the
      // innermost's author said, as Telegram says a forward's first author.
      // Only the first had Extera's mark; those inside are its text alone.
      constexpr std::string_view head = "<strong>Forwarded from ", quoted = "</strong><blockquote>", tail = "</blockquote>";
      for (bool outer = true; made.forwarded && made.body.html && made.body.html->starts_with(head) &&
                              made.body.html->ends_with(tail);
           outer = false) {
        const std::string& html = *made.body.html;
        const auto inner = html.find(quoted);
        if (inner == std::string::npos)
          break;
        if (!outer)
          if (auto deeper = from_links(html.substr(std::string_view("<strong>").size(), inner - std::string_view("<strong>").size())))
            made.forwarded = std::move(deeper);
        made.body.html = html.substr(inner + quoted.size(), html.size() - inner - quoted.size() - tail.size());
      }
      while (made.forwarded && made.body.plain.starts_with("Forwarded from "))
        if (const auto line = made.body.plain.find('\n'); line != std::string::npos)
          made.body.plain = made.body.plain.substr(line + 1);
        else
          break;
    }
    if (made.body.plain.empty() && !made.body.html && !made.attachment && made.album.empty())
      made.body.plain = "Unsupported message (" + (content.msgtype.empty() ? std::string("no msgtype") : content.msgtype) + ")";
    if (relates && relates->m_in_reply_to)
      made.replies_to = relates->m_in_reply_to->event_id;
    // In a thread: its root. Its answer to the thread's latest event, where
    // it falls back, is for clients that do not know threads -- not one.
    if (relates)
      if ((made.thread = thread_of(*relates)) && relates->is_falling_back.value_or(false))
        made.replies_to.reset();
    // A thread's root: its summary, as the server counts it.
    if (one.unsigned_ && one.unsigned_->m_relations && one.unsigned_->m_relations->m_thread) {
      const auto& thread = *one.unsigned_->m_relations->m_thread;
      thread_summary summary{.count = thread.count, .participated = thread.current_user_participated};
      if (thread.latest_event) {
        summary.last_id = thread.latest_event->event_id;
        summary.last_sender = thread.latest_event->sender;
        // In an encrypted room the server's summary is no one's word: its
        // text is not shown (the thread's own messages are, decrypted).
        if (!this->encrypted_room(in.id))
          summary.last_text = thread.latest_event->content.body.value_or("");
        summary.last_at = std::chrono::sys_time<std::chrono::milliseconds>(
            std::chrono::milliseconds(thread.latest_event->origin_server_ts));
      }
      made.threaded = summary;
    }
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
    this->added(std::move(made), where, sealed);
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
                                           .reaction = true,
                                           .reaction_key = std::string(key)};
                              this->added(std::move(made), where, sealed);
                            },
                            // Else a line of its own too, quoting what it is on: shown
                            // where the chat's settings show reactions so.
                            [&](const auto&) {
                              message made{.in = in,
                                           .id = one.event_id,
                                           .sender = one.sender,
                                           .at = at,
                                           // Who reacted as a person -- a pill, as in the
                                           // member lines -- then what with.
                                           .body = mux::body{pictured ? std::format("{} reacted :emoji:", name_in(in.id, one.sender))
                                                                      : std::format("{} reacted {}", name_in(in.id, one.sender), key),
                                                             std::format(R"(<a href="https://matrix.to/#/{}">{}</a> reacted )",
                                                                         escaped(one.sender), escaped(name_in(in.id, one.sender))) +
                                                                 (pictured ? emote : escaped(key))},
                                           .replies_to = content.m_relates_to->event_id,
                                           .outgoing = one.sender == id_.address,
                                           .service = true,
                                           .event_kind = room_event::reactions{},
                                           .reaction_key = std::string(key)};
                              this->added(std::move(made), where, sealed);
                            }},
                 where);
    }
  }, [&](const auto&) {
    // The rest, by its type: loom's timeline union does not have their
    // content yet.
    const event_type_t type = event_type_of(one.type);
    // Redacted before it came here: its content emptied, so not read as its
    // type's. A message is one deleted -- its thread's summary kept, as
    // Element keeps it -- not "sent m.room.message"; a reaction, nothing.
    const bool redacted = one.unsigned_ && one.unsigned_->redacted_because;
    const auto deleted = [&] {
      message made{.in = in, .id = one.event_id, .sender = one.sender, .at = at, .body = {},
                   .outgoing = one.sender == id_.address};
      if (one.unsigned_->m_relations && one.unsigned_->m_relations->m_thread) {
        const auto& thread = *one.unsigned_->m_relations->m_thread;
        thread_summary summary{.count = thread.count, .participated = thread.current_user_participated};
        if (thread.latest_event) {
          summary.last_id = thread.latest_event->event_id;
          summary.last_sender = thread.latest_event->sender;
          // In an encrypted room the server's summary is no one's word: its
          // text is not shown (the thread's own messages are, decrypted).
          if (!this->encrypted_room(in.id))
            summary.last_text = thread.latest_event->content.body.value_or("");
          summary.last_at = std::chrono::sys_time<std::chrono::milliseconds>(
              std::chrono::milliseconds(thread.latest_event->origin_server_ts));
        }
        made.threaded = summary;
      }
      this->added(std::move(made), where, sealed);
      sink_(change::message_redacted{in, one.event_id});
    };
    splice::visit(splice::overloaded{[&](event_type::encrypted) { encrypted(in, one, at, where); },
                          [&](event_type::message) {
                            if (redacted)
                              deleted();
                            else
                              done(in, one, type, at, where);
                          },
                          [&](event_type::reaction) {
                            if (!redacted)
                              done(in, one, type, at, where);
                          },
                          [&](event_type::redaction) { redaction(in, one, at, where); },
                          [](event_type::receipt) {},
                          [&](const auto&) { done(in, one, type, at, where); }},
               type);
  }}, one.content.data());
}

template <class Sink>
void account<Sink>::encrypted(const conversation_id& in, const loom::ev::timeline_event& one,
                 std::chrono::sys_time<std::chrono::milliseconds> at, placement_t where) {
  // An encrypted event seen: the room is one, whatever its state says --
  // and was, at the latest, when this one was sent.
  this->remember_encrypted(in.id);
  // Only noted: when it began to be is not wanted here.
  std::ignore = this->encrypted_by(in.id, std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::milliseconds(one.origin_server_ts)));
  // Read with the room's Megolm session, where this device has it: the event
  // it was, its type and content, as any event is read -- the rest of it,
  // who sent it and when, the encrypted one's.
  if (crypto_) {
    std::optional<crypto::decrypted> clear;
    try {
      splice::visit(splice::overloaded{[&](const loom::ev::m_room_encrypted_content_t& content) {
                                         clear = crypto_->room_event(in.id, one.event_id, one.sender, content);
                                       },
                                       [](const auto&) {}},
                    one.content.data());
    } catch (const std::exception& failed) {
      log(id_, "encryption stopped: {}", failed.what());
      crypto_.reset();
    }
    // Read with an imported session: shown only where the sender has a
    // device with the key the session came from -- else anyone's name.
    if (clear && clear->imported_sender_key && !this->owns_key(one.sender, *clear->imported_sender_key))
      clear.reset();
    if (clear) {
      loom::ev::timeline_event made = one;
      made.type = std::move(clear->event.type);
      made.content = std::move(clear->event.content);
      splice::visit(splice::overloaded{[&](const loom::ev::m_room_encrypted_content_t& content) {
                                         if (auto outer = knot::try_read<crypto::reference_part>(content.rest.text);
                                             outer && outer->relates_to)
                                           outer_reference_ = outer->relates_to->event_id;
                                       },
                                       [](const auto&) {}},
                    one.content.data());
      // Gone however the event's reading ends -- thrown out of too: else the
      // next one read would take this one's reference.
      struct forget_reference {
        std::optional<std::string>& kept;
        ~forget_reference() { kept.reset(); }
      } const forgetting{outer_reference_};
      this->event(in, made, where, true);
      sink_(change::message_encrypted{in, one.event_id, clear->verified});
      return;
    }
  }
  // Not readable here (yet): said so.
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
                            room_event_t kind, std::optional<std::string> html) {
  sink_(change::message_added{message{.in = in,
                                      .id = one.event_id,
                                      .sender = one.sender,
                                      .at = at,
                                      .body = {std::move(said), std::move(html)},
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
  // The people in a line as people -- pills, as a mention in a message is
  // one -- in its HTML: links to them, their names as shown.
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
  const auto person = [&](const std::string& id, const std::string& name) {
    return std::format(R"(<a href="https://matrix.to/#/{}">{}</a>)", escaped(id), escaped(name));
  };
  const std::string who_link = person(one.sender, who);
  // A line of who did what: they, as a person, then what they did.
  const auto say = [&](room_event_t kind, std::string done_what) {
    service(in, one, at, where, who + done_what, kind, who_link + escaped(done_what));
  };
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
            const std::string target_link = person(target_id, target);
            // A line of who did it ({0}) and to whom ({1}): plain, and with
            // them as people.
            const auto say_people = [&](room_event_t kind, std::string_view pattern) {
              service(in, one, at, where, std::vformat(pattern, std::make_format_args(who, target)), kind,
                      std::vformat(pattern, std::make_format_args(who_link, target_link)));
            };
            splice::visit(splice::overloaded{[&](membership::join) {
                                    if (!was_in) {
                                      say_people(room_event::joins{}, "{1} joined");
                                      return;
                                    }
                                    // Joined already: what changed of how they are shown, as
                                    // Element words it -- the name set, changed or taken away,
                                    // else the picture; else nothing.
                                    const std::optional<std::string> old_name = before ? before->displayname : std::nullopt;
                                    const std::optional<std::string>& new_name = content.displayname;
                                    const auto named = [&](const std::string& shown, const std::string& line) {
                                      service(in, one, at, where, shown + line, room_event::names{},
                                              person(target_id, shown) + escaped(line));
                                    };
                                    if (old_name && new_name && *old_name != *new_name) {
                                      named(*old_name, " changed their display name to " + *new_name);
                                      return;
                                    }
                                    if (old_name && !new_name) {
                                      named(*old_name, std::format(" removed their display name ({})", *old_name));
                                      return;
                                    }
                                    if (!old_name && new_name) {
                                      named(target_id, " set their display name to " + *new_name);
                                      return;
                                    }
                                    const std::optional<std::string> old_picture = before ? before->avatar_url : std::nullopt;
                                    if (old_picture == content.avatar_url && before) {
                                      say_people(room_event::avatars{}, "{1} made no change");
                                      return;
                                    }
                                    say_people(room_event::avatars{}, !content.avatar_url ? "{1} removed their profile picture"
                                                                      : old_picture   ? "{1} changed their profile picture"
                                                                                      : "{1} set a profile picture");
                                  },
                                  [&](membership::leave) {
                                    splice::visit(splice::overloaded{[&](membership::ban) { say_people(room_event::invites{}, "{0} unbanned {1}"); },
                                                          [&](membership::invite) {
                                                            say_people(room_event::invites{}, self ? "{1} declined the invitation"
                                                                                               : "{0} withdrew {1}'s invitation");
                                                          },
                                                          [&](const auto&) {
                                                            say_people(self ? room_event_t{room_event::joins{}} : room_event_t{room_event::invites{}},
                                                                       self ? "{1} left" : "{0} removed {1}");
                                                          }},
                                               was);
                                  },
                                  [&](membership::invite) { say_people(room_event::invites{}, "{0} invited {1}"); },
                                  [&](membership::ban) { say_people(room_event::invites{}, "{0} banned {1}"); },
                                  [&](membership::knock) { say_people(room_event::invites{}, "{1} asked to join"); },
                                  [&](membership::other) { say_people(room_event::invites{}, "{0} changed {1}'s membership"); }},
                       now);
          },
          [&](const loom::ev::m_room_name_content_t& content) {
            say(room_event::room_name{}, content.name.empty() ? std::format(" removed the room's name")
                                                              : std::format(" renamed the room to “{}”", content.name));
          },
          [&](const loom::ev::m_room_topic_content_t& content) {
            say(room_event::topic{}, content.topic.empty() ? std::format(" removed the topic")
                                                           : std::format(" changed the topic to “{}”", content.topic));
          },
          [&](const loom::ev::m_room_avatar_content_t&) { say(room_event::room_avatar{}, std::format(" changed the room's picture")); },
          [&](const loom::ev::m_room_create_content_t&) { say(room_event::other{}, std::format(" created the room")); },
          [&](const loom::ev::m_room_power_levels_content_t&) { say(room_event::permissions{}, std::format(" changed who may do what here")); },
          [&](const loom::ev::m_room_pinned_events_content_t&) { say(room_event::pins{}, std::format(" changed the pinned messages")); },
          [&](const loom::ev::m_room_join_rules_content_t& content) {
            say(room_event::access{}, std::format(" set who may join to “{}”", loom::client::choice_text(content.join_rule)));
          },
          [&](const loom::ev::m_room_guest_access_content_t& content) {
            say(room_event::access{}, std::format(" set whether guests may join to “{}”", loom::client::choice_text(content.guest_access)));
          },
          [&](const loom::ev::m_room_history_visibility_content_t& content) {
            say(room_event::access{}, std::format(" set who may read the history to “{}”",
                                                  loom::client::choice_text(content.history_visibility)));
          },
          [&](const loom::ev::m_room_canonical_alias_content_t& content) {
            const std::string alias = content.alias.value_or("");
            say(room_event::address{}, alias.empty() ? std::format(" removed the room's address")
                                                     : std::format(" set the room's address to {}", alias));
          },
          // A sticker: a picture, as a message with one is shown.
          [&](const loom::ev::m_sticker_content_t& content) {
            if (content.url.empty()) {
              say(room_event::other{}, std::format(" sent a sticker"));
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
            made.sticker = true;
            // An answer, as a message is one.
            if (content.m_relates_to && content.m_relates_to->m_in_reply_to && content.m_relates_to->m_in_reply_to->event_id)
              made.replies_to = *content.m_relates_to->m_in_reply_to->event_id;
            sink_(change::message_added{std::move(made), where});
          },
          // Any other: said by its type's name.
          [&](const auto&) { say(room_event::unreadable{}, std::format(" sent {}", one.type)); }},
      one.content.data());
}

template <class Sink>
void account<Sink>::redaction(const conversation_id& in, const loom::ev::timeline_event& one,
                              std::chrono::sys_time<std::chrono::milliseconds> at, placement_t where) {
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
    // And a line of its own, quoting what it was on: shown where the chat's
    // settings show reactions taken back -- by who reacted, or by another
    // (a moderator) for them. Not for one fetched on its own, for a quote.
    const std::string& key = reaction->second.key;
    const std::string& who = reaction->second.who;
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
    const std::string by = name_in(in.id, one.sender);
    const std::string said = one.sender == who ? by + " took back" : by + " took back " + name_in(in.id, who) + "'s";
    message made{.in = in,
                 .id = one.event_id,
                 .sender = one.sender,
                 .at = at,
                 .body = pictured ? mux::body{said + " :emoji:",
                                              escaped(said) + " " +
                                                  std::format(R"(<img data-mx-emoticon src="{}" alt=":emoji:" height="32">)",
                                                              escaped(key))}
                                  : mux::body{said + " " + key, std::nullopt},
                 .replies_to = reaction->second.target,
                 .outgoing = one.sender == id_.address,
                 .service = true,
                 .event_kind = room_event::unreactions{}};
    splice::visit(splice::overloaded{[](placement::aside) {},
                                     [&](const auto&) { sink_(change::message_added{std::move(made), where}); }},
                  where);
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
