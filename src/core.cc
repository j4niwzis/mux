// SPDX-License-Identifier: AGPL-3.0-only
// mux.core: one model for every protocol mux speaks -- accounts,
// conversations, messages, presence -- and the changes to it, as values.
//
// The protocols fill it from the network's thread, as changes; the UI reads
// it on its own thread, applying the changes a frame at a time. A change is
// a plain value in a std::variant: what happened, not a call to make, so it
// can cross the threads in a queue and be applied, logged or tested alike.
export module mux.core;

import std;
import mux.variant;

export namespace mux {

// The overloaded pattern: a visitor made of several callables.
template <class... Fs>
struct overloaded : Fs... {
  using Fs::operator()...;
};
template <class... Fs>
overloaded(Fs...) -> overloaded<Fs...>;

// Which protocol an account speaks. A closed set: what differs between them
// is in the account types, dispatched with std::visit, not behind a base
// class.
namespace protocol {
struct xmpp {
  static constexpr bool is_matrix = false;
  friend auto operator<=>(const xmpp&, const xmpp&) = default;
};
struct matrix {
  static constexpr bool is_matrix = true;
  friend auto operator<=>(const matrix&, const matrix&) = default;
};
}  // namespace protocol
using protocol_t = mux::variant<protocol::xmpp, protocol::matrix>;
[[nodiscard]] inline bool is_matrix(const protocol_t& speaks) {
  return mux::visit([](auto one) { return one.is_matrix; }, speaks);
}

// An account, as the user names it: user@example.com, or @user:example.org.
struct account_id {
  protocol_t speaks = protocol::xmpp{};
  std::string address;
  friend bool operator==(const account_id&, const account_id&) = default;
  friend auto operator<=>(const account_id&, const account_id&) = default;
};

// A conversation within an account: an XMPP contact's bare JID or a MUC's,
// or a Matrix room's id.
struct conversation_id {
  account_id account;
  std::string id;
  friend bool operator==(const conversation_id&, const conversation_id&) = default;
  friend auto operator<=>(const conversation_id&, const conversation_id&) = default;
};

namespace conversation_kind {
struct direct {
  static constexpr bool one_to_one = true;
  friend bool operator==(const direct&, const direct&) = default;
};
struct group {
  static constexpr bool one_to_one = false;
  friend bool operator==(const group&, const group&) = default;
};
}  // namespace conversation_kind
using conversation_kind_t = mux::variant<conversation_kind::direct, conversation_kind::group>;
[[nodiscard]] inline bool one_to_one(const conversation_kind_t& kind) {
  return mux::visit([](auto one) { return one.one_to_one; }, kind);
}

// Where an account is with its server. A failure says why; a connection
// being made again may say why too.
namespace connection {
struct offline {
  friend bool operator==(const offline&, const offline&) = default;
};
struct connecting {
  std::optional<std::string> reason;
  friend bool operator==(const connecting&, const connecting&) = default;
};
struct online {
  friend bool operator==(const online&, const online&) = default;
};
struct failed {
  std::string error;
  friend bool operator==(const failed&, const failed&) = default;
};
}  // namespace connection
using connection_t = mux::variant<connection::offline, connection::connecting, connection::online, connection::failed>;

[[nodiscard]] inline bool is_online(const connection_t& state) {
  return mux::visit(overloaded{[](const connection::online&) { return true; }, [](const auto&) { return false; }},
                    state);
}

namespace availability {
struct offline {
  friend bool operator==(const offline&, const offline&) = default;
};
struct online {
  friend bool operator==(const online&, const online&) = default;
};
struct away {
  friend bool operator==(const away&, const away&) = default;
};
struct extended_away {
  friend bool operator==(const extended_away&, const extended_away&) = default;
};
struct do_not_disturb {
  friend bool operator==(const do_not_disturb&, const do_not_disturb&) = default;
};
struct chat {
  friend bool operator==(const chat&, const chat&) = default;
};
}  // namespace availability
using availability_t = mux::variant<availability::offline, availability::online, availability::away,
                                    availability::extended_away, availability::do_not_disturb, availability::chat>;

struct presence {
  availability_t state = availability::offline{};
  std::optional<std::string> status;
  friend bool operator==(const presence&, const presence&) = default;
};

// What a message says, as plain text and, where it has one, as the
// protocol's formatted body (XHTML-IM's or Matrix's org.matrix.custom.html).
struct body {
  std::string plain;
  std::optional<std::string> html;
  friend bool operator==(const body&, const body&) = default;
};

namespace delivery {
struct sending {
  friend bool operator==(const sending&, const sending&) = default;
};
struct sent {
  friend bool operator==(const sent&, const sent&) = default;
};
struct delivered {
  friend bool operator==(const delivered&, const delivered&) = default;
};
struct read {
  friend bool operator==(const read&, const read&) = default;
};
struct failed {
  friend bool operator==(const failed&, const failed&) = default;
};
}  // namespace delivery
using delivery_t = mux::variant<delivery::sending, delivery::sent, delivery::delivered, delivery::read, delivery::failed>;

// What a message carries besides its text: a picture, shown in it, or a
// file, offered to be saved -- by where its protocol keeps it (an mxc://).
namespace attachment_kind {
struct image {
  static constexpr bool picture = true;
  // A GIF or a WebP: frames that move, played where it is shown.
  bool moves = false;
  friend bool operator==(image, image) = default;
};
struct file {
  static constexpr bool picture = false;
  friend bool operator==(file, file) = default;
};
}  // namespace attachment_kind
using attachment_kind_t = mux::variant<attachment_kind::image, attachment_kind::file>;
[[nodiscard]] inline bool is_picture(const attachment_kind_t& kind) {
  return mux::visit([](auto one) { return one.picture; }, kind);
}
// Whether it is a picture that moves.
[[nodiscard]] inline bool moves(const attachment_kind_t& kind) {
  return mux::visit(overloaded{[](attachment_kind::image one) { return one.moves; },
                               [](attachment_kind::file) { return false; }},
                    kind);
}
// Whether a picture of this type may move: GIF and WebP -- read where a
// picture comes in, from what its sender says it is.
// Sound: a voice message or an audio file, by its type, as the protocol
// said it; an Ogg file by its name where no type was said.
[[nodiscard]] inline bool audio_type(std::string_view mimetype, std::string_view name) {
  return mimetype.starts_with("audio/") || name.ends_with(".ogg") || name.ends_with(".opus") || name.ends_with(".oga");
}
[[nodiscard]] inline bool moving_type(std::string_view mimetype) {
  return mimetype == "image/gif" || mimetype == "image/webp";
}
struct attachment {
  attachment_kind_t kind = attachment_kind::file{};
  std::string source;      // where it is kept: an mxc:// URI
  std::string name;        // its file's name
  std::string mimetype;
  std::int64_t size = 0;   // in bytes, where said
  int width = 0, height = 0;  // a picture's, where said
  // A picture's blurhash, where its sender gave one: what is shown until
  // the picture comes.
  std::optional<std::string> blurhash;
  // A video's file, where it is one -- the source above is then its
  // thumbnail, shown as a picture is -- and how long it runs.
  std::optional<std::string> video;
  std::int64_t duration_ms = 0;
  friend bool operator==(const attachment&, const attachment&) = default;
};

// What a room event is, for choosing which to show, as Element splits them.
namespace room_event {
struct joins {
  friend bool operator==(joins, joins) = default;
};        // joins and leaves
struct invites {
  friend bool operator==(invites, invites) = default;
};      // invitations, removals, bans, knocks
struct names {
  friend bool operator==(names, names) = default;
};        // members' names changed
struct avatars {
  friend bool operator==(avatars, avatars) = default;
};      // members' pictures changed
struct room_name {
  friend bool operator==(room_name, room_name) = default;
};
struct topic {
  friend bool operator==(topic, topic) = default;
};
struct room_avatar {
  friend bool operator==(room_avatar, room_avatar) = default;
};
struct address {
  friend bool operator==(address, address) = default;
};
struct pins {
  friend bool operator==(pins, pins) = default;
};
struct permissions {
  friend bool operator==(permissions, permissions) = default;
};  // power levels
struct access {
  friend bool operator==(access, access) = default;
};       // who may join, who may read the history
struct encryption {
  friend bool operator==(encryption, encryption) = default;
};
struct other {
  friend bool operator==(other, other) = default;
};        // the room made, and what nothing here reads
struct reactions {
  friend bool operator==(reactions, reactions) = default;
};    // each reaction, as a line of its own: hidden unless chosen
}  // namespace room_event
using room_event_t =
    mux::variant<room_event::joins, room_event::invites, room_event::names, room_event::avatars, room_event::room_name,
                 room_event::topic, room_event::room_avatar, room_event::address, room_event::pins,
                 room_event::permissions, room_event::access, room_event::encryption, room_event::other,
                 room_event::reactions>;
inline constexpr std::size_t kRoomEventKinds = mux::variant_size_v<room_event_t>;
inline const std::array<room_event_t, kRoomEventKinds> all_room_events{
    room_event::joins{},     room_event::invites{}, room_event::names{},       room_event::avatars{},
    room_event::room_name{}, room_event::topic{},   room_event::room_avatar{}, room_event::address{},
    room_event::pins{},      room_event::permissions{}, room_event::access{},  room_event::encryption{},
    room_event::other{},     room_event::reactions{}};
// Which kinds of room event a chat shows, as its choices resolve: each by
// its place in room_event_t.
struct room_event_filter {
  std::array<bool, kRoomEventKinds> shown;
  room_event_filter() { shown.fill(true); }
  [[nodiscard]] bool shows(const room_event_t& kind) const { return shown[kind.index()]; }
  friend bool operator==(const room_event_filter&, const room_event_filter&) = default;
};
// Where a choice is made: for every account, for one, for one chat.
namespace choice_level {
struct everywhere {};
struct account {};
struct chat {};
}  // namespace choice_level
using choice_level_t = mux::variant<choice_level::everywhere, choice_level::account, choice_level::chat>;

struct message {
  conversation_id in;
  // The protocol's own id: an XMPP stanza id (or origin-id), a Matrix event
  // id -- what an edit, a reply, a reaction or a receipt points at.
  std::string id;
  std::string sender;  // a bare JID or a Matrix user id
  std::chrono::sys_time<std::chrono::milliseconds> at{};
  mux::body body;
  std::optional<std::string> replies_to;
  bool edited = false;
  bool redacted = false;
  bool outgoing = false;
  // Not something said but something done -- someone joined, the room was
  // renamed, an event nothing here reads -- shown as a line of its own in
  // the middle, as tdesktop shows its service messages.
  bool service = false;
  room_event_t event_kind = room_event::other{};  // what is done, where it is that
  // A reaction, as a reply quotes it: its replies_to is the message it
  // reacted to, where a press on the quote goes.
  bool reaction = false;
  delivery_t delivery = delivery::sent{};
  std::map<std::string, std::set<std::string>> reactions;  // key -> who
  // The same reactions as the events they are, where the protocol has
  // them as events (Matrix): who, with what, when -- to be listed, and
  // answered, one by one.
  struct reaction_event {
    std::string event;
    std::string key;
    std::string who;
    std::chrono::sys_time<std::chrono::milliseconds> at{};
    friend bool operator==(const reaction_event&, const reaction_event&) = default;
  };
  std::vector<reaction_event> reaction_events;
  std::optional<mux::attachment> attachment;
  // Several pictures or files in one message, as a gallery (MSC4274) carries
  // them: shown as an album.
  std::vector<mux::attachment> album;
  friend bool operator==(const message&, const message&) = default;
};

// Someone in a group: their id (a JID in the room, a Matrix user id), the
// name they go by there, and their role, where it has one (owner, admin,
// moderator).
struct member {
  std::string id;
  std::string name;
  std::optional<std::string> role;
  std::optional<std::string> avatar;  // an mxc:// or a hash, the protocol's
  friend bool operator==(const member&, const member&) = default;
};

// Who may join a room, as its join rule says.
namespace join_rule {
struct open {};      // anyone: "public"
struct invite {};    // those invited
struct knock {};     // those who ask, once let in
struct other {};     // restricted, private -- a rule not offered here
}  // namespace join_rule
using join_rule_t = mux::variant<join_rule::open, join_rule::invite, join_rule::knock, join_rule::other>;
// Who may read a room's history.
namespace history_rule {
struct shared {};          // members, all of it
struct invited {};         // members, from when they were invited
struct joined {};          // members, from when they joined
struct world_readable {};  // anyone
}  // namespace history_rule
using history_rule_t =
    mux::variant<history_rule::shared, history_rule::invited, history_rule::joined, history_rule::world_readable>;

// What a room asks of those who do something in it: the level each needs,
// as its power levels say (m.room.power_levels), Matrix's defaults where
// they say nothing. What is asked is a tag, one for each thing done; one
// that sends a kind of state event carries that kind's name, as Matrix
// writes it -- the key its level is kept by.
namespace power_need {
struct send_messages {};  // events_default
struct change_settings {};  // state_default: any state event not listed
struct default_role {};  // users_default
struct invite {};
struct kick {};
struct ban {};
struct redact {};  // remove what others sent
struct notify_everyone {};  // notifications.room: @room
struct rename {
  static constexpr std::string_view event = "m.room.name";
};
struct retopic {
  static constexpr std::string_view event = "m.room.topic";
};
struct change_avatar {
  static constexpr std::string_view event = "m.room.avatar";
};
struct change_address {
  static constexpr std::string_view event = "m.room.canonical_alias";
};
struct change_history {
  static constexpr std::string_view event = "m.room.history_visibility";
};
struct change_access {
  static constexpr std::string_view event = "m.room.join_rules";
};
struct change_permissions {
  static constexpr std::string_view event = "m.room.power_levels";
};
struct encrypt {
  static constexpr std::string_view event = "m.room.encryption";
};
struct upgrade {
  static constexpr std::string_view event = "m.room.tombstone";
};
struct change_acl {
  static constexpr std::string_view event = "m.room.server_acl";
};
struct pin {
  static constexpr std::string_view event = "m.room.pinned_events";
};
}  // namespace power_need
using power_need_t =
    mux::variant<power_need::default_role, power_need::send_messages, power_need::invite, power_need::change_settings,
                 power_need::kick, power_need::ban, power_need::redact, power_need::notify_everyone,
                 power_need::rename, power_need::retopic, power_need::change_avatar, power_need::change_address,
                 power_need::change_history, power_need::change_access, power_need::change_permissions,
                 power_need::encrypt, power_need::upgrade, power_need::change_acl, power_need::pin>;
template <class Need>
concept sends_state = requires { Need::event; };

struct power_needs {
  std::int64_t users_default = 0;
  std::int64_t events_default = 0;
  std::int64_t state_default = 50;
  std::int64_t invite = 0;
  std::int64_t kick = 50;
  std::int64_t ban = 50;
  std::int64_t redact = 50;
  std::int64_t notify_room = 50;
  std::map<std::string, std::int64_t, std::less<>> events;  // by the kind of event
  friend bool operator==(const power_needs&, const power_needs&) = default;

  [[nodiscard]] std::int64_t of(power_need::default_role) const { return users_default; }
  [[nodiscard]] std::int64_t of(power_need::send_messages) const { return events_default; }
  [[nodiscard]] std::int64_t of(power_need::change_settings) const { return state_default; }
  [[nodiscard]] std::int64_t of(power_need::invite) const { return invite; }
  [[nodiscard]] std::int64_t of(power_need::kick) const { return kick; }
  [[nodiscard]] std::int64_t of(power_need::ban) const { return ban; }
  [[nodiscard]] std::int64_t of(power_need::redact) const { return redact; }
  [[nodiscard]] std::int64_t of(power_need::notify_everyone) const { return notify_room; }
  template <sends_state Need>
  [[nodiscard]] std::int64_t of(Need) const {
    const auto found = events.find(Need::event);
    return found == events.end() ? state_default : found->second;
  }
  [[nodiscard]] std::int64_t of(const power_need_t& need) const {
    return mux::visit([this](auto one) { return this->of(one); }, need);
  }
};

// What can be done to a room by those allowed to: named, described, opened
// or closed, people let in or sent out, and given a say.
namespace room_action {
struct rename {
  std::string name;
};
struct retopic {
  std::string topic;
};
struct set_join_rule {
  join_rule_t rule;
};
struct set_history {
  history_rule_t rule;
};
struct invite {
  std::string user;
};
struct kick {
  std::string user;
};
struct ban {
  std::string user;
};
struct unban {
  std::string user;
};
struct set_power {
  std::string user;
  std::int64_t level = 0;
};
struct encrypt {};  // for good: it cannot be turned off
struct set_need {  // the level a thing done asks
  power_need_t need;
  std::int64_t level = 0;
};
}  // namespace room_action
using room_action_t =
    mux::variant<room_action::rename, room_action::retopic, room_action::set_join_rule, room_action::set_history,
                 room_action::invite, room_action::kick, room_action::ban, room_action::unban, room_action::set_power,
                 room_action::encrypt, room_action::set_need>;

// A custom emoji: its shortcode, as written between colons, and its picture
// on the server -- one of a Matrix room's packs, or the user's own.
struct emote {
  std::string shortcode;
  std::string url;
  friend bool operator==(const emote&, const emote&) = default;
};

// Something for the user in a chat, not yet seen, as Telegram's @ and heart
// buttons count them: a message mentioning them, or a reaction to one of
// theirs -- its event, the message to go to, and when.
struct unread_mark {
  std::string event;
  std::string target;
  std::chrono::sys_time<std::chrono::milliseconds> at{};
  friend bool operator==(const unread_mark&, const unread_mark&) = default;
};
namespace mark_kind {
struct mention {};
struct reaction {};
}  // namespace mark_kind
using mark_kind_t = mux::variant<mark_kind::mention, mark_kind::reaction>;

// A room of a server's public directory, as it lists it.
struct directory_room {
  std::string id;
  std::string name;
  std::string alias;
  std::string topic;
  std::optional<std::string> avatar;
  std::int64_t members = 0;
  friend bool operator==(const directory_room&, const directory_room&) = default;
};

// Someone mentioned in what is sent: the name as written in it, and who.
struct mention {
  std::string name;
  std::string user;
  friend bool operator==(const mention&, const mention&) = default;
};

struct conversation {
  conversation_id id;
  conversation_kind_t kind = conversation_kind::direct{};
  std::string name;
  std::optional<std::string> avatar;  // an mxc:// or a hash, the protocol's
  std::optional<std::string> topic;
  bool encrypted = false;
  std::int64_t unread = 0;
  std::int64_t highlights = 0;
  std::vector<std::string> typing;
  std::vector<member> members;  // a group's, as far as they are known
  std::vector<message> timeline;  // oldest first, as far back as is loaded
  // Where to page back from, in the protocol's terms: a MAM id, a Matrix
  // prev_batch. Nothing where the beginning has been reached.
  std::optional<std::string> history_from;
  // How many are in it, as its server counts: more than `members` where
  // not all of them are known yet.
  std::int64_t member_count = 0;
  // Its alias, where it has one: a Matrix room's canonical #alias.
  std::optional<std::string> alias;
  // The messages pinned in it, by their ids, oldest first: a Matrix room's
  // m.room.pinned_events.
  std::vector<std::string> pinned;
  // The custom emoji that can be used in it: its packs' and the user's own.
  std::vector<emote> emotes;
  // And the stickers, of the same packs.
  std::vector<emote> stickers;
  // Who may join it and read its history, and each one's say in it: a
  // Matrix room's power levels, those not listed having the default.
  join_rule_t join_rule = join_rule::invite{};
  history_rule_t history = history_rule::shared{};
  std::map<std::string, std::int64_t> powers;
  std::int64_t power_default = 0;
  // And what each thing done in it asks.
  power_needs needs;
  // Its room version, as it was made: what an upgrade goes from.
  std::string version;
  // Its other published addresses, besides its alias.
  std::vector<std::string> other_aliases;
  // What is for the user in it, not yet seen, oldest first: kept to a number.
  std::vector<unread_mark> unread_mentions;
  std::vector<unread_mark> unread_reactions;
  // Messages a reply in view quotes that are not in the timeline, fetched
  // on their own for their quotes -- kept out of the timeline, and let go
  // once the timeline has them, or when too many have gathered.
  std::map<std::string, message> quoted;
  // Who has read up to where: each other person's last message read, as
  // their receipts say; and the user's own, kept here whether it is sent or
  // not -- what is unread is counted from it.
  std::map<std::string, std::string> read_by;
  std::map<std::string, std::chrono::sys_time<std::chrono::milliseconds>> receipt_times;
  std::optional<std::string> read_up_to;
  // What is unread, as counted here from the user's own position where there
  // is one, and as the server counts it where not.
  //
  // Counted only where the timeline reaches the newest and holds the
  // position: a window far back in the history, or a timeline that no
  // longer has it, counted every message in it as unread -- 60 new in a
  // room where none were.
  // How many unread: those after the one read up to that are not one's own
  // -- and not room events the chat does not show (`shown`, as its settings
  // resolve): what is hidden in it is not counted either.
  [[nodiscard]] std::int64_t unread_here(const room_event_filter& shown = {}) const {
    if (!read_up_to)
      return unread;
    if (detached)
      return latest && latest->id == *read_up_to ? 0 : unread;
    std::int64_t after = 0;
    for (auto it = timeline.rbegin(); it != timeline.rend(); ++it) {
      if (it->id == *read_up_to)
        return after;
      if (!it->outgoing && (!it->service || shown.shows(it->event_kind)))
        ++after;
    }
    return unread;
  }
  // When it was last read, as the model counts: the chats read longest ago
  // lose their loaded history first.
  std::uint64_t read_at = 0;
  // A Matrix space, and the rooms it holds: a folder of chats, not a chat.
  bool space = false;
  std::vector<std::string> children;
  // The named groups it is in, as an XMPP roster's.
  std::vector<std::string> groups;
  // A window of its history, away from its newest -- a message jumped to
  // and what is around it -- rather than all of it from there to the
  // newest: `future_from` is where to page forward from, and what arrives
  // meanwhile is not put in it but only kept as `latest`, until paging
  // forward meets the newest and it is live again.
  bool detached = false;
  std::optional<std::string> future_from;
  std::optional<message> latest;
  // Counted up whenever its members change: what shows them is made again
  // only then -- a big room has thousands.
  std::uint64_t members_revision = 0;
};

// Its newest message, as the chat list shows it and sorts by: the last of
// its timeline, or the newest that came while it is a window elsewhere.
[[nodiscard]] inline const message* newest(const conversation& one) {
  if (one.latest && (one.detached || one.timeline.empty() || one.latest->at > one.timeline.back().at))
    return &*one.latest;
  return one.timeline.empty() ? nullptr : &one.timeline.back();
}
// The newest of what the chat shows: a room event it hides passed over, as
// its preview in the list says what is in it.
[[nodiscard]] inline const message* newest(const conversation& one, const room_event_filter& shown) {
  const auto visible = [&](const message& said) { return !said.service || shown.shows(said.event_kind); };
  if (one.latest && visible(*one.latest) &&
      (one.detached || one.timeline.empty() || one.latest->at > one.timeline.back().at))
    return &*one.latest;
  for (auto it = one.timeline.rbegin(); it != one.timeline.rend(); ++it)
    if (visible(*it))
      return &*it;
  return nullptr;
}

struct account {
  account_id id;
  connection_t state = connection::offline{};
  std::string display_name;
  std::map<std::string, conversation> conversations;  // by conversation id
  std::map<std::string, presence> presences;          // by contact
};

// ---------------------------------------------------------------------------
// The log: a line on standard error for each step an account takes --
// connecting, where to, logged in, what was loaded -- with when and whose.
// Lines from the protocols' threads do not run into each other.

inline void log_line(std::string_view who, std::string_view what) {
  static std::mutex writing;
  const auto now = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
  const std::scoped_lock held(writing);
  std::println(std::cerr, "{:%H:%M:%S} [{}] {}", now, who, what);
}
template <class... Args>
void log(const account_id& who, std::format_string<Args...> what, Args&&... args) {
  log_line(who.address, std::format(what, std::forward<Args>(args)...));
}

// ---------------------------------------------------------------------------
// Changes: what the protocols say happened.

// What a picture or a file is fetched for: told by its type, never by a
// word in a key.
namespace media_use {
struct avatar {  // a chat's avatar or a person's, shown by their id
  std::string of;
};
struct thumbnail {};  // a message's picture, small, as the chat shows it
struct whole {};      // a message's picture, whole, as the viewer shows it
struct to_open {      // a file, saved to Downloads and opened
  std::string name;
};
struct to_save {  // a picture or a file, saved to Downloads
  std::string name;
};
// Sound to play: fetched whole, kept as a whole picture is, and played.
struct to_play {};
// A video to watch: fetched whole into a file of its own, and played.
struct to_watch {};
}  // namespace media_use
using media_use_t = mux::variant<media_use::avatar, media_use::thumbnail, media_use::whole, media_use::to_open,
                                 media_use::to_save, media_use::to_play, media_use::to_watch>;

// Where a message goes among those of its chat.
namespace placement {
struct at_end {};     // live: after the rest -- but not into a window away from the newest
struct at_start {};   // history paged back: before the rest
struct in_window {};  // a window's own: loaded around a message, or paged forward
struct aside {};      // not in the timeline: a message a reply quotes, fetched for its quote
}  // namespace placement
using placement_t = mux::variant<placement::at_end, placement::at_start, placement::in_window, placement::aside>;

// What a link in a message is, as its page says (Open Graph): the site, the
// title, a line about it, and its picture, kept on the server.
struct link_preview {
  std::string site;
  std::string title;
  std::string description;
  std::optional<std::string> image;
  friend bool operator==(const link_preview&, const link_preview&) = default;
};

// A room the user is not in, as its server tells of it (/room_summary): its
// ID, name, address, what it is about, its picture and how many are in it --
// or, where it tells nothing, why.
struct room_preview {
  std::string id;
  std::string name;
  std::string alias;
  std::string topic;
  std::optional<std::string> avatar;
  std::optional<std::int64_t> members;
  std::string note;
  friend bool operator==(const room_preview&, const room_preview&) = default;
};

namespace change {

struct connection_changed {
  account_id account;
  connection_t state;
};

// An account the program no longer has: everything of it goes.
struct account_removed {
  account_id account;
};

struct conversation_updated {
  // The whole of what is known of it, except its timeline.
  conversation_id id;
  conversation_kind_t kind = conversation_kind::direct{};
  std::string name;
  std::optional<std::string> avatar;
  std::optional<std::string> topic;
  bool encrypted = false;
  std::int64_t unread = 0;
  std::int64_t highlights = 0;
  bool space = false;
  std::vector<std::string> children;
  std::vector<std::string> groups;
  std::int64_t member_count = 0;
  std::optional<std::string> alias;
  std::vector<std::string> pinned;
  std::vector<emote> emotes;
  std::vector<emote> stickers;
  join_rule_t join_rule = join_rule::invite{};
  history_rule_t history = history_rule::shared{};
  std::map<std::string, std::int64_t> powers;
  std::int64_t power_default = 0;
  power_needs needs;
  std::string version;
  std::vector<std::string> other_aliases;
};

// Receipts: who has read up to which message, as the server says.
struct receipts_changed {
  conversation_id in;
  std::map<std::string, std::string> read_by;  // user -> the message read up to
  // And when: the receipt's own time, for one read up to an event not
  // among the messages here -- a reaction, a state event, one not loaded.
  std::map<std::string, std::chrono::sys_time<std::chrono::milliseconds>> read_at{};
};


// A picture or a file as its protocol fetched it: the bytes, what they
// were fetched for, and the source they were fetched by.
struct avatar_loaded {
  media_use_t use;
  std::string source;  // the mxc:// or hash it was fetched by
  std::string bytes;
};

// How far a picture or a file being fetched whole has come, 0 to 1, by the
// source it is fetched by: for its loader to show.
struct preview_loaded {
  std::string url;
  link_preview preview;
};

// A room looked up before joining it: by whom, as the link named it, and
// what came of it.
struct room_previewed {
  account_id by;
  std::string asked;
  room_preview preview;
};

// What the developer tools show: a title over some JSON or an answer; and a
// room's state, every event of it, by type and key.
struct devtools_text {
  std::string title;
  std::string text;
};
struct state_entry {
  std::string type;
  std::string key;
  std::string json;
};
struct state_listed {
  conversation_id in;
  std::vector<state_entry> entries;
};

// A room the user made, to be shown once it is: a direct chat or a group.
struct room_created {
  conversation_id id;
};

struct media_progress {
  std::string source;
  float done = 0.0f;
};

// A session an account was given -- a Matrix access token and device -- to
// be kept, so the next start goes on with it rather than logging in again.
struct session_given {
  account_id account;
  std::string access_token;
  std::string device_id;
};

// Who is in a group now: the whole list.
struct members_changed {
  conversation_id in;
  std::vector<member> members;
};

struct conversation_removed {
  conversation_id id;
};

struct presence_changed {
  account_id account;
  std::string contact;
  mux::presence now;
};

// A message that arrived, or was sent from here; one with an id already
// kept replaces it (the echo of one sent, a corrected one).
struct message_added {
  mux::message message;
  placement_t where = placement::at_end{};
};

// A window of a chat's history opened in place of its timeline: emptied,
// to be filled by the messages in_window that follow, and paged back from
// `history_from` and forward from `future_from` -- none where it reaches
// the newest, and is live.
struct window_opened {
  conversation_id in;
  std::optional<std::string> history_from;
  std::optional<std::string> future_from;
};
// A window paged forward: where to go on from, or none where the newest
// was met and it is live again.
struct window_extended {
  conversation_id in;
  std::optional<std::string> future_from;
};

struct message_edited {
  conversation_id in;
  std::string id;
  mux::body now;
};

struct message_redacted {
  conversation_id in;
  std::string id;
};

// A message sent from here, known by a local id until the server answered
// with its own: from now on it is known by that one -- and where the echo
// came first and is already kept under it, the local one goes.
struct message_acknowledged {
  conversation_id in;
  std::string local_id;
  std::string id;
};

struct delivery_changed {
  conversation_id in;
  std::string id;
  delivery_t now;
};

struct reaction_changed {
  conversation_id in;
  std::string id;
  std::string key;
  std::string who;
  bool added = true;
  // The reaction's own event, and when it was sent, where it is one.
  std::string event{};
  std::chrono::sys_time<std::chrono::milliseconds> at{};
  // Come as it happened -- not read back from the history.
  bool live = false;
};

// A message that mentions the user, come as it happened.
struct mentioned {
  conversation_id in;
  std::string event;
  std::chrono::sys_time<std::chrono::milliseconds> at{};
};
// The messages of a chat on screen now: what is for the user in them is
// seen -- as tdesktop marks a mention read, once it is shown, not when the
// chat is scrolled past it.
struct marks_shown {
  conversation_id in;
  std::vector<std::string> shown;
};
// A reaction to one of the user's messages, found catching up: marked even
// where that message is not loaded.
struct reacted_to_mine {
  conversation_id in;
  std::string event;
  std::string target;
  std::chrono::sys_time<std::chrono::milliseconds> at{};
};
// A server's public directory, searched: what it listed.
struct directory_listed {
  account_id by;
  std::string server;
  std::string query;
  std::vector<directory_room> rooms;
};
// A mark of a kind, gone to: the one named, else the oldest.
struct mark_taken {
  conversation_id in;
  mark_kind_t kind;
  std::optional<std::string> event;
};

struct typing_changed {
  conversation_id in;
  std::vector<std::string> who;
};

// History paged back to its beginning, or further to go from.
struct history_position {
  conversation_id in;
  std::optional<std::string> from;
};

}  // namespace change

using change_t = mux::variant<change::connection_changed, change::account_removed, change::conversation_updated,
                              change::conversation_removed,
                              change::presence_changed, change::message_added, change::message_edited,
                              change::message_redacted, change::message_acknowledged, change::delivery_changed, change::reaction_changed,
                              change::typing_changed, change::history_position, change::members_changed,
                              change::session_given, change::avatar_loaded, change::receipts_changed,
                              change::window_opened, change::window_extended, change::media_progress,
                              change::room_created, change::preview_loaded, change::devtools_text,
                              change::state_listed, change::room_previewed, change::mentioned,
                              change::marks_shown, change::mark_taken, change::reacted_to_mine,
                              change::directory_listed>;

// The model: every account, and every change applied to it.
class model {
 public:
  // A message deleted is shown where it was, marked -- or taken out.
  bool show_deleted = false;
  // The links' previews, by their URLs: as fetched this session.
  std::map<std::string, link_preview> previews;

  const std::map<account_id, account>& accounts() const noexcept { return accounts_; }

  account& add(account_id id, std::string display_name = {}) {
    account& made = accounts_[id];
    made.id = std::move(id);
    made.display_name = std::move(display_name);
    return made;
  }

  const conversation* find(const conversation_id& id) const {
    const auto found = accounts_.find(id.account);
    if (found == accounts_.end())
      return nullptr;
    const auto in = found->second.conversations.find(id.id);
    return in == found->second.conversations.end() ? nullptr : &in->second;
  }

  void apply(const change_t& what) {
    mux::visit([this](const auto& one) { on(one); }, what);
  }

  // The user has read a chat up to a message: kept, sent or not.
  void read_up_to(const conversation_id& id, std::string message) {
    if (const auto found = accounts_.find(id.account); found != accounts_.end())
      if (const auto in = found->second.conversations.find(id.id); in != found->second.conversations.end())
        in->second.read_up_to = std::move(message);
  }
  // A chat read now: the last to lose its history.
  void touch(const conversation_id& id) {
    if (const auto found = accounts_.find(id.account); found != accounts_.end())
      if (const auto in = found->second.conversations.find(id.id); in != found->second.conversations.end())
        in->second.read_at = ++read_tick_;
  }
  // The messages held, least recently used first out: past `budget` in
  // all, the chats read longest ago keep only their last message -- what
  // the list shows of them -- and page back from their newest when read
  // again, what comes twice being one by its id. `keep`, the chat being
  // read, keeps all of its.
  void trim(std::size_t budget, const std::optional<conversation_id>& keep) {
    std::size_t held = 0;
    std::vector<conversation*> order;
    for (auto& [id, one] : accounts_)
      for (auto& [key, chat] : one.conversations) {
        held += chat.timeline.size();
        if (chat.timeline.size() > 1 && (!keep || chat.id != *keep))
          order.push_back(&chat);
      }
    if (held <= budget)
      return;
    std::ranges::sort(order, {}, &conversation::read_at);
    for (conversation* chat : order) {
      if (held <= budget)
        break;
      held -= chat->timeline.size() - 1;
      // Its newest kept -- the newest there is, where it is a window away
      // from it, which is let go with the rest: it is live again.
      const message last = newest(*chat) ? *newest(*chat) : chat->timeline.back();
      chat->timeline.assign(1, last);
      chat->detached = false;
      chat->future_from.reset();
      chat->history_from = std::string();  // from the newest
    }
  }

 private:
  account& of(const account_id& id) {
    account& made = accounts_[id];
    made.id = id;
    return made;
  }
  conversation& of(const conversation_id& id) {
    conversation& made = of(id.account).conversations[id.id];
    made.id = id;
    return made;
  }
  static message* message_in(conversation& where, std::string_view id) {
    for (auto it = where.timeline.rbegin(); it != where.timeline.rend(); ++it)
      if (it->id == id)
        return &*it;
    return nullptr;
  }

  void on(const change::connection_changed& one) {
    account& kept = of(one.account);
    kept.state = one.state;
  }
  void on(const change::account_removed& one) { accounts_.erase(one.account); }
  void on(const change::conversation_updated& one) {
    conversation& kept = of(one.id);
    kept.kind = one.kind;
    kept.name = one.name;
    kept.avatar = one.avatar;
    kept.topic = one.topic;
    kept.encrypted = one.encrypted;
    kept.unread = one.unread;
    kept.highlights = one.highlights;
    kept.space = one.space;
    kept.children = one.children;
    kept.groups = one.groups;
    kept.member_count = one.member_count;
    kept.alias = one.alias;
    kept.pinned = one.pinned;
    kept.emotes = one.emotes;
    kept.stickers = one.stickers;
    kept.join_rule = one.join_rule;
    kept.history = one.history;
    kept.powers = one.powers;
    kept.power_default = one.power_default;
    kept.needs = one.needs;
    kept.version = one.version;
    kept.other_aliases = one.other_aliases;
  }
  void on(const change::conversation_removed& one) { of(one.id.account).conversations.erase(one.id.id); }
  void on(const change::presence_changed& one) { of(one.account).presences[one.contact] = one.now; }
  void on(const change::message_added& one) {
    // A deleted one, read back from the disk: only where deleted messages
    // are kept, and something of it is left to show.
    if (one.message.redacted &&
        (!show_deleted || (one.message.body.plain.empty() && !one.message.body.html && !one.message.attachment)))
      return;
    conversation& where = of(one.message.in);
    if (message* kept = one.message.id.empty() ? nullptr : message_in(where, one.message.id)) {
      *kept = one.message;
      return;
    }
    // In the timeline now: what was fetched for a quote is not needed.
    where.quoted.erase(one.message.id);
    mux::visit(overloaded{[&](placement::at_end) {
                            if (!where.latest || one.message.at >= where.latest->at)
                              where.latest = one.message;
                            if (!where.detached)
                              where.timeline.push_back(one.message);
                          },
                          [&](placement::at_start) { where.timeline.insert(where.timeline.begin(), one.message); },
                          [&](placement::in_window) { where.timeline.push_back(one.message); },
                          [&](placement::aside) {
                            // Held to a number: what nothing here quotes or
                            // pins goes first, then one more -- never all at
                            // once, which lost every quote shown, a second
                            // after it came, in a chat that quotes much.
                            constexpr std::size_t kQuotedKept = 500;
                            if (where.quoted.size() >= kQuotedKept) {
                              std::erase_if(where.quoted, [&](const auto& kept) {
                                return !std::ranges::contains(where.pinned, kept.first) &&
                                       std::ranges::none_of(where.timeline, [&](const message& said) {
                                         return said.replies_to == kept.first;
                                       });
                              });
                              if (where.quoted.size() >= kQuotedKept)
                                where.quoted.erase(where.quoted.begin());
                            }
                            where.quoted.insert_or_assign(one.message.id, one.message);
                          }},
               one.where);
  }
  void on(const change::window_opened& one) {
    conversation& where = of(one.in);
    where.timeline.clear();
    where.history_from = one.history_from;
    where.future_from = one.future_from;
    where.detached = one.future_from.has_value();
  }
  void on(const change::window_extended& one) {
    conversation& where = of(one.in);
    where.future_from = one.future_from;
    where.detached = one.future_from.has_value();
  }
  void on(const change::message_edited& one) {
    if (message* kept = message_in(of(one.in), one.id)) {
      kept->body = one.now;
      kept->edited = true;
    }
  }
  // A message deleted: where deleted messages are kept, it stays where it
  // was with all it said and its time, marked; else it is taken out.
  void on(const change::message_redacted& one) {
    conversation& where = of(one.in);
    if (show_deleted) {
      if (message* kept = message_in(where, one.id))
        kept->redacted = true;
      return;
    }
    std::erase_if(where.timeline, [&](const message& each) { return each.id == one.id; });
    if (where.latest && where.latest->id == one.id)
      where.latest.reset();
  }
  void on(const change::message_acknowledged& one) {
    conversation& where = of(one.in);
    if (message_in(where, one.id)) {
      std::erase_if(where.timeline, [&](const message& kept) { return kept.id == one.local_id; });
      return;
    }
    if (message* kept = message_in(where, one.local_id)) {
      kept->id = one.id;
      kept->delivery = delivery::sent{};
    }
  }
  void on(const change::delivery_changed& one) {
    if (message* kept = message_in(of(one.in), one.id))
      kept->delivery = one.now;
  }
  void on(const change::reaction_changed& one) {
    if (message* kept = message_in(of(one.in), one.id)) {
      auto& who = kept->reactions[one.key];
      std::erase_if(kept->reaction_events, [&](const message::reaction_event& each) {
        return each.key == one.key && each.who == one.who;
      });
      if (one.added) {
        who.insert(one.who);
        if (!one.event.empty())
          kept->reaction_events.push_back({one.event, one.key, one.who, one.at});
        // Another's reaction to the user's own, as it happened: for them.
        if (one.live && kept->outgoing && one.who != one.in.account.address && !one.event.empty())
          keep_mark(of(one.in).unread_reactions, {one.event, one.id, one.at});
      } else {
        who.erase(one.who);
        if (who.empty())
          kept->reactions.erase(one.key);
      }
    }
  }
  // Marks kept to a number, the oldest going first: a flood of them cannot
  // grow a chat without end.
  static void keep_mark(std::vector<unread_mark>& marks, unread_mark one) {
    constexpr std::size_t kMarksKept = 500;
    if (std::ranges::contains(marks, one.event, &unread_mark::event))
      return;
    marks.push_back(std::move(one));
    if (marks.size() > kMarksKept)
      marks.erase(marks.begin());
  }
  void on(const change::mentioned& one) { keep_mark(of(one.in).unread_mentions, {one.event, one.event, one.at}); }
  void on(const change::directory_listed&) {}  // the window's: the Explore dialog
  void on(const change::reacted_to_mine& one) {
    keep_mark(of(one.in).unread_reactions, {one.event, one.target, one.at});
  }
  void on(const change::marks_shown& one) {
    conversation& where = of(one.in);
    const auto shown = [&](const unread_mark& mark) { return std::ranges::contains(one.shown, mark.target); };
    std::erase_if(where.unread_mentions, shown);
    std::erase_if(where.unread_reactions, shown);
  }
  void on(const change::mark_taken& one) {
    conversation& where = of(one.in);
    auto& marks = mux::visit(overloaded{[&](mark_kind::mention) -> std::vector<unread_mark>& { return where.unread_mentions; },
                                        [&](mark_kind::reaction) -> std::vector<unread_mark>& { return where.unread_reactions; }},
                             one.kind);
    if (one.event)
      std::erase_if(marks, [&](const unread_mark& mark) { return mark.event == *one.event; });
    else if (!marks.empty())
      marks.erase(marks.begin());
  }
  void on(const change::typing_changed& one) { of(one.in).typing = one.who; }
  void on(const change::history_position& one) { of(one.in).history_from = one.from; }
  void on(const change::members_changed& one) {
    conversation& where = of(one.in);
    where.members = one.members;
    ++where.members_revision;
  }
  void on(const change::session_given&) {}  // the program's to keep, not the model's
  void on(const change::avatar_loaded&) {}  // the window's to show, not the model's
  void on(const change::media_progress&) {}  // the window's too
  void on(const change::room_created&) {}    // the program's: it shows it
  void on(const change::devtools_text&) {}   // the window's
  void on(const change::state_listed&) {}    // the window's
  void on(const change::room_previewed&) {}  // the window's: the room's card
  void on(const change::preview_loaded& one) { previews.insert_or_assign(one.url, one.preview); }
  void on(const change::receipts_changed& one) {
    conversation& kept = of(one.in);
    for (const auto& [user, event] : one.read_by)
      kept.read_by.insert_or_assign(user, event);
    for (const auto& [user, when] : one.read_at)
      kept.receipt_times.insert_or_assign(user, when);
  }

  std::map<account_id, account> accounts_;
  std::uint64_t read_tick_ = 0;
};

// Changes from the network's thread to the UI's: pushed on one, taken all
// at once on the other. `notify` is called after a push -- for the UI to
// wake its event loop (an SDL user event), so that nothing polls.
template <class Notify>
class mailbox {
 public:
  explicit mailbox(Notify notify = {}) : notify_(std::move(notify)) {}

  void push(change_t one) {
    {
      std::lock_guard held(lock_);
      pending_.push_back(std::move(one));
    }
    notify_();
  }

  std::vector<change_t> take() {
    std::lock_guard held(lock_);
    return std::exchange(pending_, {});
  }

 private:
  std::mutex lock_;
  std::vector<change_t> pending_;
  Notify notify_;
};

}  // namespace mux
