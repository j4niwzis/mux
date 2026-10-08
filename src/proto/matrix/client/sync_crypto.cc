// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix.client:sync_crypto -- End-to-end encryption begun, keys put on the server, emoji verification, broken sessions mended.
export module mux.proto.matrix.client:sync_crypto;

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
import :account;
import :sync;

// The members defined here are declared in :account, and exported there.
namespace mux::proto::matrix::client {

// End-to-end encryption.
//
// The machine of this device, once it has one: read from its store, else
// made, and its keys put on the server -- the device's identity keys once,
// signed, and one-time keys up to half of what it may hold.
template <class Sink>
void account<Sink>::start_crypto() {
  if (crypto_ || !how_.device_id || how_.crypto_store.empty())
    return;
  try {
    crypto_.emplace(olm_machine::open(vault_keeper{how_.vault}, how_.crypto_store, id_.address, *how_.device_id));
  } catch (const std::exception& failed) {
    log(id_, "encryption not started: {}", failed.what());
    return;
  }
  log(id_, "encryption: this device's curve25519 key is {}", crypto_->curve25519());
  // What Matrix's extension points and pages know of this account now: its
  // session among them.
  sink_(change::protocol_state_changed{
      id_, protocol_state_t{proto::matrix::state{.online = true, .device_id = std::string(crypto_->device_id()),
                                                 .ed25519 = std::string(crypto_->ed25519())}}});
  this->upload_keys(0);
  this->check_own_sessions();
}

// Keys put on the server: the device's, the first time; one-time keys where
// it has fewer than half of what it may hold.
template <class Sink>
void account<Sink>::upload_keys(std::int64_t on_server) {
  if (!crypto_ || !api_)
    return;
  const bool first = !crypto_->device_keys_uploaded();
  const auto now = std::chrono::steady_clock::now();
  if (!first && keys_uploaded_at_ && now - *keys_uploaded_at_ < std::chrono::minutes(1))
    return;
  keys_uploaded_at_ = now;
  loom::cs::upload_keys ask;
  if (first)
    if (auto signed_keys = crypto_->signed_device_keys())
      ask.body.device_keys = loom::cs::upload_keys::body_t::device_keys_t{
          .user_id = signed_keys->keys.user_id,
          .device_id = signed_keys->keys.device_id,
          .algorithms = signed_keys->keys.algorithms,
          .keys = signed_keys->keys.keys,
          .signatures = {{id_.address, {{"ed25519:" + signed_keys->keys.device_id, signed_keys->signature}}}}};
  if (auto made = crypto_->one_time_keys(on_server); !made.empty())
    ask.body.one_time_keys = std::move(made);
  if (!ask.body.device_keys && !ask.body.one_time_keys)
    return;
  auto done = perform(*api_, ask);
  if (!done) {
    log(id_, "keys not uploaded: {}", done.error().said());
    return;
  }
  try {
    crypto_->published(ask.body.device_keys.has_value());
  } catch (const std::exception& failed) {
    log(id_, "encryption stopped: {}", failed.what());
    crypto_.reset();
    return;
  }
  log(id_, "keys uploaded{}", ask.body.device_keys ? ", the device's with them" : "");
}

// ---- Emoji verification (SAS) ----------------------------------------------
//
// Over to-device messages, as Element does between devices: request, ready,
// start, accept, key both ways, the emoji compared by the user on both
// sides, MACs of each side's keys, done. A device is taken as verified only
// once the user said the emoji match and the other side's MAC of its own
// ed25519 key checks against the key /keys/query gives: a server that
// swapped keys is caught there (and could not have made the emoji match).
inline constexpr std::string_view kSasInfo = "MATRIX_KEY_VERIFICATION_SAS|";
inline constexpr std::string_view kMacInfo = "MATRIX_KEY_VERIFICATION_MAC";

template <class Sink>
bool account<Sink>::send_plain(std::string type, const std::string& user, const std::string& device, knot::raw content) {
  if (!api_)
    return false;
  std::map<std::string, std::map<std::string, knot::raw>> messages;
  messages[user][device] = std::move(content);
  return perform(*api_, loom::cs::send_to_device{.event_type = std::move(type),
                                                 .txn_id = this->transaction(),
                                                 .body = {.messages = std::move(messages)}})
      .has_value();
}
template <class Sink>
void account<Sink>::verification_said(const crypto::sas_state& state, verification_step_t step) {
  sink_(proto::matrix::verification_changed{id_, state.txn, state.their_user, state.their_device, std::move(step)});
}
template <class Sink>
void account<Sink>::cancel_verification(const std::string& txn, std::string code, std::string reason) {
  const auto found = verifications_.find(txn);
  if (found == verifications_.end())
    return;
  const crypto::sas_state& state = found->second;
  this->send_step(state, "m.key.verification.cancel",
                  loom::ev::m_key_verification_cancel_content_t{.reason = reason, .code = std::move(code)});
  this->verification_said(state, verification_step::cancelled{std::move(reason)});
  verifications_.erase(found);
}

template <class Sink>
void account<Sink>::verify_start(std::string user, std::optional<std::string> device) {
  this->spawn_guarded([this, user = std::move(user), device = std::move(device)] {
    if (!crypto_)
      return;
    crypto::sas_state state{.txn = this->transaction(), .their_user = user, .we_requested = true};
    const auto now =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const loom::ev::m_key_verification_request_content_t asked{
        .from_device = crypto_->device_id(), .transaction_id = state.txn, .methods = {"m.sas.v1"}, .timestamp = now};
    if (!this->send_plain("m.key.verification.request", user, device.value_or("*"), knot::raw{knot::to_json_string(asked)})) {
      this->verification_said(state, verification_step::cancelled{"The request could not be sent."});
      return;
    }
    this->verification_said(state, verification_step::waiting{});
    verifications_.insert_or_assign(state.txn, std::move(state));
  });
}
template <class Sink>
void account<Sink>::verification_in(const std::string& sender, const loom::ev::m_key_verification_request_content_t& content) {
  if (!crypto_ || !content.transaction_id || (sender == id_.address && content.from_device == crypto_->device_id()) ||
      !std::ranges::contains(content.methods, std::string_view("m.sas.v1")))
    return;
  // One asked of this side at a time: anyone may send requests, and each
  // would put the dialog up again.
  if (std::ranges::any_of(verifications_, [](const auto& one) { return !one.second.we_requested; }))
    return;
  crypto::sas_state state{.txn = *content.transaction_id, .their_user = sender, .their_device = content.from_device};
  this->verification_said(state, verification_step::asked{});
  verifications_.insert_or_assign(state.txn, std::move(state));
}
template <class Sink>
void account<Sink>::verify_accept(std::string txn) {
  this->spawn_guarded([this, txn = std::move(txn)] {
    const auto found = verifications_.find(txn);
    if (found == verifications_.end() || !crypto_)
      return;
    const loom::ev::m_key_verification_ready_content_t ready{
        .from_device = crypto_->device_id(), .transaction_id = txn, .methods = {"m.sas.v1"}};
    this->send_step(found->second, "m.key.verification.ready", ready);
    this->verification_said(found->second, verification_step::waiting{});
  });
}
template <class Sink>
void account<Sink>::verification_in(const std::string& sender, const loom::ev::m_key_verification_ready_content_t& content) {
  const auto found = content.transaction_id ? verifications_.find(*content.transaction_id) : verifications_.end();
  if (found == verifications_.end() || !found->second.we_requested || found->second.their_user != sender ||
      !found->second.their_device.empty())
    return;
  found->second.their_device = content.from_device;
  this->sas_start(found->second);
}
template <class Sink>
void account<Sink>::sas_start(crypto::sas_state& state) {
  using start_t = loom::ev::m_key_verification_start_m_sas_v1_content_t;
  using sas_kind = start_t::short_authentication_string_item_values;
  state.begin();
  const start_t start{.from_device = crypto_->device_id(),
                      .transaction_id = state.txn,
                      .method = start_t::method_values::m_sas_v1{},
                      .key_agreement_protocols = {"curve25519-hkdf-sha256"},
                      .hashes = {"sha256"},
                      .message_authentication_codes = {"hkdf-hmac-sha256.v2"},
                      .short_authentication_string = {sas_kind::decimal{}, sas_kind::emoji{}}};
  // Committed to as it is sent: with its transport's stamp.
  const auto canonical = knot::to_canonical_json(this->stamped(state, start));
  if (!canonical)
    return this->cancel_verification(state.txn, "m.unexpected_message", "The start could not be written.");
  state.start_canonical = *canonical;
  state.we_started = true;
  this->send_step(state, "m.key.verification.start", start);
}
template <class Sink>
void account<Sink>::verification_in(const std::string& sender, const loom::ev::m_key_verification_start_content_t& content) {
  const auto found = content.transaction_id ? verifications_.find(*content.transaction_id) : verifications_.end();
  if (found == verifications_.end() || found->second.their_user != sender || found->second.their_device != content.from_device)
    return;
  crypto::sas_state& state = found->second;
  // Both sides started: the one whose user and device sort first keeps its.
  if (state.we_started && std::pair(id_.address, crypto_->device_id()) < std::pair(sender, content.from_device))
    return;
  // The SAS method's offer, as loom read it with the start.
  const std::optional<crypto::sas_offer> offer =
      content.key_agreement_protocols && content.hashes && content.message_authentication_codes && content.short_authentication_string
          ? std::optional(crypto::sas_offer{.key_agreement_protocols = *content.key_agreement_protocols,
                                            .hashes = *content.hashes,
                                            .message_authentication_codes = *content.message_authentication_codes,
                                            .short_authentication_string = *content.short_authentication_string})
          : std::nullopt;
  if (content.method != "m.sas.v1" || !offer || !crypto::speaks(*offer))
    return this->cancel_verification(state.txn, "m.unknown_method", "Only emoji verification is spoken here.");
  // Committed to as its sender wrote it: in a room, with its reference (out
  // of the ciphertext where the room is encrypted) and no transaction ID.
  auto as_sent = content;
  if (state.room) {
    as_sent.transaction_id.reset();
    if (!as_sent.m_relates_to)
      as_sent.m_relates_to = loom::ev::def::verification_relates_to_t{
          .rel_type = loom::ev::def::verification_relates_to_t::rel_type_values::m_reference{}, .event_id = state.txn};
  }
  const auto canonical = knot::to_canonical_json(as_sent);
  if (!canonical)
    return this->cancel_verification(state.txn, "m.unexpected_message", "The start could not be read.");
  state.start_canonical = *canonical;
  state.we_started = false;
  state.begin();
  using accept_t = loom::ev::m_key_verification_accept_content_t;
  using sas_kind = accept_t::short_authentication_string_item_values;
  const accept_t accept{.transaction_id = state.txn,
                        .key_agreement_protocol = "curve25519-hkdf-sha256",
                        .hash = "sha256",
                        .message_authentication_code = "hkdf-hmac-sha256.v2",
                        .short_authentication_string = {sas_kind::decimal{}, sas_kind::emoji{}},
                        .commitment = crypto::commitment_of(state.our_key, state.start_canonical)};
  this->send_step(state, "m.key.verification.accept", accept);
  this->verification_said(state, verification_step::waiting{});
}
template <class Sink>
void account<Sink>::verification_in(const std::string& sender, const loom::ev::m_key_verification_accept_content_t& content) {
  const auto found = content.transaction_id ? verifications_.find(*content.transaction_id) : verifications_.end();
  if (found == verifications_.end() || found->second.their_user != sender || !found->second.we_started)
    return;
  found->second.commitment = content.commitment;
  this->send_step(found->second, "m.key.verification.key", loom::ev::m_key_verification_key_content_t{.key = found->second.our_key});
}
template <class Sink>
void account<Sink>::verification_in(const std::string& sender, const loom::ev::m_key_verification_key_content_t& content) {
  const auto found = content.transaction_id ? verifications_.find(*content.transaction_id) : verifications_.end();
  if (found == verifications_.end() || found->second.their_user != sender || found->second.their_key)
    return;
  crypto::sas_state& state = found->second;
  // Where this side started: their key is the one they committed to before
  // seeing ours -- else they chose it to make the emoji come out.
  if (state.we_started && (!state.commitment || crypto::commitment_of(content.key, state.start_canonical) != *state.commitment))
    return this->cancel_verification(state.txn, "m.mismatched_commitment", "Their key is not the one they committed to.");
  if (!state.establish(content.key))
    return this->cancel_verification(state.txn, "m.key_mismatch", "Their key is not a key.");
  if (!state.we_started)
    this->send_step(state, "m.key.verification.key", loom::ev::m_key_verification_key_content_t{.key = state.our_key});
  this->sas_show(state);
}
template <class Sink>
void account<Sink>::sas_show(crypto::sas_state& state) {
  const std::string ours = std::format("{}|{}|{}", id_.address, crypto_->device_id(), state.our_key);
  const std::string theirs = std::format("{}|{}|{}", state.their_user, state.their_device, *state.their_key);
  const std::string info = std::string(kSasInfo) + (state.we_started ? ours + "|" + theirs : theirs + "|" + ours) + "|" + state.txn;
  this->verification_said(state, verification_step::compare{state.emoji(info)});
}
template <class Sink>
void account<Sink>::verify_confirm(std::string txn, bool match) {
  this->spawn_guarded([this, txn = std::move(txn), match] {
    const auto found = verifications_.find(txn);
    if (found == verifications_.end() || !found->second.established)
      return;
    if (!match)
      return this->cancel_verification(txn, "m.mismatched_sas", "The emoji did not match.");
    found->second.we_confirmed = true;
    this->sas_send_mac(found->second);
    if (found->second.their_mac)
      this->sas_check_mac(found->second);
    else
      this->verification_said(found->second, verification_step::waiting{});
  });
}
template <class Sink>
void account<Sink>::sas_send_mac(crypto::sas_state& state) {
  const std::string base = std::string(kMacInfo) + id_.address + crypto_->device_id() + state.their_user + state.their_device + state.txn;
  std::map<std::string, std::string> macs;
  const std::string device_key = "ed25519:" + crypto_->device_id();
  macs.emplace(device_key, state.mac(crypto_->ed25519(), base + device_key));
  if (const auto master = crypto_->pinned_master(id_.address)) {
    const std::string master_key = "ed25519:" + *master;
    macs.emplace(master_key, state.mac(*master, base + master_key));
  }
  const std::string ids = std::ranges::to<std::string>(std::views::join_with(std::views::keys(macs), ','));
  this->send_step(state, "m.key.verification.mac",
                  loom::ev::m_key_verification_mac_content_t{.mac = std::move(macs), .keys = state.mac(ids, base + "KEY_IDS")});
}
template <class Sink>
void account<Sink>::verification_in(const std::string& sender, const loom::ev::m_key_verification_mac_content_t& content) {
  const auto found = content.transaction_id ? verifications_.find(*content.transaction_id) : verifications_.end();
  if (found == verifications_.end() || found->second.their_user != sender || !found->second.established)
    return;
  found->second.their_mac = content.mac;
  found->second.their_keys_mac = content.keys;
  if (found->second.we_confirmed)
    this->sas_check_mac(found->second);
}
template <class Sink>
void account<Sink>::sas_check_mac(crypto::sas_state& state) {
  const std::string txn = state.txn;
  const std::string base = std::string(kMacInfo) + state.their_user + state.their_device + id_.address + crypto_->device_id() + txn;
  const std::string ids = std::ranges::to<std::string>(std::views::join_with(std::views::keys(*state.their_mac), ','));
  if (!state.mac_ok(ids, base + "KEY_IDS", state.their_keys_mac))
    return this->cancel_verification(txn, "m.key_mismatch", "The keys they listed are not the ones they sent.");
  if (!api_)
    return this->cancel_verification(txn, "m.key_mismatch", "Not connected.");
  auto got = this->keys_of(state.their_user);
  if (!got || !got->device_keys)
    return this->cancel_verification(txn, "m.key_mismatch", "Their keys could not be fetched.");
  const auto user = got->device_keys->find(state.their_user);
  if (user == got->device_keys->end())
    return this->cancel_verification(txn, "m.key_mismatch", "Their devices are not listed.");
  const auto device = user->second.find(state.their_device);
  if (device == user->second.end())
    return this->cancel_verification(txn, "m.key_mismatch", "Their device is not listed.");
  const auto ed25519 = device->second.keys.find("ed25519:" + state.their_device);
  if (ed25519 == device->second.keys.end())
    return this->cancel_verification(txn, "m.key_mismatch", "Their device has no signing key.");
  const auto master = crypto::master_of(*got, state.their_user);
  bool device_ok = false, master_ok = false;
  for (const auto& [key_id, mac] : *state.their_mac) {
    if (key_id == "ed25519:" + state.their_device)
      device_ok = state.mac_ok(ed25519->second, base + key_id, mac);
    else if (master && key_id == "ed25519:" + *master)
      master_ok = state.mac_ok(*master, base + key_id, mac);
  }
  if (!device_ok)
    return this->cancel_verification(txn, "m.key_mismatch", "Their device's key is not the one the server lists.");
  crypto_->mark_verified(state.their_user, ed25519->second);
  if (master_ok)
    crypto_->verify_master(state.their_user, *master);
  // Signed, where this device has the cross-signing keys: one's own device
  // with the self-signing key, another's master key with the user-signing one.
  if (state.their_user == id_.address)
    this->cross_sign_device(device->second);
  else if (master_ok && got->master_keys)
    if (const auto theirs = got->master_keys->find(state.their_user); theirs != got->master_keys->end())
      this->cross_sign_user(state.their_user, theirs->second);
  this->send_step(state, "m.key.verification.done", loom::ev::m_key_verification_done_content_t{});
  this->verification_said(state, verification_step::done{});
  this->tell_trust(state.their_user);
  // One's own other session, verified: the cross-signing keys asked of it,
  // where this device has none -- with them it signs itself.
  if (state.their_user == id_.address && !crypto_->cross_signing_keys())
    this->request_secrets(state.their_device);
  verifications_.erase(txn);
}
template <class Sink>
void account<Sink>::verification_in(const std::string& sender, const loom::ev::m_key_verification_cancel_content_t& content) {
  const auto found = content.transaction_id ? verifications_.find(*content.transaction_id) : verifications_.end();
  if (found == verifications_.end() || found->second.their_user != sender)
    return;
  this->verification_said(found->second, verification_step::cancelled{content.reason.empty() ? content.code : content.reason});
  verifications_.erase(found);
}
// In a room: the request, a message to this user from a device of theirs.
template <class Sink>
void account<Sink>::verification_request_in_room(const conversation_id& in, const loom::ev::timeline_event& one,
                                                 const crypto::room_request_fields& fields) {
  if (!crypto_ || fields.to != id_.address || one.sender == id_.address ||
      !std::ranges::contains(fields.methods, std::string_view("m.sas.v1")) ||
      std::ranges::any_of(verifications_, [](const auto& each) { return !each.second.we_requested; }))
    return;
  crypto::sas_state state{.txn = one.event_id, .room = in.id, .their_user = one.sender, .their_device = fields.from_device};
  this->verification_said(state, verification_step::asked{});
  verifications_.insert_or_assign(state.txn, std::move(state));
}
// Its steps: read by their type as the to-device ones are, the request they
// refer to standing for the transaction. Shown nowhere in the timeline.
template <class Sink>
bool account<Sink>::verification_in_room(const conversation_id& in, const loom::ev::timeline_event& one, placement_t where) {
  // The step, its reference made its transaction -- for the room it came in
  // only. An old one -- in the first sync's history -- is not one to answer.
  // Asked here, outside the step's lambda, as before: an inline member
  // reached only from inside a generic lambda was not emitted where this is
  // explicitly instantiated.
  const bool answerable = (!telling_history_ && placed_as_news(where)) && one.sender != id_.address && crypto_;
  const auto take = [&](auto content) {
    if (!answerable)
      return true;
    const auto reference = content.m_relates_to && content.m_relates_to->event_id ? content.m_relates_to->event_id
                                                                                   : outer_reference_;
    if (!reference)
      return true;
    const auto found = verifications_.find(*reference);
    if (found == verifications_.end() || found->second.room != in.id)
      return true;
    content.transaction_id = found->first;
    this->verification_in(one.sender, content);
    return true;
  };
  return spl::visit(spl::overloaded{[&](const loom::ev::m_key_verification_ready_content_t& step) { return take(step); },
                                    [&](const loom::ev::m_key_verification_start_content_t& step) { return take(step); },
                                    [&](const loom::ev::m_key_verification_accept_content_t& step) { return take(step); },
                                    [&](const loom::ev::m_key_verification_key_content_t& step) { return take(step); },
                                    [&](const loom::ev::m_key_verification_mac_content_t& step) { return take(step); },
                                    [&](const loom::ev::m_key_verification_cancel_content_t& step) { return take(step); },
                                    [](const loom::ev::m_key_verification_done_content_t&) { return true; },
                                    [](const auto&) { return false; }},
                    one.content.data());
}

template <class Sink>
void account<Sink>::verify_cancel(std::string txn) {
  this->spawn_guarded([this, txn = std::move(txn)] { this->cancel_verification(txn, "m.user", "Cancelled."); });
}

template <class Sink>
void account<Sink>::mend_session(const std::string& user, const std::string& curve25519) {
  if (!api_ || !crypto_)
    return;
  const auto now = std::chrono::steady_clock::now();
  if (const auto last = mended_at_.find(curve25519); last != mended_at_.end() && now - last->second < std::chrono::hours(1))
    return;
  mended_at_.insert_or_assign(curve25519, now);
  auto got = this->keys_of(user);
  if (!got)
    return;
  const auto theirs = crypto::recipients_of(*got, user, crypto_->pinned_master(user), std::string_view(), crypto_->verified_keys(user));
  const auto device = std::ranges::find(theirs, curve25519, &crypto::recipient::curve25519);
  if (device == theirs.end())
    return;  // not a device of theirs that passes the checks: nothing sent to it
  loom::cs::claim_keys claim;
  claim.body.one_time_keys[user][device->device_id] = "signed_curve25519";
  auto claimed = perform(*api_, claim);
  if (!claimed)
    return;
  std::optional<std::string> key;
  if (const auto by_user = claimed->one_time_keys.find(user); by_user != claimed->one_time_keys.end())
    if (const auto by_device = by_user->second.find(device->device_id); by_device != by_user->second.end())
      for (const auto& [id, raw] : by_device->second)
        if (!key)
          key = crypto::one_time_key_of(raw, *device);
  if (!key)
    return;
  const auto sealed = crypto_->mend(*device, *key);
  if (!sealed)
    return;
  std::map<std::string, std::map<std::string, knot::raw>> messages;
  messages[user][device->device_id] = knot::raw{
      knot::to_json_string(crypto::olm_content{.sender_key = crypto_->curve25519(), .ciphertext = {{curve25519, *sealed}}})};
  if (perform(*api_, loom::cs::send_to_device{.event_type = "m.room.encrypted", .txn_id = this->transaction(),
                                              .body = {.messages = std::move(messages)}}))
    log(id_, "a broken session with {}'s device {} mended", user, device->device_id);
}

template <class Sink>
void account<Sink>::upload_fallback_key() {
  if (!crypto_ || !api_)
    return;
  const auto now = std::chrono::steady_clock::now();
  if (fallback_uploaded_at_ && now - *fallback_uploaded_at_ < std::chrono::hours(1))
    return;
  fallback_uploaded_at_ = now;
  loom::cs::upload_keys ask;
  ask.body.fallback_keys = crypto_->fresh_fallback_key();
  if (ask.body.fallback_keys->empty())
    return;
  if (auto done = perform(*api_, ask); !done) {
    log(id_, "fallback key not uploaded: {}", done.error().said());
    return;
  }
  crypto_->published(false);
  log(id_, "fallback key uploaded");
}

}  // namespace mux::proto::matrix::client
