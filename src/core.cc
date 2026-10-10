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
import splice;
// The protocols, as tags in their own namespaces, and protocol_t: one list;
// and each one's account state, protocol_state_t.
export import mux.proto.tags;
export import mux.proto.state;
// What the model is made of; the changes and the model are here -- each
// protocol's own changes too (mux.proto.changes).
export import mux.core.ids;
export import mux.proto.changes;
export import mux.calls.types;

export namespace mux {

namespace change {

struct notifications_changed {
  account_id by;
  std::optional<std::string> room;
  notification_choices choices;
  bool sound_shared = true;
};

struct connection_changed {
  account_id account;
  connection_t state;
};


// Something done that is said to the user, as a notice: its heading, and
// what it says.
struct notice {
  account_id by;
  std::string heading;
  std::string what;
};

// Something asked of the server that it refused: said to the user, as a
// notice, with what the server gave as its reason.
// A person's sessions, as their keys list them: each by its id and name,
// and whether it is verified -- cross-signed by them, or by emoji here.
struct device_view {
  std::string id;
  std::string name;
  bool verified = false;
  friend bool operator==(const device_view&, const device_view&) = default;
};
struct devices_listed {
  account_id by;
  std::string user;
  std::vector<device_view> devices;
};
struct trust_changed {
  account_id by;
  std::string user;
  trust_t now;
};
struct refused {
  account_id by;
  std::string what;
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
  // When it was turned on (m.room.encryption's time): what was said before
  // was said in the clear, and is not marked for it.
  std::optional<std::chrono::sys_time<std::chrono::milliseconds>> encrypted_since;
  std::int64_t highlights = 0;
  bool space = false;
  std::vector<std::string> children;
  std::vector<std::string> groups;
  std::int64_t member_count = 0;
  std::optional<std::string> alias;
  std::vector<std::string> pinned;
  std::vector<emote> emotes;
  std::vector<emote> stickers;
  room_part_t theirs;  // its protocol's own part of it
  // Upgraded away: the room it continues in (m.room.tombstone), and what
  // its tombstone said; and the room this one continues, where it does.
  std::vector<std::string> other_aliases;
  // Invited to, not joined: who asked.
  std::optional<invite_info> invite;
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
// What an account's protocol says it is now -- its stream up, what its server
// has -- for its extension points to decide by.
struct protocol_state_changed {
  account_id account;
  protocol_state_t now;
};

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

// Who is in a group now: the whole list.
struct members_changed {
  conversation_id in;
  std::vector<member> members;
  std::vector<knock_request> knocking;  // asking to join
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
  // Who edited it: applied only where they sent what it edits. Anyone in a
  // room could otherwise rewrite anyone's message.
  std::optional<std::string> by;
  // Came from the server in the clear: never applied to a message that came
  // encrypted -- the server could otherwise rewrite it (review 4, H3).
  bool plain = false;
  // When it was edited, where the protocol says: what replaced the text
  // before it, in the message's edit history. Now, where it does not.
  std::chrono::sys_time<std::chrono::milliseconds> at{};
  std::string event;  // the replacement event, not the original message
};

// A message that came encrypted and was read so.
struct message_encrypted {
  conversation_id in;
  std::string id;
  // From a device its sender cross-signed.
  bool verified = false;
  // Read with a key that came from the backup or an import, not from its
  // sender: its authenticity cannot be guaranteed on this device (Element's
  // words).
  bool imported = false;
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

// A message sent from here that the server never took, let go: taken out
// of the chat, as Element's "Delete" does with one not sent.
struct message_discarded {
  conversation_id in;
  std::string id;
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
  std::optional<std::string> shortcode;
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
// A room's threads, as the server lists them: their roots, newest first
// (each root's own message comes aside, with its summary).
struct threads_listed {
  conversation_id in;
  std::vector<std::string> roots;
};
// The user directory searched: who it found for what was asked.
struct people_found {
  account_id by;
  std::string query;
  std::vector<found_person> people;
};
// A person's profile, as their server gives it: their name and their
// picture -- one met outside the room, a forward's sender.
struct profile_found {
  account_id by;
  std::string user;
  std::optional<std::string> name;
  std::optional<std::string> avatar;
};
// A server's public directory, searched: what it listed.
struct directory_listed {
  account_id by;
  std::string server;
  std::string query;
  std::vector<directory_room> rooms;
  // A space's rooms and spaces, joined or not, where it is its listing.
  std::optional<std::string> space;
  // Where the next page starts, where there is one; and whether this is a
  // further page, to go after what was listed.
  std::optional<std::string> next;
  bool more = false;
};
// A mark of a kind, gone to: the one named, else the oldest.
// Marks seen before, read back from the disk: not to be unread again.
struct marks_seen {
  conversation_id in;
  std::vector<std::string> events;
};
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
// An event the server says is not there -- not found, or not this user's
// to see: the marks on it let go, for nothing will ever show it.
struct event_missing {
  conversation_id in;
  std::string id;
  // Shown under another's id instead: an edit, in the message it edits.
  std::optional<std::string> instead{};
};

// A call's signalling, as its protocol carried it: what was said in one
// call, by the other side or by the user's own other session. Protocol-
// neutral: Matrix's m.call.* events and XMPP's Jingle are read into it.
namespace call_end {
struct hung_up {};
struct busy {};
struct timed_out {};    // not answered in time
struct failed {};       // the connection, or the sound, could not be had
struct other {
  std::string said;
};
}  // namespace call_end
using call_end_t = spl::variant<call_end::hung_up, call_end::busy, call_end::timed_out, call_end::failed, call_end::other>;
namespace call_said {
struct invite {
  calls::session_description offer;
  std::chrono::milliseconds lifetime{60000};
};
struct answer {
  calls::session_description it;
};
struct candidates {
  std::vector<calls::ice_candidate> them;
};
struct hangup {
  call_end_t why;
};
struct reject {};
// The caller's choice among those who answered: the one it talks to.
struct select_answer {
  std::string party;
};
}  // namespace call_said
using call_said_t =
    spl::variant<call_said::invite, call_said::answer, call_said::candidates, call_said::hangup, call_said::reject,
                    call_said::select_answer>;
struct call_signalled {
  conversation_id in;
  std::string call;    // the call's id
  std::string party;   // the device that said it: one of several a person has
  std::string sender;
  bool mine = false;   // the user's own, from another session
  std::chrono::sys_time<std::chrono::milliseconds> at{};
  call_said_t said;
};
// The servers a call of an account goes through, as its server gave them
// (Matrix's /voip/turnServer): asked as a call starts.
struct call_servers {
  account_id account;
  std::vector<calls::ice_server> servers;
};

}  // namespace change

// The changes every protocol says, here; and each protocol's own, from its
// change list -- changes_type(state), found by ADL in its folder (mux.proto.
// <p>.changes), none where it gives none -- all one variant.
using core_changes = spl::variant<change::notifications_changed, change::protocol_state_changed, change::trust_changed, change::devices_listed, change::message_encrypted, change::connection_changed, change::refused, change::notice, change::account_removed, change::conversation_updated, change::conversation_removed, change::presence_changed, change::message_added, change::message_edited, change::message_redacted, change::message_acknowledged, change::delivery_changed, change::message_discarded, change::reaction_changed, change::typing_changed, change::history_position, change::event_missing, change::members_changed, change::avatar_loaded, change::receipts_changed, change::window_opened, change::window_extended, change::media_progress, change::room_created, change::preview_loaded, change::room_previewed, change::mentioned, change::marks_shown, change::mark_taken, change::marks_seen, change::reacted_to_mine, change::directory_listed, change::people_found, change::profile_found, change::threads_listed, change::call_signalled, change::call_servers>;
namespace changes_defaults {
constexpr type_tag<change_list<>> changes_type(const auto&) { return {}; }
}  // namespace changes_defaults
template <class Tag>
constexpr auto changes_type_of() {
  using changes_defaults::changes_type;
  return changes_type(state_of<Tag>{});
}
template <class Tag>
using changes_of = typename decltype(changes_type_of<Tag>())::type;
template <class Variant, class... Lists>
struct with_changes {
  using type = Variant;
};
template <class... Have, class... Theirs, class... Lists>
struct with_changes<spl::variant<Have...>, change_list<Theirs...>, Lists...>
    : with_changes<spl::variant<Have..., Theirs...>, Lists...> {};
template <class>
struct all_changes;
template <class... Tags>
struct all_changes<protocol_list<Tags...>> {
  using type = typename with_changes<core_changes, changes_of<Tags>...>::type;
};
using change_t = all_changes<protocols>::type;

// Whether a change is a protocol's own: one of its list's.
template <class Change, class... Cs>
constexpr bool lists_change(change_list<Cs...>) {
  struct all : type_tag<Cs>... {};
  return std::derived_from<all, type_tag<Change>>;
}
template <class Change, class... Tags>
constexpr bool protocols_change(protocol_list<Tags...>) {
  return (lists_change<Change>(changes_of<Tags>{}) || ...);
}
template <class Change>
concept protocol_change = protocols_change<Change>(protocols{});

// The model: every account, and every change applied to it.
// What a protocol's own change does to the model: nothing, unless the
// protocol says (changed_in, by ADL on its change).
namespace model_defaults {
inline void changed_in(auto&, const auto&) {}
}  // namespace model_defaults

// The chats, as the model keeps them: every account, by its id, and each
// account's conversations, by theirs -- lists of a skiff model, changed by
// its edits, so that what shows one chat is told of that chat's changes.
struct chats_root {
  skiff::model::Keyed<account_id, account> accounts;
};
struct chats_reactions {};
using chats_model = skiff::model::Model<chats_root, chats_reactions>;

class model {
 public:
  // A message deleted is shown where it was, marked -- or taken out.
  bool show_deleted = false;
  // The links' previews, by their URLs: as fetched this session.
  std::map<std::string, link_preview> previews;
  // Rooms not joined here whose server said they are there, by the address
  // a message names them by, and each one's name as its server gave it: as
  // asked this session.
  std::map<std::string, std::string, std::less<>> rooms_found;

  const skiff::model::Keyed<account_id, account>& accounts() const noexcept { return chats_.root().accounts; }
  // The chats as the skiff model holds them: what binds to them.
  [[nodiscard]] const chats_model& chats() const noexcept { return chats_; }
  [[nodiscard]] chats_model& chats() noexcept { return chats_; }
  // What an account knows of a person's encryption identity, where it said.
  // How many times what is known of anyone's identity changed: what shows
  // it is made again when it moves.
  [[nodiscard]] std::uint64_t trust_revision() const { return trust_revision_; }
  // This session of an account: its ID and key, where encryption runs.
  // A person's sessions, where their account listed them.
  [[nodiscard]] const std::vector<change::device_view>* devices_of(const account_id& by, const std::string& user) const {
    const auto found = devices_.find({by, user});
    return found == devices_.end() ? nullptr : &found->second;
  }
  [[nodiscard]] std::optional<trust_t> trust_of(const account_id& by, const std::string& user) const {
    const auto found = trust_.find({by, user});
    return found == trust_.end() ? std::nullopt : std::optional<trust_t>(found->second);
  }

  void add(account_id id, std::string display_name = {}) {
    this->in_account(id, [&](account& made) { made.display_name = std::move(display_name); });
  }

  // A chat, where the model has it: as const as the model it is asked of.
  [[nodiscard]] const conversation* chat_in(const conversation_id& id) const {
    const account* found = this->accounts().find(id.account);
    return found == nullptr ? nullptr : found->conversations.find(id.id);
  }
  const conversation* find(const conversation_id& id) const { return this->chat_in(id); }
  // For a protocol's own change (changed_in): a chat it changes, where the
  // model has it, and a message in it -- a room's part, a poll's counts in a
  // message's part. Only while the change is applied: the edit it is made in.
  [[nodiscard]] conversation* chat_to_change(const conversation_id& id) {
    if (editing_ == nullptr)
      return nullptr;
    account* found = editing_->accounts.find(id.account);
    return found == nullptr ? nullptr : found->conversations.find(id.id);
  }
  [[nodiscard]] message* message_to_change(const conversation_id& in, std::string_view id) {
    conversation* chat = this->chat_to_change(in);
    if (chat == nullptr)
      return nullptr;
    const auto found = std::ranges::find(chat->timeline, id, &message::id);
    return found == chat->timeline.end() ? nullptr : &*found;
  }

  void apply(const change_t& what) {
    spl::visit([this](const auto& one) { on(one); }, what);
  }

  // Typing is transient. Schedule its expiry without polling or drawing frames.
  [[nodiscard]] double typing_wake_in() const {
    if (typing_until_.empty())
      return std::numeric_limits<double>::infinity();
    const auto next = std::ranges::min_element(typing_until_, {}, [](const auto& one) { return one.second; });
    return std::max(0.0, std::chrono::duration<double, std::milli>(next->second - std::chrono::steady_clock::now()).count());
  }
  bool expire_typing(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) {
    bool changed = false;
    for (auto it = typing_until_.begin(); it != typing_until_.end();) {
      if (it->second > now) {
        ++it;
        continue;
      }
      if (const auto* chat = find(it->first); chat && !chat->typing.empty()) {
        this->in_chat(it->first, [](conversation& chat) { chat.typing.clear(); });
        changed = true;
      }
      it = typing_until_.erase(it);
    }
    return changed;
  }

  // The user has read a chat up to a message: kept, sent or not.
  void read_up_to(const conversation_id& id, std::string message) {
    if (this->chat_in(id) != nullptr)
      this->in_chat(id, [&](conversation& where) {
        if (where.read_up_to) {
          const auto before = std::ranges::find(where.timeline, *where.read_up_to, &mux::message::id);
          const auto after = std::ranges::find(where.timeline, message, &mux::message::id);
          if (before != where.timeline.end() && after != where.timeline.end() && after < before)
            return;
        }
        where.read_up_to = std::move(message);
      });
  }
  // A chat read now: the last to lose its history.
  void touch(const conversation_id& id) {
    if (this->chat_in(id) != nullptr)
      this->in_chat(id, [&](conversation& where) { where.read_at = ++read_tick_; });
  }
  // The messages held, least recently used first out: past `budget` in
  // all, the chats read longest ago keep only their last message -- what
  // the list shows of them -- and page back from their newest when read
  // again, what comes twice being one by its id. `keep`, the chat being
  // read, keeps all of its.
  void trim(std::size_t budget, const std::optional<conversation_id>& keep) {
    std::size_t held = 0;
    std::vector<const conversation*> order;
    for (const auto& [id, one] : this->accounts())
      for (const auto& [key, chat] : one.conversations) {
        held += chat.timeline.size();
        if (chat.timeline.size() > 1 && (!keep || chat.id != *keep))
          order.push_back(&chat);
      }
    if (held <= budget)
      return;
    std::ranges::sort(order, {}, &conversation::read_at);
    std::vector<conversation_id> trimmed;
    for (const conversation* chat : order) {
      if (held <= budget)
        break;
      held -= chat->timeline.size() - 1;
      trimmed.push_back(chat->id);
    }
    // Each trimmed in its place: its newest kept -- the newest there is,
    // where it is a window away from it, which is let go with the rest: it
    // is live again.
    chats_.beginBatch();
    for (const conversation_id& id : trimmed)
      this->in_chat(id, [](conversation& chat) {
        const message last = newest(chat) ? *newest(chat) : chat.timeline.back();
        chat.timeline.assign(1, last);
        chat.detached = false;
        chat.future_from.reset();
        chat.history_from = std::string();  // from the newest
      });
    chats_.endBatch();
  }
  // Changes applied as one batch: what binds to the model told once, after.
  template <std::ranges::input_range Changes>
  void apply_all(const Changes& all) {
    chats_.beginBatch();
    for (const change_t& one : all)
      this->apply(one);
    chats_.endBatch();
  }

 private:
  // An account changed in its place, made where it is not there yet.
  template <class F>
  void in_account(const account_id& id, F&& change) {
    if (!this->accounts().contains(id))
      (void)chats_.apply(skiff::model::put<account>(id, account{.id = id}));
    (void)chats_.apply(skiff::model::edit(skiff::model::placeOf<account, chats_root>(id), [&](account& kept) { change(kept); }));
  }
  // A chat changed in its place, made where it is not there yet: only it is
  // told of the change, nothing else in the model.
  template <class F>
  void in_chat(const conversation_id& id, F&& change) {
    if (this->chat_in(id) == nullptr)
      this->in_account(id.account, [&](account& kept) {
        if (!kept.conversations.contains(id.id))
          kept.conversations.put(id.id, conversation{.id = id});
      });
    (void)chats_.apply(skiff::model::edit(skiff::model::placeOf<conversation, chats_root>(id.account, id.id),
                                          [&](conversation& kept) { change(kept); }));
  }
  // The copy of the newest kept while the chat is a window elsewhere, where it
  // is the message changed: changed as the timeline's is, the list saying
  // what the chat says.
  template <class Change>
  static void in_latest(conversation& where, std::string_view id, Change change) {
    if (where.latest && where.latest->id == id)
      change(*where.latest);
  }
  static message* message_in(conversation& where, std::string_view id) {
    for (auto it = where.timeline.rbegin(); it != where.timeline.rend(); ++it)
      if (it->id == id)
        return &*it;
    // Or an answer in a thread.
    for (auto& [root, answers] : where.threads)
      for (message& one : answers)
        if (one.id == id)
          return &one;
    return nullptr;
  }
  // A thread's root, where it is held: in the timeline, or fetched aside.
  static message* root_of(conversation& where, const std::string& root) {
    for (auto it = where.timeline.rbegin(); it != where.timeline.rend(); ++it)
      if (it->id == root)
        return &*it;
    if (const auto found = where.quoted.find(root); found != where.quoted.end())
      return &found->second;
    return nullptr;
  }
  // An answer come to a thread: the root's summary brought up to date -- one
  // more where it came live, else as many as are held at least.
  static void note_answer(conversation& where, const std::string& root, const message& answer, bool counted) {
    message* kept = root_of(where, root);
    if (!kept)
      return;
    thread_summary summary = kept->threaded.value_or(thread_summary{});
    const auto& answers = where.threads[root];
    summary.count = counted ? summary.count + 1 : std::max<std::int64_t>(summary.count, static_cast<std::int64_t>(answers.size()));
    if (answer.at >= summary.last_at) {
      summary.last_id = answer.id;
      summary.last_sender = answer.sender;
      summary.last_text = answer.body.plain;
      summary.last_at = answer.at;
    }
    summary.participated = summary.participated || answer.outgoing;
    kept->threaded = summary;
  }

  void on(const change::connection_changed& one) {
    this->in_account(one.account, [&](account& kept) { kept.state = one.state; });
  }
  void on(const change::account_removed& one) {
    std::erase_if(typing_until_, [&](const auto& pending) { return pending.first.account == one.account; });
    (void)chats_.apply(skiff::model::take<account>(one.account));
  }
  void on(const change::conversation_updated& one) {
    this->in_chat(one.id, [&](conversation& kept) {
    kept.kind = one.kind;
    kept.name = one.name;
    kept.avatar = one.avatar;
    kept.topic = one.topic;
    kept.encrypted = one.encrypted;
    kept.encrypted_since = one.encrypted_since;
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
    kept.theirs = one.theirs;
    kept.other_aliases = one.other_aliases;
    kept.invite = one.invite;
  });
  }
  void on(const change::conversation_removed& one) {
    typing_until_.erase(one.id);
    if (this->accounts().contains(one.id.account))
      this->in_account(one.id.account, [&](account& kept) { (void)kept.conversations.take(one.id.id); });
  }
  void on(const change::presence_changed& one) {
    this->in_account(one.account, [&](account& kept) { kept.presences[one.contact] = one.now; });
  }
  // Reactions come before the message they are on is here (it is further
  // back, or in a thread not loaded): kept until it comes, then put on it.
  // Dropped, they were lost for good -- the event seen in the history, the
  // message without it.
  std::map<std::pair<conversation_id, std::string>, std::vector<change::reaction_changed>> waiting_reactions_;
  static constexpr std::size_t kReactionsWaiting = 2000;
  void on(const change::message_added& one) {
    this->add_message(one);
    if (one.message.id.empty())
      return;
    if (const auto waiting = waiting_reactions_.find({one.message.in, one.message.id}); waiting != waiting_reactions_.end()) {
      const auto held = std::move(waiting->second);
      waiting_reactions_.erase(waiting);
      for (const change::reaction_changed& reaction : held)
        this->on(reaction);
    }
  }
  void add_message(const change::message_added& one) {
    // A deleted one, read back from the disk: only where deleted messages
    // are kept, and something of it is left to show.
    if (one.message.redacted &&
        (!show_deleted || one.message.outgoing ||
         (one.message.body.plain.empty() && !one.message.body.html && !one.message.attachment)))
      return;
    this->in_chat(one.message.in, [&](conversation& where) {
      auto merged = one;
      if (const auto quoted = where.quoted.find(one.message.id); quoted != where.quoted.end()) {
        for (const auto& [key, who] : quoted->second.reactions)
          merged.message.reactions[key].insert(who.begin(), who.end());
        for (const auto& event : quoted->second.reaction_events)
          if (!std::ranges::contains(merged.message.reaction_events, event)) merged.message.reaction_events.push_back(event);
      }
      this->add_to(where, merged);
    });
  }
  static bool pending(const message& one) {
    return spl::visit(spl::overloaded{[](const delivery::sending&) { return true; }, [](const auto&) { return false; }},
                      one.delivery);
  }
  static bool message_before(const message& a, const message& b) {
    if (pending(a) != pending(b))
      return !pending(a);
    return a.at < b.at;
  }
  void add_to(conversation& where, const change::message_added& one) {
    if (spl::visit(spl::overloaded{[](placement::at_end) { return true; }, [](const auto&) { return false; }}, one.where)) {
      std::erase(where.typing, one.message.sender);
      if (where.typing.empty())
        typing_until_.erase(one.message.in);
    }
    if (message* kept = one.message.id.empty() ? nullptr : message_in(where, one.message.id)) {
      // Come again -- a page, the disk, a window around it: its reactions
      // kept, which what came may not carry.
      const bool moved = kept->at != one.message.at || pending(*kept) != pending(one.message);
      auto reactions = std::move(kept->reactions);
      auto reaction_events = std::move(kept->reaction_events);
      *kept = one.message;
      for (auto& [key, who] : reactions)
        kept->reactions[key].insert(who.begin(), who.end());
      for (auto& each : reaction_events)
        if (!std::ranges::contains(kept->reaction_events, each))
          kept->reaction_events.push_back(std::move(each));
      // A thread's answers are in time's order, and one's own came first with
      // this machine's time: its copy from the server, stamped by the server,
      // may belong elsewhere -- put there. With a clock a minute behind, one's
      // reply stood above the answer it replied to.
      if (one.message.thread)
        if (const auto found = where.threads.find(*one.message.thread); found != where.threads.end())
          std::ranges::stable_sort(found->second, {}, &message::at);
      if (!one.message.thread && moved)
        std::ranges::stable_sort(where.timeline, message_before);
      return;
    }
    // An answer in a thread: with the thread's, in time's order, not in the
    // timeline; its root's summary brought up to date.
    if (one.message.thread) {
      auto& answers = where.threads[*one.message.thread];
      answers.insert(std::ranges::upper_bound(answers, one.message.at, {}, &message::at), one.message);
      const bool live = spl::visit(spl::overloaded{[](placement::at_end) { return true; }, [](const auto&) { return false; }},
                                      one.where);
      note_answer(where, *one.message.thread, one.message, live);
      return;
    }
    // In the timeline now: what was fetched for a quote is not needed.
    where.quoted.erase(one.message.id);
    // Both history and live events use the same chronological insertion.
    // A linear search also tolerates clock-skewed entries from an older client;
    // upper_bound requires a sorted range. Unconfirmed local echoes stay last.
    const auto in_time = [&] {
      auto& timeline = where.timeline;
      if (timeline.empty() || !message_before(one.message, timeline.back())) {
        timeline.push_back(one.message);
        return;
      }
      const auto at = std::ranges::find_if(timeline, [&](const message& said) {
        return message_before(one.message, said);
      });
      timeline.insert(at, one.message);
    };
    spl::visit(spl::overloaded{[&](placement::at_end) {
                            if (!where.latest || one.message.at >= where.latest->at)
                              where.latest = one.message;
                            if (where.detached)
                              return;
                            // Being sent from here: said now, after all that is
                            // shown, whatever its time -- this machine's clock,
                            // which a few seconds behind the server's put a
                            // reply above the message it answered.
                            const bool sending = spl::visit(
                                spl::overloaded{[](const delivery::sending&) { return true; },
                                                   [](const auto&) { return false; }},
                                one.message.delivery);
                            if (sending) {
                              where.timeline.push_back(one.message);
                              return;
                            }
                            in_time();
                          },
                          [&](placement::at_start) { in_time(); },
                          [&](placement::in_window) { in_time(); },
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
    this->in_chat(one.in, [&](conversation& where) {
    where.timeline.clear();
    where.history_from = one.history_from;
    where.future_from = one.future_from;
    where.detached = one.future_from.has_value();
  });
  }
  void on(const change::window_extended& one) {
    this->in_chat(one.in, [&](conversation& where) {
    where.future_from = one.future_from;
    where.detached = one.future_from.has_value();
  });
  }
  void on(const change::message_encrypted& one) {
    this->in_chat(one.in, [&](conversation& where) {
    const auto mark = [&](message& kept) {
      kept.encrypted = true;
      kept.unverified = !one.verified;
      kept.unauthenticated = one.imported;
    };
    if (message* kept = message_in(where, one.id))
      mark(*kept);
    in_latest(where, one.id, mark);
  });
  }
  void on(const change::message_edited& one) {
    this->in_chat(one.in, [&](conversation& where) {
    if (message* kept = message_in(where, one.id)) {
      if ((one.by && *one.by != kept->sender) || (one.plain && kept->encrypted))
        return;
      // Keep each distinct edit event, including an edit with unchanged
      // text. A local optimistic edit has no event ID until its echo arrives.
      const bool changed = kept->body != one.now;
      const bool another_event = !one.event.empty() && one.event != kept->latest_edit_event &&
          (kept->versions.empty() || !kept->latest_edit_event.empty());
      if (changed || another_event) {
        const std::string previous = kept->versions.empty() ? kept->id : kept->latest_edit_event;
        kept->versions.push_back({kept->body, one.at != std::chrono::sys_time<std::chrono::milliseconds>{}
            ? one.at : std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()), previous});
      }
      kept->body = one.now;
      kept->edited = true;
      if (changed || !one.event.empty()) kept->latest_edit_event = one.event;
    }
    in_latest(where, one.id, [&](message& kept) {
      if ((one.by && *one.by != kept.sender) || (one.plain && kept.encrypted))
        return;
      kept.body = one.now;
      kept.edited = true;
      if (!one.event.empty()) kept.latest_edit_event = one.event;
    });
    // And the copy fetched aside for the replies quoting it: what they quote
    // is what it says now.
    if (const auto aside = where.quoted.find(one.id);
        aside != where.quoted.end() && (!one.by || *one.by == aside->second.sender) &&
        !(one.plain && aside->second.encrypted)) {
      aside->second.body = one.now;
      aside->second.edited = true;
      if (!one.event.empty()) aside->second.latest_edit_event = one.event;
    }
  });
  }
  // A message deleted: where deleted messages are kept, it stays where it
  // was with all it said and its time, marked -- someone else's; the user's
  // own, and every one where they are not kept, is taken out.
  void on(const change::message_redacted& one) {
    this->in_chat(one.in, [&](conversation& where) {
    // A mark on the event taken back -- a reaction to the user's own,
    // removed where the message it was on is not here to match it: gone too.
    for (auto* marks : {&where.unread_reactions, &where.unread_mentions})
      std::erase_if(*marks, [&](const unread_mark& mark) { return mark.event == one.id; });
    // Kept where deleted messages are -- but never the user's own: what they
    // deleted themselves goes, whatever is kept of others'.
    const message* found = message_in(where, one.id);
    if (show_deleted && !(found && found->outgoing)) {
      if (message* kept = message_in(where, one.id))
        kept->redacted = true;
      in_latest(where, one.id, [](message& kept) { kept.redacted = true; });
      return;
    }
    std::erase_if(where.timeline, [&](const message& each) { return each.id == one.id; });
    for (auto& [root, answers] : where.threads)
      std::erase_if(answers, [&](const message& each) { return each.id == one.id; });
    if (where.latest && where.latest->id == one.id)
      where.latest.reset();
  });
  }
  void on(const change::threads_listed& one) {
    this->in_chat(one.in, [&](conversation& where) { where.thread_roots = one.roots; });
  }
  // A call's: the program's calls part's (mux.app.calls), not the model's.
  void on(const change::call_signalled&) {}
  void on(const change::call_servers&) {}
  void on(const change::message_acknowledged& one) {
    this->in_chat(one.in, [&](conversation& where) {
    in_latest(where, one.local_id, [&](message& kept) {
      kept.id = one.id;
      kept.delivery = delivery::sent{};
    });
    if (message_in(where, one.id)) {
      std::erase_if(where.timeline, [&](const message& kept) { return kept.id == one.local_id; });
      for (auto& [root, answers] : where.threads)
        std::erase_if(answers, [&](const message& kept) { return kept.id == one.local_id; });
      return;
    }
    if (message* kept = message_in(where, one.local_id)) {
      kept->id = one.id;
      kept->delivery = delivery::sent{};
      if (kept->thread) {
        if (const auto found = where.threads.find(*kept->thread); found != where.threads.end())
          std::ranges::stable_sort(found->second, {}, &message::at);
      } else {
        std::ranges::stable_sort(where.timeline, message_before);
      }
    }
  });
  }
  void on(const change::delivery_changed& one) {
    this->in_chat(one.in, [&](conversation& where) {
    if (message* kept = message_in(where, one.id)) {
      const bool was_pending = pending(*kept);
      kept->delivery = one.now;
      if (!kept->thread && was_pending != pending(*kept))
        std::ranges::stable_sort(where.timeline, message_before);
    }
    in_latest(where, one.id, [&](message& kept) { kept.delivery = one.now; });
  });
  }
  void on(const change::message_discarded& one) {
    this->in_chat(one.in, [&](conversation& where) {
    std::erase_if(where.timeline, [&](const message& each) { return each.id == one.id; });
    if (where.latest && where.latest->id == one.id)
      where.latest.reset();
  });
  }
  static message* reaction_target(conversation& where, std::string_view id) {
    if (auto* event = message_in(where, id)) return event;
    if (const auto quoted = where.quoted.find(std::string(id)); quoted != where.quoted.end()) return &quoted->second;
    return nullptr;
  }
  void on(const change::reaction_changed& one) {
    this->in_chat(one.in, [&](conversation& where) {
    if (reaction_target(where, one.id) == nullptr) {
      auto& waiting = waiting_reactions_[{one.in, one.id}];
      if (one.added) {
        if (waiting.size() < kReactionsWaiting)
          waiting.push_back(one);
      } else {
        std::erase_if(waiting, [&](const change::reaction_changed& each) { return each.key == one.key && each.who == one.who; });
      }
      return;
    }
    if (message* kept = reaction_target(where, one.id)) {
      auto& who = kept->reactions[one.key];
      // Taken back: its mark too -- a reaction changed for another was
      // counted twice by the heart, the one taken back still in it.
      if (!one.added)
        for (const message::reaction_event& each : kept->reaction_events)
          if (each.key == one.key && each.who == one.who)
            std::erase_if(where.unread_reactions, [&](const unread_mark& mark) { return mark.event == each.event; });
      std::erase_if(kept->reaction_events, [&](const message::reaction_event& each) {
        return each.key == one.key && each.who == one.who;
      });
      if (one.added) {
        who.insert(one.who);
        if (!one.event.empty())
          kept->reaction_events.push_back({one.event, one.key, one.who, one.at, one.shortcode});
        // Another's reaction to the user's own, as it happened: for them.
        if (one.live && kept->outgoing && one.who != one.in.account.address && !one.event.empty())
          keep_mark(where, where.unread_reactions, {one.event, one.id, one.at});
      } else {
        who.erase(one.who);
        if (who.empty())
          kept->reactions.erase(one.key);
      }
    }
  });
  }
  // Marks kept to a number, the oldest going first: a flood of them cannot
  // grow a chat without end.
  static constexpr std::size_t kMarksKept = 500;
  static void keep_mark(const conversation& where, std::vector<unread_mark>& marks, unread_mark one) {
    if (std::ranges::contains(marks, one.event, &unread_mark::event) || std::ranges::contains(where.seen_marks, one.event))
      return;
    marks.push_back(std::move(one));
    if (marks.size() > kMarksKept)
      marks.erase(marks.begin());
  }
  void on(const change::mentioned& one) {
    this->in_chat(one.in, [&](conversation& where) {
    keep_mark(where, where.unread_mentions, {one.event, one.event, one.at});
  });
  }
  static void mark_seen(conversation& where, const std::string& event) {
    if (std::ranges::contains(where.seen_marks, event))
      return;
    where.seen_marks.push_back(event);
    if (where.seen_marks.size() > kMarksKept)
      where.seen_marks.erase(where.seen_marks.begin());
  }
  void on(const change::marks_seen& one) {
    this->in_chat(one.in, [&](conversation& where) {
    for (const std::string& event : one.events)
      mark_seen(where, event);
    // Seen is seen, whichever came first: a mention caught up from the
    // server before the kept ones were read back is let go here, not left
    // unread because the seen list was not in yet.
    const auto seen = [&](const unread_mark& mark) { return std::ranges::contains(where.seen_marks, mark.event); };
    std::erase_if(where.unread_mentions, seen);
    std::erase_if(where.unread_reactions, seen);
  });
  }
  void on(const change::directory_listed&) {}  // the window's: the Explore dialog
  void on(const change::people_found&) {}  // the window's: the Start chat dialog
  void on(const change::profile_found&) {}  // the window's: a pill's picture
  void on(const change::refused&) {}  // the window's: a notice
  void on(const change::trust_changed& one) {
    trust_.insert_or_assign({one.by, one.user}, one.now);
    ++trust_revision_;
  }
  std::uint64_t trust_revision_ = 0;
  void on(const change::devices_listed& one) {
    devices_.insert_or_assign({one.by, one.user}, one.devices);
    ++trust_revision_;
  }
  std::map<std::pair<account_id, std::string>, std::vector<change::device_view>> devices_;
  std::map<std::pair<account_id, std::string>, trust_t> trust_;
  void on(const change::notice&) {}
  void on(const change::reacted_to_mine& one) {
    this->in_chat(one.in, [&](conversation& where) {
    keep_mark(where, where.unread_reactions, {one.event, one.target, one.at});
  });
  }
  void on(const change::marks_shown& one) {
    this->in_chat(one.in, [&](conversation& where) {
    const auto shown = [&](const unread_mark& mark) { return std::ranges::contains(one.shown, mark.target); };
    for (const auto* marks : {&where.unread_mentions, &where.unread_reactions})
      for (const unread_mark& mark : *marks)
        if (shown(mark))
          mark_seen(where, mark.event);
    std::erase_if(where.unread_mentions, shown);
    std::erase_if(where.unread_reactions, shown);
  });
  }
  void on(const change::mark_taken& one) {
    this->in_chat(one.in, [&](conversation& where) {
    auto& marks = spl::visit(spl::overloaded{[&](mark_kind::mention) -> std::vector<unread_mark>& { return where.unread_mentions; },
                                        [&](mark_kind::reaction) -> std::vector<unread_mark>& { return where.unread_reactions; }},
                             one.kind);
    if (one.event) {
      mark_seen(where, *one.event);
      std::erase_if(marks, [&](const unread_mark& mark) { return mark.event == *one.event; });
    } else if (!marks.empty()) {
      mark_seen(where, marks.front().event);
      marks.erase(marks.begin());
    }
  });
  }
  std::map<conversation_id, std::chrono::steady_clock::time_point> typing_until_;
  void on(const change::typing_changed& one) {
    if (one.who.empty())
      typing_until_.erase(one.in);
    else
      typing_until_.insert_or_assign(one.in, std::chrono::steady_clock::now() + std::chrono::seconds(30));
    if (const auto* chat = find(one.in); chat && chat->typing == one.who)
      return;
    this->in_chat(one.in, [&](conversation& where) { where.typing = one.who; });
  }
  void on(const change::history_position& one) {
    this->in_chat(one.in, [&](conversation& where) { where.history_from = one.from; });
  }
  void on(const change::event_missing& one) {
    this->in_chat(one.in, [&](conversation& where) {
    for (auto* marks : {&where.unread_mentions, &where.unread_reactions})
      for (const unread_mark& gone : *marks)
        if (gone.target == one.id && !std::ranges::contains(where.seen_marks, gone.event))
          where.seen_marks.push_back(gone.event);
    for (auto* marks : {&where.unread_mentions, &where.unread_reactions})
      std::erase_if(*marks, [&](const unread_mark& mark) { return mark.target == one.id; });
  });
  }
  void on(const change::members_changed& one) {
    this->in_chat(one.in, [&](conversation& where) {
    // Who was in it and is no longer -- left, kicked, banned -- types no
    // more: their typing, said before they went, stayed under the name.
    std::erase_if(where.typing, [&](const std::string& who) {
      return std::ranges::contains(where.members, who, &member::id) && !std::ranges::contains(one.members, who, &member::id);
    });
    where.members = one.members;
    where.knocking = one.knocking;
    ++where.members_revision;
  });
  }
  void on(const change::avatar_loaded&) {}  // the window's to show, not the model's
  void on(const change::notifications_changed&) {}
  void on(const change::protocol_state_changed&) {}  // the window's: what it offers
  // A protocol's own change: what its changed_in(model, change) makes of the
  // model, found by ADL -- nothing by default (the window's, then).
  template <protocol_change Change>
  void on(const Change& one) {
    using model_defaults::changed_in;
    // In an edit of the whole: what the protocol changes is changed there.
    (void)chats_.apply(skiff::model::edit(skiff::model::placeOf<chats_root, chats_root>(), [&](chats_root& root) {
      editing_ = &root;
      changed_in(*this, one);
      editing_ = nullptr;
    }));
  }
  void on(const change::media_progress&) {}  // the window's too
  void on(const change::room_created&) {}    // the program's: it shows it
  void on(const change::room_previewed&) {}  // the window's: the room's card
  void on(const change::preview_loaded& one) { previews.insert_or_assign(one.url, one.preview); }
  void on(const change::receipts_changed& one) {
    this->in_chat(one.in, [&](conversation& kept) {
    const auto place = [&](const std::string& id) { return std::ranges::find(kept.timeline, id, &message::id); };
    for (const auto& [user, event] : one.read_by) {
      const auto previous = kept.read_by.find(user);
      bool accept = previous == kept.read_by.end() || previous->second == event;
      if (!accept) {
        const auto before = place(previous->second), after = place(event);
        if (before != kept.timeline.end() && after != kept.timeline.end())
          accept = after > before;
        else {
          const auto old_time = kept.receipt_times.find(user);
          const auto new_time = one.read_at.find(user);
          if (old_time != kept.receipt_times.end() && new_time != one.read_at.end())
            accept = new_time->second > old_time->second;
        }
        // Without either positions or timestamps, an unknown event cannot
        // prove that an existing receipt moved forward.
      }
      if (!accept) continue;
      kept.read_by.insert_or_assign(user, event);
      if (const auto time = one.read_at.find(user); time != one.read_at.end()) {
        const auto old = kept.receipt_times.find(user);
        if (old == kept.receipt_times.end() || time->second > old->second)
          kept.receipt_times.insert_or_assign(user, time->second);
      }
    }
    // The user's own, from another device (or this one's, echoed): where it
    // is past the position kept here -- or there is none -- the position
    // moves on to it. Never back: a receipt older than what was read here.
    if (const auto mine = one.read_by.find(one.in.account.address); mine != one.read_by.end()) {
      const auto theirs = place(mine->second);
      const bool past = !kept.read_up_to ||
                        (theirs != kept.timeline.end() &&
                         (place(*kept.read_up_to) != kept.timeline.end() && theirs > place(*kept.read_up_to)));
      if (past)
        kept.read_up_to = mine->second;
    }
  });
  }

  chats_model chats_;
  // The root being changed by a protocol's own change, while it is.
  chats_root* editing_ = nullptr;
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
