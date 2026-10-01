// SPDX-License-Identifier: AGPL-3.0-only
// mux.matrix:account -- A Matrix account: the class, its state and what it does, declared.
export module mux.matrix:account;

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
import loom.cs.create_room;
import loom.cs.account_data;
import loom.cs.sync;
import loom.cs.typing;
import loom.cs.wellknown;
import mux.config;
import mux.core;
import mux.http;
import mux.net;
export import :names;

export namespace mux::matrix {

struct failure {
  std::optional<loom::error> server;
  std::string network;
  std::string said() const {
    if (server)
      return server->errcode + (server->message.empty() ? "" : ": " + server->message);
    return network;
  }
};

template <class Sink>
class account {
 public:
  account(net::loop& loop, net::tls& tls, settings how, Sink sink)
      : loop_(&loop), tls_(&tls), how_(std::move(how)), sink_(std::move(sink)) {
    id_ = account_id{protocol::matrix{}, how_.user_id};
    const auto colon = how_.user_id.find(':');
    localpart_ = how_.user_id.substr(how_.user_id.starts_with('@') ? 1 : 0,
                                     colon == std::string::npos ? std::string::npos : colon - 1);
    server_name_ = colon == std::string::npos ? std::string() : how_.user_id.substr(colon + 1);
  }
  account(const account&) = delete;
  account& operator=(const account&) = delete;

  const account_id& id() const noexcept;

  void start();

  void stop();

  // A read receipt for an event of a room: the people in it see how far this
  // account has read.
  void mark_read(std::string room, std::string event);

  // Older messages of a room, paged back from `from`: before the rest, and
  // where to page back from next -- nothing where the beginning is reached.
  void load_older(std::string room, std::string from);
  // A window of a room's history around a message (/context), in place of
  // its timeline; and a window paged forward, to the newest.
  void load_context(std::string room, std::string target);
  // A message a reply quotes, fetched on its own, beside the timeline.
  void fetch_quoted(std::string room, std::string target);
  // Something done to a room by one allowed to: its state set, or someone
  // let in or sent out.
  void manage(std::string room, room_action_t action);
  // A message sent on to another room: its content as it is -- a picture's
  // or a file's URL with it, so nothing is uploaded again -- less what tied
  // it to its own room (the reply it was).
  void forward(std::string from, std::string event, std::string to);
  // A room made: a direct chat with someone -- written into m.direct, as
  // other clients know it is one -- or a group, private, with a name.
  void create_direct(std::string user);
  void create_group(std::string name);
  // A link's preview, as the homeserver makes it.
  void fetch_preview(std::string url);
  // A server's public directory searched -- the account's own where none is
  // named -- for what matches, all of it where nothing is asked.
  void search_directory(std::string server, std::string query);
  // The room being read, for the sliding sync to follow apart; none, none.
  void follow(std::optional<std::string> room) { followed_room_ = std::move(room); }
  // What a space holds -- its rooms and spaces, joined or not -- as its
  // server's hierarchy lists them, a level down.
  void explore_space(std::string room);
  // A room made, as Element's Create room makes one: named, about
  // something, public -- with an address -- or private.
  void create_room(std::string name, std::string topic, bool open, std::string alias, bool federate = true);
  // A picture's caption edited: the picture kept, its caption the text.
  void edit_caption(std::string room, std::string event, std::string caption, mux::attachment picture);
  // Packs of custom emoji and stickers (MSC2545): a room's, or one's own
  // where none is named, listed to edit; one saved, or a room's taken away;
  // an image uploaded for one.
  void list_packs(std::optional<std::string> room);
  void save_pack(emote_pack pack);
  void delete_pack(std::string room, std::string state_key);
  void upload_pack_picture(pack_picture picture, std::string bytes);
  // Threads (m.thread): a room's roots listed; a thread's answers loaded,
  // each as a message in it; an answer sent in one.
  void list_threads(std::string room);
  void load_thread(std::string room, std::string root);
  void send_in_thread(std::string room, std::string body, std::string root, std::string latest, std::optional<std::string> reply_to);
  // The user directory searched: people_found for what was asked.
  void search_people(std::string term);
  // A room's gap since the last run, from where the sync left it back to
  // the event it had last: read for mentions of the user and reactions to
  // theirs, and nothing else -- no message kept, nothing fetched.
  void catch_up(std::string room, std::string from, std::string until);
  // A room not joined, as its server tells of it, before it is joined.
  void preview_room(std::string room, std::vector<std::string> via);
  // A person's profile -- their name and picture -- from their server:
  // profile_found, for one met outside the rooms, a forward's sender.
  void fetch_profile(std::string user);
  // Its sessions (devices), as Element's Sessions: listed; one renamed;
  // some signed out -- the server asking for the password, it is given
  // (the one typed, else the one logged in with).
  void list_sessions();
  void rename_session(std::string device, std::string name);
  void sign_out_sessions(std::vector<std::string> devices, std::string password);
  // The developer tools, as Element's: an event as the server has it; the
  // room's state, every event of it; and an event of any type sent.
  void view_source(std::string room, std::string event);
  void list_state(std::string room);
  void send_custom(std::string room, std::string type, std::optional<std::string> state_key, std::string json);
  // A sticker sent: an m.sticker, its picture's URL and its name -- an
  // answer to a message, where one is being answered.
  void send_sticker(std::string room, mux::emote sticker, std::optional<std::string> reply_to = std::nullopt);
  void load_newer(std::string room, std::string from);

  // An avatar's picture: the server's thumbnail of an mxc:// URI, at the size
  // it is drawn at twice over, handed on for `key`. The authenticated media
  // API first (v1.11), the older one where the server has no such thing.
  void fetch_avatar(std::string source, std::string of);
  // What an mxc:// URI keeps: its thumbnail at `size` (cropped to a square,
  // or scaled to fit), or, where `size` is 0, the whole of it -- handed on
  // for `key`. The authenticated media API first, the older one where the
  // server has no such thing.
  void fetch_media(std::string source, media_use_t use, int size, bool crop = false);
  // A download of the whole of `source` stopped, where it is going on.
  void cancel_media(std::string source) { cancelled_.insert(std::move(source)); }
  // Downloads to stop, as their progress is next said. On the loop's thread.
  std::set<std::string> cancelled_;

  // A file sent: shown at once under `local` (its picture, where it is one,
  // already known to the window), uploaded to the media repository, and
  // sent as m.image or m.file -- its caption the body, its name apart
  // (Matrix 1.10) -- then known by the event the server gives it.
  void send_file(std::string room, std::string local, std::string bytes, std::string name, std::string mimetype,
                 bool image, int width, int height, std::string caption,
                 std::optional<std::string> reply_to = std::nullopt);

  // A message of one's own edited (m.replace): the new text in its place.
  void edit(std::string room, std::string event, std::string text);
  // A message removed (redacted).
  void remove(std::string room, std::string event);

  // A reaction to a message put, or taken back: m.reaction with its key,
  // or the redaction of the account's own.
  void react(std::string room, std::string target, std::string key, bool on);
  // A message pinned in its room, or unpinned: the room's list as the last
  // sync had it, with it put in or taken out, set as the room's state.
  void pin(std::string room, std::string target, bool on);

  // The account leaves a room; the next sync says it has, and the room goes.
  void leave(std::string room);

  // A text message sent to a room, from a fiber of its own. It is in the
  // conversation at once, under its transaction id; the server's answer
  // gives it its event id, and the echo in the next sync is the same message.
  void send(std::string room, std::string body, std::optional<std::string> reply_to = std::nullopt,
            std::vector<mention> mentions = {});

 private:
  void say(connection_t state);

  // A request made and its answer read into its type.
  template <class Endpoint>
  std::expected<typename Endpoint::response, failure> perform(auto& over, const Endpoint& endpoint,
                                                              std::chrono::seconds timeout = std::chrono::seconds(60)) {
    const loom::request asked = endpoint.to_send();
    try {
      const std::optional<std::string_view> bearer =
          asked.authenticated && token_ ? std::optional<std::string_view>(*token_) : std::nullopt;
      // Outside a release build, the exchange is one function of mux.http's,
      // not the request templates inlined into every endpoint's perform.
      const auto got = [&] {
        if constexpr (net::kErasedHandlers)
          return http::exchange(over, asked.method_name(), asked.target, asked.body, bearer, timeout);
        else
          return over.request(asked.method_name(), asked.target, asked.body, bearer, timeout);
      }();
      auto read = loom::read<Endpoint>(got.status, got.body);
      if (!read) {
        loom::error said = std::move(read).error();
        if (!said.retry_after_ms)
          said.retry_after_ms = got.retry_after_ms;
        return std::unexpected(failure{std::move(said), {}});
      }
      return std::move(*read);
    } catch (const net::failure& failed) {
      return std::unexpected(failure{std::nullopt, failed.what()});
    }
  }

  // The client-server API's base URL: the one given, or the one the server
  // name's .well-known says (the spec's server discovery), or the server
  // name itself.
  std::optional<http::url> homeserver();

  void run();

  // The file the sync is kept in, for this account.
  std::filesystem::path kept_file() const;
  // The sync as it stands, as a sync's answer: every joined room's state,
  // its timeline since its last gap and where that pages back from, its
  // summary, unread counts and data; the account's data; and the token to
  // go on from. Read back, it is applied as an answer is.
  void save_kept() const;
  void load_kept();

  // What the rooms a sync named look like now, and what their timelines
  // brought.
  void tell(const loom::cs::sync::response& got);

  // A room's picture: its own, or, for a chat with one other person, theirs.
  std::optional<std::string> avatar_of(const std::string& room, const loom::client::joined_room& kept) const;

  // A room's name as the spec says a client works it out: m.room.name, the
  // canonical alias, the heroes, the room's id.
  static std::string name_of(const std::string& room, const loom::client::joined_room& kept);

  bool direct(const std::string& room) const;

  void conversation(const conversation_id& in, const loom::client::joined_room& kept);

  // Whether a room is a space: its creation says so, by its type.
  static bool space(const loom::client::joined_room& kept);
  // The rooms a space holds: an m.space.child for each, whose content is
  // not empty -- an emptied one is a child taken out.
  static std::vector<std::string> children_of(const loom::client::joined_room& kept);
  // The room's pinned messages, as its state says.
  static std::vector<std::string> pinned_of(const loom::client::joined_room& kept);
  // A state event's content, as a tree; null where the room has none.
  // The custom emoji usable in a room: the user's own (im.ponies.user_emotes)
  // and the room's packs (im.ponies.room_emotes), a shortcode once.
  [[nodiscard]] std::vector<mux::emote> emotes_of(const loom::client::joined_room& kept, bool stickers = false) const;
  [[nodiscard]] std::vector<mux::emote> emotes_in(const std::string& room) const;

  // Who is in a room, as its state says: those joined, by their names there.
  // Who is in a room: all of it, where it was asked for (/joined_members),
  // with what the syncs say over it -- a sync's state has only those who
  // spoke lately, the members being loaded lazily -- those who left or were
  // banned taken out.
  void members(const conversation_id& in, const loom::client::joined_room& kept);
 public:
  // The user typing in a room, or not: for thirty seconds, or until said.
  void typing(std::string room, bool on);
  // A room joined, by its id or an alias, through the servers `via` names:
  // it comes with the next sync.
  void join(std::string room, std::vector<std::string> via);
  // A room's members, all of them, from the server: kept, and said.
  void fetch_members(std::string room);
  std::map<std::string, std::map<std::string, mux::member>> full_members_;

 private:

  // An event of a room's timeline, as changes: at the end, or before the
  // rest where it is history paged back to.
  void event(const conversation_id& in, const loom::ev::timeline_event& one, placement_t where = placement::at_end{});

  // An encrypted message: said to be there, not yet readable.
  void encrypted(const conversation_id& in, const loom::ev::timeline_event& one,
                 std::chrono::sys_time<std::chrono::milliseconds> at, placement_t where);
  // A redaction: a reaction taken back, or a message removed.
  void redaction(const conversation_id& in, const loom::ev::timeline_event& one,
                 std::chrono::sys_time<std::chrono::milliseconds> at, placement_t where);
  // What is done in a room rather than said, as a line of its own; and a
  // person's name there, as the room's state has it.
  void service(const conversation_id& in, const loom::ev::timeline_event& one,
               std::chrono::sys_time<std::chrono::milliseconds> at, placement_t where, std::string said,
               room_event_t kind = room_event::other{}, std::optional<std::string> html = std::nullopt);
  void done(const conversation_id& in, const loom::ev::timeline_event& one, event_type_t type,
            std::chrono::sys_time<std::chrono::milliseconds> at, placement_t where);
  [[nodiscard]] std::string name_in(const std::string& room, const std::string& user) const;

  // A message's body: its plain text, and its HTML where it says it has
  // org.matrix.custom.html.
  static body body_of(std::string plain, const std::optional<std::string>& format,
                      const std::optional<std::string>& formatted_body);

  struct reaction {
    std::string target, key, who;
  };

  net::loop* loop_;
  net::tls* tls_;
  settings how_;
  Sink sink_;
  account_id id_;
  std::string localpart_, server_name_;
  std::optional<std::string> token_;
  // The rooms whose place to page back from was told.
  std::set<std::string> paged_;
  http::pool* api_ = nullptr;
  loom::client::state state_;
  // Simplified sliding sync (MSC4186), where the server has it: whether it
  // is used, where its last answer left off, and how many rooms its list
  // holds.
  bool sliding_ = false;
  std::optional<std::string> sliding_pos_;
  std::int64_t sliding_range_ = 200;
  // The room being read: followed apart, with more of its newest -- set from
  // the program's thread through the loop, read by the sync.
  std::optional<std::string> followed_room_;
  std::map<std::string, reaction> reactions_;
  // A transaction id, unique across runs and not only within one. A server
  // remembers the ids it has seen per access token and answers a repeated
  // one with the event it made the first time, sending nothing: a counter
  // from zero at every start sent "mux1" again after a restart, the answer
  // was the old message's event, and the message written -- shown for a
  // moment as it went -- merged into that old one and was never sent.
  // The direct rooms, as m.direct said at the sync they were read at.
  mutable std::set<std::string, std::less<>> direct_rooms_;
  mutable std::optional<std::optional<std::string>> direct_rooms_since_;
  [[nodiscard]] std::string transaction() { return std::format("mux-{}-{}", run_, ++transactions_); }
  std::uint64_t transactions_ = 0;
  // When this run began, in the clock's ticks: what makes its ids its own.
  std::int64_t run_ = std::chrono::system_clock::now().time_since_epoch().count();
  bool stopping_ = false;
};

}  // namespace mux::matrix
