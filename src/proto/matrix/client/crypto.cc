// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix.crypto -- End-to-end encryption, as Matrix does it: Olm between
// devices, Megolm in rooms, by vodozemac (the kazv project's C++ bindings).
//
// One olm_machine per account and device. It holds this device's Olm account
// -- its identity keys and its one-time keys -- the Olm sessions with other
// devices, and the Megolm sessions rooms' messages are read with; all of it
// pickled into one file beside the account's other state, under a key of its
// own in a file of its own that only the user can read.
//
// What comes in is read once, where it comes in, into types: an Olm message's
// parts, an Olm payload's type and content (a room key, by loom's types), a
// Megolm message's plaintext -- a room event, read as loom reads any.
module;
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/core_names.h>
#include <openssl/params.h>
#include <rust/cxx.h>
#include <vodozemac/src/lib.rs.h>
export module mux.proto.matrix.crypto;

import std;
import splice;
import knot;
import loom.ev;
import loom.cs.keys;
import mux.vault;
import splice.bytes;

export import :wire;
export import :keys;
export import :sas;

export namespace mux::proto::matrix::client::crypto {

class olm_machine {
 public:
  // The machine of this device: read from its file, else made anew.
  static olm_machine open(mux::vault::vault& vault, std::filesystem::path store, std::string user_id, std::string device_id) {
    olm_machine made(vault, std::move(store), std::move(user_id), std::move(device_id));
    made.key_ = made.pickle_key();
    std::error_code there;
    const bool exists = std::filesystem::exists(made.store_, there);
    const auto opened = exists ? made.vault_->read_file(made.store_) : std::optional<std::string>(std::string());
    if (!opened)
      throw std::runtime_error("the encryption store cannot be opened (local data locked?): " + made.store_.string());
    const std::string& text = *opened;
    // A store there that cannot be read -- damaged, or its key lost -- is not
    // replaced by a new identity: the device's keys on the server would no
    // longer be its own, and every session would be lost without a word.
    if (!text.empty()) {
      auto read = knot::try_read<kept_file>(text);
      if (!read)
        throw std::runtime_error("the encryption store cannot be read: " + made.store_.string());
      made.kept_ = std::move(*read);
      try {
        made.account_.emplace(vodozemac::olm::account_from_pickle(made.kept_.account, made.key_));
      } catch (const rust::Error& failed) {
        throw std::runtime_error(std::string("the encryption store's account cannot be unpickled: ") + failed.what());
      }
    }
    if (!made.account_) {
      made.account_.emplace(vodozemac::olm::new_account());
      made.kept_ = kept_file{};
      made.save();
    }
    return made;
  }

  [[nodiscard]] std::string curve25519() const { return std::string((*account_)->curve25519_key()->to_base64()); }
  [[nodiscard]] std::string ed25519() const { return std::string((*account_)->ed25519_key()->to_base64()); }
  [[nodiscard]] bool device_keys_uploaded() const { return kept_.device_keys_uploaded; }
  [[nodiscard]] bool was_encrypted(std::string_view room) const { return kept_.encrypted_rooms && kept_.encrypted_rooms->contains(room); }
  // Kept where it is new; saved, and an error where that fails (as any save).
  void remember_encrypted(std::string_view room) {
    if (!kept_.encrypted_rooms)
      kept_.encrypted_rooms.emplace();
    if (kept_.encrypted_rooms->emplace(room, true).second)
      this->save();
  }
  [[nodiscard]] const std::optional<std::string>& to_device_since() const { return kept_.to_device_since; }
  // What was read since the last save -- the replay indices -- saved.
  void flush() {
    if (unsaved_)
      this->save();
  }
  void went_on_to(std::string since) {
    kept_.to_device_since = std::move(since);
    this->save();
  }

  // This device's keys, and their signature over their canonical JSON:
  // what keys/upload takes the first time.
  struct signed_keys {
    device_keys keys;
    std::string signature;  // under "ed25519:<device>"
  };
  [[nodiscard]] std::optional<signed_keys> signed_device_keys() const {
    device_keys keys{.algorithms = {"m.olm.v1.curve25519-aes-sha2", "m.megolm.v1.aes-sha2"},
                     .device_id = device_id_,
                     .keys = {{"curve25519:" + device_id_, this->curve25519()}, {"ed25519:" + device_id_, this->ed25519()}},
                     .user_id = user_id_};
    auto canonical = knot::to_canonical_json(keys);
    if (!canonical)
      return std::nullopt;
    std::string signature = this->sign(*canonical);
    return signed_keys{std::move(keys), std::move(signature)};
  }

  // One-time keys made up to `wanted` on the server, signed: what keys/upload
  // takes for them -- none where the server has enough.
  [[nodiscard]] std::map<std::string, knot::raw> one_time_keys(std::int64_t on_server) {
    std::map<std::string, knot::raw> out;
    const auto most = static_cast<std::int64_t>((*account_)->max_number_of_one_time_keys());
    const std::int64_t wanted = most / 2;
    if (on_server >= wanted)
      return out;
    (void)(*account_)->generate_one_time_keys(static_cast<std::size_t>(wanted - on_server));
    for (const auto& one : (*account_)->one_time_keys()) {
      const std::string key(one.key->to_base64());
      auto canonical = knot::to_canonical_json(bare_key{key});
      if (!canonical)
        continue;
      const signed_key signed_one{.key = key, .signatures = {{user_id_, {{"ed25519:" + device_id_, this->sign(*canonical)}}}}};
      out.emplace("signed_curve25519:" + std::string(one.key_id), knot::raw{knot::to_json_string(signed_one)});
    }
    return out;
  }
  // A fallback key made anew and signed: what others start Olm with once
  // the server has given out every one-time key (the one before stays,
  // until a message made with it has come).
  [[nodiscard]] std::map<std::string, knot::raw> fresh_fallback_key() {
    (void)(*account_)->generate_fallback_key();
    std::map<std::string, knot::raw> out;
    for (const auto& one : (*account_)->fallback_key()) {
      const std::string key(one.key->to_base64());
      const auto canonical = knot::to_canonical_json(fallback_bare{.key = key});
      if (!canonical)
        continue;
      const signed_fallback signed_one{.key = key, .signatures = {{user_id_, {{"ed25519:" + device_id_, this->sign(*canonical)}}}}};
      out.emplace("signed_curve25519:" + std::string(one.key_id), knot::raw{knot::to_json_string(signed_one)});
    }
    this->save();
    return out;
  }
  // The server has them: kept as published, and the device keys too.
  void published(bool with_device_keys) {
    (*account_)->mark_keys_as_published();
    if (with_device_keys)
      kept_.device_keys_uploaded = true;
    this->save();
  }

  // A to-device m.room.encrypted from a device: an Olm message for this one
  // read, and the room key it carries offered -- taken only once the
  // account has found the device it came from (accept_room_key). Nothing
  // where it is not for this device, cannot be read, or carries no key.
  [[nodiscard]] std::optional<to_device_said> to_device(const std::string& sender,
                                                        const loom::ev::m_room_encrypted_content_t& content) {
    const bool olm = splice::visit(
        splice::overloaded{[](loom::ev::m_room_encrypted_content_t::algorithm_values::m_olm_v1_curve25519_aes_sha2) { return true; },
                           [](const auto&) { return false; }},
        content.algorithm);
    if (!olm || !content.sender_key)
      return std::nullopt;
    auto ciphertexts = knot::try_read<std::map<std::string, olm_ciphertext>>(content.ciphertext.text);
    if (!ciphertexts)
      return std::nullopt;
    const auto mine = ciphertexts->find(this->curve25519());
    if (mine == ciphertexts->end())
      return std::nullopt;
    const auto bytes = from_base64(mine->second.body);
    if (!bytes)
      return std::nullopt;
    vodozemac::olm::OlmMessageParts parts{.message_type = static_cast<std::size_t>(mine->second.type), .ciphertext = {}};
    for (const std::uint8_t b : *bytes)
      parts.ciphertext.push_back(b);
    std::optional<std::string> plaintext;
    try {
      auto message = vodozemac::olm::olm_message_from_parts(parts);
      plaintext = this->olm_decrypt(*content.sender_key, *message, mine->second.type == 0);
    } catch (const rust::Error&) {
      plaintext.reset();
    }
    // A normal message no session here opens: theirs is one this device has
    // lost (a store restored, say). Noted, to be mended with a new session.
    if (!plaintext && mine->second.type != 0)
      wedged_.emplace_back(sender, *content.sender_key);
    if (!plaintext)
      return std::nullopt;
    auto payload = knot::try_read<olm_payload>(*plaintext);
    auto envelope = knot::try_read<olm_envelope>(*plaintext);
    if (!payload || !envelope)
      return std::nullopt;
    // For this user and this device, from the user who sent it: else a
    // payload decrypted elsewhere and passed on, or one claiming another's
    // name, would be believed.
    if (envelope->sender != sender || envelope->recipient != user_id_ || envelope->recipient_keys.ed25519 != this->ed25519())
      return std::nullopt;
    return splice::visit(
        splice::overloaded{[&](const loom::ev::m_room_key_content_t& key) -> std::optional<to_device_said> {
                             return to_device_said{room_key_offer{key, kept_file::origin{.sender_key = *content.sender_key,
                                                                                         .ed25519 = envelope->keys.ed25519,
                                                                                         .sender = sender}}};
                           },
                           [&](const loom::ev::m_secret_send_content_t& said) -> std::optional<to_device_said> {
                             return to_device_said{secret_got{said.request_id, said.secret, sender, envelope->keys.ed25519}};
                           },
                           [](const auto&) -> std::optional<to_device_said> { return std::nullopt; }},
        payload->content.data());
  }

  // A room key offered, its device found: taken where the ed25519 key the
  // Olm payload claimed is that device's own -- else refused.
  [[nodiscard]] bool accept_room_key(const room_key_offer& offer, const device_identity& device) {
    if (device.ed25519 != offer.from.ed25519)
      return false;
    kept_file::origin from = offer.from;
    from.device = device.device_id;
    from.cross_signed = device.cross_signed;
    return this->room_key(offer.key, std::move(from));
  }
  // A room's session to send with, for the devices that are its readers
  // now: kept where it may go on, else made anew -- none yet, too old or too
  // used, or any device it was given to no longer among them (someone left,
  // or a device went: they read nothing sent after). Made, it is kept for
  // this device to read its own messages by, as anyone's.
  [[nodiscard]] outbound_plan outbound_for(const std::string& room, const std::vector<recipient>& readers, rotation limits,
                                           std::int64_t now_ms) {
    auto& all = kept_.outbound ? *kept_.outbound : kept_.outbound.emplace();
    const auto kept = all.find(room);
    const auto is_reader = [&](const std::string& user, const std::string& device) {
      return std::ranges::any_of(readers, [&](const recipient& one) { return one.user == user && one.device_id == device; });
    };
    const bool fresh = kept == all.end() || now_ms - kept->second.created_ms >= limits.most_ms ||
                       kept->second.messages >= limits.most_messages ||
                       std::ranges::any_of(kept->second.shared_with, [&](const auto& by_user) {
                         return std::ranges::any_of(by_user.second,
                                                    [&](const std::string& device) { return !is_reader(by_user.first, device); });
                       });
    if (fresh) {
      auto made = vodozemac::megolm::new_group_session();
      const std::string id(made->session_id());
      const std::string key(made->session_key()->to_base64());
      all.insert_or_assign(room, kept_file::outbound_session{.pickle = std::string(made->pickle(key_)), .created_ms = now_ms});
      // Its first index readable here too, from this device, cross-signed
      // by definition.
      (void)this->keep_inbound(room, id, key,
                               kept_file::origin{.sender_key = this->curve25519(), .ed25519 = this->ed25519(), .sender = user_id_,
                                                 .device = device_id_, .cross_signed = true});
      this->save();
    }
    auto& now = all.at(room);
    auto session = vodozemac::megolm::group_session_from_pickle(now.pickle, key_);
    outbound_plan plan{.session_id = std::string(session->session_id()),
                       .session_key = std::string(session->session_key()->to_base64())};
    for (const recipient& one : readers) {
      const auto had = now.shared_with.find(one.user);
      if (had == now.shared_with.end() || !std::ranges::contains(had->second, one.device_id))
        plan.to_share.push_back(one);
    }
    return plan;
  }
  // The session given to a device: it is not given again.
  void shared(const std::string& room, const recipient& with) {
    if (!kept_.outbound)
      return;
    if (const auto kept = kept_.outbound->find(room); kept != kept_.outbound->end())
      kept->second.shared_with[with.user].push_back(with.device_id);
    this->save();
  }
  // Whether this device has an Olm session with that one.
  [[nodiscard]] bool has_session(const std::string& curve25519) const {
    const auto found = kept_.olm.find(curve25519);
    return found != kept_.olm.end() && !found->second.empty();
  }
  // A room's key, for a device, over Olm: through its newest session, or one
  // made from its one-time key. None where there is neither.
  [[nodiscard]] std::optional<olm_ciphertext> room_key_for(const recipient& to, const std::string& room,
                                                           const outbound_plan& plan,
                                                           const std::optional<std::string>& one_time_key) {
    const room_key_payload payload{.content = {.room_id = room, .session_id = plan.session_id, .session_key = plan.session_key},
                                   .sender = user_id_,
                                   .recipient = to.user,
                                   .recipient_keys = {to.ed25519},
                                   .keys = {this->ed25519()}};
    return this->seal_olm(to, knot::to_json_string(payload), one_time_key);
  }
  // A secret, for one of this user's own devices that asked, over Olm.
  [[nodiscard]] std::optional<olm_ciphertext> secret_for(const recipient& to, const std::string& request_id,
                                                         const std::string& secret,
                                                         const std::optional<std::string>& one_time_key) {
    const secret_send_payload payload{.content = {.request_id = request_id, .secret = secret},
                                      .sender = user_id_,
                                      .recipient = to.user,
                                      .recipient_keys = {to.ed25519},
                                      .keys = {this->ed25519()}};
    return this->seal_olm(to, knot::to_json_string(payload), one_time_key);
  }
  // A new Olm session to a device, from its one-time key.
  [[nodiscard]] auto outbound_to(const std::string& curve25519, const std::string& one_time_key) {
    auto config = vodozemac::olm::new_session_config_version_1();
    auto identity = vodozemac::types::curve_key_from_base64(curve25519);
    auto otk = vodozemac::types::curve_key_from_base64(one_time_key);
    return (*account_)->create_outbound_session(*config, *identity, *otk);
  }
  // An Olm message as the ciphertext m.room.encrypted carries for a device.
  [[nodiscard]] static olm_ciphertext ciphertext_of(const auto& message) {
    const auto parts = message.to_parts();
    return olm_ciphertext{.type = static_cast<std::int64_t>(parts.message_type), .body = splice::bytes::base64_text(parts.ciphertext)};
  }
  // A payload, for a device, over Olm: through its newest session, or one
  // made from its one-time key.
  [[nodiscard]] std::optional<olm_ciphertext> seal_olm(const recipient& to, const std::string& plaintext,
                                                       const std::optional<std::string>& one_time_key) {
    try {
      auto& sessions = kept_.olm[to.curve25519];
      if (sessions.empty()) {
        if (!one_time_key)
          return std::nullopt;
        auto made = this->outbound_to(to.curve25519, *one_time_key);
        sessions.push_back(std::string(made->pickle(key_)));
      }
      auto session = vodozemac::olm::session_from_pickle(sessions.back(), key_);
      auto message = session->encrypt(plaintext);
      sessions.back() = std::string(session->pickle(key_));
      this->save();
      return ciphertext_of(*message);
    } catch (const rust::Error&) {
      return std::nullopt;
    }
  }
  // A room event encrypted with the room's session, as m.room.encrypted's
  // content; its relation kept in the clear.
  // Only with the session that was given out: where another took its place
  // meanwhile (another send, rotating), none -- its readers may not have it.
  [[nodiscard]] std::optional<megolm_content> encrypt(const std::string& room, const std::string& session_id, std::string type,
                                                      knot::raw content, std::optional<knot::raw> relates_to) {
    if (!kept_.outbound)
      return std::nullopt;
    const auto kept = kept_.outbound->find(room);
    if (kept == kept_.outbound->end())
      return std::nullopt;
    try {
      auto session = vodozemac::megolm::group_session_from_pickle(kept->second.pickle, key_);
      if (std::string(session->session_id()) != session_id)
        return std::nullopt;
      const megolm_plaintext plain{.type = std::move(type), .content = std::move(content), .room_id = room};
      auto message = session->encrypt(knot::to_json_string(plain));
      kept->second.pickle = std::string(session->pickle(key_));
      ++kept->second.messages;
      this->save();
      return megolm_content{.ciphertext = std::string(message->to_base64()),
                            .device_id = device_id_,
                            .sender_key = this->curve25519(),
                            .session_id = std::string(session->session_id()),
                            .relates_to = std::move(relates_to)};
    } catch (const rust::Error&) {
      return std::nullopt;
    }
  }
  [[nodiscard]] const std::string& device_id() const { return device_id_; }

  // The devices whose Olm messages no session here opened, since asked
  // last: by user, their curve25519 key.
  [[nodiscard]] std::vector<std::pair<std::string, std::string>> take_wedged() { return std::exchange(wedged_, {}); }
  // A broken session mended: a new one made from the device's one-time key,
  // whatever sessions there are, and an m.dummy sent over it.
  [[nodiscard]] std::optional<olm_ciphertext> mend(const recipient& to, const std::string& one_time_key) {
    const dummy_payload payload{.sender = user_id_, .recipient = to.user, .recipient_keys = {to.ed25519}, .keys = {this->ed25519()}};
    try {
      auto made = this->outbound_to(to.curve25519, one_time_key);
      auto message = made->encrypt(knot::to_json_string(payload));
      kept_.olm[to.curve25519].push_back(std::string(made->pickle(key_)));
      this->save();
      return ciphertext_of(*message);
    } catch (const rust::Error&) {
      return std::nullopt;
    }
  }

  // Every room key held, at the first message each can read: what a key
  // file holds.
  [[nodiscard]] std::vector<exported_session> export_sessions() {
    std::vector<exported_session> out;
    for (const auto& [room, sessions] : kept_.megolm)
      for (const auto& [id, pickled] : sessions) {
        const auto origin = kept_.origins.find(id);
        if (origin == kept_.origins.end())
          continue;
        try {
          auto session = vodozemac::megolm::inbound_group_session_from_pickle(pickled, key_);
          auto key = session->export_at(session->first_known_index());
          out.push_back(exported_session{.room_id = room,
                                         .sender_key = origin->second.sender_key,
                                         .sender_claimed_keys = {{"ed25519", origin->second.ed25519}},
                                         .session_id = id,
                                         .session_key = std::string(key->to_base64())});
        } catch (const rust::Error&) {
        }
      }
    return out;
  }
  // Room keys from a key file, taken where none is held under their ID:
  // how many were.
  [[nodiscard]] std::size_t import_sessions(const std::vector<exported_session>& sessions) {
    std::size_t taken = 0;
    for (const exported_session& one : sessions) {
      if (one.algorithm != "m.megolm.v1.aes-sha2" || one.room_id.empty() || kept_.origins.contains(one.session_id))
        continue;
      try {
        auto key = vodozemac::megolm::exported_session_key_from_base64(one.session_key);
        auto session = vodozemac::megolm::import_inbound_group_session(*key);
        if (std::string(session->session_id()) != one.session_id)
          continue;
        const auto claimed = one.sender_claimed_keys.find("ed25519");
        kept_.megolm[one.room_id][one.session_id] = std::string(session->pickle(key_));
        kept_.origins.emplace(one.session_id,
                              kept_file::origin{.sender_key = one.sender_key,
                                                .ed25519 = claimed == one.sender_claimed_keys.end() ? std::string() : claimed->second,
                                                .cross_signed = false,
                                                .imported = true});
        ++taken;
      } catch (const rust::Error&) {
      }
    }
    if (taken > 0)
      this->save();
    return taken;
  }

  // Verified here by comparing emoji: a device's key, and a master key --
  // the one pinned from then on, whatever was pinned before.
  [[nodiscard]] std::vector<std::string> verified_keys(const std::string& user) const {
    if (!kept_.verified)
      return {};
    const auto found = kept_.verified->find(user);
    return found == kept_.verified->end() ? std::vector<std::string>{} : found->second;
  }
  void mark_verified(const std::string& user, const std::string& ed25519) {
    auto& all = kept_.verified ? *kept_.verified : kept_.verified.emplace();
    if (!std::ranges::contains(all[user], ed25519))
      all[user].push_back(ed25519);
    this->save();
  }
  void verify_master(const std::string& user, const std::string& master) {
    (kept_.verified_masters ? *kept_.verified_masters : kept_.verified_masters.emplace()).insert_or_assign(user, master);
    (kept_.masters ? *kept_.masters : kept_.masters.emplace()).insert_or_assign(user, master);
    this->save();
  }
  [[nodiscard]] bool master_verified(const std::string& user) const {
    return kept_.verified_masters && kept_.verified_masters->contains(user);
  }

  // This user's cross-signing private keys, kept sealed under the store's
  // key; and read back.
  void keep_cross_signing(const cross_signing_secrets& secrets) {
    const auto sealed = mux::vault::detail::seal(key_, splice::bytes::of(knot::to_json(secrets)), "cross-signing");
    kept_.cross_signing = splice::bytes::base64_text(sealed);
    this->save();
  }
  [[nodiscard]] std::optional<cross_signing_secrets> cross_signing_keys() const {
    if (!kept_.cross_signing)
      return std::nullopt;
    const auto sealed = from_base64(*kept_.cross_signing);
    if (!sealed)
      return std::nullopt;
    const auto opened = mux::vault::detail::open(key_, *sealed, "cross-signing");
    if (!opened)
      return std::nullopt;
    auto read = knot::try_read<cross_signing_secrets>(splice::bytes::chars(*opened));
    if (!read)
      return std::nullopt;
    return std::move(*read);
  }
  // The key backup written to: its version and private key, sealed; the
  // sessions not in it yet; and those that now are.
  // The key backup let go of: deleted on the server, nothing kept of it.
  void forget_backup() {
    kept_.backup_version.reset();
    kept_.backup_key.reset();
    kept_.backed_up.reset();
    this->save();
  }
  void keep_backup(const std::string& version, const std::string& secret_b64) {
    kept_.backup_version = version;
    kept_.backup_key = splice::bytes::base64_text(mux::vault::detail::seal(key_, splice::bytes::of(secret_b64), "key backup"));
    kept_.backed_up.reset();
    this->save();
  }
  [[nodiscard]] std::optional<std::pair<std::string, std::string>> backup() const {
    if (!kept_.backup_version || !kept_.backup_key)
      return std::nullopt;
    const auto sealed = from_base64(*kept_.backup_key);
    const auto opened = sealed ? mux::vault::detail::open(key_, *sealed, "key backup") : std::nullopt;
    if (!opened)
      return std::nullopt;
    return std::pair(*kept_.backup_version, splice::bytes::text_of(*opened));
  }
  [[nodiscard]] std::vector<backup_entry> not_backed_up(std::size_t most) {
    std::vector<backup_entry> out;
    for (const auto& [room, sessions] : kept_.megolm)
      for (const auto& [id, pickled] : sessions) {
        if (out.size() >= most)
          return out;
        if (kept_.backed_up && kept_.backed_up->contains(id))
          continue;
        const auto origin = kept_.origins.find(id);
        if (origin == kept_.origins.end())
          continue;
        try {
          auto session = vodozemac::megolm::inbound_group_session_from_pickle(pickled, key_);
          const auto first = session->first_known_index();
          auto key = session->export_at(first);
          out.push_back(backup_entry{.room = room,
                                     .session_id = id,
                                     .first_index = static_cast<std::int64_t>(first),
                                     .verified = origin->second.cross_signed.value_or(false),
                                     .plain = {.sender_claimed_keys = {{"ed25519", origin->second.ed25519}},
                                               .sender_key = origin->second.sender_key,
                                               .session_key = std::string(key->to_base64())}});
        } catch (const rust::Error&) {
        }
      }
    return out;
  }
  void backed_up(const std::vector<std::string>& ids) {
    auto& all = kept_.backed_up ? *kept_.backed_up : kept_.backed_up.emplace();
    all.insert_range(ids | std::views::transform([](const std::string& id) { return std::pair(id, true); }));
    this->save();
  }

  // Signed with this device's own key: what the master key carries, so that
  // this device's word for it can be seen.
  [[nodiscard]] std::string sign_as_device(std::string_view canonical) const { return this->sign(canonical); }

  // A user's master key as first seen; pinned the first time.
  [[nodiscard]] std::optional<std::string> pinned_master(const std::string& user) const {
    if (!kept_.masters)
      return std::nullopt;
    const auto found = kept_.masters->find(user);
    return found == kept_.masters->end() ? std::nullopt : std::optional<std::string>(found->second);
  }
  // A user's reset identity taken as theirs now (Element's "Withdraw
  // verification"): their new master key pinned in the old one's place, and
  // what was verified of them dropped -- it was of the old one.
  void accept_master(const std::string& user, const std::string& key) {
    (kept_.masters ? *kept_.masters : kept_.masters.emplace()).insert_or_assign(user, key);
    if (kept_.verified_masters)
      kept_.verified_masters->erase(user);
    if (kept_.verified)
      kept_.verified->erase(user);
    this->save();
  }
  void pin_master(const std::string& user, const std::string& key) {
    if (!kept_.masters)
      kept_.masters.emplace();
    if (kept_.masters->emplace(user, key).second)
      this->save();
  }

  // A room's m.room.encrypted: its Megolm message read with the session it
  // names, where this device has it; the room event it was, read as any.
  [[nodiscard]] std::optional<decrypted> room_event(const std::string& room, const std::string& event_id,
                                                         const std::string& sender,
                                                         const loom::ev::m_room_encrypted_content_t& content) {
    if (!content.session_id)
      return std::nullopt;
    // From the user its key came from, by the device it came from where the
    // event still says (sender_key, deprecated but sent): a session shared
    // by one is not another's to send with.
    const auto origin = kept_.origins.find(*content.session_id);
    if (origin == kept_.origins.end() || (!origin->second.imported.value_or(false) && origin->second.sender != sender) ||
        (content.sender_key && *content.sender_key != origin->second.sender_key))
      return std::nullopt;
    const auto in_room = kept_.megolm.find(room);
    if (in_room == kept_.megolm.end())
      return std::nullopt;
    const auto found = in_room->second.find(*content.session_id);
    if (found == in_room->second.end())
      return std::nullopt;
    auto ciphertext = knot::try_read<std::string>(content.ciphertext.text);
    if (!ciphertext)
      return std::nullopt;
    try {
      auto session = vodozemac::megolm::inbound_group_session_from_pickle(found->second, key_);
      auto message = vodozemac::megolm::megolm_message_from_base64(*ciphertext);
      const auto clear = session->decrypt(*message);
      // The same index in another event: a replay, refused.
      auto& seen = kept_.indices[*content.session_id];
      const auto before = std::ranges::find(seen, clear.message_index, &kept_file::read_index::index);
      if (before != seen.end() && before->event != event_id)
        return std::nullopt;
      const std::string text(clear.plaintext.begin(), clear.plaintext.end());
      auto read = knot::try_read<megolm_payload>(text);
      auto in_room = knot::try_read<megolm_room>(text);
      // Sent in this room: a message from another room put here is not.
      if (!read || !in_room || in_room->room_id != room)
        return std::nullopt;
      // Only what a message is: a message, a sticker, a reaction -- or an
      // event nothing here reads, said as such. Never state, nor a
      // redaction: the server checks who may send those, and inside an
      // encrypted event it checks nothing (a room's name, its members, its
      // power levels, a message removed -- all forged by anyone in it).
      const bool message_like = splice::visit(
          splice::overloaded{[](const loom::ev::m_room_message_content_t&) { return true; },
                             [](const loom::ev::m_sticker_content_t&) { return true; },
                             [](const loom::ev::m_reaction_content_t&) { return true; },
                             [](const knot::raw&) { return true; }, [](const auto&) { return false; }},
          read->content.data());
      if (!message_like)
        return std::nullopt;
      // Kept with the next save, not one of its own: a whole store written
      // for each message read was a cost any busy room could drive. Saved
      // at the next sync at the latest (flush).
      if (before == seen.end()) {
        seen.push_back({clear.message_index, std::string(event_id)});
        unsaved_ = true;
      }
      return decrypted{std::move(*read), origin->second.cross_signed.value_or(false),
                       origin->second.imported.value_or(false) ? std::optional<std::string>(origin->second.sender_key)
                                                               : std::nullopt};
    } catch (const rust::Error&) {
      return std::nullopt;
    }
  }

 private:
  olm_machine(mux::vault::vault& vault, std::filesystem::path store, std::string user_id, std::string device_id)
      : vault_(&vault), store_(std::move(store)), user_id_(std::move(user_id)), device_id_(std::move(device_id)) {}

  // The store's key: 32 random bytes in a file of its own, the user's alone
  // and sealed where local data is encrypted (review 4, L5) -- made the
  // first time.
  [[nodiscard]] std::array<std::uint8_t, 32> pickle_key() const {
    std::array<std::uint8_t, 32> key{};
    const auto path = std::filesystem::path(store_).concat(".key");
    // There: read whole, or an error. A key there that cannot be read --
    // its permissions, cut short, sealed and locked -- was made anew over
    // the old one, and the store under it lost for good.
    std::error_code ignored;
    if (std::filesystem::exists(path, ignored)) {
      const auto opened = vault_->read_file(path);
      if (!opened || opened->size() != key.size())
        throw std::runtime_error("the encryption store's key cannot be read: " + path.string());
      std::ranges::copy(splice::bytes::of(*opened), key.begin());
      return key;
    }
    // Not there, and a store there: its key lost -- an error, not a new key.
    if (std::filesystem::exists(store_, ignored))
      throw std::runtime_error("the encryption store's key is missing: " + path.string());
    // The system's own randomness (RAND_bytes), not std::random_device.
    const auto random = mux::vault::vault::random(key.size());
    std::ranges::copy(random, key.begin());
    if (!vault_->write_file(path, splice::bytes::text_of(key), true))
      throw std::runtime_error("the encryption store's key cannot be written: " + path.string());
    return key;
  }

  bool unsaved_ = false;
  std::vector<std::pair<std::string, std::string>> wedged_;
  void save() {
    unsaved_ = false;
    kept_.account = std::string((*account_)->pickle(key_));
    std::error_code ignored;
    std::filesystem::create_directories(store_.parent_path(), ignored);
    // Not kept is an error, not a word to no one: an account whose used
    // one-time key comes back at the next start opens a session again for a
    // pre-key message replayed to it. Through the vault: sealed where local
    // data is encrypted, the user's alone either way.
    if (!vault_->write_file(store_, knot::to_json_string(kept_), true))
      throw std::runtime_error("the encryption store cannot be written: " + store_.string());
  }

  [[nodiscard]] std::string sign(std::string_view canonical) const {
    const auto signed_bytes = splice::bytes::buffer_of(splice::bytes::of(canonical));  // the Slice is read whole
    const rust::Slice<const std::uint8_t> bytes(signed_bytes.data(), signed_bytes.size());
    return std::string((*account_)->sign(bytes)->to_base64());
  }

  // An Olm message from the device of `their_key`: a session of theirs it
  // matches, else -- a pre-key message -- a new one it opens.
  [[nodiscard]] std::optional<std::string> olm_decrypt(const std::string& their_key, const vodozemac::olm::OlmMessage& message,
                                                       bool pre_key) {
    auto& sessions = kept_.olm[their_key];
    for (std::string& pickled : sessions) {
      auto session = vodozemac::olm::session_from_pickle(pickled, key_);
      if (pre_key && !session->session_matches(message))
        continue;
      try {
        const auto clear = session->decrypt(message);
        pickled = std::string(session->pickle(key_));
        this->save();
        return std::string(clear.begin(), clear.end());
      } catch (const rust::Error&) {
      }
    }
    if (!pre_key)
      return std::nullopt;
    // Devices with sessions, a thousand at most: a new one past that takes
    // the place of another's. The server holds this device's one-time keys
    // and can make up identity keys without end -- each a new device, each
    // its sessions written to the store.
    constexpr std::size_t kMostDevices = 1000;
    if (sessions.empty() && kept_.olm.size() > kMostDevices)
      if (const auto other = std::ranges::find_if(kept_.olm, [&](const auto& one) { return one.first != their_key; });
          other != kept_.olm.end())
        kept_.olm.erase(other);
    auto& still = kept_.olm[their_key];
    auto identity = vodozemac::types::curve_key_from_base64(their_key);
    auto config = vodozemac::olm::new_session_config_version_1();
    auto made = (*account_)->create_inbound_session(*config, *identity, message);
    still.push_back(std::string(made.session->pickle(key_)));
    auto& sessions_now = still;
    // A few per device, the oldest going first: the server holds this
    // device's one-time keys, and could otherwise open sessions without end,
    // each written to the store (review 4, L1).
    constexpr std::size_t kMostSessions = 16;
    if (sessions_now.size() > kMostSessions)
      sessions_now.erase(sessions_now.begin(), sessions_now.end() - kMostSessions);
    this->save();
    return std::string(made.plaintext.begin(), made.plaintext.end());
  }

  // A room key: its Megolm session, kept for its room with where it came
  // from. Never over one already held under its id: a second key for the
  // same session, from whoever, would replace the first one's messages'.
  bool room_key(const loom::ev::m_room_key_content_t& key, kept_file::origin from) {
    return this->keep_inbound(key.room_id, key.session_id, key.session_key, std::move(from));
  }
  bool keep_inbound(const std::string& room, const std::string& session_id, const std::string& session_key,
                    kept_file::origin from) {
    if (room.empty() || kept_.origins.contains(session_id))
      return false;
    try {
      auto key = vodozemac::megolm::session_key_from_base64(session_key);
      auto session = vodozemac::megolm::new_inbound_group_session(*key);
      // The id the key says is the session's own: else it would be kept
      // under another session's name.
      if (std::string(session->session_id()) != session_id)
        return false;
      kept_.megolm[room][session_id] = std::string(session->pickle(key_));
      kept_.origins.emplace(session_id, std::move(from));
      this->save();
      return true;
    } catch (const rust::Error&) {
      return false;
    }
  }

  // The program's vault, which the store and its key are sealed by.
  mux::vault::vault* vault_;
  std::filesystem::path store_;
  std::string user_id_;
  std::string device_id_;
  std::array<std::uint8_t, 32> key_{};
  kept_file kept_;
  std::optional<rust::Box<vodozemac::olm::Account>> account_;
};

}  // namespace mux::proto::matrix::client::crypto
