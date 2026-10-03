// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix.client:requests_send -- Messages sent, edited, removed and reacted to; pins, threads, typing, joining and members.
export module mux.proto.matrix.client:requests_send;

import std;
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
import mux.proto.matrix.crypto;
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
import :requests;
import :requests_more;

// The members defined here are declared in :account, and exported there.
namespace mux::proto::matrix::client {

// Defined further down, beside send: a message made HTML.
[[nodiscard]] inline std::optional<std::string> html_of(std::string_view body, const std::vector<mux::emote>& emotes);

template <class Sink>
void account<Sink>::edit(std::string room, std::string event, std::string text) {
  this->spawn_sending([this, room = std::move(room), event = std::move(event), text = std::move(text)] {
    if (!api_)
      return;
    // Made HTML as a message sent is: its Markdown, the room's emoji.
    const auto html = html_of(text, emotes_in(room));
    const auto content = loom::client::edit_message(event, text, html);
    if (this->send_room_event(loom::cs::send_message{.room_id = room,
                                              .event_type = "m.room.message",
                                              .txn_id = this->transaction(),
                                              .body = as_body(content)}))
      sink_(change::message_edited{{id_, room}, event, body{text, html}});
  });
}

template <class Sink>
void account<Sink>::edit_caption(std::string room, std::string event, std::string caption, mux::attachment picture) {
  this->spawn_sending([this, room = std::move(room), event = std::move(event), caption = std::move(caption),
                picture = std::move(picture)] {
    if (!api_)
      return;
    const auto content = loom::client::edit_picture(
        event, loom::client::media_said{.uri = picture.source, .name = picture.name, .caption = caption,
                                        .mimetype = picture.mimetype, .size = picture.size},
        picture.width, picture.height);
    if (this->send_room_event(loom::cs::send_message{.room_id = room,
                                              .event_type = "m.room.message",
                                              .txn_id = this->transaction(),
                                              .body = as_body(content)}))
      sink_(change::message_edited{{id_, room}, event, body{caption.empty() ? picture.name : caption, std::nullopt}});
  });
}

template <class Sink>
void account<Sink>::remove(std::string room, std::string event) {
  this->spawn_guarded([this, room = std::move(room), event = std::move(event)] {
    if (!api_)
      return;
    // Taken out only where the server did it; else why not, said.
    if (auto done = perform(*api_, loom::cs::redact_event{.room_id = room,
                                                          .event_id = event,
                                                          .txn_id = this->transaction()}))
      sink_(change::message_redacted{{id_, room}, event});
    else
      sink_(change::refused{id_, std::format("The message was not deleted: {}", done.error().said())});
  });
}

template <class Sink>
void account<Sink>::react(std::string room, std::string target, std::string key, bool on) {
  this->spawn_sending([this, room = std::move(room), target = std::move(target), key = std::move(key), on] {
    if (!api_)
      return;
    if (on) {
      auto content = loom::client::reaction(target, key);
      if (key.starts_with("mxc://")) {
        const auto emotes = emotes_in(room);
        if (const auto found = std::ranges::find(emotes, key, &mux::emote::url); found != emotes.end())
          content.rest = as_body(reaction_shortcode{std::format(":{}:", found->shortcode)});
      }
      (void)this->send_room_event(loom::cs::send_message{.room_id = room,
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
  this->spawn_guarded([this, room = std::move(room), target = std::move(target), on] {
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
  this->spawn_guarded([this, room = std::move(room)] {
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
    // Escaped as markup, and a line break a <br>.
    const char& c = body[at];
    if (c == '\n')
      html += "<br>";
    else
      html.append_range(chevron::value_reference(c));
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
  this->spawn_sending([this, room = std::move(room), body = std::move(body), reply_to = std::move(reply_to),
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
    this->send_text(in, room, txn, as_body(content));
  });
}

// A text message sent under its transaction ID: acknowledged with the
// event ID the server gave it, or marked failed.
template <class Sink>
void account<Sink>::send_text(const conversation_id& in, const std::string& room, const std::string& txn, knot::raw body) {
  auto sent = this->send_room_event(
      loom::cs::send_message{.room_id = room, .event_type = "m.room.message", .txn_id = txn, .body = std::move(body)});
  if (!sent) {
    sink_(change::delivery_changed{in, txn, delivery::failed{}});
    return;
  }
  sink_(change::message_acknowledged{in, txn, sent->event_id});
}

template <class Sink>
void account<Sink>::list_threads(std::string room) {
  this->spawn_guarded([this, room = std::move(room)] {
    if (!api_)
      return;
    using asked = loom::cs::get_thread_roots;
    auto got = perform(*api_, asked{.room_id = room, .include = asked::include_t{asked::include_values::all{}}, .limit = 50});
    if (!got) {
      log(id_, "the threads of {}: {}", room, got.error().said());
      return;
    }
    const conversation_id in{id_, room};
    std::vector<std::string> roots;
    for (const auto& one : got->chunk) {
      roots.push_back(one.event_id);
      this->event(in, one, placement::aside{});
    }
    sink_(change::threads_listed{in, std::move(roots)});
  });
}

template <class Sink>
void account<Sink>::load_thread(std::string room, std::string root) {
  this->spawn_guarded([this, room = std::move(room), root = std::move(root)] {
    if (!api_)
      return;
    using asked = loom::cs::get_relating_events_with_rel_type;
    const conversation_id in{id_, room};
    // Up to five pages of a hundred, newest first: each answer a message
    // in the thread.
    std::optional<std::string> from;
    for (int page = 0; page < 5; ++page) {
      auto got = perform(*api_, asked{.room_id = room, .event_id = root, .rel_type = "m.thread", .from = from, .limit = 100,
                                      .dir = asked::dir_t{asked::dir_values::b{}}});
      if (!got) {
        log(id_, "the thread {}: {}", root, got.error().said());
        return;
      }
      for (const auto& one : got->chunk)
        this->event(in, one, placement::aside{});
      if (!got->next_batch)
        break;
      from = got->next_batch;
    }
  });
}

template <class Sink>
void account<Sink>::send_in_thread(std::string room, std::string body, std::string root, std::string latest, std::optional<std::string> reply_to) {
  this->spawn_sending([this, room = std::move(room), body = std::move(body), root = std::move(root), latest = std::move(latest),
                 reply_to = std::move(reply_to)] {
    const std::string txn = this->transaction();
    const conversation_id in{id_, room};
    const auto html = html_of(body, emotes_in(room));
    sink_(change::message_added{message{.in = in,
                                        .id = txn,
                                        .sender = id_.address,
                                        .at = std::chrono::time_point_cast<std::chrono::milliseconds>(
                                            std::chrono::system_clock::now()),
                                        .body = {body, html},
                                        .replies_to = reply_to,
                                        .outgoing = true,
                                        .delivery = delivery::sending{},
                                        .thread = root}});
    if (!api_) {
      sink_(change::delivery_changed{in, txn, delivery::failed{}});
      return;
    }
    const auto content = loom::client::text_message(
        loom::client::text_said{.body = body, .html = html, .reply_to = reply_to, .thread = root, .thread_latest = latest});
    this->send_text(in, room, txn, as_body(content));
  });
}

template <class Sink>
void account<Sink>::typing(std::string room, bool on) {
  this->spawn_guarded([this, room = std::move(room), on] {
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
  this->spawn_guarded([this, room = std::move(room), via = std::move(via)] {
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
  this->spawn_guarded([this, room = std::move(room)] {
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

}  // namespace mux::proto::matrix::client

namespace mux::proto::matrix::client {
template <class Sink>
void account<Sink>::reset_backup() {
  this->spawn_guarded([this] {
    if (!crypto_ || !api_)
      return;
    const auto keys = crypto_->cross_signing_keys();
    if (!keys) {
      sink_(change::refused{id_, "Not reset: this session has no cross-signing keys. Set up cross-signing, or restore "
                                 "it with your recovery key, first."});
      return;
    }
    if (const auto old = crypto_->backup())
      (void)perform(*api_, loom::cs::delete_room_keys_version{.version = old->first});
    crypto_->forget_backup();
    const auto backup_secret = this->make_backup(*keys);
    const auto recovery = this->store_secrets(*keys, backup_secret);
    sink_(change::notice{id_, "Key backup reset",
                         recovery ? std::format("A new key backup holds your room keys from now. Your cross-signing keys and "
                                                "its key are kept on your server under this new recovery key -- the old "
                                                "one no longer opens them. Write it down:\n\n{}",
                                                *recovery)
                                  : std::string("A new key backup holds your room keys from now.")});
  });
}
template <class Sink>
void account<Sink>::delete_backup() {
  this->spawn_guarded([this] {
    if (!crypto_ || !api_)
      return;
    const auto old = crypto_->backup();
    if (!old) {
      sink_(change::refused{id_, "This session writes to no key backup."});
      return;
    }
    if (auto gone = perform(*api_, loom::cs::delete_room_keys_version{.version = old->first}); !gone) {
      sink_(change::refused{id_, "The key backup was not deleted: " + gone.error().said()});
      return;
    }
    crypto_->forget_backup();
    sink_(change::notice{id_, "Key backup deleted",
                         "Your room keys are no longer backed up on your server: a session you sign in to anew will not "
                         "read what was said before it."});
  });
}
template <class Sink>
void account<Sink>::sign_out_unverified(std::string password) {
  this->spawn_guarded([this, password = std::move(password)] {
    if (!crypto_ || !api_)
      return;
    const auto all = this->own_sessions_now();
    if (!all)
      return;
    std::vector<std::string> unverified =
        *all | std::views::filter([&](const own_session& one) { return one.id != crypto_->device_id() && !one.trusted; }) |
        std::views::transform(&own_session::id) | std::ranges::to<std::vector>();
    if (unverified.empty()) {
      sink_(change::notice{id_, "Sign out unverified sessions", "Every other session of yours is verified."});
      return;
    }
    this->sign_out_sessions(std::move(unverified), password);
  });
}
}  // namespace mux::proto::matrix::client
