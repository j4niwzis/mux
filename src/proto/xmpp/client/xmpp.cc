// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.xmpp.client: an XMPP account, run by a fiber on mux.net's loop through tern,
// saying what happens as mux.core's changes.
//
// Connecting: the domain's SRV records tried in order, STARTTLS with the
// certificate checked against the domain, SCRAM (-PLUS with channel
// binding) through tern; then the roster -- from the version kept, where
// the server versions rosters -- presence, and the stanzas as they come,
// through a tern inbox of the account's own.
export module mux.proto.xmpp.client;

import std;
import splice;
import tern;
import mux.core;
import mux.net;
import mux.logic.text;
import mux.proto.xmpp.changes;

export namespace mux::proto::xmpp::client {

// A MUC occupant's affiliation or role (XEP-0045), where it is one shown
// beside their name: read into a type once, where it comes in.
// Each says whether it is shown when it is an affiliation, and when it is
// a role.
namespace muc_rank {
struct owner_or_admin {  // owner, admin
  static constexpr bool shown_as_affiliation = true, shown_as_role = false;
};
struct moderator {
  static constexpr bool shown_as_affiliation = false, shown_as_role = true;
};
struct other {
  static constexpr bool shown_as_affiliation = false, shown_as_role = false;
};
}  // namespace muc_rank
using muc_rank_t = spl::variant<muc_rank::owner_or_admin, muc_rank::moderator, muc_rank::other>;
[[nodiscard]] inline muc_rank_t muc_rank_of(std::optional<std::string_view> name) {
  static const std::unordered_map<std::string_view, muc_rank_t> known = {
      {"owner", muc_rank::owner_or_admin{}}, {"admin", muc_rank::owner_or_admin{}}, {"moderator", muc_rank::moderator{}}};
  if (!name)
    return muc_rank::other{};
  const auto found = known.find(*name);
  return found == known.end() ? muc_rank_t{muc_rank::other{}} : found->second;
}

struct settings {
  std::string address;  // user@domain
  std::string password;
  std::string resource = "mux";
  // Where to connect, instead of what the domain's SRV records say.
  std::optional<std::string> host;
  std::optional<std::uint16_t> port;
  // PLAIN over a stream TLS has not secured: only for a server on this
  // machine, under test. Never over a network.
  bool plain_without_tls = false;
  // A proxy to connect through, where there is one: the domain's SRV
  // records are asked through it too.
  std::optional<net::proxy> proxy;
  // XEP-0077: the account made on the server first, with these answers to
  // what it asks beyond the address and the password.
  std::optional<std::vector<tern::registration::answer>> create;
};

// What the server asks to register, as the account's form shows it: each
// field of its form -- a captcha's picture, sent with it, decoded -- or
// the old fields it lists, or only the page to register on.
inline mux::proto::xmpp::registration_asked registration_asked_of(const account_id& id, const tern::registration::asked& asked) {
  using mux::proto::xmpp::registration_field;
  namespace shown = mux::proto::xmpp::field_shown;
  const auto picture_of = [&](const tern::data_form::field& one) {
    const auto cids = one.media ? std::ranges::to<std::vector<std::string_view>>(std::views::transform(std::views::filter(one.media->uri, [](const tern::data_form::uri& where) {
                                    return where.location.starts_with("cid:");
                                  }), [](const tern::data_form::uri& where) {
                                    return std::string_view(where.location).substr(4);
                                  }))
                                : std::vector<std::string_view>{};
    const auto sent = std::ranges::find_if(asked.data, [&](const tern::bob::data& one_sent) {
      return std::ranges::contains(cids, std::string_view(one_sent.cid));
    });
    if (sent == asked.data.end())
      return std::vector<std::uint8_t>{};
    const std::string packed = std::ranges::to<std::string>(std::views::filter(sent->base64, [](char c) { return c != ' ' && c != '\n' && c != '\r' && c != '\t'; }));
    return tern::crypto::base64_decode(packed).value_or(std::vector<std::uint8_t>{});
  };
  const auto links_of = [](const tern::data_form::field& one) {
    return one.media ? std::ranges::to<std::vector<std::string>>(std::views::transform(std::views::filter(one.media->uri, [](const tern::data_form::uri& where) {
                         return where.location.starts_with("http://") || where.location.starts_with("https://");
                       }), &tern::data_form::uri::location))
                     : std::vector<std::string>{};
  };
  // The form's type says how a field is shown; what XEP-0004 has not, typed.
  const auto shown_as = [](const std::optional<std::string>& type) -> mux::proto::xmpp::field_shown_t {
    static const std::map<std::string, mux::proto::xmpp::field_shown_t, std::less<>> by_type{
        {"text-private", shown::masked{}}, {"hidden", shown::hidden{}}, {"fixed", shown::read{}}};
    const auto found = type ? by_type.find(*type) : by_type.end();
    return found == by_type.end() ? mux::proto::xmpp::field_shown_t{shown::typed{}} : found->second;
  };
  mux::proto::xmpp::registration_asked out{.account = id,
                                            .instructions = asked.instructions.value_or(""),
                                            .page = asked.page ? asked.page->url : std::nullopt};
  if (asked.form) {
    out.instructions = (std::ranges::to<std::string>(std::views::join_with(asked.form->instructions, '\n')));
    if (out.instructions.empty())
      out.instructions = asked.instructions.value_or("");
    // The address and the password are the form's own fields: not asked
    // again.
    out.fields = std::ranges::to<std::vector<registration_field>>(std::views::transform(std::views::filter(asked.form->fields, [](const tern::data_form::field& one) {
                   return one.var != std::optional<std::string>("username") && one.var != std::optional<std::string>("password");
                 }), [&](const tern::data_form::field& one) {
                   return registration_field{.var = one.var.value_or(""),
                                             .label = one.label.value_or(one.var.value_or("")),
                                             .desc = one.desc.value_or(""),
                                             .shown = one.var ? shown_as(one.type) : shown::read{},
                                             .value = one.value,
                                             .required = one.required.has_value(),
                                             .picture = picture_of(one),
                                             .links = links_of(one),
                                             .choices = std::ranges::to<std::vector<std::string>>(std::views::transform(one.options, [](const tern::data_form::option& o) {
                                                          return o.label ? *o.label + " (" + o.value + ")" : o.value;
                                                        }))};
                 }));
  } else if (asked.email) {
    out.fields.push_back(registration_field{.var = "email", .label = "Email", .shown = shown::typed{}, .required = true});
  }
  return out;
}

// The protocol spoken: the standard one, rooms (XEP-0045), bookmarks
// (XEP-0402) and chat markers (XEP-0333).
using proto = tern::client;

// A JID's bare part: what a conversation is kept under.
inline std::string bare(std::string_view jid) { return std::string(jid.substr(0, jid.find('/'))); }
// A JID's resource: in a room, the occupant's nick.
inline std::string resource_of(std::string_view jid) {
  const auto slash = jid.find('/');
  return slash == std::string_view::npos ? std::string() : std::string(jid.substr(slash + 1));
}

inline availability_t availability_of(const std::optional<std::string>& show) {
  if (!show)
    return availability::online{};
  static const std::unordered_map<std::string_view, availability_t> known = {
      {"away", availability::away{}},
      {"xa", availability::extended_away{}},
      {"dnd", availability::do_not_disturb{}},
      {"chat", availability::chat{}}};
  const auto found = known.find(*show);
  return found == known.end() ? availability_t{availability::online{}} : found->second;
}

// XEP-0203's stamp, XEP-0082's DateTime: CCYY-MM-DDThh:mm:ss[.sss](Z|+hh:mm|-hh:mm)
// -- when a stanza was first sent. Read by hand: libc++ has no
// std::chrono::parse yet.
inline std::optional<std::chrono::sys_time<std::chrono::milliseconds>> stamp_of(std::string_view text) {
  std::size_t at = 0;
  const auto number = [&](std::size_t digits) -> std::optional<int> {
    if (at + digits > text.size())
      return std::nullopt;
    int value = 0;
    for (std::size_t i = 0; i < digits; ++i) {
      const char c = text[at + i];
      if (c < '0' || c > '9')
        return std::nullopt;
      value = value * 10 + (c - '0');
    }
    at += digits;
    return value;
  };
  const auto expect = [&](char c) {
    if (at < text.size() && text[at] == c) {
      ++at;
      return true;
    }
    return false;
  };
  const auto year = number(4);
  if (!year || !expect('-'))
    return std::nullopt;
  const auto month = number(2);
  if (!month || !expect('-'))
    return std::nullopt;
  const auto day = number(2);
  if (!day || !expect('T'))
    return std::nullopt;
  const auto hour = number(2);
  if (!hour || !expect(':'))
    return std::nullopt;
  const auto minute = number(2);
  if (!minute || !expect(':'))
    return std::nullopt;
  const auto second = number(2);
  if (!second)
    return std::nullopt;
  int millis = 0;
  if (expect('.')) {
    int digits = 0;
    while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
      if (digits < 3)
        millis = millis * 10 + (text[at] - '0');
      ++digits;
      ++at;
    }
    if (digits == 0)
      return std::nullopt;
    for (; digits < 3; ++digits)
      millis *= 10;
  }
  std::chrono::minutes offset{0};
  if (!expect('Z')) {
    const bool behind = at < text.size() && text[at] == '-';
    if (!expect('+') && !expect('-'))
      return std::nullopt;
    const auto hours = number(2);
    if (!hours || !expect(':'))
      return std::nullopt;
    const auto minutes = number(2);
    if (!minutes)
      return std::nullopt;
    offset = std::chrono::hours(*hours) + std::chrono::minutes(*minutes);
    if (behind)
      offset = -offset;
  }
  if (at != text.size())
    return std::nullopt;
  const std::chrono::year_month_day date{std::chrono::year(*year), std::chrono::month(static_cast<unsigned>(*month)),
                                         std::chrono::day(static_cast<unsigned>(*day))};
  if (!date.ok() || *hour > 23 || *minute > 59 || *second > 60)
    return std::nullopt;
  return std::chrono::sys_days(date) + std::chrono::hours(*hour) + std::chrono::minutes(*minute) +
         std::chrono::seconds(*second) + std::chrono::milliseconds(millis) - offset;
}

// An account, and the changes it makes: `Sink` is called with each change,
// on the loop's thread -- a mailbox's push, or a test's vector.
template <class Sink>
class account {
 public:
  using session_type = decltype(tern::connect<proto>(std::declval<net::stream&>(), std::declval<const tern::options&>(),
                                              tern::answering<>{}, net::scheduler{}));

  account(net::loop& loop, net::tls& tls, settings how, Sink sink)
      : loop_(&loop), tls_(&tls), how_(std::move(how)), sink_(std::move(sink)) {
    const auto at = how_.address.find('@');
    user_ = how_.address.substr(0, at);
    domain_ = at == std::string::npos ? how_.address : how_.address.substr(at + 1);
    id_ = account_id{protocol::xmpp{}, bare(how_.address)};
  }
  account(const account&) = delete;
  account& operator=(const account&) = delete;

  const account_id& id() const noexcept { return id_; }

  // Connected, and kept connected, by a fiber of its own.
  void start() {
    if (started_ || stopping_) return;
    started_ = true;
    loop_->spawn([this] { run(); });
  }

  // XEP-0492 stores a room's mode inside its private bookmark. Account
  // defaults and sounds have no standard shared representation in XMPP.
  void set_notifications(std::optional<std::string> room, notification_choices choices, notification_choices effective) {
    if (!room || (!bookmarks_.contains(*room) && !rooms_.contains(*room))) return;
    pending_notifications_.insert_or_assign(*room, tern::notifications::decision{
        effective.on.value_or(true), effective.mentions.value_or(false)});
    flush_notifications();
  }

  // What an account of another protocol does and this does not -- avatars
  // (XEP-0084), reactions (XEP-0444), chat states (XEP-0085), files (HTTP
  // upload, XEP-0363), stickers, threads, pins, link previews,
  // forwarding, push -- is not here at all: what is asked of an
  // account is done where its type has it (mux.app.network's ask_if_able),
  // and nothing is written for what it has not.

  // Starting a direct chat needs no Matrix-style create-room request:
  // messages are addressed to the contact's prepared bare JID.
  void create_direct(std::string user) {
    this->spawn_guarded([this, user = std::move(user)] {
      const auto jid = tern::jid::parse(user);
      if (!jid || jid->local().empty()) {
        sink_(change::refused{id_, "Enter a contact's JID, such as user@example.com."});
        return;
      }
      const conversation_id chat{id_, jid->bare().str()};
      sink_(change::conversation_updated{.id = chat, .kind = conversation_kind::direct{}, .name = chat.id});
      sink_(change::history_position{chat, std::string()});
      sink_(change::room_created{chat});
    });
  }
  // Room management is dispatched by action type. Invitations go through
  // the room so its server checks the inviter's privileges.
  void manage(std::string room, const room_action_t& action) {
    spl::visit(spl::overloaded{
        [&](const room_action::invite& invite) { this->invite_to(room, invite.user); },
        [&](const auto&) { sink_(change::refused{id_, "This XMPP room action is not implemented."}); }}, action);
  }
  void invite_to(std::string room, std::string user) {
    this->spawn_guarded([this, room = std::move(room), user = std::move(user)] {
      const auto jid = tern::jid::parse(user);
      if (!jid || jid->local().empty()) {
        sink_(change::refused{id_, "Enter the invitee's JID, such as user@example.com."}); return;
      }
      if (!session_ || !rooms_.contains(room)) {
        sink_(change::refused{id_, "Join the XMPP room before inviting someone."}); return;
      }
      const auto target = jid->bare().str();
      const auto info = session_->template try_request<tern::query::disco_info>({.to = room});
      if (!info) { sink_(change::refused{id_, "The room did not answer its access query."}); return; }
      if (std::ranges::contains(info->features, std::string_view("muc_membersonly"), &tern::disco::feature::var)) {
        // Verify existing affiliations before granting membership. In
        // particular, never turn an existing owner/admin into a member.
        bool allowed = false, banned = false, complete = true;
        std::ranges::for_each(tern::muc::affiliation_lists, [&](auto rank) {
          if (!complete || !session_) { complete = false; return; }
          const auto list = session_->template try_request<tern::query::muc_affiliations>(
              {.to = room, .query = {.item = {.affiliation = std::string(rank)}}});
          if (!list) { complete = false; return; }
          std::ranges::for_each(list->items, [&](const auto& item) {
            if (!item.jid || bare(*item.jid) != target) return;
            const auto admission = tern::muc::admission_of(item);
            allowed = allowed || admission.allowed;
            banned = banned || admission.banned;
          });
        });
        if (banned) {
          sink_(change::refused{id_, "That user is banned from the room."}); return;
        }
        // A member can be permitted to invite without being permitted to
        // read every affiliation list. Let the service grant admission in
        // that case; never overwrite an affiliation we could not verify.
        if (complete && !allowed && (!session_ || !session_->template try_request<tern::query::muc_affiliate>(
              {.to = room, .query = {.item = {.jid = target, .affiliation = "member"}}}))) {
          sink_(change::refused{id_, "The service refused to grant the invitee room membership."}); return;
        }
      }
      if (!session_) return;
      proto::message::normal invite{.to = room};
      invite.payload.emplace_back(tern::muc::user{.invitations = {{.to = target}}});
      session_->send(invite);
      sink_(change::notice{id_, "Room invitation", "An invitation was sent to " + target + "."});
    });
  }
  // XEP-0045 creates a room by joining a new JID. Only status 201 on our
  // own presence permits configuration; an existing room is never changed.
  void create_room(std::string name, std::string topic, bool open, std::string address,
                   bool federate, bool encrypted, room_place place) {
    this->spawn_guarded([this, name = std::move(name), topic = std::move(topic), open,
                        address = std::move(address), encrypted, place] {
      if (!session_) {
        sink_(change::refused{id_, "Connect the XMPP account before creating a room."});
        return;
      }
      const auto jid = tern::jid::parse(address);
      if (!jid || jid->local().empty() || !jid->resource().empty()) {
        sink_(change::refused{id_, "Enter a room's bare JID, such as room@conference.example.com."});
        return;
      }
      if (encrypted || place.space || place.make_space) {
        sink_(change::refused{id_, "XMPP room encryption and Matrix spaces are not implemented."});
        return;
      }
      const auto room = jid->bare().str();
      if (rooms_.contains(room) || creating_.contains(room)) {
        sink_(change::refused{id_, "That room is already joined or being created."});
        return;
      }
      creating_.emplace(room, room_creation{std::move(name), std::move(topic), open});
      this->join(room, user_);
    });
  }

  // XEP-0030: a MUC service lists its rooms; an account domain lists its
  // services first. Searches are local because disco has no search query.
  void search_directory(std::string server, std::string query, std::optional<std::string> since = std::nullopt) {
    this->spawn_guarded([this, server = std::move(server), query = std::move(query)] {
      const std::string target = server.empty() ? domain_ : server;
      const auto is_muc = [&](const std::string& jid) {
        if (!session_) return false;
        auto info = session_->template try_request<tern::query::disco_info>({.to = jid});
        return info && std::ranges::contains(info->features, std::string_view("http://jabber.org/protocol/muc"),
                                             &tern::disco::feature::var);
      };
      const auto rooms_at = [&](const std::string& jid) {
        if (!session_) return std::vector<directory_room>{};
        auto items = session_->template try_request<tern::query::disco_items>({.to = jid});
        if (!items) return std::vector<directory_room>{};
        return std::ranges::to<std::vector>(std::views::transform(items->list, [](const auto& item) {
          return directory_room{.id = item.jid, .name = item.name.value_or(item.jid), .alias = item.jid};
        }));
      };
      std::vector<directory_room> rooms;
      if (session_) {
        if (is_muc(target)) {
          rooms = rooms_at(target);
        } else if (session_) {
          auto services = session_->template try_request<tern::query::disco_items>({.to = target});
          if (services) {
            // Each reply owns its room data across the yielding requests.
            const auto pages = std::ranges::to<std::vector>(std::views::transform(
                services->list, [&](const auto& item) {
                  return !item.node && is_muc(item.jid) ? rooms_at(item.jid) : std::vector<directory_room>{};
                }));
            rooms = std::ranges::to<std::vector>(std::views::join(pages));
          }
        }
      }
      const auto wanted = mux::logic::folded(query);
      rooms = std::ranges::to<std::vector>(std::views::filter(rooms, [&](const auto& room) {
        return wanted.empty() || mux::logic::folded(room.name).contains(wanted) || mux::logic::folded(room.id).contains(wanted);
      }));
      sink_(change::directory_listed{.by = id_, .server = server, .query = query, .rooms = std::move(rooms)});
    });
  }
  // A join URI's room card: XEP-0030 identities supply the room name.
  // Private rooms can refuse discovery while still allowing a join.
  void preview_room(std::string room, const std::vector<std::string>&) {
    this->spawn_guarded([this, room = std::move(room)] {
      mux::room_preview preview{.id = room, .name = room, .alias = room};
      if (!session_) {
        preview.note = "Connect the XMPP account before joining this room.";
      } else if (const auto info = session_->template try_request<tern::query::disco_info>({.to = room})) {
        for (const auto& identity : info->identities)
          if (identity.category == "conference" && identity.name) {
            preview.name = *identity.name;
            break;
          }
      } else {
        preview.note = "The room did not provide a preview. You can still try to join it.";
      }
      sink_(change::room_previewed{id_, room, std::move(preview)});
    });
  }

  // A directory room joined with the account's normal nickname.
  void join(std::string room, const std::vector<std::string>&) {
    this->spawn_guarded([this, room = bare(room)] {
      if (!session_) return;
      const auto address = tern::jid::parse(room);
      if (!address || address->local().empty()) {
        sink_(change::refused{id_, "Enter a room JID such as room@conference.example.com."});
        return;
      }
      joining_.insert(room);
      this->join(room, user_);
    });
  }

  // A chat message sent: from any fiber, or posted to the loop from another
  // thread. What was sent is said as a change at once, and marked sent once
  // it has gone out.
  // A displayed marker (XEP-0333) for a message: its sender, or the room,
  // sees it was read.
  void mark_read(std::string to, std::string id) {
    this->spawn_guarded([this, to = std::move(to), id = std::move(id)] {
      if (!session_)
        return;
      if (rooms_.contains(to)) {
        proto::message::groupchat marker{.to = to};
        marker.payload.emplace_back(tern::markers::displayed{.id = id});
        session_->send(marker);
      } else {
        proto::message::chat marker{.to = to};
        marker.payload.emplace_back(tern::markers::displayed{.id = id});
        session_->send(marker);
      }
    });
  }

  // Older messages of a conversation, from the server's archive (XEP-0313):
  // one's own archive with a contact, a room's own for a room. `before` is
  // the archive id to page back from, or empty for the latest page.
  // No window around a message here, and so nothing newer to page to: a
  // jump pages back instead.
  void load_older(std::string with, std::string before) {
    this->spawn_guarded([this, with = std::move(with), before = std::move(before)] {
      if (!session_)
        return;
      const bool room = rooms_.contains(with);
      tern::mam::query asked{.filter = tern::mam::filter(room ? std::nullopt : std::optional<std::string>(with)),
                             .page = tern::rsm::set{.max = 40, .before = before}};
      log(id_, "history of {}: asking the archive", with);
      auto page = session_->try_archive(std::move(asked), room ? std::optional<std::string>(with) : std::nullopt);
      if (!page) {
        log(id_, "history of {}: the archive did not answer", with);
        return;
      }
      log(id_, "history of {}: {} message{}{}", with, page->results.size(), page->results.size() == 1 ? "" : "s",
          page->fin.complete.value_or(false) ? ", the beginning" : "");
      const conversation_id in{id_, with};
      // Oldest first in the page: each put before the rest, newest first.
      for (auto it = page->results.rbegin(); it != page->results.rend(); ++it)
        this->archived(in, *it, room);
      std::optional<std::string> next;
      if (!page->fin.complete.value_or(false) && page->fin.page && page->fin.page->first)
        next = page->fin.page->first;
      sink_(change::history_position{in, next});
    });
  }

  // A message of the archive, as history: under its own id, so that one
  // already had live is the same message.
  void archived(const conversation_id& in, const tern::mam::result& one, bool room) {
    if (!one.forwarded.message)
      return;
    const auto take = [&](const auto* got) {
      if (!got || !got->body || !got->from)
        return;
      const std::string nick = resource_of(*got->from);
      const auto found = rooms_.find(in.id);
      message made{.in = in,
                   .id = got->id.value_or(one.id),
                   .sender = room ? *got->from : bare(*got->from),
                   .at = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()),
                   .body = {*got->body, std::nullopt},
                   .outgoing = room ? (found != rooms_.end() && nick == found->second.nick)
                                    : bare(*got->from) == id_.address};
      if (one.forwarded.delay)
        if (const auto at = stamp_of(one.forwarded.delay->stamp))
          made.at = *at;
      sink_(change::message_added{std::move(made), placement::at_start{}});
    };
    const auto& got = *one.forwarded.message;
    take(got.template get_if<tern::basic::message_chat<tern::forward::plain>>());
    take(got.template get_if<tern::basic::message_normal<tern::forward::plain>>());
    take(got.template get_if<tern::basic::message_groupchat<tern::forward::plain>>());
  }

  // A room left: unavailable to it, and the conversation gone.
  void leave(std::string room) {
    this->spawn_guarded([this, room = std::move(room)] {
      if (const auto invite = invitations_.find(room); invite != invitations_.end()) {
        if (session_) {
          proto::message::normal declined{.to = room};
          declined.payload.emplace_back(tern::muc::user{.declined = tern::muc::decline{.to = invite->second.inviter}});
          session_->send(declined);
        }
        invitations_.erase(invite);
        sink_(change::conversation_removed{{id_, room}});
        return;
      }
      const auto found = rooms_.find(room);
      if (found == rooms_.end())
        return;
      if (session_)
        session_->send(proto::presence::unavailable{.to = room + "/" + found->second.nick});
      rooms_.erase(found);
      sink_(change::conversation_removed{{id_, room}});
      if (session_ && !session_->template try_request<tern::query::bookmark_remove>(
                          {.query = {.retract = {.item = {.id = room}}}}))
        sink_(change::notice{id_, "Room bookmark", "The room was left, but the server could not remove its bookmark."});
    });
  }

  // The text with its runs as XEP-0393 (Message Styling) writes them: marks
  // in the body itself -- *strong*, _emphasis_, ~strike~ and `code`. A run
  // is marked where its text starts and ends with no space, as the XEP
  // asks; underline, spoilers and links it has none for, and they go as
  // plain text. Put in from the end back, so each offset before is as it was.
  [[nodiscard]] static std::string styled_body(std::string text, const std::vector<mux::styled_run>& runs) {
    struct mark_at {
      std::size_t at;
      std::string_view mark;
      bool closing;
    };
    std::vector<mark_at> marks;
    const auto blank = [](char c) { return c == ' ' || c == '\n' || c == '\t'; };
    for (const mux::styled_run& run : runs) {
      std::size_t first = run.first, last = std::min(run.last, text.size());
      while (first < last && blank(text[first]))
        ++first;
      while (last > first && blank(text[last - 1]))
        --last;
      if (first >= last)
        continue;
      const std::string_view mark = spl::visit(
          spl::overloaded{[](const mux::run_style::bold&) { return std::string_view("*"); },
                          [](const mux::run_style::italic&) { return std::string_view("_"); },
                          [](const mux::run_style::strike&) { return std::string_view("~"); },
                          [](const mux::run_style::code&) { return std::string_view("`"); },
                          [](const mux::run_style::underline&) { return std::string_view(); },
                          [](const mux::run_style::spoiler&) { return std::string_view(); },
                          [](const mux::run_style::custom_emoji&) { return std::string_view(); },
                          [](const mux::run_style::link&) { return std::string_view(); }},
          run.style);
      if (mark.empty())
        continue;
      marks.push_back({first, mark, false});
      marks.push_back({last, mark, true});
    }
    // Back to front; at one place, closings before openings in the text --
    // so put in openings first going backwards.
    std::ranges::sort(marks, [](const mark_at& a, const mark_at& b) {
      return a.at != b.at ? a.at > b.at : a.closing < b.closing;
    });
    for (const mark_at& one : marks)
      text.insert(one.at, one.mark);
    return text;
  }

  void send(std::string to, std::string text, std::optional<std::string> reply_to = std::nullopt,
            std::vector<mux::mention> = {}, std::vector<mux::styled_run> styles = {}) {
    text = styled_body(std::move(text), styles);
    this->spawn_guarded([this, to = bare(to), text = std::move(text), reply_to = std::move(reply_to)] {
      message out{.in = {id_, to},
                  .id = "mux-" + std::to_string(++sent_),
                  .sender = id_.address,
                  .at = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()),
                  .body = {text, std::nullopt},
                  .outgoing = true,
                  .delivery = delivery::sending{}};
      out.replies_to = reply_to;
      sink_(change::message_added{out});
      if (!session_) {
        sink_(change::delivery_changed{out.in, out.id, delivery::failed{}});
        return;
      }
      this->send_to(to, out.id, text, [&](auto& one) {
        if (reply_to)
          one.payload.emplace_back(tern::replies::reply{.id = *reply_to});
      });
      sink_(change::delivery_changed{out.in, out.id, wire_ && !wire_->failed() ? delivery_t{delivery::sent{}} : delivery_t{delivery::failed{}}});
    });
  }

  // A message of one's own corrected (XEP-0308): the new text in its place.
  void edit(std::string to, std::string id, std::string text, std::vector<mux::styled_run> styles = {}) {
    text = styled_body(std::move(text), styles);
    this->spawn_guarded([this, to = bare(to), id = std::move(id), text = std::move(text)] {
      if (!session_)
        return;
      this->send_to(to, "mux-" + std::to_string(++sent_), text,
                    [&](auto& one) { one.payload.emplace_back(tern::corrections::replace{.id = id}); });
      sink_(change::message_edited{{id_, to}, id, body{text, std::nullopt}});
    });
  }
  // A message of one's own taken back (XEP-0424).
  void remove(std::string to, std::string id) {
    this->spawn_guarded([this, to = bare(to), id = std::move(id)] {
      if (!session_)
        return;
      this->send_to(to, "mux-" + std::to_string(++sent_), "This message was retracted.",
                    [&](auto& one) { one.payload.emplace_back(tern::retractions::retract{.id = id}); });
      sink_(change::message_redacted{{id_, to}, id});
    });
  }

  // A message to a contact or a room, as chat or groupchat, with what
  // `extra` puts in it.
  template <class Extra>
  void send_to(const std::string& to, std::string id, const std::string& text, Extra&& extra) {
    if (rooms_.contains(to)) {
      proto::message::groupchat one{.to = to, .id = std::move(id), .body = text};
      extra(one);
      session_->send(one);
    } else {
      proto::message::chat one{.to = to, .id = std::move(id), .body = text};
      extra(one);
      session_->send(one);
    }
  }

  // Called on the network loop: stop recovery before canceling its I/O.
  void stop() {
    stopping_ = true;
    if (wire_) wire_->abort();
  }

 private:
  void say(connection_t state) { sink_(change::connection_changed{id_, std::move(state)}); }
  // A fiber of this account. What it throws past its own handling -- the
  // unforeseen, a bug -- is caught here: let out, it left the loop and
  // stopped every account's network without a word. It is logged
  // and said; the account shows as failed, to be connected again from what
  // it kept, so that nothing half done of it is relied on.
  template <class Body>
  void spawn_guarded(Body body) {
    ++commands_;
    loop_->spawn([this, body = std::move(body)] mutable {
      struct finished {
        std::size_t& commands;
        ~finished() { --commands; }
      } done{commands_};
      try {
        body();
      } catch (const std::exception& failed) {
        log(id_, "stopped by an error: {}", failed.what());
        this->say(connection::failed{std::format("Stopped by an error: {}", failed.what())});
      }
    });
  }


  void run() {
    std::chrono::seconds delay{1};
    while (!stopping_) {
      retry_ = true;
      connected_ = false;
      try {
        this->connect_once();
      } catch (const std::exception& failed) {
        log(id_, "connection stopped by an error: {}", failed.what());
        say(connection::failed{failed.what()});
      }
      sink_(change::protocol_state_changed{id_, protocol_state_t{mux::proto::xmpp::state{}}});
      if (stopping_ || !retry_) break;
      if (connected_) delay = std::chrono::seconds(1);
      log(id_, "reconnecting in {} seconds", delay.count());
      // Short sleeps let disabling an account finish promptly during backoff.
      for (std::chrono::seconds waited{0}; waited < delay && !stopping_; waited += std::chrono::seconds(1))
        loop_->sleep(std::chrono::seconds(1));
      delay = std::min(delay * 2, std::chrono::seconds(60));
    }
    started_ = false;
  }

  void connect_once() {
    say(connection::connecting{});
    if (how_.proxy)
      log(id_, "through the proxy {}:{}", how_.proxy->host, how_.proxy->port);
    std::vector<tern::srv::target> targets;
    if (how_.host) {
      targets.push_back({0, 0, how_.port.value_or(5222), *how_.host});
      log(id_, "connecting to {}:{}, as set", *how_.host, how_.port.value_or(5222));
    } else {
      log(id_, "looking up the servers of {}", domain_);
      targets = net::xmpp_targets(*loop_, how_.proxy, domain_);
      log(id_, "{} server{} to try", targets.size(), targets.size() == 1 ? "" : "s");
    }
    std::optional<net::tcp::socket> socket;
    std::string why;
    for (const auto& target : targets) {
      try {
        log(id_, "connecting to {}:{}", target.host, target.port);
        socket.emplace(net::connect(*loop_, how_.proxy, target.host, target.port));
        log(id_, "connected to {}:{}", target.host, target.port);
        break;
      } catch (const net::failure& failed) {
        why = failed.what();
        log(id_, "{}:{} did not answer: {}", target.host, target.port, why);
      }
    }
    if (!socket) {
      log(id_, "no server of {} answered", domain_);
      say(connection::failed{"no server of " + domain_ + " answered: " + why});
      return;
    }
    log(id_, "opening the stream: TLS, then logging in");
    net::stream wire(*loop_, *tls_, std::move(*socket));
    wire_ = &wire;
    struct forget_connection {
      account& own;
      ~forget_connection() { own.session_ = nullptr; own.wire_ = nullptr; }
    } forget{*this};
    if (stopping_) { wire.abort(); return; }
    tern::options options;
    options.username = user_;
    options.domain = domain_;
    options.password = how_.password;
    options.resource = how_.resource;
    options.plain_without_tls = how_.plain_without_tls;
    options.create = how_.create;
    options.self = {.identities = {{.category = "client", .type = "pc", .name = "mux"}}};
    options.caps_node = "https://github.com/j4niwzis/mux";
    auto made = tern::try_connect<proto>(wire, options, tern::answering<>{}, net::scheduler{loop_});
    if (!made) {
      log(id_, "the stream failed: {}", made.error().detail.empty() ? "the connection failed" : made.error().detail);
      wire_ = nullptr;
      // Registering: what the server asks more, to the account's form; what
      // it refused, said as what it means.
      if (made.error().registration) {
        retry_ = false;
        sink_(registration_asked_of(id_, *made.error().registration));
        say(connection::failed{"the server asks more to register: see the account's settings"});
        return;
      }
      if (made.error().code == tern::connect_code::registration_refused) {
        retry_ = false;
        const auto& refused = made.error().stanza;
        std::string meaning = made.error().detail;
        const auto closed = [&](const auto&) { meaning = "the server does not let accounts be made here"; };
        if (refused && refused->what)
          refused->what->with(spl::overloaded{
              [&](const tern::conditions::conflict&) { meaning = "that address is taken"; },
              // The answers not taken -- a captcha's, perhaps: asked afresh
              // next time, the form shown again.
              [&](const tern::conditions::not_acceptable&) {
                meaning = "the server did not take the answers -- a captcha's, perhaps: it asks again";
                how_.create = std::vector<tern::registration::answer>{};
              },
              [&](const tern::conditions::service_unavailable& one) { closed(one); },
              [&](const tern::conditions::not_allowed& one) { closed(one); },
              [&](const tern::conditions::forbidden& one) { closed(one); },
              [](const auto&) {}});
        const std::string text = refused && refused->text ? refused->text->content : std::string();
        say(connection::failed{"registration refused: " + meaning + (text.empty() ? std::string() : " -- " + text)});
        return;
      }
      // Credentials and unsupported authentication require an account edit.
      // Network and stream failures are retried with the same settings.
      if (made.error().code == tern::connect_code::not_authorized ||
          made.error().code == tern::connect_code::authentication ||
          made.error().code == tern::connect_code::no_mechanism)
        retry_ = false;
      say(connection::failed{made.error().detail.empty() ? "the connection failed" : made.error().detail});
      return;
    }
    // Registered: from now on only signed in to.
    if (how_.create) {
      log(id_, "registered");
      how_.create.reset();
      sink_(mux::proto::xmpp::registered{id_});
    }
    session_type& session = *made;
    session_ = &session;
    connected_ = true;
    log(id_, "logged in, as {}", how_.resource.empty() ? std::string("a resource the server chose") : how_.resource);
    say(connection::online{});

    // Opened before anything is asked, so that nothing that arrives while
    // the roster is fetched is missed.
    auto inbox = session.open_inbox();
    // What the server has (its disco#info, XEP-0030), read once, here, into
    // XMPP's state -- what its extension points decide by.
    {
      const std::string own = bare(how_.address);
      mux::proto::xmpp::state now{.online = true};
      if (auto info = session.template try_request<tern::query::disco_info>({.to = own.substr(own.find('@') + 1)}))
        now.archive = std::ranges::contains(info->features, std::string_view("urn:xmpp:mam:2"), &tern::disco::feature::var);
      sink_(change::protocol_state_changed{id_, protocol_state_t{now}});
    }
    if (session.try_sync(roster_)) {
      log(id_, "the roster: {} contact{}", roster_.items.size(), roster_.items.size() == 1 ? "" : "s");
      for (const auto& [jid, item] : roster_.items)
        contact(item);
    } else {
      log(id_, "the roster could not be fetched");
    }
    session.available();
    // The rooms kept as bookmarks, and those to join joined.
    if (auto marks = session.template try_request<tern::query::bookmarks>(); marks && marks->items) {
      log(id_, "bookmarks: {} room{}", marks->items->items.size(), marks->items->items.size() == 1 ? "" : "s");
      for (const auto& one : marks->items->items)
        if (one.conference)
          this->bookmarked(one.id, *one.conference);
    } else {
      log(id_, "no bookmarks");
    }
    flush_notifications();
    // Restore every room joined here, including rooms whose bookmark could
    // not be saved. Drop occupants from the previous connection.
    std::ranges::for_each(rooms_, [](auto& entry) { entry.second.occupants.clear(); });
    // Sending may yield to a leave request: do not retain map iterators or
    // references across it, or join a room removed while another was sent.
    const auto rejoining = std::ranges::to<std::vector>(std::views::transform(rooms_, [](const auto& entry) {
      return std::pair{entry.first, entry.second.nick};
    }));
    for (const auto& [jid, nick] : rejoining)
      if (rooms_.contains(jid)) this->join(jid, nick);

    for (;;) {
      auto one = inbox.try_next();
      if (!one) {
        if (!stopping_) {
          log(id_, "the connection was lost: {}", one.error().detail);
          say(connection::failed{one.error().detail});
        }
        break;
      }
      if (!*one)
        break;
      on(**one);
    }
    session_ = nullptr;
    // Tern woke requests waiting on this session when its reader ended.
    // Keep the session and transport alive until those fibers have returned.
    while (commands_ != 0) loop_->sleep(std::chrono::milliseconds(1));
    if (!creating_.empty() || !joining_.empty())
      sink_(change::refused{id_, "Disconnected before the requested room could be opened."});
    creating_.clear();
    joining_.clear();
    wire_ = nullptr;
    if (stopping_ || !wire.failed()) {
      log(id_, "disconnected");
      sink_(change::protocol_state_changed{id_, protocol_state_t{mux::proto::xmpp::state{}}});
      say(connection::offline{});
    }
  }

  // A room, as its bookmark says: in the list, and joined where it says to.
  void bookmarked(const std::string& jid, const tern::bookmarks::conference& mark) {
    bookmarks_.insert_or_assign(jid, mark);
    notification_choices choices;
    if (mark.extensions)
      for (const auto& child : mark.extensions->children)
        spl::visit(spl::overloaded{[&](const tern::notifications::notify& notify) {
          if (const auto chosen = tern::notifications::selected(notify)) {
            choices.on = chosen->on;
            choices.mentions = chosen->mentions;
          }
        }, [](const auto&) {}}, child.data());
    if (!pending_notifications_.contains(jid)) sink_(change::notifications_changed{id_, jid, choices, false});
    sink_(change::conversation_updated{.id = {id_, jid},
                                       .kind = conversation_kind::group{},
                                       .name = mark.name.value_or(jid)});
    sink_(change::history_position{{id_, jid}, std::string()});
    if (tern::bookmarks::autojoins(mark))
      rooms_[jid].nick = mark.nick.value_or(user_);
  }
  void join(const std::string& jid, const std::string& nick) {
    rooms_[jid].nick = nick;
    proto::presence::available joining{.to = jid + "/" + nick};
    const auto invite = invitations_.find(jid);
    joining.payload.emplace_back(tern::muc::join{.history = tern::muc::history{.maxstanzas = "50"},
        .password = invite == invitations_.end() ? std::nullopt : invite->second.password});
    session_->send(joining);
  }

  void contact(const tern::roster_item& item) {
    sink_(change::conversation_updated{.id = {id_, item.jid},
                                       .kind = conversation_kind::direct{},
                                       .name = item.name.value_or(item.jid),
                                       .groups = item.group});
    // Its archive, paged back from the latest.
    sink_(change::history_position{{id_, item.jid}, std::string()});
  }

  void on(const proto::stanza_t& one) {
    spl::visit(spl::overloaded{[this](const proto::message_t& message) {
                            spl::visit([this](const auto& got) { on_message(got); }, message);
                          },
                          [this](const proto::presence_t& presence) {
                            spl::visit([this](const auto& got) { on_presence(got); }, presence);
                          },
                          [this](const proto::iq_t& iq) { spl::visit([this](const auto& got) { on_iq(got); }, iq); }},
               one);
  }
  // A roster push, handed out once tern has answered it; the other iqs are
  // tern's to answer.
  void on_iq(const proto::iq::set& set) {
    if (roster_.apply(set))
      for (const auto& [jid, item] : roster_.items)
        contact(item);
  }
  void on_iq(const auto&) {}

  // A chat or a normal message with a body is a message; anything else is
  // not the conversation's.
  void on_message(const proto::message::chat& got) { this->text_message(got); }
  void on_message(const proto::message::normal& got) {
    bookmark_event(got);
    if (!this->invitation_message(got)) this->text_message(got);
  }
  void on_message(const proto::message::headline& got) { bookmark_event(got); }
  template <class Message> void bookmark_event(const Message& got) {
    if (!got.from || bare(*got.from) != id_.address) return;
    for (const auto& child : got.payload)
      spl::visit(spl::overloaded{[&](const tern::bookmarks::event& event) {
        if (!event.items || event.items->node != tern::bookmarks::bookmarks_namespace) return;
        for (const auto& item : event.items->items)
          spl::visit(spl::overloaded{[&](const tern::bookmarks::published& item) {
            if (item.conference) bookmarked(item.id, *item.conference);
          }, [&](const tern::bookmarks::retracted& item) {
            bookmarks_.erase(item.id);
            sink_(change::notifications_changed{id_, item.id, {}, false});
          }}, item.data());
      }, [](const auto&) {}}, child.data());
  }
  void on_message(const proto::message::error& got) {
    for (const auto& carried : got.payload)
      if (const auto* user = carried.template get_if<tern::muc::user>(); user && !user->invitations.empty()) {
        sink_(change::refused{id_, "The XMPP room refused the invitation."});
        return;
      }
  }
  bool invitation_message(const proto::message::normal& got) {
    if (!got.from) return false;
    const auto address = tern::jid::parse(*got.from);
    if (!address || address->local().empty() || !address->resource().empty()) return false;
    const auto room = address->bare().str();
    for (const auto& carried : got.payload)
      if (const auto* user = carried.template get_if<tern::muc::user>()) {
        if (user->declined) {
          sink_(change::notice{id_, "Room invitation", user->declined->from.value_or("The user") + " declined the invitation to " + room + "."});
          return true;
        }
        for (const auto& invite : user->invitations) {
          if (!invite.from || rooms_.contains(room)) continue;
          invitations_.insert_or_assign(room, pending_invitation{*invite.from, user->password});
          sink_(change::conversation_updated{.id = {id_, room}, .kind = conversation_kind::group{},
              .name = room, .invite = invite_info{.from = *invite.from}});
          return true;
        }
      }
    return false;
  }
  // A room's message: the room is the conversation, the occupant the sender,
  // and one's own nick says it was sent from here -- the room's echo of it.
  void on_message(const proto::message::groupchat& got) {
    if (!got.body || !got.from)
      return;
    const std::string room = bare(*got.from);
    const auto found = rooms_.find(room);
    if (found == rooms_.end())
      return;
    const std::string nick = resource_of(*got.from);
    message in{.in = {id_, room},
               .id = got.id.value_or(""),
               .sender = *got.from,
               .at = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()),
               .body = {*got.body, std::nullopt},
               .outgoing = nick == found->second.nick};
    this->arrived(got, std::move(in));
  }
  template <class Other>
  void on_message(const Other&) {}

  template <class Message>
  void text_message(const Message& got) {
    if (!got.body || !got.from)
      return;
    const std::string from = bare(*got.from);
    message in{.in = {id_, from},
               .id = got.id.value_or(""),
               .sender = from,
               .at = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()),
               .body = {*got.body, std::nullopt}};
    this->arrived(got, std::move(in));
  }

  // A message come: a correction or a retraction of an earlier one, or one
  // of its own -- a reply, where it answers another.
  template <class Message>
  void arrived(const Message& got, message in) {
    for (const auto& carried : got.payload) {
      if (const auto* delayed = carried.template get_if<tern::delay>())
        if (const auto at = stamp_of(delayed->stamp))
          in.at = *at;
      if (const auto* correction = carried.template get_if<tern::corrections::replace>()) {
        // Its sender's own message only (XEP-0308): anyone's else.
        sink_(change::message_edited{in.in, correction->id, in.body, in.sender});
        return;
      }
      if (const auto* taken = carried.template get_if<tern::retractions::retract>()) {
        sink_(change::message_redacted{in.in, taken->id});
        return;
      }
      if (const auto* reply = carried.template get_if<tern::replies::reply>())
        in.replies_to = reply->id;
    }
    sink_(change::message_added{std::move(in)});
  }

  struct room_creation { std::string name, topic; bool open = false; };
  void flush_notifications() {
    if (!session_ || notifications_busy_ || pending_notifications_.empty()) return;
    notifications_busy_ = true;
    this->spawn_guarded([this] {
      struct finished { bool& busy; ~finished() { busy = false; } } done{notifications_busy_};
      while (session_ && !pending_notifications_.empty()) {
        const auto [room, wanted] = *pending_notifications_.begin();
        if (!bookmarks_.contains(room) && !rooms_.contains(room)) { pending_notifications_.erase(room); continue; }
        auto conference = bookmarks_.contains(room) ? bookmarks_.at(room) : tern::bookmarks::conference{
            .name = room, .autojoin = "true", .nick = rooms_.at(room).nick};
        if (!conference.extensions) conference.extensions.emplace();
        auto& children = conference.extensions->children;
        auto found = std::ranges::find_if(children, [](const auto& child) {
          return child.template is<tern::notifications::notify>();
        });
        if (found == children.end()) {
          children.emplace_back(tern::notifications::notify{});
          found = std::prev(children.end());
        }
        const auto previous = tern::notifications::selected(found->template as<tern::notifications::notify>());
        if (previous && previous->on == wanted.on && previous->mentions == wanted.mentions) {
          pending_notifications_.erase(room);
          continue;
        }
        tern::notifications::choose(found->template as<tern::notifications::notify>(), wanted);
        const auto saved = session_->template try_request<tern::query::bookmark_save>({.query = {
            .publish = {.item = {.id = room, .conference = conference}}}});
        if (!saved) {
          sink_(change::notice{id_, "Notifications", "The server could not save the room's notification mode."});
          return; // Retain the latest edit for the next connection.
        }
        bookmarks_.insert_or_assign(room, std::move(conference));
        const auto current = pending_notifications_.find(room);
        if (current != pending_notifications_.end() && current->second.on == wanted.on && current->second.mentions == wanted.mentions)
          pending_notifications_.erase(current);
      }
    });
  }
  void save_room(std::string room, std::string name) {
    this->spawn_guarded([this, room = std::move(room), name = std::move(name)] {
      if (!session_) return;
      auto conference = bookmarks_.contains(room) ? bookmarks_.at(room) : tern::bookmarks::conference{};
      conference.name = name; conference.autojoin = "true"; conference.nick = user_;
      const auto info = session_->template try_request<tern::query::disco_info>({.to = id_.address});
      if (!info || !std::ranges::contains(info->features, std::string_view("http://jabber.org/protocol/pubsub#publish-options"),
                                         &tern::disco::feature::var) || !session_ ||
          !session_->template try_request<tern::query::bookmark_save>({.query = {
              .publish = {.item = {.id = room, .conference = conference}}}}))
        sink_(change::notice{id_, "Room bookmark", "The room is open, but the server could not save its private bookmark for reconnecting."});
    });
  }
  void configure_created(std::string room, room_creation wanted) {
    this->spawn_guarded([this, room = std::move(room), wanted = std::move(wanted)] {
      const auto failed = [&](std::string why) {
        if (session_) {
          (void)session_->template try_request<tern::query::muc_configure>(
              {.to = room, .query = {{.type = "cancel"}}});
          if (session_) session_->send(proto::presence::unavailable{.to = room + "/" + user_});
        }
        rooms_.erase(room);
        sink_(change::refused{id_, std::move(why)});
      };
      if (!session_) { failed("Disconnected before the room could be configured."); return; }
      auto configuration = session_->template try_request<tern::query::muc_configuration>({.to = room});
      if (!configuration || !configuration->form) {
        failed("The service did not provide a room configuration form."); return;
      }
      // Preserve every unknown field's server default and the hidden FORM_TYPE.
      // Require the access controls before accepting an invite-only room.
      bool members = false, listed = false, persistent = false;
      auto form = *configuration->form;
      form.type = "submit";
      form.fields = std::ranges::to<std::vector>(std::views::transform(form.fields, [&](auto field) {
        spl::visit(spl::overloaded{
            [&](tern::muc::config_field::name) { field.value = {wanted.name}; },
            [&](tern::muc::config_field::description) { field.value = {wanted.topic}; },
            [&](tern::muc::config_field::persistent) { persistent = true; field.value = {"1"}; },
            [&](tern::muc::config_field::public_room) { listed = true; field.value = {wanted.open ? "1" : "0"}; },
            [&](tern::muc::config_field::members_only) { members = true; field.value = {wanted.open ? "0" : "1"}; },
            [](tern::muc::config_field::form_type) {},
            [](tern::muc::config_field::other) {}}, tern::muc::kind_of(field));
        return field;
      }));
      if (!members || !listed || !persistent) { failed("The service cannot configure the requested persistent room and access."); return; }
      if (!session_ || !session_->template try_request<tern::query::muc_configure>({.to = room, .query = {std::move(form)}})) {
        failed("The service refused the room configuration."); return;
      }
      if (!session_) { failed("Disconnected before the room could be opened."); return; }
      if (!wanted.topic.empty()) session_->send(proto::message::groupchat{.to = room, .subject = wanted.topic});
      const conversation_id chat{id_, room};
      sink_(change::conversation_updated{.id = chat, .kind = conversation_kind::group{}, .name = wanted.name, .topic = wanted.topic});
      sink_(change::history_position{chat, std::string()});
      sink_(change::room_created{chat});
      this->save_room(room, wanted.name);
    });
  }
  void on_presence(const proto::presence::error& got) {
    if (!got.from) return;
    const auto room = bare(*got.from);
    if (joining_.erase(room)) {
      rooms_.erase(room);
      sink_(change::refused{id_, "The service refused to join " + room + "."});
    }
    if (creating_.erase(room)) {
      rooms_.erase(room);
      sink_(change::refused{id_, "The service refused to create " + room + "."});
    }
  }

  void on_presence(const proto::presence::available& got) {
    if (!got.from)
      return;
    if (this->occupant(got, true, availability_of(got.show)))
      return;
    sink_(change::presence_changed{id_, bare(*got.from), {availability_of(got.show), got.status}});
  }
  void on_presence(const proto::presence::unavailable& got) {
    if (!got.from)
      return;
    if (this->occupant(got, false, availability::offline{}))
      return;
    sink_(change::presence_changed{id_, bare(*got.from), {availability::offline{}, got.status}});
  }

  // A room's occupant come or gone: its members, and how the occupant is,
  // told. False where the presence is not a room's.
  template <class Presence>
  bool occupant(const Presence& got, bool here, availability_t state) {
    const std::string room = bare(*got.from);
    const auto found = rooms_.find(room);
    if (found == rooms_.end())
      return false;
    const std::string nick = resource_of(*got.from);
    auto& occupants = found->second.occupants;
    if (here) {
      member one{.id = *got.from, .name = nick};
      for (const auto& carried : got.payload)
        if (const auto* user = carried.template get_if<tern::muc::user>())
          for (const auto& item : user->items) {
            if (spl::visit([](auto rank) { return rank.shown_as_affiliation; }, muc_rank_of(item.affiliation)))
              one.role = item.affiliation;
            else if (spl::visit([](auto rank) { return rank.shown_as_role; }, muc_rank_of(item.role)))
              one.role = item.role;
          }
      occupants[nick] = std::move(one);
    } else {
      occupants.erase(nick);
    }
    std::vector<member> who;
    for (const auto& [name, one] : occupants)
      who.push_back(one);
    sink_(change::members_changed{{id_, room}, std::move(who)});
    sink_(change::presence_changed{id_, *got.from, {std::move(state), got.status}});
    if (here && joining_.contains(room))
      for (const auto& carried : got.payload)
        if (const auto* user = carried.template get_if<tern::muc::user>(); user && tern::muc::flags_of(*user).self) {
          joining_.erase(room);
          invitations_.erase(room);
          const conversation_id chat{id_, room};
          sink_(change::conversation_updated{.id = chat, .kind = conversation_kind::group{}, .name = room});
          sink_(change::history_position{chat, std::string()});
          sink_(change::room_created{chat});
          break;
        }
    if (here && creating_.contains(room))
      for (const auto& carried : got.payload)
        if (const auto* user = carried.template get_if<tern::muc::user>()) {
          const auto flags = tern::muc::flags_of(*user);
          if (!flags.self) continue;
          auto wanted = std::move(creating_.at(room));
          creating_.erase(room);
          if (flags.created) this->configure_created(room, std::move(wanted));
          else {
            session_->send(proto::presence::unavailable{.to = room + "/" + user_});
            rooms_.erase(room);
            sink_(change::refused{id_, "That room already exists; its configuration was left unchanged."});
          }
          break;
        }
    return true;
  }
  template <class Other>
  void on_presence(const Other&) {}

  net::loop* loop_;
  net::tls* tls_;
  settings how_;
  Sink sink_;
  std::string user_, domain_;
  account_id id_;
  tern::roster_cache roster_;
  // The rooms joined, by their JIDs: one's nick in each, and who is there.
  struct room {
    std::string nick;
    std::map<std::string, member> occupants;
  };
  std::map<std::string, room> rooms_;
  std::map<std::string, tern::bookmarks::conference> bookmarks_;
  std::map<std::string, tern::notifications::decision> pending_notifications_;
  bool notifications_busy_ = false;
  std::map<std::string, room_creation> creating_;
  std::set<std::string> joining_;
  struct pending_invitation { std::string inviter; std::optional<std::string> password; };
  std::map<std::string, pending_invitation> invitations_;
  session_type* session_ = nullptr;
  net::stream* wire_ = nullptr;
  std::uint64_t sent_ = 0;
  bool stopping_ = false;
  bool started_ = false, retry_ = true, connected_ = false;
  std::size_t commands_ = 0;
};

}  // namespace mux::proto::xmpp::client
