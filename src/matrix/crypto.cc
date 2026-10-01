// SPDX-License-Identifier: AGPL-3.0-only
// mux.matrix.crypto -- End-to-end encryption, as Matrix does it: Olm between
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
#include <rust/cxx.h>
#include <vodozemac/src/lib.rs.h>
export module mux.matrix.crypto;

import std;
import splice;
import knot;
import loom.ev;
import loom.cs.keys;
import mux.vault;
import mux.bytes;

export namespace mux::matrix::crypto {

// What is kept on disk: every pickle under the store's own key.
struct kept_file {
  std::string account;
  bool device_keys_uploaded = false;
  // Where the to-device stream went on from: kept with what it gave, or a
  // room key read and not kept would be asked of no one again.
  std::optional<std::string> to_device_since;
  std::map<std::string, std::vector<std::string>> olm;               // their curve25519 key -> sessions
  std::map<std::string, std::map<std::string, std::string>> megolm;  // room -> session id -> session
  // Who each Megolm session came from: the curve25519 key of the Olm session
  // its room key came over, and the ed25519 key the sender's payload claimed.
  struct origin {
    std::string sender_key;
    std::string ed25519;
    std::string sender;  // the user it came from
    // The device it came from, as the sender's device list named it and its
    // own key signed it; and whether the sender's self-signing key, under
    // their pinned master key, signed it too (review 4, H1). None: kept
    // before that was checked -- unverified.
    std::optional<std::string> device;
    std::optional<bool> cross_signed;
    friend consteval auto json_schema(knot::type<origin>) { return knot::schema<origin>(); }
  };
  std::map<std::string, origin> origins;  // session id -> where it came from
  // Each session's message indices read, and the event each was: the same
  // index in another event is a replay.
  std::map<std::string, std::map<std::uint32_t, std::string>> indices;
  // The rooms known to be encrypted: never plain again.
  std::set<std::string, std::less<>> encrypted_rooms;
  // Each user's master cross-signing key, as first seen (trust on first
  // use): a server that swaps it later does not make its own devices theirs.
  std::map<std::string, std::string> masters;
  friend consteval auto json_schema(knot::type<kept_file>) { return knot::schema<kept_file>(); }
};

// An Olm ciphertext for one device: its type (0 a pre-key message, which
// opens a session, 1 a normal one) and its body, unpadded base64.
struct olm_ciphertext {
  std::int64_t type = 0;
  std::string body;
  friend consteval auto json_schema(knot::type<olm_ciphertext>) { return knot::schema<olm_ciphertext>(); }
};

// What an Olm message decrypts to: an event of its own, for this device.
using olm_payload = loom::ev::basic_event<loom::ev::other_content>;
// And who it says it is from and for: checked against who sent it and who
// this is, before anything in it is believed (the spec's Olm payload).
struct olm_envelope {
  struct ed25519_key {
    std::string ed25519;
    friend consteval auto json_schema(knot::type<ed25519_key>) { return knot::schema<ed25519_key>(); }
  };
  std::string sender;
  std::string recipient;
  ed25519_key recipient_keys;
  ed25519_key keys;
  friend consteval auto json_schema(knot::type<olm_envelope>) { return knot::schema<olm_envelope>(); }
};
// A Megolm plaintext's room: the room it was sent in, checked against the
// room it came in.
struct megolm_room {
  std::string room_id;
  friend consteval auto json_schema(knot::type<megolm_room>) { return knot::schema<megolm_room>(); }
};
// What a Megolm message decrypts to: a room event's type and content.
using megolm_payload = loom::ev::basic_event<loom::ev::timeline_content>;

// A key signed, as keys/upload takes a one-time key.
struct signed_key {
  std::string key;
  std::map<std::string, std::map<std::string, std::string>> signatures;
  friend consteval auto json_schema(knot::type<signed_key>) { return knot::schema<signed_key>(); }
};
// What is signed of one: the key alone.
struct bare_key {
  std::string key;
  friend consteval auto json_schema(knot::type<bare_key>) { return knot::schema<bare_key>(); }
};
// This device's keys, as keys/upload takes them: without their signatures
// they are what is signed.
struct device_keys {
  std::vector<std::string> algorithms;
  std::string device_id;
  std::map<std::string, std::string> keys;
  std::string user_id;
  friend consteval auto json_schema(knot::type<device_keys>) { return knot::schema<device_keys>(); }
};

// Unpadded base64 (and padded): what Olm bodies and keys are written in.
[[nodiscard]] constexpr std::optional<std::vector<std::uint8_t>> from_base64(std::string_view text) {
  constexpr auto value_of = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
  };
  while (!text.empty() && text.back() == '=')
    text.remove_suffix(1);
  std::vector<std::uint8_t> out;
  out.reserve(text.size() * 3 / 4);
  std::uint32_t buffer = 0;
  int bits = 0;
  for (const char c : text) {
    const int v = value_of(c);
    if (v < 0)
      return std::nullopt;
    buffer = (buffer << 6) | static_cast<std::uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xFFu));
    }
  }
  return out;
}

// A device's keys as they sign themselves: all of them but the signatures
// and what is unsigned -- the rest of what the server gave kept as given,
// for a field this client does not know is signed as well.
struct device_signed_part {
  std::vector<std::string> algorithms;
  std::string device_id;
  std::map<std::string, std::string> keys;
  std::string user_id;
  knot::raw rest;
  friend consteval auto json_schema(knot::type<device_signed_part>) {
    return knot::schema<device_signed_part>().member<"rest">(knot::rest);
  }
};
// A cross-signing key as it is signed: all of it but its signatures.
template <class Key>
struct key_signed_part {
  std::string user_id;
  std::vector<typename Key::usage_item_t> usage;
  std::map<std::string, std::string> keys;
  knot::raw rest;
  friend consteval auto json_schema(knot::type<key_signed_part>) {
    return knot::schema<key_signed_part>().template member<"rest">(knot::rest);
  }
};
using keys_answer = loom::cs::query_keys::response_t;
using signatures_t = std::map<std::string, std::map<std::string, std::string>>;

// A signature checked: `signature` of `canonical` by the ed25519 `key`.
[[nodiscard]] inline bool signed_by(std::string_view canonical, std::string_view key, std::string_view signature) {
  try {
    auto public_key = vodozemac::types::ed25519_key_from_base64(rust::Str(key.data(), key.size()));
    auto made = vodozemac::types::ed25519_signature_from_base64(rust::Str(signature.data(), signature.size()));
    const auto bytes = mux::bytes::of(canonical);
    public_key->verify(rust::Slice<const std::uint8_t>(bytes.data(), bytes.size()), *made);
    return true;
  } catch (const rust::Error&) {
    return false;
  }
}
// The signature `user`'s key `key_id` made, among `all`.
[[nodiscard]] inline std::optional<std::string> signature_of(const signatures_t& all, const std::string& user, const std::string& key_id) {
  const auto by = all.find(user);
  if (by == all.end())
    return std::nullopt;
  const auto found = by->second.find(key_id);
  if (found == by->second.end())
    return std::nullopt;
  return found->second;
}
// A cross-signing key's one ed25519 key, where it is for `user` and used as
// `Usage`.
template <class Usage, class Key>
[[nodiscard]] std::optional<std::string> cross_key_of(const Key& key, const std::string& user) {
  const bool used = std::ranges::any_of(key.usage, [](const auto& each) {
    return splice::visit(splice::overloaded{[](const Usage&) { return true; }, [](const auto&) { return false; }}, each);
  });
  if (!used || key.user_id != user || key.keys.size() != 1)
    return std::nullopt;
  const auto& [id, value] = *key.keys.begin();
  if (id != "ed25519:" + value)
    return std::nullopt;
  return value;
}
// A cross-signing key signed by `signer` (of `user`): checked.
template <class Key>
[[nodiscard]] bool key_signed_by(const Key& key, const std::string& user, const std::string& signer) {
  if (!key.signatures)
    return false;
  const auto all = knot::try_read<signatures_t>(key.signatures->text);
  if (!all)
    return false;
  const auto signature = signature_of(*all, user, "ed25519:" + signer);
  const auto canonical = knot::to_canonical_json(key_signed_part<Key>{key.user_id, key.usage, key.keys, key.rest});
  return signature && canonical && signed_by(*canonical, signer, *signature);
}

// A device of a user, found by its curve25519 key in what /keys/query gave,
// and how far it is vouched for.
struct device_identity {
  std::string device_id;
  std::string ed25519;
  // Signed by the user's self-signing key, itself signed by their master
  // key -- the one pinned, where one was.
  bool cross_signed = false;
  std::optional<std::string> master;  // the master key seen, to be pinned
};
// None where no device of `user` has that key, or where the one that has it
// did not sign its own keys: the server's word alone is not a device.
[[nodiscard]] inline std::optional<device_identity> device_of(const keys_answer& got, const std::string& user,
                                                             const std::string& curve25519,
                                                             const std::optional<std::string>& pinned) {
  if (!got.device_keys)
    return std::nullopt;
  const auto devices = got.device_keys->find(user);
  if (devices == got.device_keys->end())
    return std::nullopt;
  const auto found = std::ranges::find_if(devices->second, [&](const auto& each) {
    const auto key = each.second.keys.find("curve25519:" + each.first);
    return key != each.second.keys.end() && key->second == curve25519;
  });
  if (found == devices->second.end())
    return std::nullopt;
  const auto& [id, info] = *found;
  if (info.device_id != id || info.user_id != user)
    return std::nullopt;
  const auto ed25519 = info.keys.find("ed25519:" + id);
  if (ed25519 == info.keys.end())
    return std::nullopt;
  const auto canonical = knot::to_canonical_json(device_signed_part{info.algorithms, info.device_id, info.keys, info.user_id, info.rest});
  if (!canonical)
    return std::nullopt;
  const auto self = signature_of(info.signatures, user, "ed25519:" + id);
  if (!self || !signed_by(*canonical, ed25519->second, *self))
    return std::nullopt;
  device_identity made{.device_id = id, .ed25519 = ed25519->second};
  // Cross-signed: master -> self-signing -> this device.
  if (!got.master_keys)
    return made;
  const auto master_entry = got.master_keys->find(user);
  if (master_entry == got.master_keys->end())
    return made;
  using master_t = keys_answer::cross_signing_key_t;
  const auto master = cross_key_of<master_t::usage_item_values::master>(master_entry->second, user);
  if (!master)
    return made;
  made.master = master;
  if (pinned && *pinned != *master)
    return made;  // not the master key first seen: nothing it signs is trusted
  if (!got.self_signing_keys)
    return made;
  const auto ssk_entry = got.self_signing_keys->find(user);
  if (ssk_entry == got.self_signing_keys->end())
    return made;
  using ssk_t = keys_answer::cross_signing_key_2_t;
  const auto ssk = cross_key_of<ssk_t::usage_item_values::self_signing>(ssk_entry->second, user);
  if (!ssk || !key_signed_by(ssk_entry->second, user, *master))
    return made;
  const auto by_ssk = signature_of(info.signatures, user, "ed25519:" + *ssk);
  made.cross_signed = by_ssk && signed_by(*canonical, *ssk, *by_ssk);
  return made;
}

// A room key read from an Olm message, not yet taken: where it came from is
// checked against the sender's devices first.
struct room_key_offer {
  loom::ev::m_room_key_content_t key;
  kept_file::origin from;
};
// A room event read, and whether the device it came from is cross-signed.
struct decrypted {
  loom::ev::basic_event<loom::ev::timeline_content> event;
  bool verified = false;
};

class olm_machine {
 public:
  // The machine of this device: read from its file, else made anew.
  static olm_machine open(std::filesystem::path store, std::string user_id, std::string device_id) {
    olm_machine made(std::move(store), std::move(user_id), std::move(device_id));
    made.key_ = made.pickle_key();
    std::error_code there;
    const bool exists = std::filesystem::exists(made.store_, there);
    const auto opened = exists ? mux::vault::the().read_file(made.store_) : std::optional<std::string>(std::string());
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
  [[nodiscard]] bool was_encrypted(std::string_view room) const { return kept_.encrypted_rooms.contains(room); }
  // Kept where it is new; saved, and an error where that fails (as any save).
  void remember_encrypted(std::string_view room) {
    if (kept_.encrypted_rooms.emplace(room).second)
      this->save();
  }
  [[nodiscard]] const std::optional<std::string>& to_device_since() const { return kept_.to_device_since; }
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
  [[nodiscard]] std::optional<room_key_offer> to_device(const std::string& sender,
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
      return std::nullopt;
    }
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
        splice::overloaded{[&](const loom::ev::m_room_key_content_t& key) -> std::optional<room_key_offer> {
                             return room_key_offer{key, kept_file::origin{.sender_key = *content.sender_key,
                                                                          .ed25519 = envelope->keys.ed25519,
                                                                          .sender = sender}};
                           },
                           [](const auto&) -> std::optional<room_key_offer> { return std::nullopt; }},
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
  // A user's master key as first seen; pinned the first time.
  [[nodiscard]] std::optional<std::string> pinned_master(const std::string& user) const {
    const auto found = kept_.masters.find(user);
    return found == kept_.masters.end() ? std::nullopt : std::optional<std::string>(found->second);
  }
  void pin_master(const std::string& user, const std::string& key) {
    if (kept_.masters.emplace(user, key).second)
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
    if (origin == kept_.origins.end() || origin->second.sender != sender ||
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
      if (const auto before = seen.find(clear.message_index); before != seen.end() && before->second != event_id)
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
      seen.emplace(clear.message_index, event_id);
      this->save();
      return decrypted{std::move(*read), origin->second.cross_signed.value_or(false)};
    } catch (const rust::Error&) {
      return std::nullopt;
    }
  }

 private:
  olm_machine(std::filesystem::path store, std::string user_id, std::string device_id)
      : store_(std::move(store)), user_id_(std::move(user_id)), device_id_(std::move(device_id)) {}

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
      const auto opened = mux::vault::the().read_file(path);
      if (!opened || opened->size() != key.size())
        throw std::runtime_error("the encryption store's key cannot be read: " + path.string());
      std::ranges::copy(mux::bytes::of(*opened), key.begin());
      return key;
    }
    // Not there, and a store there: its key lost -- an error, not a new key.
    if (std::filesystem::exists(store_, ignored))
      throw std::runtime_error("the encryption store's key is missing: " + path.string());
    // The system's own randomness (RAND_bytes), not std::random_device.
    const auto random = mux::vault::vault::random(key.size());
    std::ranges::copy(random, key.begin());
    if (!mux::vault::the().write_file(path, mux::bytes::text_of(key), true))
      throw std::runtime_error("the encryption store's key cannot be written: " + path.string());
    return key;
  }

  void save() {
    kept_.account = std::string((*account_)->pickle(key_));
    std::error_code ignored;
    std::filesystem::create_directories(store_.parent_path(), ignored);
    // Not kept is an error, not a word to no one: an account whose used
    // one-time key comes back at the next start opens a session again for a
    // pre-key message replayed to it. Through the vault: sealed where local
    // data is encrypted, the user's alone either way.
    if (!mux::vault::the().write_file(store_, knot::to_json_string(kept_), true))
      throw std::runtime_error("the encryption store cannot be written: " + store_.string());
  }

  [[nodiscard]] std::string sign(std::string_view canonical) const {
    const auto signed_bytes = mux::bytes::of(canonical);
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
    auto identity = vodozemac::types::curve_key_from_base64(their_key);
    auto config = vodozemac::olm::new_session_config_version_1();
    auto made = (*account_)->create_inbound_session(*config, *identity, message);
    sessions.push_back(std::string(made.session->pickle(key_)));
    // A few per device, the oldest going first: the server holds this
    // device's one-time keys, and could otherwise open sessions without end,
    // each written to the store (review 4, L1).
    constexpr std::size_t kMostSessions = 16;
    if (sessions.size() > kMostSessions)
      sessions.erase(sessions.begin(), sessions.end() - kMostSessions);
    this->save();
    return std::string(made.plaintext.begin(), made.plaintext.end());
  }

  // A room key: its Megolm session, kept for its room with where it came
  // from. Never over one already held under its id: a second key for the
  // same session, from whoever, would replace the first one's messages'.
  bool room_key(const loom::ev::m_room_key_content_t& key, kept_file::origin from) {
    if (key.room_id.empty() || kept_.origins.contains(key.session_id))
      return false;
    try {
      auto session_key = vodozemac::megolm::session_key_from_base64(key.session_key);
      auto session = vodozemac::megolm::new_inbound_group_session(*session_key);
      // The id the key says is the session's own: else it would be kept
      // under another session's name.
      if (std::string(session->session_id()) != key.session_id)
        return false;
      kept_.megolm[key.room_id][key.session_id] = std::string(session->pickle(key_));
      kept_.origins.emplace(key.session_id, std::move(from));
      this->save();
      return true;
    } catch (const rust::Error&) {
      return false;
    }
  }

  std::filesystem::path store_;
  std::string user_id_;
  std::string device_id_;
  std::array<std::uint8_t, 32> key_{};
  kept_file kept_;
  std::optional<rust::Box<vodozemac::olm::Account>> account_;
};

}  // namespace mux::matrix::crypto
