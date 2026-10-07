// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix.client:sync_keys -- What comes for this device: room keys vetted, keys exported and imported, the encrypted rooms remembered, secrets, trust and sessions.
export module mux.proto.matrix.client:sync_keys;

import mux.vault;
import std;
import splice.bytes;
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
import loom.cs.account_data;
import loom.cs.typing;
import loom.cs.wellknown;
import mux.config;
import mux.core;
import mux.http;
import mux.net;
import :account;
import :sync;
import :sync_crypto;

// The members defined here are declared in :account, and exported there.
namespace mux::proto::matrix::client {

// What a sliding sync's answer brought for this device: to-device messages
// -- Olm, carrying room keys -- and how many one-time keys the server holds.
template <class Sink>
void account<Sink>::crypto_answer(const loom::cs::sliding_sync::response_t& got) {
  if (!crypto_ || !got.extensions)
    return;
  try {
    this->crypto_answer_now(got);
  } catch (const std::exception& failed) {
    log(id_, "encryption stopped: {}", failed.what());
    crypto_.reset();
  }
}
template <class Sink>
void account<Sink>::crypto_answer_now(const loom::cs::sliding_sync::response_t& got) {
  // What the last answer's messages left unsaved, saved first.
  crypto_->flush();
  const auto& extensions = *got.extensions;
  if (extensions.to_device) {
    if (extensions.to_device->events)
      for (const auto& one : *extensions.to_device->events)
        spl::visit(spl::overloaded{[&](const loom::ev::m_room_encrypted_content_t& content) {
                                           if (auto said = crypto_->to_device(one.sender.value_or(""), content))
                                             spl::visit(spl::overloaded{[&](const crypto::room_key_offer& offer) { this->vet_room_key(offer); },
                                                                              [&](const crypto::secret_got& got) { this->secret_in(got); }},
                                                           *said);
                                         },
                                         [&](const loom::ev::m_room_key_withheld_content_t& content) { this->withheld_in(content); },
                                         [&](const loom::ev::m_secret_request_content_t& content) {
                                           this->secret_request_in(one.sender.value_or(""), content);
                                         },
                                         [&](const loom::ev::m_key_verification_request_content_t& content) {
                                           this->verification_in(one.sender.value_or(""), content);
                                         },
                                         [&](const loom::ev::m_key_verification_ready_content_t& content) {
                                           this->verification_in(one.sender.value_or(""), content);
                                         },
                                         [&](const loom::ev::m_key_verification_start_content_t& content) {
                                           this->verification_in(one.sender.value_or(""), content);
                                         },
                                         [&](const loom::ev::m_key_verification_accept_content_t& content) {
                                           this->verification_in(one.sender.value_or(""), content);
                                         },
                                         [&](const loom::ev::m_key_verification_key_content_t& content) {
                                           this->verification_in(one.sender.value_or(""), content);
                                         },
                                         [&](const loom::ev::m_key_verification_mac_content_t& content) {
                                           this->verification_in(one.sender.value_or(""), content);
                                         },
                                         [&](const loom::ev::m_key_verification_cancel_content_t& content) {
                                           this->verification_in(one.sender.value_or(""), content);
                                         },
                                         [](const auto&) {}},
                      one.content.data());
    for (const auto& [user, curve] : crypto_->take_wedged())
      this->mend_session(user, curve);
    crypto_->went_on_to(extensions.to_device->next_batch);
    // Room keys that came, into the key backup.
    this->upload_backup();
  }
  // No unused fallback key on the server (none, or one used): a new one.
  if (extensions.e2ee && extensions.e2ee->device_unused_fallback_key_types &&
      !std::ranges::contains(*extensions.e2ee->device_unused_fallback_key_types, std::string_view("signed_curve25519")))
    this->upload_fallback_key();
  if (extensions.e2ee && extensions.e2ee->device_one_time_keys_count)
    if (const auto left = extensions.e2ee->device_one_time_keys_count->find("signed_curve25519");
        left != extensions.e2ee->device_one_time_keys_count->end())
      this->upload_keys(left->second);
}

template <class Sink>
void account<Sink>::export_room_keys(std::string path, std::string passphrase) {
  this->spawn_guarded([this, path = std::move(path), passphrase = std::move(passphrase)] {
    if (!crypto_) {
      sink_(change::refused{id_, "Not exported: encryption is not running for this account."});
      return;
    }
    const auto sessions = crypto_->export_sessions();
    const std::string text = crypto::export_file(sessions, passphrase);
    std::error_code failed;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), failed);
    { std::ofstream(path, std::ios::binary | std::ios::trunc); }
    std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace, failed);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    if (!out.flush()) {
      sink_(change::refused{id_, std::format("Not exported: {} could not be written.", path)});
      return;
    }
    sink_(change::notice{id_, "Room keys exported",
                         std::format("{} room keys written to {}, sealed under the passphrase.", sessions.size(), path)});
  });
}
template <class Sink>
void account<Sink>::import_room_keys(std::string path, std::string passphrase) {
  this->spawn_guarded([this, path = std::move(path), passphrase = std::move(passphrase)] {
    if (!crypto_) {
      sink_(change::refused{id_, "Not imported: encryption is not running for this account."});
      return;
    }
    const std::optional<std::string> read = spl::bytes::file_text(path);
    if (!read) {
      sink_(change::refused{id_, std::format("Not imported: {} could not be read.", path)});
      return;
    }
    const auto sessions = crypto::import_file(*read, passphrase);
    if (!sessions) {
      sink_(change::refused{id_, "Not imported: not a key file, the passphrase is another, or the file was changed."});
      return;
    }
    const std::size_t taken = crypto_->import_sessions(*sessions);
    this->decrypt_all_waiting();
    sink_(change::notice{id_, "Room keys imported",
                         std::format("{} of {} room keys taken (the rest were held already). Messages read with them are "
                                     "marked as from an unverified device: the file is only as good as where it came from.",
                                     taken, sessions->size())});
  });
}

template <class Sink>
bool account<Sink>::owns_key(const std::string& user, const std::string& curve25519) {
  const auto key = std::pair(user, curve25519);
  if (const auto known = owns_key_.find(key); known != owns_key_.end())
    return known->second;
  // Not connected (a kept sync read at the start): not known yet, so not
  // shown -- asked once there is a server to ask.
  if (!api_)
    return false;
  auto got = this->keys_of(user);
  if (!got)
    return false;  // not known now: asked again next time
  const bool owns = crypto::device_of(*got, user, curve25519, crypto_ ? crypto_->pinned_master(user) : std::nullopt).has_value();
  owns_key_.insert_or_assign(key, owns);
  return owns;
}

// The room's readers now -- its joined members' devices that pass the
// checks (recipients_of) -- and its session given to those that have not
// got it: over an Olm session where there is one, else one made from a
// one-time key claimed for the device and signed by it.
template <class Sink>
std::optional<std::string> account<Sink>::share_room_key(const std::string& room) {
  if (!api_ || !crypto_)
    return std::nullopt;
  auto members = perform(*api_, loom::cs::get_joined_members_by_room{.room_id = room});
  if (!members || !members->joined) {
    log(id_, "{}: its members could not be fetched", room);
    return std::nullopt;
  }
  loom::cs::query_keys ask;
  for (const auto& [user, profile] : *members->joined)
    ask.body.device_keys.emplace(user, std::vector<std::string>{});
  auto got = perform(*api_, ask);
  if (!got) {
    log(id_, "{}: its members' devices could not be fetched: {}", room, got.error().said());
    return std::nullopt;
  }
  std::vector<crypto::recipient> readers;
  for (const auto& [user, profile] : *members->joined) {
    if (!crypto_->pinned_master(user))
      if (const auto master = crypto::master_of(*got, user))
        crypto_->pin_master(user, *master);
    auto theirs = crypto::recipients_of(*got, user, crypto_->pinned_master(user),
                                        user == id_.address ? std::string_view(crypto_->device_id()) : std::string_view(),
                                        crypto_->verified_keys(user), user == id_.address || how_.only_verified);
    readers.insert(readers.end(), std::make_move_iterator(theirs.begin()), std::make_move_iterator(theirs.end()));
  }
  crypto::rotation limits;
  if (const auto kept = state_.joined.find(room); kept != state_.joined.end())
    if (const auto* how = kept->second.state.template content<loom::ev::m_room_encryption_content_t>("m.room.encryption"))
      limits = crypto::rotation_of(how->rotation_period_ms, how->rotation_period_msgs);
  const auto now_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  const auto plan = crypto_->outbound_for(room, readers, limits, now_ms);
  if (plan.to_share.empty())
    return plan.session_id;
  // One-time keys for the devices there is no session with yet.
  loom::cs::claim_keys claim;
  for (const crypto::recipient& one : plan.to_share)
    if (!crypto_->has_session(one.curve25519))
      claim.body.one_time_keys[one.user][one.device_id] = "signed_curve25519";
  std::map<std::string, std::string> keys_by_curve;
  if (!claim.body.one_time_keys.empty())
    if (auto claimed = perform(*api_, claim))
      for (const crypto::recipient& one : plan.to_share)
        if (const auto user = claimed->one_time_keys.find(one.user); user != claimed->one_time_keys.end())
          if (const auto device = user->second.find(one.device_id); device != user->second.end())
            for (const auto& [id, key] : device->second)
              if (auto checked = crypto::one_time_key_of(key, one))
                keys_by_curve.insert_or_assign(one.curve25519, std::move(*checked));
  std::map<std::string, std::map<std::string, knot::raw>> messages;
  std::vector<crypto::recipient> given;
  for (const crypto::recipient& one : plan.to_share) {
    const auto key = keys_by_curve.find(one.curve25519);
    auto sealed = crypto_->room_key_for(one, room, plan,
                                        key == keys_by_curve.end() ? std::nullopt : std::optional<std::string>(key->second));
    if (!sealed) {
      log(id_, "{}: no session with {}'s device {}, which does not get the key", room, one.user, one.device_id);
      continue;
    }
    messages[one.user][one.device_id] = knot::raw{
        knot::to_json_string(crypto::olm_content{.sender_key = crypto_->curve25519(), .ciphertext = {{one.curve25519, *sealed}}})};
    given.push_back(one);
  }
  if (messages.empty())
    return plan.session_id;
  auto sent = perform(*api_, loom::cs::send_to_device{.event_type = "m.room.encrypted",
                                                      .txn_id = this->transaction(),
                                                      .body = {.messages = std::move(messages)}});
  if (!sent) {
    log(id_, "{}: the room's key could not be sent: {}", room, sent.error().said());
    return std::nullopt;
  }
  for (const crypto::recipient& one : given)
    crypto_->shared(room, one);
  return plan.session_id;
}

// A room key offered: the sender's devices asked of the server, and the key
// taken only where the Olm session it came over is one of them, signed by
// itself, and the ed25519 key the payload claimed is that device's. The
// to-device sender is the server's word: a server (or a device it made up)
// otherwise hands over a session of its own as anyone's (review 4, H1).
template <class Sink>
void account<Sink>::vet_room_key(const crypto::room_key_offer& offer) {
  const std::string& user = offer.from.sender;
  auto got = perform(*api_, loom::cs::query_keys{.body = {.device_keys = {{user, {}}}}});
  if (!got) {
    log(id_, "room key from {} not taken: their devices could not be fetched: {}", user, got.error().said());
    return;
  }
  const auto device = crypto::device_of(*got, user, offer.from.sender_key, crypto_->pinned_master(user));
  if (!device) {
    log(id_, "room key from {} refused: no device of theirs, signed by itself, has the key it came with", user);
    return;
  }
  if (device->master && !crypto_->pinned_master(user))
    crypto_->pin_master(user, *device->master);
  // Their master key is not the one first seen: said once, plainly -- a
  // server that swaps it is making their devices its own (review 6).
  if (const auto pinned = crypto_->pinned_master(user);
      device->master && pinned && *pinned != *device->master && identity_changed_.emplace(user).second)
  {
    sink_(change::refused{id_, std::format("{}'s encryption identity changed. Their messages are marked as from an "
                                           "unverified device until it is verified.",
                                           user)});
    this->tell_trust(user);
  }
  // Cross-signed, or verified here by comparing emoji.
  crypto::device_identity trusted = *device;
  trusted.cross_signed = device->cross_signed || std::ranges::contains(crypto_->verified_keys(user), device->ed25519);
  if (!crypto_->accept_room_key(offer, trusted)) {
    log(id_, "room key from {} ({}) not taken", user, device->device_id);
    return;
  }
  log(id_, "room key from {} ({}){}", user, device->device_id, device->cross_signed ? "" : ", unverified device");
  // What came before it, read now.
  this->decrypt_waiting(offer.key.session_id);
}

// Encrypted, once known so: by its state now, or by what was known before --
// kept beside the sync, and in the E2EE store where there is one, so that a
// server hiding m.room.encryption later, or at the next start, does not make
// it plain.
template <class Sink>
bool account<Sink>::encrypted_room(std::string_view room) {
  if (encrypted_rooms_.contains(room))
    return true;
  bool now = false;
  if (const auto kept = state_.joined.find(std::string(room)); kept != state_.joined.end()) {
    now = kept->second.state.encrypted();
    // The room an encrypted room was upgraded into: encrypted too, whatever
    // its state says -- a server dropping m.room.encryption from the new
    // room would otherwise have its first messages in the clear.
    if (const auto before = predecessor_of(kept->second); !now && before && encrypted_rooms_.contains(*before))
      now = true;
  }
  if (!now && crypto_)
    now = crypto_->was_encrypted(room);
  if (now)
    this->remember_encrypted(room);
  return now;
}
template <class Sink>
void account<Sink>::remember_encrypted(std::string_view room) {
  if (!encrypted_rooms_.emplace(room).second)
    return;
  this->save_encrypted();
  if (crypto_) {
    try {
      crypto_->remember_encrypted(room);
    } catch (const std::exception& failed) {
      log(id_, "encryption stopped: {}", failed.what());
      crypto_.reset();
    }
  }
}
// What the file keeps: the rooms, and when each was encrypted at the latest.
struct encrypted_kept {
  std::vector<std::string> rooms;
  std::optional<std::map<std::string, std::int64_t>> since;
  friend consteval auto json_schema(knot::type<encrypted_kept>) { return knot::schema<encrypted_kept>(); }
};
template <class Sink>
void account<Sink>::save_encrypted() {
  const encrypted_kept all{.rooms = std::vector<std::string>(encrypted_rooms_.begin(), encrypted_rooms_.end()),
                           .since = std::map<std::string, std::int64_t>(encrypted_since_.begin(), encrypted_since_.end())};
  if (!how_.vault->write_file(this->encrypted_rooms_file(), knot::to_json_string(all), true))
    log(id_, "the encrypted rooms could not be kept in {}", this->encrypted_rooms_file().string());
}
template <class Sink>
std::optional<typename account<Sink>::since_t> account<Sink>::encrypted_by(std::string_view room, std::optional<since_t> seen) {
  const auto kept = encrypted_since_.find(room);
  if (seen && (kept == encrypted_since_.end() || seen->time_since_epoch().count() < kept->second)) {
    encrypted_since_.insert_or_assign(std::string(room), seen->time_since_epoch().count());
    this->save_encrypted();
    return seen;
  }
  if (kept == encrypted_since_.end())
    return std::nullopt;
  return since_t(std::chrono::milliseconds(kept->second));
}
template <class Sink>
std::filesystem::path account<Sink>::encrypted_rooms_file() const {
  return std::filesystem::path(this->kept_file()).concat(".encrypted");
}
template <class Sink>
void account<Sink>::load_encrypted() {
  const auto opened = how_.vault->read_file(this->encrypted_rooms_file());
  if (!opened)
    return;
  if (auto read = knot::try_read<encrypted_kept>(std::string_view(*opened))) {
    encrypted_rooms_.insert(read->rooms.begin(), read->rooms.end());
    if (read->since)
      encrypted_since_.insert(read->since->begin(), read->since->end());
  } else if (auto old = knot::try_read<std::vector<std::string>>(std::string_view(*opened))) {
    encrypted_rooms_.insert(old->begin(), old->end());  // as kept before the times were
  }
}

}  // namespace mux::proto::matrix::client

namespace mux::proto::matrix::client {
// A changed identity first -- whatever was verified was the old one; then
// verified, by emoji (a device of theirs) or their master key; else not.
template <class Sink>
void account<Sink>::tell_trust(std::string user) {
  if (!crypto_)
    return;
  trust_t now = trust::unverified{};
  if (identity_changed_.contains(user))
    now = trust::changed{};
  else if (crypto_->master_verified(user) || !crypto_->verified_keys(user).empty())
    now = trust::verified{};
  sink_(change::trust_changed{id_, std::move(user), std::move(now)});
}
}  // namespace mux::proto::matrix::client

namespace mux::proto::matrix::client {

template <class Sink>
void account<Sink>::request_secrets(const std::string& device) {
  if (!api_ || !crypto_)
    return;
  for (const auto& [name, at] : crypto::kSecretNames) {
    const std::string request = this->transaction();
    secrets_asked_.insert_or_assign(request, crypto::secret_name_at(at));
    std::map<std::string, std::map<std::string, knot::raw>> messages;
    messages[id_.address][device] = knot::raw{knot::to_json_string(crypto::secret_request_part{
        .name = std::string(name), .requesting_device_id = crypto_->device_id(), .request_id = request})};
    (void)perform(*api_, loom::cs::send_to_device{.event_type = "m.secret.request", .txn_id = this->transaction(),
                                                  .body = {.messages = std::move(messages)}});
  }
  // And the key read mentions are sealed under, mux's own (not one of
  // loom's names): asked the same way, kept apart.
  {
    const std::string request = this->transaction();
    mentions_key_asked_.insert(request);
    std::map<std::string, std::map<std::string, knot::raw>> messages;
    messages[id_.address][device] = knot::raw{knot::to_json_string(crypto::secret_request_part{
        .name = "net.mux.mentions_key", .requesting_device_id = crypto_->device_id(), .request_id = request})};
    (void)perform(*api_, loom::cs::send_to_device{.event_type = "m.secret.request", .txn_id = this->transaction(),
                                                  .body = {.messages = std::move(messages)}});
  }
  log(id_, "asked {} for the cross-signing keys, the backup key and the mentions key", device);
}

// A secret given: taken only as the answer to one asked here, from this
// user, from a device of theirs verified here by emoji. The three
// cross-signing keys, together, checked against the master key the server
// lists, kept, and this device signed with them; the backup's key, the
// backup restored.
template <class Sink>
void account<Sink>::secret_in(const crypto::secret_got& got) {
  if (!api_ || !crypto_)
    return;
  // The mentions key: kept, where it is one, from a session verified here.
  if (mentions_key_asked_.contains(got.request_id)) {
    if (got.sender != id_.address || !std::ranges::contains(crypto_->verified_keys(id_.address), got.ed25519))
      return;
    mentions_key_asked_.erase(got.request_id);
    if (auto bytes = crypto::from_base64(got.secret); bytes && bytes->size() == 32) {
      this->keep_mentions_key(std::move(*bytes));
      log(id_, "the mentions key given by another session of yours");
    }
    return;
  }
  const auto asked = secrets_asked_.find(got.request_id);
  if (asked == secrets_asked_.end() || got.sender != id_.address)
    return;
  if (!std::ranges::contains(crypto_->verified_keys(id_.address), got.ed25519)) {
    log(id_, "a secret refused: not from a session of yours verified here");
    return;
  }
  const crypto::secret_name_t which = asked->second;
  secrets_asked_.erase(asked);
  spl::visit(spl::overloaded{[&](crypto::secret_name::master) { secrets_got_.master = got.secret; },
                                   [&](crypto::secret_name::self_signing) { secrets_got_.self_signing = got.secret; },
                                   [&](crypto::secret_name::user_signing) { secrets_got_.user_signing = got.secret; },
                                   [&](crypto::secret_name::backup) {
                                     const std::size_t restored = this->restore_backup(got.secret);
                                     log(id_, "the backup key given: {} room keys restored", restored);
                                   }},
                which);
  if (!secrets_got_.master || !secrets_got_.self_signing || !secrets_got_.user_signing)
    return;
  const crypto::cross_signing_secrets secrets{.master = *secrets_got_.master, .self_signing = *secrets_got_.self_signing,
                                              .user_signing = *secrets_got_.user_signing};
  auto listed = this->keys_of(id_.address);
  const auto server_master = listed ? crypto::master_of(*listed, id_.address) : std::nullopt;
  if (!server_master || crypto::detail::ed25519_public(secrets.master) != server_master) {
    sink_(change::refused{id_, "The cross-signing keys your other session gave are not the ones your account has."});
    return;
  }
  crypto_->keep_cross_signing(secrets);
  crypto_->verify_master(id_.address, *server_master);
  if (listed->device_keys)
    if (const auto own = listed->device_keys->find(id_.address); own != listed->device_keys->end())
      if (const auto device = own->second.find(crypto_->device_id()); device != own->second.end())
        this->cross_sign_device(device->second);
  secrets_got_ = {};
  sink_(change::notice{id_, "Verified",
                       "Your other session gave this one your cross-signing keys: it has signed itself with them, and "
                       "the people you talk to see it as yours."});
}

// A secret asked by one of this user's own other sessions: given where this
// device has it and the asker is a device of theirs verified here by emoji
// -- over Olm, a session with it opened from its one-time key where none is.
template <class Sink>
void account<Sink>::secret_request_in(const std::string& sender, const loom::ev::m_secret_request_content_t& content) {
  if (!api_ || !crypto_ || sender != id_.address || content.requesting_device_id == crypto_->device_id() || !content.name)
    return;
  const bool asking = spl::visit(spl::overloaded{[](loom::ev::m_secret_request_content_t::action_values::request_) { return true; },
                                                       [](const auto&) { return false; }},
                                    content.action);
  if (!asking)
    return;
  // The secret asked for, as this device has it: one of loom's names, or
  // mux's own mentions key -- told apart where the name comes in.
  const auto keys = crypto_->cross_signing_keys();
  const auto which = crypto::secret_name_of(*content.name);
  std::optional<std::string> secret;
  if (which && keys)
    secret = spl::visit(spl::overloaded{[&](crypto::secret_name::master) { return std::optional<std::string>(keys->master); },
                                        [&](crypto::secret_name::self_signing) { return std::optional<std::string>(keys->self_signing); },
                                        [&](crypto::secret_name::user_signing) { return std::optional<std::string>(keys->user_signing); },
                                        [](crypto::secret_name::backup) { return std::optional<std::string>(); }},
                        *which);
  else if (!which)
    spl::visit(spl::overloaded{[&](own_secret::mentions_key) {
                                 if (this->mentions_key())
                                   secret = spl::bytes::base64_text(*this->mentions_key());
                               },
                               [](own_secret::other) {}},
               own_secret_of(*content.name));
  if (!secret)
    return;
  auto got = this->keys_of(id_.address);
  if (!got)
    return;
  const auto mine = crypto::recipients_of(*got, id_.address, crypto_->pinned_master(id_.address), std::string_view(),
                                          crypto_->verified_keys(id_.address));
  const auto device = std::ranges::find(mine, content.requesting_device_id, &crypto::recipient::device_id);
  if (device == mine.end() || !std::ranges::contains(crypto_->verified_keys(id_.address), device->ed25519)) {
    log(id_, "{} asked for {}: not a session of yours verified here, not given", content.requesting_device_id, *content.name);
    return;
  }
  std::optional<std::string> one_time_key;
  if (!crypto_->has_session(device->curve25519)) {
    loom::cs::claim_keys claim;
    claim.body.one_time_keys[id_.address][device->device_id] = "signed_curve25519";
    if (auto claimed = perform(*api_, claim))
      if (const auto by_user = claimed->one_time_keys.find(id_.address); by_user != claimed->one_time_keys.end())
        if (const auto by_device = by_user->second.find(device->device_id); by_device != by_user->second.end())
          for (const auto& [id, raw] : by_device->second)
            if (!one_time_key)
              one_time_key = crypto::one_time_key_of(raw, *device);
  }
  const auto sealed = crypto_->secret_for(*device, content.request_id, *secret, one_time_key);
  if (!sealed)
    return;
  std::map<std::string, std::map<std::string, knot::raw>> messages;
  messages[id_.address][device->device_id] = knot::raw{
      knot::to_json_string(crypto::olm_content{.sender_key = crypto_->curve25519(), .ciphertext = {{device->curve25519, *sealed}}})};
  if (perform(*api_, loom::cs::send_to_device{.event_type = "m.room.encrypted", .txn_id = this->transaction(),
                                              .body = {.messages = std::move(messages)}}))
    log(id_, "{} given to your session {}", *content.name, device->device_id);
}
}  // namespace mux::proto::matrix::client

namespace mux::proto::matrix::client {
template <class Sink>
void account<Sink>::tell_devices(std::string user) {
  if (!api_ || !crypto_)
    return;
  auto got = this->keys_of(user);
  if (!got || !got->device_keys)
    return;
  const auto theirs = got->device_keys->find(user);
  if (theirs == got->device_keys->end())
    return;
  const auto verified_here = crypto_->verified_keys(user);
  std::vector<change::device_view> devices;
  for (const auto& [id, info] : theirs->second) {
    const auto curve = info.keys.find("curve25519:" + id);
    const auto identity = curve == info.keys.end() ? std::nullopt
                                                   : crypto::device_of(*got, user, curve->second, crypto_->pinned_master(user));
    devices.push_back({.id = id,
                       .name = info.unsigned_ && info.unsigned_->device_display_name ? *info.unsigned_->device_display_name : std::string(),
                       .verified = identity && (identity->cross_signed || std::ranges::contains(verified_here, identity->ed25519))});
  }
  sink_(change::devices_listed{id_, std::move(user), std::move(devices)});
}
}  // namespace mux::proto::matrix::client

namespace mux::proto::matrix::client {
template <class Sink>
auto account<Sink>::own_sessions_now() -> std::optional<std::vector<own_session>> {
  auto got = this->keys_of(id_.address);
  if (!got || !got->device_keys)
    return std::nullopt;
  const auto mine = got->device_keys->find(id_.address);
  if (mine == got->device_keys->end())
    return std::nullopt;
  const auto verified_here = crypto_->verified_keys(id_.address);
  const auto trusted = [&](const std::string& id, const auto& info) {
    const auto curve = info.keys.find("curve25519:" + id);
    const auto identity = curve == info.keys.end()
                              ? std::nullopt
                              : crypto::device_of(*got, id_.address, curve->second, crypto_->pinned_master(id_.address));
    return identity && (identity->cross_signed || std::ranges::contains(verified_here, identity->ed25519));
  };
  return std::ranges::to<std::vector>(std::views::transform(mine->second, [&](const auto& each) {
           const auto& [id, info] = each;
           return own_session{.id = id,
                              .name = info.unsigned_ && info.unsigned_->device_display_name ? *info.unsigned_->device_display_name : id,
                              .trusted = trusted(id, info)};
         }));
}

template <class Sink>
void account<Sink>::check_own_sessions() {
  if (!api_ || !crypto_)
    return;
  const auto all = this->own_sessions_now();
  if (!all)
    return;
  const auto here = std::ranges::find(*all, crypto_->device_id(), &own_session::id);
  const bool this_one = here != all->end() && here->trusted;
  const auto others = std::ranges::to<std::vector>(std::views::transform(std::views::filter(*all, [&](const own_session& one) { return one.id != crypto_->device_id() && !one.trusted; }), &own_session::name));
  if (!this_one)
    sink_(change::notice{id_, "Verify this session",
                         "Verify this session to allow it to read your message history, and so that others can trust "
                         "what it sends: verify it with emoji from another session of yours (Sessions), or restore "
                         "with your recovery key (Sessions, Device verification)."});
  if (!others.empty())
    sink_(change::notice{id_, "New login. Was this you?",
                         std::format("Not verified: {}. Verify each from Sessions -- or sign it out, if it was not you.",
                                     std::ranges::to<std::string>(std::views::join_with(others, std::string_view(", "))))});
}
}  // namespace mux::proto::matrix::client

namespace mux::proto::matrix::client {
template <class Sink>
void account<Sink>::withheld_in(const loom::ev::m_room_key_withheld_content_t& content) {
  if (!content.session_id)
    return;
  using codes = loom::ev::m_room_key_withheld_content_t::code_values;
  const std::string said = spl::visit(
      spl::overloaded{
          [](codes::m_unverified) { return std::string("🔒 You don't have access to this message: the sender does not trust this session (it is not verified)."); },
          [](codes::m_blacklisted) { return std::string("🔒 You don't have access to this message: the sender has blocked this session."); },
          [](codes::m_unauthorised) { return std::string("🔒 You don't have access to this message."); },
          [](codes::m_history_not_shared) { return std::string("🔒 You don't have access to this message: it was sent before you joined."); },
          [](codes::m_unavailable) { return std::string("🔒 Unable to decrypt message: its key could not be sent."); },
          [](codes::m_no_olm) { return std::string("🔒 Unable to decrypt message: the sender could not reach this session."); },
          [](const std::string&) { return std::string("🔒 Unable to decrypt message."); }},
      content.code);
  withheld_.insert_or_assign(*content.session_id, said);
  if (const auto kept = undecrypted_.find(*content.session_id); kept != undecrypted_.end())
    for (const undecrypted_event& one : kept->second)
      sink_(change::message_added{message{.in = one.in,
                                          .id = one.event.event_id,
                                          .sender = one.event.sender,
                                          .at = std::chrono::sys_time<std::chrono::milliseconds>(
                                              std::chrono::milliseconds(one.event.origin_server_ts)),
                                          .body = {said, std::nullopt},
                                          .outgoing = one.event.sender == id_.address},
                                  placement::aside{}});
}
}  // namespace mux::proto::matrix::client

namespace mux::proto::matrix::client {
template <class Sink>
void account<Sink>::set_only_verified(bool on) {
  how_.only_verified = on;
}
}  // namespace mux::proto::matrix::client

namespace mux::proto::matrix::client {
template <class Sink>
void account<Sink>::accept_identity(std::string user) {
  if (!api_ || !crypto_)
    return;
  auto got = this->keys_of(user);
  const auto master = got ? crypto::master_of(*got, user) : std::nullopt;
  if (!master)
    return;
  crypto_->accept_master(user, *master);
  identity_changed_.erase(user);
  this->tell_trust(std::move(user));
}

// Read mentions shared between this account's sessions: a room's account
// data, net.mux.mentions_read (loom's net_mux_mentions_read_content_t) --
// seen, the event IDs, in the clear; or sealed, as Secret Storage seals a
// secret, under a key of its own named for the room.
using mentions_read = loom::ev::net_mux_mentions_read_content_t;
inline constexpr std::string_view kMentionsRead = "net.mux.mentions_read";
inline constexpr std::string_view kMentionsKeySecret = "net.mux.mentions_key";
// The most a room's list keeps: what each session keeps, a few times over.
inline constexpr std::size_t kMentionsShared = 2000;
[[nodiscard]] inline std::string mentions_sealed_for(std::string_view room) {
  return std::string(kMentionsRead) + ":" + std::string(room);
}

template <class Sink>
void account<Sink>::set_mentions_sharing(bool shared, bool sealed) {
  how_.mentions_shared = shared;
  how_.mentions_sealed = sealed;
  mentions_key_missing_told_ = false;
}
template <class Sink>
std::filesystem::path account<Sink>::mentions_key_file() const {
  return std::filesystem::path(this->kept_file()).concat(".mentions-key");
}
template <class Sink>
const std::optional<std::vector<std::uint8_t>>& account<Sink>::mentions_key() {
  if (!mentions_key_read_) {
    mentions_key_read_ = true;
    if (const auto opened = how_.vault->read_file(this->mentions_key_file()))
      if (auto bytes = crypto::from_base64(*opened); bytes && bytes->size() == 32)
        mentions_key_ = std::move(*bytes);
  }
  return mentions_key_;
}
template <class Sink>
void account<Sink>::keep_mentions_key(std::vector<std::uint8_t> key) {
  if (!how_.vault->write_file(this->mentions_key_file(), spl::bytes::base64_text(key), true))
    log(id_, "the key read mentions are sealed under could not be kept in {}", this->mentions_key_file().string());
  mentions_key_ = std::move(key);
  mentions_key_read_ = true;
}
template <class Sink>
void account<Sink>::mentions_from(const conversation_id& in, const mentions_read& read) {
  if (!how_.mentions_shared)
    return;
  std::vector<std::string> seen;
  if (read.seen) {
    seen = *read.seen;
  } else if (read.sealed) {
    const auto& key = this->mentions_key();
    if (!key)
      return;
    const crypto::sealed_secret sealed{.iv = read.sealed->iv, .ciphertext = read.sealed->ciphertext, .mac = read.sealed->mac};
    // What opens is plaintext come in as text: read once, here.
    const auto opened = crypto::detail::open_secret(*key, mentions_sealed_for(in.id), sealed);
    if (!opened)
      return;
    auto list = knot::try_read<std::vector<std::string>>(std::string_view(*opened));
    if (!list)
      return;
    seen = std::move(*list);
  }
  mentions_remote_[in.id].insert(seen.begin(), seen.end());
  if (!seen.empty())
    sink_(change::marks_seen{in, std::move(seen)});
}
template <class Sink>
void account<Sink>::share_marks_seen(std::string room, std::vector<std::string> seen) {
  if (!how_.mentions_shared || !api_)
    return;
  auto& remote = mentions_remote_[room];
  const auto news = [&](const std::string& one) { return !remote.contains(one); };
  if (std::ranges::none_of(seen, news))
    return;
  // What the server has, then what is news to it: the oldest let go past
  // the most a list keeps.
  std::vector<std::string> all(remote.begin(), remote.end());
  std::ranges::copy_if(seen, std::back_inserter(all), news);
  if (all.size() > kMentionsShared)
    all.erase(all.begin(), all.end() - static_cast<std::ptrdiff_t>(kMentionsShared));
  mentions_read body;
  if (how_.mentions_sealed) {
    const auto& key = this->mentions_key();
    if (!key) {
      if (!std::exchange(mentions_key_missing_told_, true))
        sink_(change::notice{id_, "Read mentions not synced",
                             "They are to be sealed, and this session has no key for them yet. Restore with the recovery "
                             "key (Accounts, Encryption) once, and they go from then on -- or turn sealing off."});
      return;
    }
    const crypto::sealed_secret sealed = crypto::detail::seal_secret(*key, mentions_sealed_for(room), knot::to_json_string(all));
    body.sealed = mentions_read::sealed_t{.iv = sealed.iv, .ciphertext = sealed.ciphertext, .mac = sealed.mac};
  } else {
    body.seen = all;
  }
  remote.insert(seen.begin(), seen.end());
  this->spawn_guarded([this, room = std::move(room), text = knot::to_json_string(body)] {
    if (!api_)
      return;
    if (!perform(*api_, loom::cs::set_account_data_per_room{.user_id = id_.address, .room_id = room,
                                                              .type = std::string(kMentionsRead), .body = knot::raw{text}}))
      log(id_, "the mentions read in {} could not be shared", room);
  });
}

}  // namespace mux::proto::matrix::client
