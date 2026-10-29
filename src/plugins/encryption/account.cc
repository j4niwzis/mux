// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.encryption.matrix:account -- A Matrix account with end-to-end
// encryption: the client's account (mux.proto.matrix.client), and over it
// this device's Olm machine, its keys, emoji verification, cross-signing,
// the key backup and secret storage. Made in its place where this plugin is
// on (contract::matrix_account, account_of): the account calls what is here
// through whole(), a static_cast -- no virtual call -- and what it hides of
// the account's (the account's own say "built without encryption").
export module mux.plugin.encryption.matrix:account;

import mux.vault;
import std;
import loom.crypto;
import splice;
import knot;
import loom.cs.sliding_sync;
import loom.cs.versions;
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
import splice.bytes;
import loom.cs.account_data;
import loom.media;
import chevron.escape;
import mux.logic.forwarding;
import loom.cs.key_backup;
import loom.cs.keys;
import loom.cs.event_context;
import loom.cs.users;
import loom.cs.relations;
import loom.cs.threads_list;
import loom.cs.room_summary;
import loom.cs.list_public_rooms;
import loom.cs.space_hierarchy;
import loom.cs.cross_signing;
import loom.cs.room_state;
import loom.cs.content_repo;
import loom.cs.authed_content_repo;
import loom.cs.create_room;
import loom.cs.kicking;
import loom.cs.banning;
import loom.cs.inviting;
import loom.cs.profile;
import loom.cs.presence;
import loom.cs.device_management;
import loom.cs.pusher;
import mux.logic.markdown;
import loom.names;
import loom.cs.voip;
import mux.logic.reactions;
import loom.cs.to_device;
import loom.cs.room_upgrades;
import loom.cs.knocking;
import mux.composition;
import mux.proto.matrix.requests;
import mux.proto.matrix.client;
import mux.plugin.encryption.types;
import mux.plugin.encryption.changes;

export namespace mux::proto::matrix::client {

template <class Sink>
class encrypted_account : public account<Sink> {
  using base = account<Sink>;
  friend class account<Sink>;

 public:
  using account<Sink>::account;

  // What the account asks of encryption (its hooks, called through whole()).
  void start_crypto();
  void device_extensions(loom::cs::sliding_sync::body_t::extensions_t& asked) {
    if (!crypto_)
      return;
    using extension = loom::cs::sliding_sync::body_t::extension_t;
    asked.to_device = extension{.enabled = true, .since = crypto_->to_device_since()};
    asked.e2ee = extension{.enabled = true};
  }
  void crypto_answer(const loom::cs::sliding_sync::response_t& got);
  [[nodiscard]] bool encrypting() const { return crypto_.has_value(); }
  [[nodiscard]] bool store_says_encrypted(std::string_view room) { return crypto_ && crypto_->was_encrypted(room); }
  void store_encrypted(std::string_view room) {
    if (!crypto_)
      return;
    try {
      crypto_->remember_encrypted(room);
    } catch (const std::exception& failed) {
      log(this->id_, "encryption stopped: {}", failed.what());
      crypto_.reset();
    }
  }
  [[nodiscard]] proto::matrix::security_state security_now() {
    return proto::matrix::security_state{this->id_, crypto_ && crypto_->cross_signing_keys().has_value(),
                                          crypto_ && crypto_->backup().has_value()};
  }
  void mentions_from(const conversation_id& in, const loom::ev::net_mux_mentions_read_content_t& content);
  void encrypted(const conversation_id& in, const loom::ev::timeline_event& one,
                 std::chrono::sys_time<std::chrono::milliseconds> at, placement_t where);
  void verification_request_in_room(const conversation_id& in, const loom::ev::timeline_event& one,
                                    const crypto::room_request_fields& fields);
  [[nodiscard]] bool verification_in_room(const conversation_id& in, const loom::ev::timeline_event& one, placement_t where);

  // What this account knows of a person's encryption identity, said
  // (trust_changed): when asked, after a verification, when it changes.
  void tell_trust(std::string user);
  // A person's sessions, from the server's list of their keys: each
  // cross-signed by them (their master key the one pinned), or verified by
  // emoji here.
  void tell_devices(std::string user);
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
  // A session a message waits for, looked for in the key backup -- once:
  // the backup was read only as its key was given, and what other sessions
  // backed up after it was never taken (a tester's: a whole chat waiting).
  void key_from_backup(std::string room, std::string session);
  std::set<std::string, std::less<>> backup_asked_;
  // Not in the backup either: asked of this user's other sessions
  // (m.room_key_request), and their answer (m.forwarded_room_key) taken
  // only for a session asked for, from a session of theirs this one trusts.
  void ask_own_sessions(const std::string& room, const std::string& session);
  void forwarded_in(const crypto::forwarded_key_got& got);
  std::set<std::string, std::less<>> keys_requested_;
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

 private:
  using base::api_;
  using base::connected;
  using base::done;
  using base::edit;
  using base::encrypted_by;
  using base::event;
  using base::how_;
  using base::id;
  using base::id_;
  using base::kept_file;
  using base::keys_of;
  using base::late_news_;
  using base::members;
  using base::perform;
  using base::remember_encrypted;
  using base::request;
  using base::run;
  using base::say;
  using base::send;
  using base::send_room_event;
  using base::sign_out_sessions;
  using base::sink_;
  using base::spawn_guarded;
  using base::start;
  using base::state_;
  using base::synced;
  using base::telling_history_;
  using base::told;
  using base::transaction;
  using base::uia_;
  using base::uia_answer;
  using base::upload;
  using base::whole;
  using typename base::uia_password;
  using typename base::uia_browser;
  using typename base::uia_none;
  using typename base::uia_cross_signing;
  using typename base::pending_uia;
  using typename base::late_news_while;

  // End-to-end encryption: this device's machine, made once the device is
  // known; its keys uploaded, and what comes for it read.
  std::optional<olm_machine> crypto_;
  // Users whose master key changed from the one pinned, told once a run.
  std::set<std::string, std::less<>> identity_changed_;
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
  // The key in Secret Storage, under the storage key there: taken where it
  // is, put there where it is not.
  template <class Key>
  void mentions_key_in_storage(const Key& storage, const std::string& storage_id);
  // Into an encrypted room: the room's key given to its readers' devices
  // that lack it, then the event sealed with it and sent as m.room.encrypted
  // under the same transaction. Anything that fails on the way refuses it,
  // with what failed: it never goes in the clear.
  template <class Ask>
  auto send_encrypted(Ask ask, std::optional<knot::raw> relates_to) {
    const conversation_id in{id_, ask.room_id};
    constexpr std::string_view kNot = "Not sent: it could not be encrypted -- ";
    if (!crypto_)
      throw plaintext_refused(in, ask.txn_id, std::string(kNot) + "encryption is not running for this account.");
    try {
      // Shared, then sealed with that very session: where another send
      // rotated it in between, shared and sealed again.
      std::optional<crypto::megolm_content> sealed;
      for (int attempt = 0; attempt < 3 && !sealed; ++attempt) {
        const auto session = this->share_room_key(ask.room_id);
        if (!session)
          throw plaintext_refused(in, ask.txn_id, std::string(kNot) + "the room's key could not be given to its members.");
        sealed = crypto_->encrypt(ask.room_id, *session, ask.event_type, ask.body, relates_to);
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
    const auto step = this->stamped(state, content);
    const knot::raw body{knot::to_json_string(step)};
    if (!state.room) {
      (void)this->send_plain(std::move(type), state.their_user, state.their_device.empty() ? std::string("*") : state.their_device,
                             body);
      return;
    }
    try {
      (void)this->send_room_event(
          loom::cs::send_message{.room_id = *state.room, .event_type = std::move(type), .txn_id = this->transaction(), .body = body},
          relates_to_of(step));
    } catch (const plaintext_refused& refused) {
      log(id_, "verification step not sent: {}", refused.what());
    }
  }
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
  void crypto_answer_now(const loom::cs::sliding_sync::response_t& got);
  // A room key offered: taken only from a device the sender's device list
  // has, signed by itself; marked unverified unless cross-signed.
  void vet_room_key(const crypto::room_key_offer& offer);
  // The secrets (cross-signing keys, the backup's key) shared with this
  // user's own other devices: asked of one verified here by emoji, taken
  // from it, and given to one verified so that asks (m.secret.request/send).
  void request_secrets(const std::string& device);
  void secret_in(const crypto::secret_got& got);
  void secret_request_in(const std::string& sender, const loom::ev::m_secret_request_content_t& content);
  std::map<std::string, crypto::secret_name_t> secrets_asked_;  // request id -> which secret
  crypto::secrets_gathered secrets_got_;

};

}  // namespace mux::proto::matrix::client

export namespace mux::plugins::encryption {
// The account this plugin makes (contract::matrix_account).
template <class Sink>
std::type_identity<proto::matrix::client::encrypted_account<Sink>> matrix_account_made(contract::matrix_account, type_tag<feature>,
                                                                                       type_tag<Sink>) {
  return {};
}
}  // namespace mux::plugins::encryption
