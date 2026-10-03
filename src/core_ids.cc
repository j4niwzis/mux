// SPDX-License-Identifier: AGPL-3.0-only
// mux.core.ids: what the model is made of, as values -- accounts and their
// ids, conversations, messages, presence, rooms' rules -- apart from the
// changes to it and the model itself (mux.core), so that what a protocol
// says of its own (its changes, its parts of the model) can be made of them
// below the model, which holds them.
export module mux.core.ids;

import std;
import splice;
export import mux.proto.tags;
export import mux.proto.state;

export namespace mux {

// Which protocol an account speaks. A closed set: what differs between them
// is in the account types, dispatched with std::visit, not behind a base
// class.

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
using conversation_kind_t = splice::variant<conversation_kind::direct, conversation_kind::group>;
[[nodiscard]] inline bool one_to_one(const conversation_kind_t& kind) {
  return splice::visit([](auto one) { return one.one_to_one; }, kind);
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
using connection_t = splice::variant<connection::offline, connection::connecting, connection::online, connection::failed>;

[[nodiscard]] inline bool is_online(const connection_t& state) {
  return splice::visit(splice::overloaded{[](const connection::online&) { return true; }, [](const auto&) { return false; }},
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
using availability_t = splice::variant<availability::offline, availability::online, availability::away,
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
using delivery_t = splice::variant<delivery::sending, delivery::sent, delivery::delivered, delivery::read, delivery::failed>;

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
using attachment_kind_t = splice::variant<attachment_kind::image, attachment_kind::file>;
[[nodiscard]] inline bool is_picture(const attachment_kind_t& kind) {
  return splice::visit([](auto one) { return one.picture; }, kind);
}
// Whether it is a picture that moves.
[[nodiscard]] inline bool moves(const attachment_kind_t& kind) {
  return splice::visit(splice::overloaded{[](attachment_kind::image one) { return one.moves; },
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
};        // the room made, and the rest of what is said of the room
struct unreadable {
  friend bool operator==(unreadable, unreadable) = default;
};   // an event mux cannot read: said as "sent <its type>"
struct reactions {
  friend bool operator==(reactions, reactions) = default;
};    // each reaction, as a line of its own: hidden unless chosen
struct unreactions {
  friend bool operator==(unreactions, unreactions) = default;
};    // each reaction taken back, as a line of its own: hidden unless chosen
}  // namespace room_event
using room_event_t =
    splice::variant<room_event::joins, room_event::invites, room_event::names, room_event::avatars, room_event::room_name,
                 room_event::topic, room_event::room_avatar, room_event::address, room_event::pins,
                 room_event::permissions, room_event::access, room_event::encryption, room_event::other,
                 room_event::unreadable, room_event::reactions, room_event::unreactions>;
inline constexpr std::size_t kRoomEventKinds = splice::variant_size_v<room_event_t>;
inline const std::array<room_event_t, kRoomEventKinds> all_room_events{
    room_event::joins{},     room_event::invites{}, room_event::names{},       room_event::avatars{},
    room_event::room_name{}, room_event::topic{},   room_event::room_avatar{}, room_event::address{},
    room_event::pins{},      room_event::permissions{}, room_event::access{},  room_event::encryption{},
    room_event::other{},     room_event::unreadable{}, room_event::reactions{}, room_event::unreactions{}};
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
using choice_level_t = splice::variant<choice_level::everywhere, choice_level::account, choice_level::chat>;

// A thread's summary, on its root (m.relations' m.thread, or counted
// here): how many answers, the latest -- who, what, when -- and whether the
// user took part.
struct thread_summary {
  std::int64_t count = 0;
  std::string last_id;
  std::string last_sender;
  std::string last_text;
  std::chrono::sys_time<std::chrono::milliseconds> last_at{};
  bool participated = false;
  friend bool operator==(const thread_summary&, const thread_summary&) = default;
};

// A message forwarded, as the client that sent it marked it: who it is from
// -- their id and name -- and a link to the original.
// Sent into a thread: its root, and its latest event -- what a client that
// does not know threads shows it as an answer to.
struct thread_place {
  std::string root;
  std::string latest;
};
// A video sent: its length, and its first picture (PNG) as its thumbnail,
// with that picture's size -- what m.video says, so that it is shown as a
// video, not a file.
struct video_look {
  std::int64_t duration_ms = 0;
  std::string thumbnail;
  int thumbnail_width = 0, thumbnail_height = 0;
};
struct forward_info {
  std::string from;
  std::string name;
  std::string link;
  friend bool operator==(const forward_info&, const forward_info&) = default;
};
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
  // Came end-to-end encrypted, and was read: in an encrypted room, one that
  // did not is marked as such -- a server or anyone in the room can put a
  // plain message there, and it looked the same.
  bool encrypted = false;
  // And from a device its sender did not cross-sign: their account's, as
  // the server says, but not vouched for by them (review 4, H1).
  bool unverified = false;
  // Came in the clear, live, into a room known then to be encrypted: marked
  // "not encrypted" whatever time it says it was sent at.
  bool came_plain = false;
  // Its key from the backup or an import: its authenticity not guaranteed.
  bool unauthenticated = false;
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
  // In a thread (m.thread): its root -- kept in the chat's threads, not in
  // its timeline. And, where it is a thread's root, the thread's summary.
  std::optional<std::string> thread;
  std::optional<thread_summary> threaded;
  // Forwarded: from whom, and where it was.
  std::optional<forward_info> forwarded;
  // A sticker (m.sticker): its picture drawn as Telegram draws one -- no
  // bubble, smaller than a photo, the time on a plate over its corner.
  bool sticker = false;
  // A reaction's key, where it is one -- shown as a line, or fetched aside:
  // what its menu changes it from.
  std::string reaction_key;
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

// One asking to join a room that lets people knock: who, and why they say.
struct knock_request {
  std::string id;
  std::string name;
  std::string reason;
  friend bool operator==(const knock_request&, const knock_request&) = default;
};

// What can be done to a room by those allowed to, in any protocol: named,
// described, people let in or sent out. What a protocol has beyond these is
// its own (Matrix's room changes: rules, power levels, encryption, upgrade).
namespace room_action {
struct rename {
  std::string name;
};
struct retopic {
  std::string topic;
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
}  // namespace room_action
using room_action_t = splice::variant<room_action::rename, room_action::retopic, room_action::invite, room_action::kick,
                                     room_action::ban, room_action::unban>;

// A custom emoji: its shortcode, as written between colons, and its picture
// on the server -- one of a Matrix room's packs, or the user's own.
struct emote {
  std::string shortcode;
  std::string url;
  // As its pack says: its words, its size and type -- sent with a sticker,
  // which other clients size by -- and the pack it is of, for the picker.
  std::string body;
  std::optional<std::int64_t> w, h, size;
  std::optional<std::string> mimetype;
  std::string pack;
  std::optional<std::string> pack_avatar;
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
using mark_kind_t = splice::variant<mark_kind::mention, mark_kind::reaction>;

// A room of a server's public directory, as it lists it.
// Someone found -- in the user directory, or among one's chats: their ID,
// their name and picture where known.
struct found_person {
  std::string id;
  std::string name;
  std::optional<std::string> avatar;
  friend bool operator==(const found_person&, const found_person&) = default;
};
// A pack of custom emoji and stickers (MSC2545), as it is edited: one's own
// (account data) or a room's (a state event, by its state key) -- its name,
// picture, attribution and use, and its images.
struct pack_picture {
  std::string shortcode;
  std::string url;  // mxc://
  std::string body;
  bool emoji = true;    // usable as an emoji
  bool sticker = true;  // and as a sticker
  std::string mimetype;
  std::int64_t width = 0, height = 0, size = 0;
  friend bool operator==(const pack_picture&, const pack_picture&) = default;
};
// A pack of custom emoji and stickers, as its protocol keeps it: a chat's
// (a Matrix room's packs, a Telegram group's set) or the account's own (a
// Matrix user's pack, Telegram's installed sets), by the key its protocol
// names it with -- made by the protocol for a new one.
struct emote_pack {
  std::optional<std::string> chat;  // none: the account's own
  std::string key;
  std::string name;
  std::optional<std::string> avatar;
  std::string attribution;
  bool emoji = true;
  bool sticker = true;
  std::vector<pack_picture> pictures;
  friend bool operator==(const emote_pack&, const emote_pack&) = default;
};
struct directory_room {
  std::string id;
  std::string name;
  std::string alias;
  std::string topic;
  std::optional<std::string> avatar;
  std::int64_t members = 0;
  bool space = false;  // a space: browsed into, not only joined
  friend bool operator==(const directory_room&, const directory_room&) = default;
};

// Someone mentioned in what is sent: the name as written in it, and who.
struct mention {
  std::string name;
  std::string user;
  friend bool operator==(const mention&, const mention&) = default;
};

// An invite to a room not joined yet: who sent it, by their ID and their
// name, and whether it is to a direct chat.
struct invite_info {
  std::string from;
  std::string from_name;
  bool direct = false;
  friend bool operator==(const invite_info&, const invite_info&) = default;
};
struct conversation {
  conversation_id id;
  conversation_kind_t kind = conversation_kind::direct{};
  std::string name;
  std::optional<std::string> avatar;  // an mxc:// or a hash, the protocol's
  std::optional<std::string> topic;
  bool encrypted = false;
  std::int64_t unread = 0;
  // When it was turned on (m.room.encryption's time): what was said before
  // was said in the clear, and is not marked for it.
  std::optional<std::chrono::sys_time<std::chrono::milliseconds>> encrypted_since;
  std::int64_t highlights = 0;
  std::vector<std::string> typing;
  std::vector<knock_request> knocking;  // asking to join, where it lets them knock
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
  // What its protocol keeps of it beyond this (Matrix: its rules and power
  // levels), or none.
  room_part_t theirs;
  // Its other published addresses, besides its alias.
  std::vector<std::string> other_aliases;
  // What is for the user in it, not yet seen, oldest first: kept to a number.
  std::vector<unread_mark> unread_mentions;
  std::vector<unread_mark> unread_reactions;
  // The marks already seen or gone to, by their event: kept (and on disk),
  // so that what the next start catches up on again is not unread again.
  std::vector<std::string> seen_marks;
  // Messages a reply in view quotes that are not in the timeline, fetched
  // on their own for their quotes -- kept out of the timeline, and let go
  // once the timeline has them, or when too many have gathered.
  std::map<std::string, message> quoted;
  // Each thread's answers, by its root, oldest first: kept apart from the
  // timeline, as Element keeps them; and the roots the server listed.
  std::map<std::string, std::vector<message>> threads;
  std::vector<std::string> thread_roots;
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
  // Made for a forum's row in the chat list only: the room its newest is in.
  std::optional<std::string> forum_topic;
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
  // Invited to, not joined: who asked. Joined, or declined, it goes.
  std::optional<invite_info> invite;
};

// Its newest message, as the chat list shows it and sorts by: the last of
// its timeline, or the newest that came while it is a window elsewhere.
// Live, its timeline has all that came: `latest` is a copy of what came last
// by its time, not changed as the timeline's is -- one's own still with the
// id and the clock it was sent with, an edit or a deletion not in it -- and,
// a clock ahead, it stayed "newest" over what others said after it: the list
// said one thing and the chat another.
[[nodiscard]] inline const message* newest(const conversation& one) {
  if (one.latest && (one.detached || one.timeline.empty()))
    return &*one.latest;
  return one.timeline.empty() ? nullptr : &one.timeline.back();
}
// The newest of what the chat shows: a room event it hides passed over, as
// its preview in the list says what is in it.
[[nodiscard]] inline const message* newest(const conversation& one, const room_event_filter& shown) {
  const auto visible = [&](const message& said) { return !said.service || shown.shows(said.event_kind); };
  if (one.latest && visible(*one.latest) && (one.detached || one.timeline.empty()))
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
// A picture to copy: fetched whole, kept as a whole picture is, and put on
// the clipboard.
struct to_copy {};
}  // namespace media_use
using media_use_t = splice::variant<media_use::avatar, media_use::thumbnail, media_use::whole, media_use::to_open,
                                 media_use::to_save, media_use::to_play, media_use::to_watch, media_use::to_copy>;

// Where a message goes among those of its chat.
namespace placement {
struct at_end {};     // live: after the rest -- but not into a window away from the newest
struct at_start {};   // history paged back: before the rest
struct in_window {};  // a window's own: loaded around a message, or paged forward
struct aside {};      // not in the timeline: a message a reply quotes, fetched for its quote
}  // namespace placement
using placement_t = splice::variant<placement::at_end, placement::at_start, placement::in_window, placement::aside>;

// What a link in a message is, as its page says (Open Graph): the site, the
// title, a line about it, and its picture, kept on the server.
struct link_preview {
  std::string site;
  std::string title;
  std::string description;
  std::optional<std::string> image;
  // Read from the site itself: its picture an https address, to
  // be fetched from there too -- and only where the chat fetches previews
  // so; else kept on the server (an mxc://).
  bool from_site = false;
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
  // An invite to it: Accept and Decline, in place of Join.
  bool invite = false;
  // Its join rule lets people ask: Ask to join, in place of Join.
  bool knock = false;
  friend bool operator==(const room_preview&, const room_preview&) = default;
};

// Emoji verification (SAS, the spec's m.sas.v1): the 64 emoji it shows, by
// the index its bytes give, with the names the spec gives them.
inline constexpr std::array<std::pair<std::string_view, std::string_view>, 64> sas_emoji{{
    {"\U0001F436", "Dog"},       {"\U0001F431", "Cat"},        {"\U0001F981", "Lion"},       {"\U0001F40E", "Horse"},
    {"\U0001F984", "Unicorn"},   {"\U0001F437", "Pig"},        {"\U0001F418", "Elephant"},   {"\U0001F430", "Rabbit"},
    {"\U0001F43C", "Panda"},     {"\U0001F413", "Rooster"},    {"\U0001F427", "Penguin"},    {"\U0001F422", "Turtle"},
    {"\U0001F41F", "Fish"},      {"\U0001F419", "Octopus"},    {"\U0001F98B", "Butterfly"},  {"\U0001F337", "Flower"},
    {"\U0001F333", "Tree"},      {"\U0001F335", "Cactus"},     {"\U0001F344", "Mushroom"},   {"\U0001F30F", "Globe"},
    {"\U0001F319", "Moon"},      {"\u2601\uFE0F", "Cloud"},     {"\U0001F525", "Fire"},       {"\U0001F34C", "Banana"},
    {"\U0001F34E", "Apple"},     {"\U0001F353", "Strawberry"}, {"\U0001F33D", "Corn"},       {"\U0001F355", "Pizza"},
    {"\U0001F382", "Cake"},      {"\u2764\uFE0F", "Heart"},     {"\U0001F600", "Smiley"},     {"\U0001F916", "Robot"},
    {"\U0001F3A9", "Hat"},       {"\U0001F453", "Glasses"},    {"\U0001F527", "Spanner"},    {"\U0001F385", "Santa"},
    {"\U0001F44D", "Thumbs Up"}, {"\u2602\uFE0F", "Umbrella"},  {"\u231B", "Hourglass"},     {"\u23F0", "Clock"},
    {"\U0001F381", "Gift"},      {"\U0001F4A1", "Light Bulb"}, {"\U0001F4D5", "Book"},       {"\u270F\uFE0F", "Pencil"},
    {"\U0001F4CE", "Paperclip"}, {"\u2702\uFE0F", "Scissors"},  {"\U0001F512", "Lock"},       {"\U0001F511", "Key"},
    {"\U0001F528", "Hammer"},    {"\u260E\uFE0F", "Telephone"}, {"\U0001F3C1", "Flag"},       {"\U0001F682", "Train"},
    {"\U0001F6B2", "Bicycle"},   {"\u2708\uFE0F", "Aeroplane"}, {"\U0001F680", "Rocket"},     {"\U0001F3C6", "Trophy"},
    {"\u26BD", "Ball"},          {"\U0001F3B8", "Guitar"},     {"\U0001F3BA", "Trumpet"},    {"\U0001F514", "Bell"},
    {"\u2693", "Anchor"},        {"\U0001F3A7", "Headphones"}, {"\U0001F4C1", "Folder"},     {"\U0001F4CC", "Pin"},
}};

// How far an emoji verification has come: asked by them, to be accepted;
// waiting on the other side; the emoji to compare (by the spec's indices);
// done; or stopped, and why.
namespace verification_step {
struct asked {
  friend bool operator==(asked, asked) = default;
};
struct waiting {
  friend bool operator==(waiting, waiting) = default;
};
struct compare {
  std::array<int, 7> emoji{};
  friend bool operator==(const compare&, const compare&) = default;
};
struct done {
  friend bool operator==(done, done) = default;
};
struct cancelled {
  std::string reason;
  friend bool operator==(const cancelled&, const cancelled&) = default;
};
}  // namespace verification_step
using verification_step_t = splice::variant<verification_step::asked, verification_step::waiting, verification_step::compare,
                                            verification_step::done, verification_step::cancelled>;
// What this account knows of another's encryption identity, as Element
// says it: verified (here, by emoji, or their master key verified), not
// verified, or changed since it was first seen -- a new identity, which may
// be the server's, until verified again.
namespace trust {
struct unverified {
  friend bool operator==(unverified, unverified) = default;
};
struct verified {
  friend bool operator==(verified, verified) = default;
};
struct changed {
  friend bool operator==(changed, changed) = default;
};
}  // namespace trust
using trust_t = splice::variant<trust::unverified, trust::verified, trust::changed>;

// A list of a protocol's own changes (its changes_type(state)), made into the
// one variant of all changes in mux.core.
template <class... Changes>
struct change_list {};

}  // namespace mux
