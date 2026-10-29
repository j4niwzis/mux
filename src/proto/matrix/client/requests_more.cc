// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix.client:requests_more -- A room's gap caught up on, rooms looked up and made, forwards, profiles, sessions, cross-signing and the key backup.
export module mux.proto.matrix.client:requests_more;

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
import loom.cs.profile;
import loom.cs.presence;
import loom.cs.device_management;
import mux.config;
import mux.core;
import mux.http;
import mux.net;
import loom.cs.pusher;
import mux.logic.formatting;
import loom.names;
import :account;
import :requests;

// The members defined here are declared in :account, and exported there.
namespace mux::proto::matrix::client {

template <class Sink>
void account<Sink>::catch_up(std::string room, std::string from, std::string until) {
  this->spawn_guarded([this, room = std::move(room), from = std::move(from), until = std::move(until)] {
    const conversation_id in{id_, room};
    std::map<std::string, std::string> sender_of;  // of every event the pages held
    struct reaction_found {
      std::string event, target;
      std::chrono::sys_time<std::chrono::milliseconds> at;
    };
    std::vector<reaction_found> reactions;
    std::optional<std::string> token = from;
    std::size_t read = 0, mentions = 0;
    // The gap's events, newest first as the pages bring them.
    std::vector<loom::ev::timeline_event> gap;
    // At most ten pages of a hundred: a gap longer than that is the
    // history's, paged back to when read.
    for (int page = 0; page < 10 && api_ && token; ++page) {
      // Behind what the user waits on: after a wake, every room with a gap
      // asked ten pages of matrix.org at once, each taking seconds, and a
      // message sent waited a minute for a connection.
      auto got = perform(*api_,
                         loom::cs::get_room_events{.room_id = room,
                                                   .from = token,
                                                   .dir = loom::cs::get_room_events::dir_values::b{},
                                                   .limit = 100},
                         std::chrono::seconds(60), http::lane::background);
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
        gap.push_back(one);
        if (one.sender == id_.address)
          continue;
        const auto at = std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::milliseconds(one.origin_server_ts));
        spl::visit(
            spl::overloaded{
                [&](const loom::ev::m_room_message_content_t& content) {
                  // Not an edit: it is never shown under its own id -- its
                  // mark said "Loading..." for ever, the server finding it.
                  using values = loom::ev::m_room_message_content_t::m_relates_to_t::rel_type_values;
                  const bool edit = content.m_relates_to && content.m_relates_to->rel_type &&
                                    spl::visit(spl::overloaded{[](values::m_replace) { return true; },
                                                                     [](const auto&) { return false; }},
                                                  *content.m_relates_to->rel_type);
                  // An edit that mentions the user: the message it edits, as
                  // a live one is (tdesktop's mention added by an edit).
                  if (edit && content.m_relates_to->event_id && loom::client::mentions(content, id_.address)) {
                    ++mentions;
                    sink_(change::mentioned{in, *content.m_relates_to->event_id, at});
                  }
                  if (!edit && loom::client::mentions(content, id_.address)) {
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
    // Into the timeline, oldest first, by their time: between what was there
    // and what the sync brought.
    // News still, to a plugin that hears it: a call's signals in the gap,
    // each judged by its call, an invite by its lifetime.
    {
      const late_news_while news{late_news_, true};
      for (const auto& one : gap | std::views::reverse)
        this->event(in, one, placement::in_window{});
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
    log(id_, "caught up on {}: {} event{} put in, {} mention{}, {} reaction{} to yours", room, read, read == 1 ? "" : "s",
        mentions, mentions == 1 ? "" : "s", to_mine, to_mine == 1 ? "" : "s");
  });
}

template <class Sink>
void account<Sink>::preview_room(std::string room, std::vector<std::string> via) {
  this->spawn_guarded([this, room = std::move(room), via = std::move(via)] {
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
                                  .members = got->num_joined_members,
                                  .knock = spl::visit(spl::overloaded{[](mux::proto::matrix::join_rule::knock) { return true; },
                                                                            [](const mux::proto::matrix::join_rule::knock_restricted&) { return true; },
                                                                            [](const auto&) { return false; }},
                                                         join_rule_of(got->join_rule))}});
  });
}

// Asked to be let in, where the room lets people knock.
template <class Sink>
void account<Sink>::knock(std::string room, std::vector<std::string> via, std::string reason) {
  this->spawn_guarded([this, room = std::move(room), via = std::move(via), reason = std::move(reason)] {
    if (!api_)
      return;
    auto got = perform(*api_, loom::cs::knock_room{.room_id_or_alias = room,
                                                   .via = via.empty() ? std::nullopt : std::optional<std::vector<std::string>>(via),
                                                   .body = {.reason = reason.empty() ? std::nullopt : std::optional<std::string>(reason)}});
    if (!got)
      log(id_, "could not knock on {}: {}", room, got.error().said());
  });
}

template <class Sink>
void account<Sink>::create_group(std::string name) {
  this->spawn_guarded([this, name = std::move(name)] {
    if (!api_)
      return;
    auto made = perform(*api_, loom::cs::create_room{.body = {.name = name,
                                                              .initial_state = encrypted_from_the_start(),
                                                              .preset = loom::cs::create_room::body_t::preset_values::private_chat{}}});
    if (!made) {
      log(id_, "could not make the room {}: {}", name, made.error().said());
      return;
    }
    this->remember_encrypted(made->room_id);
    sink_(change::room_created{{id_, made->room_id}});
  });
}

// Where a forwarded message is from, beside its content (MSC2723, under
// its unstable name): the original's event, room, sender and time.
struct forwarded_mark {
  struct where_t {
    std::string event_id;
    std::string room_id;
    std::string sender;
    std::int64_t origin_server_ts = 0;
    friend consteval auto json_schema(knot::type<where_t>) { return knot::schema<where_t>(); }
  };
  where_t forwarded;
  friend consteval auto json_schema(knot::type<forwarded_mark>) {
    return knot::schema<forwarded_mark>().member<"forwarded">(knot::key("com.famedly.app.forwarded"));
  }
};

template <class Sink>
void account<Sink>::forward(std::string from, std::vector<forward_source> events, std::string to) {
  this->spawn_sending([this, from = std::move(from), events = std::move(events), to = std::move(to)] {
    // Fetching and sending each event finishes before the next begins.
    // Separate fibers let a faster fetch overtake an earlier selection.
    for (const auto& source : events) {
      const auto& event = source.event;
      if (!api_)
        return;
      auto got = perform(*api_, loom::cs::get_one_room_event{.room_id = from, .event_id = event});
      if (!got) {
        log(id_, "could not fetch {} to forward: {}", event, got.error().said());
        continue;
      }
      // A message's content as it is typed, without what it answered or
      // replaced: sent on as a message of its own. Anything else is not
      // forwarded.
      std::optional<loom::ev::m_room_message_content_t> content;
      spl::visit(spl::overloaded{[&](const loom::ev::m_room_message_content_t& one) { content = one; },
                                       [](const auto&) {}},
                    got->content.data());
      if (!content) {
        log(id_, "{} is not a message: not forwarded", event);
        continue;
      }
      if (source.replacement) {
        content->body = source.replacement->plain;
        content->formatted_body = source.replacement->html;
        content->format = source.replacement->html ? std::optional<std::string>("org.matrix.custom.html") : std::nullopt;
      }
      content->m_relates_to.reset();
      content->m_new_content.reset();
      // The original's own forward marks let go: a forward of a forward is
      // from the message forwarded, not from where that one came. Kept, the
      // original's Extera attribution went on with this one, and a client
      // reading it named the first sender, not the one forwarded from.
      content->m_forwarded.reset();
      content->com_famedly_app_forwarded.reset();
      content->xyz_extera_forward.reset();
      // As MSC2723 forwards: the current content, and where it is from
      // beside it -- no words added to it; a client shows the forward its way.
      content->rest = as_body(forwarded_mark{.forwarded = {.event_id = event,
                                                           .room_id = from,
                                                           .sender = got->sender,
                                                           .origin_server_ts = static_cast<std::int64_t>(got->origin_server_ts)}});
      // And as MSC4553 marks it, which mux reads first: where it is from and
      // the original's content as it was -- what a client that knows the
      // mark shows.
      // The original's content whole -- its text and what it carries, a
      // picture's url and info, an encrypted one's file -- by knot's typed
      // conversion: the mark alone, a picture forwarded showed nothing in a
      // client that reads the mark.
      using msc4553 = std::remove_cvref_t<decltype(*content->org_matrix_msc4553_forwarded)>;
      content->org_matrix_msc4553_forwarded = msc4553{
          .event_id = event,
          .room_id = from,
          .sender = got->sender,
          .origin_server_ts = static_cast<std::int64_t>(got->origin_server_ts),
          .content = knot::convert<typename msc4553::content_t>(*content)};
      // And its fallback, as MSC4553 has it, for a client that knows no
      // forwards: who it is from and a link to the original over the body,
      // and the original quoted under the same in HTML. A file's or a
      // picture's body is its name or caption, kept as it is.
      if (!content->url && !content->file) {
        const std::string link = std::format("https://matrix.to/#/{}/{}", from, event);
        const std::string quoted = spl::visit([](auto of) { return of.html_given; }, loom::names::body_format_of(content->format)) &&
                                           content->formatted_body
                                       ? *content->formatted_body
                                       : chevron::escaped(content->body);
        content->formatted_body = std::format(
            R"(<strong>Forwarded from <a href="https://matrix.to/#/{0}">{0}</a> - <a href="{1}">view original message</a></strong><br><blockquote>{2}</blockquote>)",
            chevron::escaped(got->sender), chevron::escaped(link), quoted);
        content->format = "org.matrix.custom.html";
        content->body = std::format("Forwarded from {} - view original message: {}\n{}", got->sender, link, content->body);
      }
      auto done = this->send_room_event(loom::cs::send_message{.room_id = to,
                                                        .event_type = "m.room.message",
                                                        .txn_id = this->transaction(),
                                                        .body = as_body(*content)});
      if (!done)
        log(id_, "could not forward {} to {}: {}", event, to, done.error().said());
    }
  });
}

// MSC4247, MSC4427 and MSC4440 are parsed directly from the profile
// response. Their unstable names are aliases at this boundary, never keys
// looked up in the generated API's opaque additional JSON.
namespace profile_wire {
struct pronoun {
  std::string language, summary;
  friend consteval auto json_schema(knot::type<pronoun>) { return knot::schema<pronoun>(); }
};
namespace mime {
struct plain { static constexpr std::string_view json_value = "text/plain"; };
struct html { static constexpr std::string_view json_value = "text/html"; };
}
using mime_t = spl::variant<mime::plain, mime::html, std::string>;
struct representation {
  std::string body;
  std::optional<mime_t> mimetype;
  friend consteval auto json_schema(knot::type<representation>) { return knot::schema<representation>(); }
};
struct biography {
  std::vector<representation> text;
  friend consteval auto json_schema(knot::type<biography>) {
    return knot::schema<biography>().member<"text">(knot::key("m.text"));
  }
};
struct profile {
  std::optional<std::string> displayname, avatar_url, banner, unstable_banner;
  std::optional<std::vector<pronoun>> pronouns, unstable_pronouns;
  std::optional<biography> bio, unstable_bio;
  friend consteval auto json_schema(knot::type<profile>) {
    return knot::schema<profile>()
        .member<"banner">(knot::key("m.banner_url"))
        .member<"unstable_banner">(knot::key("chat.commet.profile_banner"))
        .member<"pronouns">(knot::key("m.pronouns"))
        .member<"unstable_pronouns">(knot::key("io.fsky.nyx.pronouns"))
        .member<"bio">(knot::key("m.biography"))
        .member<"unstable_bio">(knot::key("gay.fomx.biography"));
  }
};
struct get {
  std::string user_id;
  using response = profile;
  auto to_head() const { return loom::cs::get_user_profile{.user_id = user_id}.to_head(); }
  void write_body(auto&&) const {}
  auto to_send() const { return loom::detail::collect_request(*this); }
};
inline body biography_of(const biography& bio) {
  std::optional<std::string> plain, html;
  for (const auto& text : bio.text) {
    spl::visit(spl::overloaded{
        [&](mime::plain) { if (!plain) plain = text.body; },
        [&](mime::html) { if (!html) html = text.body; },
        [](const std::string&) {}}, text.mimetype.value_or(mime::plain{}));
  }
  return {plain.value_or(""), std::move(html)};
}
inline profile_details details_of(const profile& wire) {
  profile_details out{.banner = wire.banner ? wire.banner : wire.unstable_banner};
  const auto& pronouns = wire.pronouns ? wire.pronouns : wire.unstable_pronouns;
  if (pronouns) out.pronouns = (*pronouns) | std::views::transform([](const auto& p) { return mux::pronouns{p.language, p.summary}; }) | std::ranges::to<std::vector<mux::pronouns>>();
  const auto& bio = wire.bio ? wire.bio : wire.unstable_bio;
  if (bio) out.biography = biography_of(*bio);
  return out;
}
struct display_name {
  static constexpr std::string_view key = "displayname";
  std::string displayname;
  friend consteval auto json_schema(knot::type<display_name>) { return knot::schema<display_name>(); }
};
struct banner_key { static constexpr std::string_view value = "chat.commet.profile_banner"; };
struct stable_banner_key { static constexpr std::string_view value = "m.banner_url"; };
struct pronouns_key { static constexpr std::string_view value = "io.fsky.nyx.pronouns"; };
struct stable_pronouns_key { static constexpr std::string_view value = "m.pronouns"; };
struct biography_key { static constexpr std::string_view value = "gay.fomx.biography"; };
struct stable_biography_key { static constexpr std::string_view value = "m.biography"; };
template <class Key, class Value> struct field {
  static constexpr std::string_view key = Key::value;
  Value value;
  friend consteval auto json_schema(knot::type<field>) { return knot::schema<field>().template member<"value">(knot::key(key)); }
};
using banner = field<banner_key, std::string>;
using stable_banner = field<stable_banner_key, std::string>;
using pronouns = field<pronouns_key, std::vector<pronoun>>;
using stable_pronouns = field<stable_pronouns_key, std::vector<pronoun>>;
using bio = field<biography_key, biography>;
using stable_bio = field<stable_biography_key, biography>;
template <class Field> struct put {
  std::string user;
  Field body;
  bool unstable = false;
  using response = knot::raw;
  auto to_head() const {
    auto head = loom::cs::set_profile_field{.user_id = user, .key_name = std::string(Field::key)}.to_head();
    if (unstable) head.target = "/_matrix/client/unstable/uk.tcpip.msc4133/profile/" + loom::percent_encoded(user) + "/" + loom::percent_encoded(Field::key);
    return head;
  }
  void write_body(auto&& sink) const { knot::write_chunks(std::forward<decltype(sink)>(sink), body); }
  auto to_send() const { return loom::detail::collect_request(*this); }
};
template <class Field> struct remove {
  std::string user;
  bool unstable = false;
  using response = knot::raw;
  auto to_head() const {
    auto head = loom::cs::delete_profile_field{.user_id = user, .key_name = std::string(Field::key)}.to_head();
    if (unstable) head.target = "/_matrix/client/unstable/uk.tcpip.msc4133/profile/" + loom::percent_encoded(user) + "/" + loom::percent_encoded(Field::key);
    return head;
  }
  void write_body(auto&&) const {}
  auto to_send() const { return loom::detail::collect_request(*this); }
};

}

template <class Sink>
void account<Sink>::fetch_profile(std::string user) {
  this->spawn_guarded([this, user = std::move(user)] {
    if (!api_) return;
    auto got = perform(*api_, profile_wire::get{user});
    if (!got) {
      log(id_, "no profile of {}: {}", user, got.error().said());
      return;
    }
    sink_(change::profile_found{id_, user, got->displayname, got->avatar_url, profile_wire::details_of(*got)});
  });
}

template <class Sink>
void account<Sink>::set_profile(user_profile profile) {
  this->spawn_guarded([this, profile = std::move(profile)] {
    if (!api_) { sink_(change::profile_saved{id_, "The account is offline."}); return; }
    const auto current = perform(*api_, profile_wire::get{id_.address});
    if (!current) { sink_(change::profile_saved{id_, current.error().said()}); return; }
    std::optional<std::string> error;
    const auto send_request = [&](auto request) {
      if (error) return;
      if (!api_) { error = "Disconnected before the profile could be saved."; return; }
      auto done = perform(*api_, request);
      // An unrecognised endpoint permits the MSC4133 fallback. Never
      // retry authorization failures through another API.
      if (!done && done.error().server && done.error().server->status == 404 && api_) {
        request.unstable = true;
        done = perform(*api_, request);
      }
      if (!done) error = done.error().said();
    };
    const auto send = [&](auto field) { send_request(profile_wire::put{id_.address, std::move(field)}); };
    send(profile_wire::display_name{profile.name.value_or("")});
    auto pronouns = profile.details.pronouns | std::views::transform([](const auto& p) { return profile_wire::pronoun{p.language, p.summary}; }) | std::ranges::to<std::vector<profile_wire::pronoun>>();
    if (current->pronouns) send(profile_wire::stable_pronouns{std::move(pronouns)});
    else send(profile_wire::pronouns{std::move(pronouns)});
    profile_wire::biography biography{{profile_wire::representation{profile.details.biography.plain, profile_wire::mime::plain{}}}};
    if (profile.details.biography.html) biography.text.insert(biography.text.begin(), {*profile.details.biography.html, profile_wire::mime::html{}});
    if (current->bio) send(profile_wire::stable_bio{std::move(biography)});
    else send(profile_wire::bio{std::move(biography)});
    if (profile.details.banner && !profile.details.banner->empty()) {
      if (current->banner) send(profile_wire::stable_banner{*profile.details.banner});
      else send(profile_wire::banner{*profile.details.banner});
    } else {
      if (current->banner) send_request(profile_wire::remove<profile_wire::stable_banner>{id_.address});
      if (current->unstable_banner) send_request(profile_wire::remove<profile_wire::banner>{id_.address});
    }
    if (!error) sink_(change::profile_found{id_, id_.address, profile.name, profile.avatar, profile.details});
    sink_(change::profile_saved{id_, std::move(error)});
  });
}

template <class Sink>
void account<Sink>::set_display_name(std::string name) {
  this->spawn_guarded([this, name = std::move(name)] {
    if (!api_) { sink_(change::profile_saved{id_, "The account is offline."}); return; }
    auto request = profile_wire::put{id_.address, profile_wire::display_name{name}};
    auto done = perform(*api_, request);
    // An unrecognised endpoint permits the MSC4133 fallback, as set_profile.
    if (!done && done.error().server && done.error().server->status == 404 && api_) {
      request.unstable = true;
      done = perform(*api_, request);
    }
    if (!done) {
      sink_(change::refused{id_, "Could not change the name: " + done.error().said()});
      return;
    }
    sink_(change::notice{id_, "Name changed", "The account's name is now " + name + "."});
  });
}

template <class Sink>
void account<Sink>::upload_profile_banner(std::string bytes, std::string name, std::string mime) {
  this->spawn_guarded([this, bytes = std::move(bytes), name = std::move(name), mime = std::move(mime)] {
    auto uri = upload_content(bytes, name, mime);
    if (!uri) { sink_(change::profile_saved{id_, uri.error().said()}); return; }
    if (!api_) { sink_(change::profile_saved{id_, "Disconnected before the banner could be saved."}); return; }
    auto current = perform(*api_, profile_wire::get{id_.address});
    if (!current) { sink_(change::profile_saved{id_, current.error().said()}); return; }
    const auto put = [&](auto field) {
      auto request = profile_wire::put{id_.address, field};
      auto done = perform(*api_, request);
      if (!done && done.error().server && done.error().server->status == 404 && api_) {
        request.unstable = true; done = perform(*api_, request);
      }
      return done;
    };
    auto saved = current->banner ? put(profile_wire::stable_banner{*uri}) : put(profile_wire::banner{*uri});
    if (!saved) { sink_(change::profile_saved{id_, saved.error().said()}); return; }
    auto details = profile_wire::details_of(*current); details.banner = *uri;
    sink_(change::profile_found{id_, id_.address, current->displayname, current->avatar_url, std::move(details)});
    sink_(change::profile_saved{id_, {}});
  });
}
template <class Sink>
void account<Sink>::remove_profile_banner() {
  this->spawn_guarded([this] {
    if (!api_) { sink_(change::profile_saved{id_, "The account is offline."}); return; }
    auto current = perform(*api_, profile_wire::get{id_.address});
    if (!current) { sink_(change::profile_saved{id_, current.error().said()}); return; }
    const auto remove = [&](auto request) {
      if (!api_) return std::expected<knot::raw, failure>(std::unexpected(failure{{}, "The account is offline."}));
      auto done = perform(*api_, request);
      if (!done && done.error().server && done.error().server->status == 404 && api_) {
        request.unstable = true; done = perform(*api_, request);
      }
      return done;
    };
    if (current->banner) {
      auto done = remove(profile_wire::remove<profile_wire::stable_banner>{id_.address});
      if (!done) { sink_(change::profile_saved{id_, done.error().said()}); return; }
    }
    if (current->unstable_banner) {
      auto done = remove(profile_wire::remove<profile_wire::banner>{id_.address});
      if (!done) { sink_(change::profile_saved{id_, done.error().said()}); return; }
    }
    auto details = profile_wire::details_of(*current); details.banner.reset();
    sink_(change::profile_found{id_, id_.address, current->displayname, current->avatar_url, std::move(details)});
    sink_(change::profile_saved{id_, {}});
  });
}

template <class Sink>
void account<Sink>::fetch_presence(std::string user) {
  this->spawn_guarded([this, user = std::move(user)] {
    if (!api_) return;
    auto got = perform(*api_, loom::cs::get_presence{.user_id = user});
    if (!got) return;
    using values = loom::cs::get_presence::response_t::presence_values;
    auto state = spl::visit(spl::overloaded{
        [](values::online) -> availability_t { return availability::online{}; },
        [](values::unavailable) -> availability_t { return availability::away{}; },
        [](values::offline) -> availability_t { return availability::offline{}; },
        [](const std::string&) -> availability_t { return availability::offline{}; }}, got->presence);
    sink_(change::presence_changed{id_, user, {std::move(state), got->status_msg}});
  });
}

template <class Sink>
void account<Sink>::set_presence(presence now) {
  this->spawn_guarded([this, now = std::move(now)] {
    if (!api_) return;
    using values = loom::cs::set_presence::body_t::presence_values;
    auto state = spl::visit(spl::overloaded{
        [](availability::online) -> loom::cs::set_presence::body_t::presence_t { return values::online{}; },
        [](availability::chat) -> loom::cs::set_presence::body_t::presence_t { return values::online{}; },
        [](availability::away) -> loom::cs::set_presence::body_t::presence_t { return values::unavailable{}; },
        [](availability::extended_away) -> loom::cs::set_presence::body_t::presence_t { return values::unavailable{}; },
        [](availability::do_not_disturb) -> loom::cs::set_presence::body_t::presence_t { return values::unavailable{}; },
        [](availability::offline) -> loom::cs::set_presence::body_t::presence_t { return values::offline{}; }}, now.state);
    auto done = perform(*api_, loom::cs::set_presence{.user_id = id_.address, .body = {.presence = state, .status_msg = now.status}});
    if (!done) { sink_(change::refused{id_, "Presence: " + done.error().said()}); return; }
    using sync_values = loom::cs::sync::set_presence_values;
    chosen_presence_ = spl::visit(spl::overloaded{
        [](values::online) -> loom::cs::sync::set_presence_t { return sync_values::online{}; },
        [](values::unavailable) -> loom::cs::sync::set_presence_t { return sync_values::unavailable{}; },
        [](values::offline) -> loom::cs::sync::set_presence_t { return sync_values::offline{}; },
        [](const std::string& unknown) -> loom::cs::sync::set_presence_t { return unknown; }}, state);
    cut_long_poll();
    sink_(change::presence_changed{id_, id_.address, now});
  });
}

// The password answered, as the spec's interactive auth takes it: who, by
// their user ID, and the password -- beside its type and session.
struct password_auth {
  struct identifier_t {
    std::string type = "m.id.user";
    std::string user;
    friend consteval auto json_schema(knot::type<identifier_t>) { return knot::schema<identifier_t>(); }
  };
  identifier_t identifier;
  std::string password;
  friend consteval auto json_schema(knot::type<password_auth>) { return knot::schema<password_auth>(); }
};

template <class Sink>
void account<Sink>::list_sessions() {
  this->spawn_guarded([this] {
    if (!api_)
      return;
    auto got = perform(*api_, loom::cs::get_devices{});
    if (!got) {
      sink_(proto::matrix::sessions_refused{id_, "Could not list the sessions: " + got.error().said()});
      return;
    }
    std::vector<mux::proto::matrix::session_info> out =
        got->devices.value_or(std::vector<loom::cs::def::device_t>{}) | std::views::transform([](const loom::cs::def::device_t& one) {
          return mux::proto::matrix::session_info{
              .id = one.device_id,
              .name = one.display_name.value_or(""),
              .ip = one.last_seen_ip,
              .last_seen = one.last_seen_ts ? std::optional(std::chrono::sys_time<std::chrono::milliseconds>(
                                                  std::chrono::milliseconds(*one.last_seen_ts)))
                                            : std::nullopt};
        }) | std::ranges::to<std::vector>();
    sink_(proto::matrix::sessions_listed{id_, how_.device_id.value_or(""), std::move(out)});
    sink_(this->whole().security_now());
  });
}

template <class Sink>
void account<Sink>::rename_session(std::string device, std::string name) {
  this->spawn_guarded([this, device = std::move(device), name = std::move(name)] {
    if (!api_)
      return;
    auto done = perform(*api_, loom::cs::update_device{.device_id = device, .body = {.display_name = name}});
    if (!done)
      sink_(proto::matrix::sessions_refused{id_, "Could not rename the session: " + done.error().said()});
    this->list_sessions();
  });
}

template <class Sink>
void account<Sink>::sign_out_sessions(std::vector<std::string> devices, std::string password) {
  this->spawn_guarded([this, devices = std::move(devices), password = std::move(password)] {
    if (!api_ || devices.empty())
      return;
    using body_t = loom::cs::delete_devices::body_t;
    // Asked first without: the server says what it wants, and its session.
    auto first = perform(*api_, loom::cs::delete_devices{.body = body_t{.devices = devices}});
    if (first) {
      this->list_sessions();
      return;
    }
    const auto& said = first.error().server;
    if (!said || said->status != 401 || !said->session) {
      sink_(proto::matrix::sessions_refused{id_, "Could not sign out: " + first.error().said()});
      return;
    }
    const std::string given = !password.empty() ? password : how_.password;
    const bool asked_elsewhere = spl::visit(
        spl::overloaded{[](const uia_password&) { return false; },
                        [&](const uia_browser& page) {
                          uia_ = pending_uia{*said->session, uia_sign_out{devices}};
                          sink_(proto::matrix::uia_in_browser{id_, page.url, "Sign sessions out"});
                          return true;
                        },
                        [&](const uia_none&) {
                          sink_(proto::matrix::sessions_refused{id_, "Your password is needed to sign sessions out.", true});
                          return true;
                        }},
        this->uia_answer(*said, given));
    if (asked_elsewhere)
      return;
    body_t::authentication_data_t auth{.type = "m.login.password", .session = *said->session};
    auth.rest = as_body(password_auth{.identifier = {.user = how_.user_id}, .password = given});
    auto done = perform(*api_, loom::cs::delete_devices{.body = body_t{.devices = devices, .auth = std::move(auth)}});
    if (!done) {
      const bool wrong = done.error().server && done.error().server->status == 401;
      sink_(proto::matrix::sessions_refused{id_, wrong ? std::string("The password was not accepted.") : "Could not sign out: " + done.error().said(),
                                     wrong});
      return;
    }
    this->list_sessions();
  });
}



template <class Sink>
auto account<Sink>::uia_answer(const loom::error& said, const std::string& given) -> uia_way_t {
  const bool by_password = !said.auth || std::ranges::any_of(said.auth->flows, [](const std::vector<loom::auth_stage_t>& flow) {
    return flow == std::vector<loom::auth_stage_t>{loom::auth_stage::password{}};
  });
  if (by_password && !given.empty())
    return uia_password{given};
  if (!said.auth || !said.session || said.auth->flows.empty())
    return uia_none{};
  if (said.auth->reset_url)
    return uia_browser{*said.auth->reset_url};
  const auto& flow = said.auth->flows.front();
  const auto stage = std::ranges::find_if(flow, [&](const loom::auth_stage_t& one) { return !std::ranges::contains(said.auth->completed, one); });
  const auto home = this->homeserver();
  if (stage == flow.end() || !home)
    return uia_none{};
  return uia_browser{std::format("https://{}{}{}/_matrix/client/v3/auth/{}/fallback/web?session={}", home->host,
                                 home->port == 443 ? std::string() : std::format(":{}", home->port), home->path,
                                 loom::name_of(*stage), *said.session)};
}

template <class Sink>
void account<Sink>::continue_uia() {
  this->spawn_guarded([this] {
    if (!api_ || !uia_)
      return;
    pending_uia pending = std::move(*std::exchange(uia_, std::nullopt));
    spl::visit(
        spl::overloaded{
            [&](uia_sign_out& one) {
              using body_t = loom::cs::delete_devices::body_t;
              auto done = perform(*api_, loom::cs::delete_devices{
                                             .body = body_t{.devices = one.devices,
                                                            .auth = body_t::authentication_data_t{.session = pending.session}}});
              if (!done)
                sink_(proto::matrix::sessions_refused{id_, "Could not sign out: " + done.error().said()});
              this->list_sessions();
            },
            [&](uia_whole) { this->whole().continue_uia(pending.session); }},
        pending.what);
  });
}








template <class Sink>
void account<Sink>::fetch_quoted(std::string room, std::string target) {
  this->spawn_guarded([this, room = std::move(room), target = std::move(target)] {
    if (!api_)
      return;
    auto got = perform(*api_, loom::cs::get_one_room_event{.room_id = room, .event_id = target});
    if (!got) {
      log(id_, "could not fetch {} in {}: {}", target, room, got.error().said());
      // Not there, or not to be seen by this user: said so, and whatever
      // waits on it (a mark) let go. A network's failure is not that; the
      // server's status is: not found, not to be seen, or an id it cannot
      // read (400).
      if (const auto& server = got.error().server;
          server && (server->status == 404 || server->status == 403 || server->status == 400))
        sink_(change::event_missing{conversation_id{id_, room}, target});
      return;
    }
    event(conversation_id{id_, room}, *got, placement::aside{});
  });
}

template <class Sink>
void account<Sink>::load_context(std::string room, std::string target) {
  this->spawn_guarded([this, room = std::move(room), target = std::move(target)] {
    if (!api_)
      return;
    auto got = perform(*api_, loom::cs::get_event_context{.room_id = room, .event_id = target, .limit = 60});
    if (!got) {
      log(id_, "context of {} in {}: {}", target, room, got.error().said());
      // Not there, or not to be seen: said so -- a jump to it stops, rather
      // than paging the whole history back for it.
      if (const auto& server = got.error().server; server && (server->status == 404 || server->status == 403))
        sink_(change::event_missing{conversation_id{id_, room}, target});
      return;
    }
    const conversation_id in{id_, room};
    sink_(change::window_opened{in, got->start, got->end, target});
    // Before it, newest first: given oldest first.
    if (got->events_before)
      for (auto it = got->events_before->rbegin(); it != got->events_before->rend(); ++it)
        event(in, *it, placement::in_window{target});
    // It, read as a timeline event from what the server gave.
    if (got->event)
      event(in, *got->event, placement::in_window{target});
    if (got->events_after)
      for (const auto& one : *got->events_after)
        event(in, one, placement::in_window{target});
    log(id_, "context of {} in {}: {} before, {} after", target, room,
        got->events_before ? got->events_before->size() : 0, got->events_after ? got->events_after->size() : 0);
  });
}

template <class Sink>
void account<Sink>::load_newer(std::string room, std::string from) {
  this->spawn_guarded([this, room = std::move(room), from = std::move(from)] {
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
  this->fetch_media(std::move(source), media_use::avatar{std::move(of)}, 512, true);
}

}  // namespace mux::proto::matrix::client
