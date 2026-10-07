// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix.client:account -- A Matrix account: the class, its state and what it does, declared.
export module mux.proto.matrix.client:account;

import std;
import splice;
import knot;
import loom.api;
import loom.ev;
import loom.state;
import loom.cs.joining;
import loom.cs.cross_signing;
import loom.cs.keys;
import loom.cs.to_device;
import loom.cs.sliding_sync;
import loom.crypto;
import mux.vault;
import loom.cs.leaving;
import loom.cs.login;
import loom.cs.message_pagination;
import loom.cs.receipts;
import loom.cs.redaction;
import loom.cs.room_send;
import loom.cs.voip;
import loom.cs.rooms;
import loom.cs.room_upgrades;
import loom.cs.knocking;
import loom.cs.create_room;
import loom.cs.account_data;
import loom.cs.sync;
import loom.cs.typing;
import loom.cs.wellknown;
import mux.config;
import mux.core;
import mux.proto.matrix.requests;
import mux.http;
import mux.net;
export import :names;

export namespace mux::proto::matrix::client {

// What mux reads of the secrets asked of it, by their names: read once,
// where the name comes in, into a kind -- its own, or any other, passed over.
namespace own_secret {
struct mentions_key {};  // net.mux.mentions_key: what read mentions are sealed under
struct other {};
}  // namespace own_secret
using own_secret_t = spl::variant<own_secret::mentions_key, own_secret::other>;
[[nodiscard]] inline own_secret_t own_secret_of(std::string_view name) {
  struct named {
    std::string_view name;
    own_secret_t kind;
  };
  static const std::array<named, 1> kKinds{{{"net.mux.mentions_key", own_secret::mentions_key{}}}};
  const auto found = std::ranges::find(kKinds, name, &named::name);
  return found != kKinds.end() ? found->kind : own_secret_t{own_secret::other{}};
}

struct failure {
  std::optional<loom::error> server;
  std::string network;
  std::string said() const {
    if (server)
      return server->errcode + (server->message.empty() ? "" : ": " + server->message);
    return network;
  }
};

// What is refused in the clear, thrown: the room, the message's id here (its
// transaction) and why. Out of the account template, its destructor its key
// function: as a member of the template, its vtable and type information
// were made nowhere, and the link failed.
struct plaintext_refused : std::runtime_error {
  conversation_id in;
  std::string local;
  plaintext_refused(conversation_id room, std::string txn,
                    std::string why = "Not sent: this room is end-to-end encrypted, and nothing goes into it in the clear.")
      : std::runtime_error(std::move(why)), in(std::move(room)), local(std::move(txn)) {}
  plaintext_refused(const plaintext_refused&) = default;
  plaintext_refused& operator=(const plaintext_refused&) = default;
  ~plaintext_refused() override;
};
plaintext_refused::~plaintext_refused() = default;

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

  // UnifiedPush's endpoint, given to the server as an http pusher -- through
  // the endpoint's own Matrix gateway where its push server has one -- once
  // logged in; none: forgotten here (the distributor drops it, and the
  // gateway refuses the server from then on).
  void set_pusher(std::optional<std::string> endpoint);
  // A push come: the long poll cut short, and the sync gone again at once.
  void sync_now();

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
  // What Matrix changes of a room beyond that: rules, levels, encryption, version.
  void change_room(std::string room, proto::matrix::room_change_t change);
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
  void search_directory(std::string server, std::string query, std::optional<std::string> since = std::nullopt);
  // The room being read, for the sliding sync to follow apart; none, none.
  void follow(std::optional<std::string> room) { followed_room_ = std::move(room); }
  // What a space holds -- its rooms and spaces, joined or not -- as its
  // server's hierarchy lists them, a level down.
  void explore_space(std::string room);
  // A room made, as Element's Create room makes one: named, about
  // something, public -- with an address -- or private.
  void create_room(std::string name, std::string topic, bool open, std::string alias, bool federate = true,
                   bool encrypted = false, mux::room_place place = {});
  // A picture's caption edited: the picture kept, its caption the text.
  void edit_caption(std::string room, std::string event, std::string caption, mux::attachment picture);
  // Packs of custom emoji and stickers (MSC2545): a room's, or one's own
  // where none is named, listed to edit; one saved, or a room's taken away;
  // an image uploaded for one.
  void list_packs(std::optional<std::string> room);
  void save_pack(emote_pack pack);
  void delete_pack(emote_pack pack);
  void upload_pack_picture(pack_picture picture, std::string bytes);
  // Threads (m.thread): a room's roots listed; a thread's answers loaded,
  // each as a message in it; an answer sent in one.
  void list_threads(std::string room);
  void load_thread(std::string room, std::string root);
  void send_in_thread(std::string room, std::string body, std::string root, std::string latest, std::optional<std::string> reply_to);
  // The user directory searched: people_found for what was asked.
  void search_people(std::string term);
  // What this account knows of a person's encryption identity, said
  // (trust_changed): when asked, after a verification, when it changes.
  void tell_trust(std::string user);
  // A person's sessions, from the server's list of their keys: each
  // cross-signed by them (their master key the one pinned), or verified by
  // emoji here.
  void tell_devices(std::string user);
  void set_only_verified(bool on);
  // Read mentions shared with this account's other sessions -- room account
  // data, net.mux.mentions_read -- sealed or in the clear, as set.
  void set_mentions_sharing(bool shared, bool sealed);
  // A room's mentions seen here: shared where some are news to the server.
  void share_marks_seen(std::string room, std::vector<std::string> seen);
  // A person's reset identity accepted: Element's "Withdraw verification".
  void accept_identity(std::string user);
  // Element's Secure Backup: the key backup made anew (the old one deleted,
  // a new recovery key shown), or deleted.
  void reset_backup();
  void delete_backup();
  // Element's "Sign out unverified sessions": one's own not cross-signed,
  // nor verified by emoji here.
  void sign_out_unverified(std::string password);
  // Element's toasts, as notices once a run: this session not verified --
  // others cannot be sure what it sends is the user's; and other sessions of
  // the user's that are not ("New login. Was this you?").
  void check_own_sessions();
  // Each session of the user's own, as the server lists them now: its ID,
  // its name, and whether it is trusted -- cross-signed, or verified here.
  // None where they could not be asked.
  struct own_session {
    std::string id;
    std::string name;
    bool trusted = false;
  };
  [[nodiscard]] std::optional<std::vector<own_session>> own_sessions_now();
  // Events not read for want of their session's key, by that session --
  // each as it came, and where it went -- to be read again once the key is
  // here; and why a sender withheld a session's key (m.room_key.withheld),
  // in Element's words -- the messages said so, those that come later too.
  struct undecrypted_event {
    conversation_id in;
    loom::ev::timeline_event event;
    placement_t where;
  };
  std::map<std::string, std::vector<undecrypted_event>> undecrypted_;
  std::map<std::string, std::string> withheld_;
  void withheld_in(const loom::ev::m_room_key_withheld_content_t& content);
  // The events waiting for a session's key read again, now that it is here
  // -- or, a key file or the backup having brought many, every one waiting.
  // Each in place of what was said of it: in its window (a live one is not
  // live again), or aside where it was fetched aside.
  void decrypt_waiting(const std::string& session);
  void decrypt_all_waiting();
  // A room's gap since the last sync, from where the sync left it back to
  // the event it had last: its events put in the timeline by their time --
  // between what was there and what the sync brought, where nothing pages
  // back to them -- and read for mentions of the user and reactions to
  // theirs. At most ten pages; past that, the gap is the history's.
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
  // Emoji verification, as the user asks it: begun with a user (one of
  // their devices, or all), accepted, the emoji said to match or not, or
  // cancelled.
  void verify_start(std::string user, std::optional<std::string> device);
  void verify_accept(std::string txn);
  void verify_confirm(std::string txn, bool match);
  void verify_cancel(std::string txn);
  // Cross-signing set up for this user: three keys made, uploaded (the
  // password asked by the server's user-interactive auth), kept here
  // sealed, and this device signed with the self-signing key.
  void setup_cross_signing(std::string password, bool reset = false);
  // The cross-signing keys put in secret storage under a new recovery key,
  // which is said to the user to write down; and taken back with one.
  std::optional<std::string> store_secrets(const crypto::cross_signing_secrets& secrets,
                                           const std::optional<std::string>& backup_secret);
  // A key backup made on the server, its auth data signed by this device and
  // the master key: its private key, for secret storage.
  std::optional<std::string> make_backup(const crypto::cross_signing_secrets& secrets);
  // What follows cross-signing keys uploaded: kept, this device signed, the
  // backup and secret storage made, the recovery key said.
  void finish_cross_signing(const crypto::cross_signing_secrets& secrets, const std::string& master_pub,
                            const std::string& self_pub);
  // How a 401's interactive auth is answered: with the password, where a
  // flow of it is the password alone and there is one; else on a page in
  // the browser -- an OIDC server's page for a cross-signing reset where it
  // names one (MSC4312), else the spec's fallback page for the first stage
  // of the first flow not done; else not at all.
  struct uia_password {
    std::string given;
  };
  struct uia_browser {
    std::string url;
  };
  struct uia_none {};
  using uia_way_t = spl::variant<uia_password, uia_browser, uia_none>;
  uia_way_t uia_answer(const loom::error& said, const std::string& given);
  // What was asked while its auth is done in the browser: done again with
  // the session once Continue is pressed -- the same request, as the
  // server's session is for it.
  struct uia_sign_out {
    std::vector<std::string> devices;
  };
  struct uia_cross_signing {
    crypto::cross_signing_secrets secrets;
    loom::cs::upload_cross_signing_keys::body_t body;
    std::string master_pub;
    std::string self_pub;
  };
  struct pending_uia {
    std::string session;
    spl::variant<uia_sign_out, uia_cross_signing> what;
  };
  std::optional<pending_uia> uia_;
  // The room keys not in the backup yet, put in it: after each sync.
  void upload_backup();
  // The backup read with its private key, from secret storage: how many
  // room keys were taken.
  std::size_t restore_backup(const std::string& secret);
  void restore_cross_signing(std::string recovery);
  // After emoji verification, the other side signed where this device has
  // the keys for it: one's own device with the self-signing key, another
  // user's master key with the user-signing key.
  void cross_sign_device(const loom::cs::query_keys::response_t::device_information_t& info);
  void cross_sign_user(const std::string& user, const loom::cs::query_keys::response_t::cross_signing_key_t& master);
  // This account's room keys written to `path`, sealed under a passphrase,
  // as Element writes them; and read back from one.
  void export_room_keys(std::string path, std::string passphrase);
  void import_room_keys(std::string path, std::string passphrase);
  void rename_session(std::string device, std::string name);
  void sign_out_sessions(std::vector<std::string> devices, std::string password);
  // A step of interactive auth done in the browser: what it was for, done
  // again with its session; or let go.
  void continue_uia();
  void cancel_uia() { uia_.reset(); }
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
                 std::optional<std::string> reply_to = std::nullopt, std::optional<thread_place> thread = std::nullopt,
                 std::optional<video_look> video = std::nullopt);

  // A message of one's own edited (m.replace): the new text in its place.
  void edit(std::string room, std::string event, std::string text, std::vector<styled_run> styles = {});
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
            std::vector<mention> mentions = {}, std::vector<styled_run> styles = {});

  // A call's signalling sent to its room: an m.call.* event, version 1, from
  // this session's party -- encrypted where the room is, as a message is.
  void call(std::string room, std::string call_id, change::call_said_t what);
  // The servers a call goes through, as the server gives them
  // (/voip/turnServer): said as change::call_servers.
  void call_servers();

 private:
  void say(connection_t state);

  // What the server has of a user's keys: their devices, and their
  // cross-signing keys.
  [[nodiscard]] std::expected<loom::cs::query_keys::response, failure> keys_of(const std::string& user) {
    loom::cs::query_keys ask;
    ask.body.device_keys.emplace(user, std::vector<std::string>{});
    return this->perform(*api_, ask);
  }
  // A state event of a room set, its content given; and what was asked of a
  // room, logged where it failed.
  void set_room_state(const std::string& room, std::string type, const auto& content, std::string key = {});
  [[nodiscard]] std::string server_of(const std::string& room) const;
  void send_text(const conversation_id& in, const std::string& room, const std::string& txn, knot::raw body);
  void told_failing(const std::string& room, const char* what, const auto& done) {
    if (!done)
      log(id_, "could not {} in {}: {}", what, room, done.error().said());
  }
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
          return http::exchange(over, asked.method_name(), asked.target, asked.body, bearer, timeout, asked.content_type);
        else
          return over.request(asked.method_name(), asked.target, asked.body, bearer, timeout, asked.content_type);
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

  // End-to-end encryption: this device's machine, made once the device is
  // known; its keys uploaded, and what comes for it read.
  std::optional<olm_machine> crypto_;
  void start_crypto();
  // Rooms known to be encrypted: never sent to in the clear, whatever their
  // state says later -- a server that drops or hides m.room.encryption does
  // not turn a room back to plain text (the spec's "no downgrade"). Kept in
  // a file of their own beside the sync, apart from the E2EE store: one that
  // does not start does not make them forgotten (review 4, M1).
  std::set<std::string, std::less<>> encrypted_rooms_;
  [[nodiscard]] bool encrypted_room(std::string_view room);
  void remember_encrypted(std::string_view room);
  // When each room was encrypted, at the latest: the earliest time ever
  // seen for it -- its m.room.encryption's, or an encrypted event's -- kept
  // beside the rooms. A server that sends that state again later, or with
  // a later time, does not move it: messages put in the clear before it
  // would no longer say "not encrypted" (review 6).
  std::map<std::string, std::int64_t, std::less<>> encrypted_since_;
  using since_t = std::chrono::sys_time<std::chrono::milliseconds>;
  [[nodiscard]] std::optional<since_t> encrypted_by(std::string_view room, std::optional<since_t> seen);
  void save_encrypted();
  // A message, added: one that came in the clear, live, into a room known
  // to be encrypted, says so whatever its time says (review 6).
  void added(message made, placement_t where, bool sealed) {
    const bool live = spl::visit(spl::overloaded{[](placement::at_end) { return true; }, [](const auto&) { return false; }}, where);
    made.came_plain = !sealed && live && encrypted_rooms_.contains(made.in.id);
    sink_(change::message_added{std::move(made), where});
  }
  // Users whose master key changed from the one pinned, told once a run.
  std::set<std::string, std::less<>> identity_changed_;
  [[nodiscard]] std::filesystem::path encrypted_rooms_file() const;
  void load_encrypted();
  // The mentions read, as the server last said of each room: nothing is
  // sent that it has already, so two sessions never answer each other.
  std::map<std::string, std::set<std::string>, std::less<>> mentions_remote_;
  // The key read mentions are sealed under: kept on this device, sealed, and
  // in Secret Storage, from which a session restored with the recovery key
  // takes it.
  std::optional<std::vector<std::uint8_t>> mentions_key_;
  bool mentions_key_read_ = false;
  bool mentions_key_missing_told_ = false;
  // The mentions key asked of another session of this account, by request.
  std::set<std::string, std::less<>> mentions_key_asked_;
  [[nodiscard]] std::filesystem::path mentions_key_file() const;
  [[nodiscard]] const std::optional<std::vector<std::uint8_t>>& mentions_key();
  void keep_mentions_key(std::vector<std::uint8_t> key);
  // A room's net.mux.mentions_read, as knot read it: its mentions seen.
  void mentions_from(const conversation_id& in, const loom::ev::net_mux_mentions_read_content_t& content);
  // The key in Secret Storage, under the storage key there: taken where it
  // is, put there where it is not.
  template <class Key>
  void mentions_key_in_storage(const Key& storage, const std::string& storage_id);
  // What was to go in the clear into an encrypted room (#12169, review 4,
  // H2): thrown where it would leave, before any of it does -- the text, the
  // file's bytes -- and caught where its fiber began: the message marked not
  // sent, the user told in a dialog. This client does not send encrypted
  // yet (part 2 of the E2EE PR).
  using plaintext_refused = client::plaintext_refused;
  void refuse_plaintext(std::string_view room, std::string_view local) {
    if (!this->encrypted_room(room))
      return;
    log(id_, "not sent: {} is encrypted, and files are not sent encrypted yet", room);
    throw plaintext_refused(conversation_id{id_, std::string(room)}, std::string(local));
  }
  // A room event sent: encrypted where the room is -- or, where it cannot
  // be, not sent at all.
  template <class Ask>
  auto send_room_event(Ask ask) {
    if (this->encrypted_room(ask.room_id))
      return this->send_encrypted(std::move(ask));
    return perform(*api_, ask);
  }
  // Into an encrypted room: the room's key given to its readers' devices
  // that lack it, then the event sealed with it and sent as m.room.encrypted
  // under the same transaction. Anything that fails on the way refuses it,
  // with what failed: it never goes in the clear.
  template <class Ask>
  auto send_encrypted(Ask ask) {
    const conversation_id in{id_, ask.room_id};
    constexpr std::string_view kNot = "Not sent: it could not be encrypted -- ";
    if (!crypto_)
      throw plaintext_refused(in, ask.txn_id, std::string(kNot) + "encryption is not running for this account.");
    try {
      // Shared, then sealed with that very session: where another send
      // rotated it in between, shared and sealed again.
      const auto relation = knot::try_read<crypto::relation_part>(ask.body.text);
      std::optional<crypto::megolm_content> sealed;
      for (int attempt = 0; attempt < 3 && !sealed; ++attempt) {
        const auto session = this->share_room_key(ask.room_id);
        if (!session)
          throw plaintext_refused(in, ask.txn_id, std::string(kNot) + "the room's key could not be given to its members.");
        sealed = crypto_->encrypt(ask.room_id, *session, ask.event_type, ask.body, relation ? relation->relates_to : std::nullopt);
      }
      if (!sealed)
        throw plaintext_refused(in, ask.txn_id, std::string(kNot) + "the room's session failed.");
      return perform(*api_, loom::cs::send_message{.room_id = ask.room_id,
                                                   .event_type = "m.room.encrypted",
                                                   .txn_id = ask.txn_id,
                                                   .body = knot::raw{knot::to_json_string(*sealed)}});
    } catch (const plaintext_refused&) {
      throw;
    } catch (const std::exception& failed) {
      log(id_, "encryption stopped: {}", failed.what());
      crypto_.reset();
      throw plaintext_refused(in, ask.txn_id, std::string(kNot) + failed.what());
    }
  }
  // The room's key given to every device of its members that should read it
  // and has not got it: the session's ID, or none where they could not be
  // known, or the key could not be sent.
  std::optional<std::string> share_room_key(const std::string& room);
  // The encrypted files events named, by their mxc:// URI: what opens each
  // once it is downloaded.
  std::map<std::string, crypto::encrypted_file, std::less<>> encrypted_media_;
  // A fiber of this account. What it throws past its own handling -- the
  // unforeseen, a bug -- is caught here: let out, it left the loop and
  // stopped every account's network without a word. It is logged
  // and said; the account shows as failed, to be connected again from what
  // it kept, so that nothing half done of it is relied on.
  template <class Body>
  void spawn_guarded(Body body) {
    loop_->spawn([this, body = std::move(body)] mutable {
      try {
        body();
      } catch (const std::exception& failed) {
        log(id_, "stopped by an error: {}", failed.what());
        this->say(connection::failed{std::format("Stopped by an error: {}", failed.what())});
      }
    });
  }
  // A fiber that sends: a refusal of it caught here, for every sender alike.
  template <class Body>
  void spawn_sending(Body body) {
    this->spawn_guarded([this, body = std::move(body)] mutable {
      try {
        body();
      } catch (const plaintext_refused& refused) {
        sink_(change::delivery_changed{refused.in, refused.local, delivery::failed{}});
        sink_(change::refused{id_, refused.what()});
      }
    });
  }
  void upload_keys(std::int64_t on_server);
  // When one-time keys were last uploaded: once a minute at most. A server
  // that says, sync after sync, that it holds none would otherwise have a
  // batch made and signed every time.
  std::optional<std::chrono::steady_clock::time_point> keys_uploaded_at_;
  // A fallback key uploaded where the server has none unused: once an hour
  // at most, whatever the server says.
  void upload_fallback_key();
  // Emoji verification (SAS), over to-device messages as Element does it:
  // asked of a user's devices, or of one; answered; compared; ended.
  std::map<std::string, crypto::sas_state, std::less<>> verifications_;
  bool send_plain(std::string type, const std::string& user, const std::string& device, knot::raw content);
  // A step's content as its transport has it -- a transaction ID to a
  // device, a reference to the request in a room -- and sent so.
  template <class Content>
  Content stamped(const crypto::sas_state& state, Content content) {
    if (state.room) {
      content.transaction_id.reset();
      content.m_relates_to = loom::ev::def::verification_relates_to_t{
          .rel_type = loom::ev::def::verification_relates_to_t::rel_type_values::m_reference{}, .event_id = state.txn};
    } else {
      content.transaction_id = state.txn;
    }
    return content;
  }
  template <class Content>
  void send_step(const crypto::sas_state& state, std::string type, const Content& content) {
    const knot::raw body{knot::to_json_string(this->stamped(state, content))};
    if (!state.room) {
      (void)this->send_plain(std::move(type), state.their_user, state.their_device.empty() ? std::string("*") : state.their_device,
                             body);
      return;
    }
    try {
      (void)this->send_room_event(
          loom::cs::send_message{.room_id = *state.room, .event_type = std::move(type), .txn_id = this->transaction(), .body = body});
    } catch (const plaintext_refused& refused) {
      log(id_, "verification step not sent: {}", refused.what());
    }
  }
  // A verification in a room: its request (a message to this user), and its
  // steps, by their events -- live ones only, and never this side's own.
  void verification_request_in_room(const conversation_id& in, const loom::ev::timeline_event& one,
                                     const crypto::room_request_fields& fields);
  [[nodiscard]] bool verification_in_room(const conversation_id& in, const loom::ev::timeline_event& one, const knot::raw& raw,
                                          placement_t where);
  // While a decrypted event is read: the event its cleartext relation
  // refers to, where its content does not say.
  std::optional<std::string> outer_reference_;
  // Whose device each curve25519 key is, as /keys/query said: for messages
  // read with an imported session, whose sender is taken only where it
  // holds the key. Asked once per user and key.
  std::map<std::pair<std::string, std::string>, bool> owns_key_;
  [[nodiscard]] bool owns_key(const std::string& user, const std::string& curve25519);
  void verification_said(const crypto::sas_state& state, verification_step_t step);
  void cancel_verification(const std::string& txn, std::string code, std::string reason);
  void sas_start(crypto::sas_state& state);
  void sas_show(crypto::sas_state& state);
  void sas_send_mac(crypto::sas_state& state);
  void sas_check_mac(crypto::sas_state& state);
  void verification_in(const std::string& sender, const loom::ev::m_key_verification_request_content_t& content);
  void verification_in(const std::string& sender, const loom::ev::m_key_verification_ready_content_t& content);
  void verification_in(const std::string& sender, const loom::ev::m_key_verification_start_content_t& content);
  void verification_in(const std::string& sender, const loom::ev::m_key_verification_accept_content_t& content);
  void verification_in(const std::string& sender, const loom::ev::m_key_verification_key_content_t& content);
  void verification_in(const std::string& sender, const loom::ev::m_key_verification_mac_content_t& content);
  void verification_in(const std::string& sender, const loom::ev::m_key_verification_cancel_content_t& content);
  // A device whose Olm messages no session here opens, given a new session
  // (an m.dummy over one made from its one-time key): once an hour at most
  // for each, whatever comes.
  void mend_session(const std::string& user, const std::string& curve25519);
  std::map<std::string, std::chrono::steady_clock::time_point> mended_at_;
  std::optional<std::chrono::steady_clock::time_point> fallback_uploaded_at_;
  void crypto_answer(const loom::cs::sliding_sync::response_t& got);
  void crypto_answer_now(const loom::cs::sliding_sync::response_t& got);
  // A room key offered: taken only from a device the sender's device list
  // has, signed by itself; marked unverified unless cross-signed.
  void vet_room_key(const crypto::room_key_offer& offer);

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


  bool direct(const std::string& room) const;

  void conversation(const conversation_id& in, const loom::client::joined_room& kept);

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
  void knock(std::string room, std::vector<std::string> via, std::string reason);
  // A room's members, all of them, from the server: kept, and said.
  void fetch_members(std::string room);
  std::map<std::string, std::map<std::string, mux::member>> full_members_;

 private:

  // An event of a room's timeline, as changes: at the end, or before the
  // rest where it is history paged back to.
  // `sealed`: it came end-to-end encrypted, and was read here -- an edit
  // so is one an encrypted message may take (review 4, H3).
  // What a sync tells is history, not news, while this is set: the first
  // sync of a login, and the sync kept on disk told again at a start. Its
  // events are shown, but mark no mention or reaction as unseen and ring
  // no call -- a login showed every old mention as new.
  bool telling_history_ = false;
  // Whether an event placed so is news: come at the end, not from history.
  [[nodiscard]] bool live(const placement_t& where) const {
    return !telling_history_ &&
           spl::visit(spl::overloaded{[](placement::at_end) { return true; }, [](const auto&) { return false; }}, where);
  }
  void event(const conversation_id& in, const loom::ev::timeline_event& one, placement_t where = placement::at_end{},
             bool sealed = false);

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
  // A call's signalling come, to the program -- as it happens only: what
  // history pages back is the timeline's, a call long over.
  void call_signal(const conversation_id& in, const loom::ev::timeline_event& one,
                   std::chrono::sys_time<std::chrono::milliseconds> at, placement_t where, std::string call,
                   std::string party, change::call_said_t said);
  [[nodiscard]] std::string name_in(const std::string& room, const std::string& user) const;

  // A message's body: its plain text, and its HTML where it says it has
  // org.matrix.custom.html.
  static body body_of(std::string plain, const std::optional<std::string>& format,
                      const std::optional<std::string>& formatted_body);

  struct reaction {
    std::string target, key, who;
  };

  // The sync's long poll, to be cut short -- the machine woken from sleep, a
  // push come: whether the sync is still there, and whether it was cut.
  struct waking {
    bool alive = true;
    bool woke = false;
  };
  std::shared_ptr<waking> waking_;
  http::connection* long_poll_ = nullptr;
  void cut_long_poll();
  // UnifiedPush's endpoint, and the one the server was given.
  std::optional<std::string> push_endpoint_;
  std::optional<std::string> pushed_to_;
  void register_pusher();

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
  // The secrets (cross-signing keys, the backup's key) shared with this
  // user's own other devices: asked of one verified here by emoji, taken
  // from it, and given to one verified so that asks (m.secret.request/send).
  void request_secrets(const std::string& device);
  void secret_in(const crypto::secret_got& got);
  void secret_request_in(const std::string& sender, const loom::ev::m_secret_request_content_t& content);
  std::map<std::string, crypto::secret_name_t> secrets_asked_;  // request id -> which secret
  crypto::secrets_gathered secrets_got_;
  [[nodiscard]] std::string transaction() { return std::format("mux-{}-{}", run_, ++transactions_); }
  std::uint64_t transactions_ = 0;
  // When this run began, in the clock's ticks: what makes its ids its own.
  std::int64_t run_ = std::chrono::system_clock::now().time_since_epoch().count();
  bool stopping_ = false;
};

}  // namespace mux::proto::matrix::client
