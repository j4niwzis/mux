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
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/core_names.h>
#include <openssl/params.h>
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
    // Imported from a key file: who sent with it is not said there, so it
    // reads any sender's messages whose sender_key matches -- marked as
    // from an unverified device, as the file is only as good as its source.
    std::optional<bool> imported;
    friend consteval auto json_schema(knot::type<origin>) { return knot::schema<origin>(); }
  };
  std::map<std::string, origin> origins;  // session id -> where it came from
  // Each session's message indices read, and the event each was: the same
  // index in another event is a replay.
  struct read_index {
    std::uint32_t index = 0;
    std::string event;
    friend consteval auto json_schema(knot::type<read_index>) { return knot::schema<read_index>(); }
  };
  std::map<std::string, std::vector<read_index>> indices;
  // The rooms known to be encrypted: never plain again.
  // Optional, as every field added after the store was first written: knot
  // requires the rest, and a store kept before it came would not open.
  // A set, as an object knot reads: each room to true.
  std::optional<std::map<std::string, bool, std::less<>>> encrypted_rooms;
  // Each user's master cross-signing key, as first seen (trust on first
  // use): a server that swaps it later does not make its own devices theirs.
  std::optional<std::map<std::string, std::string>> masters;  // optional, as encrypted_rooms
  // Each room's Megolm session this device sends with now: its pickle, when
  // it was made, how many messages it has sent, and the devices it was given
  // to (by user, their IDs). Optional, as encrypted_rooms.
  struct outbound_session {
    std::string pickle;
    std::int64_t created_ms = 0;
    std::int64_t messages = 0;
    std::map<std::string, std::vector<std::string>> shared_with;
    friend consteval auto json_schema(knot::type<outbound_session>) { return knot::schema<outbound_session>(); }
  };
  std::optional<std::map<std::string, outbound_session>> outbound;
  // Devices verified here, by comparing emoji (SAS): by user, their ed25519
  // keys; and users' master keys verified so. Optional, as encrypted_rooms.
  std::optional<std::map<std::string, std::vector<std::string>>> verified;
  std::optional<std::map<std::string, std::string>> verified_masters;
  // This user's cross-signing private keys, where this device made or got
  // them: sealed under the store's key (AES-256-GCM), base64. Optional, as
  // encrypted_rooms.
  std::optional<std::string> cross_signing;
  // The server-side key backup this device writes to: its version, and its
  // private key sealed as cross_signing is; and the sessions in it already.
  // Optional, as encrypted_rooms.
  std::optional<std::string> backup_version;
  std::optional<std::string> backup_key;
  std::optional<std::map<std::string, bool, std::less<>>> backed_up;  // a set, as encrypted_rooms
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
// A fallback key, as it is signed and uploaded: the key, and that it is one.
struct fallback_bare {
  bool fallback = true;
  std::string key;
  friend consteval auto json_schema(knot::type<fallback_bare>) { return knot::schema<fallback_bare>(); }
};
struct signed_fallback {
  bool fallback = true;
  std::string key;
  std::map<std::string, std::map<std::string, std::string>> signatures;
  friend consteval auto json_schema(knot::type<signed_fallback>) { return knot::schema<signed_fallback>(); }
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
    const auto bytes = mux::bytes::buffer_of(mux::bytes::of(canonical));  // the Slice is read whole
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
  // A device whose ID is one of the user's cross-signing keys is no device:
  // signatures are found by "ed25519:<ID>", and one key standing for the
  // other is how matrix-js-sdk's CVE-2022-39250 confused them.
  if (id == *master || id == *ssk)
    return std::nullopt;
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
// A secret come over Olm (m.secret.send): the request it answers, the
// secret, and who sent it -- the user and the ed25519 key of the device the
// Olm payload says it came from.
struct secret_got {
  std::string request_id;
  std::string secret;
  std::string sender;
  std::string ed25519;
};
using to_device_said = splice::variant<room_key_offer, secret_got>;
// A room event read, and whether the device it came from is cross-signed.
struct decrypted {
  loom::ev::basic_event<loom::ev::timeline_content> event;
  bool verified = false;
  // From a session imported (a key file, the key backup): who sent with it
  // was not said, so the sender is taken only where a device of theirs has
  // this curve25519 key -- checked by the account against /keys/query.
  std::optional<std::string> imported_sender_key;
};


// A device a room's key may go to: who, which, and its keys -- found and
// checked as device_of() checks one.
struct recipient {
  std::string user;
  std::string device_id;
  std::string curve25519;
  std::string ed25519;
  friend bool operator==(const recipient&, const recipient&) = default;
};
// The devices of a user that a room's key goes to: every one that signed
// its own keys -- and, where the user has cross-signing, only those their
// self-signing key signed too, so that a device the server made up for them
// gets nothing. Never this device itself.
[[nodiscard]] inline std::vector<recipient> recipients_of(const keys_answer& got, const std::string& user,
                                                         const std::optional<std::string>& pinned,
                                                         std::string_view own_device,
                                                         const std::vector<std::string>& verified_keys = {},
                                                         bool strict = false) {
  std::vector<recipient> out;
  if (!got.device_keys)
    return out;
  const auto devices = got.device_keys->find(user);
  if (devices == got.device_keys->end())
    return out;
  // Strict -- cross-signed or verified here, nothing else -- where the user
  // has cross-signing, or had it (a master key pinned: a server hiding it now
  // would have every self-signed device taken, its own made-up one too), or
  // is this account itself (its own devices read all it writes; a device the
  // server adds to it gets nothing until verified).
  const bool cross_signing = (got.master_keys && got.master_keys->contains(user)) || pinned.has_value() || strict;
  for (const auto& [id, info] : devices->second) {
    if (id == own_device && info.user_id == user)
      continue;
    const auto curve = info.keys.find("curve25519:" + id);
    if (curve == info.keys.end())
      continue;
    const auto device = device_of(got, user, curve->second, pinned);
    // Cross-signed, or verified here by comparing emoji.
    if (!device || device->device_id != id ||
        (cross_signing && !device->cross_signed && !std::ranges::contains(verified_keys, device->ed25519)))
      continue;
    out.push_back(recipient{user, id, curve->second, device->ed25519});
  }
  return out;
}

// What goes over Olm to give a device a room's key (the spec's m.room_key
// in an Olm payload).
struct room_key_payload {
  struct content_t {
    std::string algorithm = "m.megolm.v1.aes-sha2";
    std::string room_id;
    std::string session_id;
    std::string session_key;
    friend consteval auto json_schema(knot::type<content_t>) { return knot::schema<content_t>(); }
  };
  std::string type = "m.room_key";
  content_t content;
  std::string sender;
  std::string recipient;
  olm_envelope::ed25519_key recipient_keys;
  olm_envelope::ed25519_key keys;
  friend consteval auto json_schema(knot::type<room_key_payload>) { return knot::schema<room_key_payload>(); }
};
// A secret, over Olm, to one of this user's own devices that asked for it
// (the spec's m.secret.send): the request's id and the secret.
struct secret_send_payload {
  struct content_t {
    std::string request_id;
    std::string secret;
    friend consteval auto json_schema(knot::type<content_t>) { return knot::schema<content_t>(); }
  };
  std::string type = "m.secret.send";
  content_t content;
  std::string sender;
  std::string recipient;
  olm_envelope::ed25519_key recipient_keys;
  olm_envelope::ed25519_key keys;
  friend consteval auto json_schema(knot::type<secret_send_payload>) { return knot::schema<secret_send_payload>(); }
};
// The secrets shared between one's own devices, by what they are -- read
// from their names once, here, by the table below.
namespace secret_name {
struct master {};
struct self_signing {};
struct user_signing {};
struct backup {};
}  // namespace secret_name
using secret_name_t = splice::variant<secret_name::master, secret_name::self_signing, secret_name::user_signing, secret_name::backup>;
inline constexpr std::array<std::pair<std::string_view, std::size_t>, 4> kSecretNames{{{"m.cross_signing.master", 0},
                                                                                       {"m.cross_signing.self_signing", 1},
                                                                                       {"m.cross_signing.user_signing", 2},
                                                                                       {"m.megolm_backup.v1", 3}}};
[[nodiscard]] inline secret_name_t secret_name_at(std::size_t at) {
  switch (at) {
    case 0: return secret_name::master{};
    case 1: return secret_name::self_signing{};
    case 2: return secret_name::user_signing{};
    default: return secret_name::backup{};
  }
}
[[nodiscard]] inline std::optional<secret_name_t> secret_name_of(std::string_view name) {
  const auto found = std::ranges::find(kSecretNames, name, &std::pair<std::string_view, std::size_t>::first);
  return found == kSecretNames.end() ? std::nullopt : std::optional<secret_name_t>(secret_name_at(found->second));
}
// The secrets of one's own, as they come: the three cross-signing keys,
// kept until all are here.
struct secrets_gathered {
  std::optional<std::string> master, self_signing, user_signing;
};
// A secret asked of this user's other devices (m.secret.request), sent as
// it is, unencrypted, as the spec has it.
struct secret_request_part {
  std::string name;
  std::string action = "request";
  std::string requesting_device_id;
  std::string request_id;
  friend consteval auto json_schema(knot::type<secret_request_part>) { return knot::schema<secret_request_part>(); }
};
// What goes over a new Olm session to mend a broken one (the spec's
// m.dummy): nothing, but a pre-key message, which the other side opens a
// session with.
struct dummy_payload {
  struct content_t {
    friend consteval auto json_schema(knot::type<content_t>) { return knot::schema<content_t>(); }
  };
  std::string type = "m.dummy";
  content_t content;
  std::string sender;
  std::string recipient;
  olm_envelope::ed25519_key recipient_keys;
  olm_envelope::ed25519_key keys;
  friend consteval auto json_schema(knot::type<dummy_payload>) { return knot::schema<dummy_payload>(); }
};
// An m.room.encrypted to a device: Olm ciphertexts by their curve25519 key.
struct olm_content {
  std::string algorithm = "m.olm.v1.curve25519-aes-sha2";
  std::string sender_key;
  std::map<std::string, olm_ciphertext> ciphertext;
  friend consteval auto json_schema(knot::type<olm_content>) { return knot::schema<olm_content>(); }
};
// A room event as Megolm takes it in: its type, its content as it was made,
// and its room.
struct megolm_plaintext {
  std::string type;
  knot::raw content;
  std::string room_id;
  friend consteval auto json_schema(knot::type<megolm_plaintext>) { return knot::schema<megolm_plaintext>(); }
};
// An m.room.encrypted in a room: Megolm's ciphertext, and what says where
// it is from. A relation stays in the clear, as the spec has it, for the
// server to group edits and threads by.
struct megolm_content {
  std::string algorithm = "m.megolm.v1.aes-sha2";
  std::string ciphertext;
  std::string device_id;
  std::string sender_key;
  std::string session_id;
  std::optional<knot::raw> relates_to;
  friend consteval auto json_schema(knot::type<megolm_content>) {
    return knot::schema<megolm_content>().member<"relates_to">(knot::key("m.relates_to"));
  }
};
// How long a room's session may send, as its m.room.encryption says --
// bounded, for that state is the server's to write: a week and a hundred
// messages at most, a minute and one message at least.
struct rotation {
  std::int64_t most_ms = 7LL * 24 * 3600 * 1000;
  std::int64_t most_messages = 100;
};
[[nodiscard]] constexpr rotation rotation_of(std::optional<std::int64_t> ms, std::optional<std::int64_t> messages) {
  constexpr rotation widest;
  return {.most_ms = std::clamp<std::int64_t>(ms.value_or(widest.most_ms), 60LL * 1000, widest.most_ms),
          .most_messages = std::clamp<std::int64_t>(messages.value_or(widest.most_messages), 1, widest.most_messages)};
}
// A user's master cross-signing key, as /keys/query gave it: to be pinned
// the first time it is seen.
[[nodiscard]] inline std::optional<std::string> master_of(const keys_answer& got, const std::string& user) {
  if (!got.master_keys)
    return std::nullopt;
  const auto found = got.master_keys->find(user);
  if (found == got.master_keys->end())
    return std::nullopt;
  return cross_key_of<keys_answer::cross_signing_key_t::usage_item_values::master>(found->second, user);
}
// A one-time key a device's owner claimed for it: its key, where its
// signature is that device's own -- else the server's to have made up.
[[nodiscard]] inline std::optional<std::string> one_time_key_of(const knot::raw& claimed, const recipient& of) {
  const auto read = knot::try_read<signed_key>(claimed.text);
  if (!read)
    return std::nullopt;
  const auto canonical = knot::to_canonical_json(bare_key{read->key});
  const auto signature = signature_of(read->signatures, of.user, "ed25519:" + of.device_id);
  if (!canonical || !signature || !signed_by(*canonical, of.ed25519, *signature))
    return std::nullopt;
  return read->key;
}
// What of a content stays in the clear: its relation.
struct relation_part {
  std::optional<knot::raw> relates_to;
  friend consteval auto json_schema(knot::type<relation_part>) {
    return knot::schema<relation_part>().member<"relates_to">(knot::key("m.relates_to"));
  }
};

// An encrypted file, as an event carries it (the spec's EncryptedFile): where
// its ciphertext is, its AES-256-CTR key as a JWK, its counter's start, and
// the ciphertext's SHA-256 -- the server keeps only ciphertext, under no name.
struct jwk {
  std::string kty = "oct";
  std::vector<std::string> key_ops{"encrypt", "decrypt"};
  std::string alg = "A256CTR";
  std::string k;  // unpadded base64url
  bool ext = true;
  friend consteval auto json_schema(knot::type<jwk>) { return knot::schema<jwk>(); }
};
struct encrypted_file {
  std::string url;
  jwk key;
  std::string iv;
  std::map<std::string, std::string> hashes;
  std::string v = "v2";
  friend consteval auto json_schema(knot::type<encrypted_file>) { return knot::schema<encrypted_file>(); }
};
// The part of a message's content that names it.
struct file_part {
  encrypted_file file;
  friend consteval auto json_schema(knot::type<file_part>) { return knot::schema<file_part>(); }
};

namespace detail {
template <class Bytes>
concept byte_range = std::ranges::input_range<Bytes> && std::same_as<std::ranges::range_value_t<Bytes>, std::uint8_t>;
// A cipher run over any range of bytes, a piece at a time (CTR, CBC): its
// key and counter taken as exactly the sizes they are.
template <byte_range Key, byte_range Iv, byte_range In>
[[nodiscard]] inline std::optional<std::vector<std::uint8_t>> cipher(const EVP_CIPHER* kind, bool encrypt, Key&& key, Iv&& iv,
                                                                     In&& in) {
  const auto k = mux::bytes::exactly<32>(std::forward<Key>(key));
  const auto v = mux::bytes::exactly<16>(std::forward<Iv>(iv));
  if (!k || !v)
    return std::nullopt;
  std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
  if (!ctx || EVP_CipherInit_ex(ctx.get(), kind, nullptr, k->data(), v->data(), encrypt ? 1 : 0) != 1)
    return std::nullopt;
  std::vector<std::uint8_t> out;
  bool fine = true;
  mux::bytes::in_pieces(std::forward<In>(in), [&](std::span<const std::uint8_t> piece) {
    const std::size_t at = out.size();
    out.resize(at + piece.size() + 32);
    int len = 0;
    fine = fine && EVP_CipherUpdate(ctx.get(), out.data() + at, &len, piece.data(), static_cast<int>(piece.size())) == 1;
    out.resize(at + static_cast<std::size_t>(len));
  });
  const std::size_t at = out.size();
  out.resize(at + 32);
  int tail = 0;
  if (!fine || EVP_CipherFinal_ex(ctx.get(), out.data() + at, &tail) != 1)
    return std::nullopt;
  out.resize(at + static_cast<std::size_t>(tail));
  return out;
}
// AES-256-CTR, either way: the same operation.
template <byte_range Key, byte_range Iv, byte_range In>
[[nodiscard]] inline std::optional<std::vector<std::uint8_t>> aes_ctr(Key&& key, Iv&& iv, In&& in) {
  return cipher(EVP_aes_256_ctr(), true, std::forward<Key>(key), std::forward<Iv>(iv), std::forward<In>(in));
}
// SHA-256 of any ranges of bytes, one after another.
template <byte_range... Parts>
[[nodiscard]] inline std::array<std::uint8_t, 32> sha256(Parts&&... parts) {
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> md(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
  bool fine = md && EVP_DigestInit_ex(md.get(), EVP_sha256(), nullptr) == 1;
  (mux::bytes::in_pieces(std::forward<Parts>(parts), [&](std::span<const std::uint8_t> piece) {
     fine = fine && EVP_DigestUpdate(md.get(), piece.data(), piece.size()) == 1;
   }),
   ...);
  std::array<std::uint8_t, 32> out{};
  unsigned int size = 0;
  if (!fine || EVP_DigestFinal_ex(md.get(), out.data(), &size) != 1 || size != out.size())
    throw std::runtime_error("SHA-256 failed");
  return out;
}
[[nodiscard]] inline std::string url_safe(std::string text) {
  return text | std::views::transform([](char c) { return c == '+' ? '-' : c == '/' ? '_' : c; }) | std::ranges::to<std::string>();
}
}  // namespace detail

// A file sealed for an encrypted room: its ciphertext, and what opens it --
// its URL filled in once it is uploaded.
struct sealed_file {
  std::vector<std::uint8_t> bytes;
  encrypted_file info;
};
template <detail::byte_range Plain>
[[nodiscard]] inline sealed_file seal_file(Plain&& plain) {
  const auto key = mux::vault::vault::random(32);
  // The counter's start: 8 random bytes, then a zero counter (as the spec
  // has it, so that no file is long enough to wrap it).
  auto iv = mux::vault::vault::random(8);
  iv.resize(16, 0);
  auto ciphertext = detail::aes_ctr(key, iv, std::forward<Plain>(plain));
  if (!ciphertext)
    throw std::runtime_error("the file could not be encrypted");
  const auto hash = detail::sha256(*ciphertext);
  return sealed_file{.bytes = std::move(*ciphertext),
                     .info = encrypted_file{.key = jwk{.k = detail::url_safe(mux::bytes::base64_text(key))},
                                            .iv = mux::bytes::base64_text(iv),
                                            .hashes = {{"sha256", mux::bytes::base64_text(hash)}}}};
}
// A file opened: none where it is not what its event says -- its hash
// another, its key or counter not AES-256-CTR's.
// The sealed bytes walked twice -- hashed, then decrypted -- never copied.
template <class Sealed>
  requires detail::byte_range<Sealed> && std::ranges::forward_range<Sealed>
[[nodiscard]] inline std::optional<std::vector<std::uint8_t>> open_file(Sealed&& sealed, const encrypted_file& info) {
  if (info.key.alg != "A256CTR" || info.key.kty != "oct")
    return std::nullopt;
  const auto key = from_base64(info.key.k);
  const auto iv = from_base64(info.iv);
  const auto said = info.hashes.find("sha256");
  if (!key || !iv || said == info.hashes.end())
    return std::nullopt;
  const auto expected = from_base64(said->second);
  const auto hash = detail::sha256(sealed);
  if (!expected || !std::ranges::equal(*expected, hash))
    return std::nullopt;
  return detail::aes_ctr(*key, *iv, sealed);
}

// Room keys as Element exports them (the spec's key export format): each
// session's room, where it came from, and its key from the first message it
// can read.
struct exported_session {
  std::string algorithm = "m.megolm.v1.aes-sha2";
  std::vector<std::string> forwarding_curve25519_key_chain;
  std::string room_id;
  std::string sender_key;
  std::map<std::string, std::string> sender_claimed_keys;
  std::string session_id;
  std::string session_key;
  friend consteval auto json_schema(knot::type<exported_session>) { return knot::schema<exported_session>(); }
};

namespace detail {
inline constexpr std::string_view kExportHeader = "-----BEGIN MEGOLM SESSION DATA-----";
inline constexpr std::string_view kExportFooter = "-----END MEGOLM SESSION DATA-----";
inline constexpr std::uint32_t kExportRounds = 500000;
// The AES key and the HMAC key a passphrase makes with a salt: PBKDF2,
// HMAC-SHA-512, 64 bytes.
template <byte_range Salt>
[[nodiscard]] inline std::optional<std::array<std::uint8_t, 64>> export_keys_of(std::string_view passphrase, Salt&& salted,
                                                                              std::uint32_t rounds) {
  std::array<std::uint8_t, 64> out{};
  const std::string secret(passphrase);
  const auto salt = mux::bytes::buffer_of(std::forward<Salt>(salted));  // PBKDF2 reads it whole
  if (PKCS5_PBKDF2_HMAC(secret.data(), static_cast<int>(secret.size()), salt.data(), static_cast<int>(salt.size()),
                        static_cast<int>(rounds), EVP_sha512(), static_cast<int>(out.size()), out.data()) != 1)
    return std::nullopt;
  return out;
}
// HMAC-SHA-256 of any ranges of bytes, one after another, under a key.
template <byte_range Key, byte_range... Parts>
[[nodiscard]] inline std::array<std::uint8_t, 32> hmac_sha256(Key&& key, Parts&&... parts) {
  const auto secret = mux::bytes::buffer_of(std::forward<Key>(key));  // OpenSSL keeps the key whole
  std::unique_ptr<EVP_MAC, decltype(&EVP_MAC_free)> mac(EVP_MAC_fetch(nullptr, "HMAC", nullptr), &EVP_MAC_free);
  std::unique_ptr<EVP_MAC_CTX, decltype(&EVP_MAC_CTX_free)> ctx(mac ? EVP_MAC_CTX_new(mac.get()) : nullptr, &EVP_MAC_CTX_free);
  std::string digest = "SHA256";
  const OSSL_PARAM params[] = {OSSL_PARAM_construct_utf8_string("digest", digest.data(), 0), OSSL_PARAM_construct_end()};
  bool fine = ctx && EVP_MAC_init(ctx.get(), secret.data(), secret.size(), params) == 1;
  (mux::bytes::in_pieces(std::forward<Parts>(parts), [&](std::span<const std::uint8_t> piece) {
     fine = fine && EVP_MAC_update(ctx.get(), piece.data(), piece.size()) == 1;
   }),
   ...);
  std::array<std::uint8_t, 32> out{};
  std::size_t size = 0;
  if (!fine || EVP_MAC_final(ctx.get(), out.data(), &size, out.size()) != 1 || size != out.size())
    throw std::runtime_error("HMAC-SHA-256 failed");
  return out;
}
}  // namespace detail

// Sessions sealed under a passphrase, as a file Element reads: version 1,
// a salt, a counter's start with bit 63 clear, the rounds, the ciphertext,
// and an HMAC-SHA-256 of all of it -- in base64, between its two lines.
[[nodiscard]] inline std::string export_file(const std::vector<exported_session>& sessions, std::string_view passphrase) {
  const auto salt = mux::vault::vault::random(16);
  auto iv = mux::vault::vault::random(16);
  iv[8] &= 0x7F;
  const auto keys = detail::export_keys_of(passphrase, salt, detail::kExportRounds);
  if (!keys)
    throw std::runtime_error("the key could not be made from the passphrase");
  const auto ciphertext = detail::aes_ctr(std::span(*keys).first(32), iv, mux::bytes::of(knot::to_json(sessions)));
  if (!ciphertext)
    throw std::runtime_error("the keys could not be encrypted");
  std::vector<std::uint8_t> body{1};
  body.insert(body.end(), salt.begin(), salt.end());
  body.insert(body.end(), iv.begin(), iv.end());
  for (const int shift : {24, 16, 8, 0})
    body.push_back(static_cast<std::uint8_t>((detail::kExportRounds >> shift) & 0xFF));
  body.insert(body.end(), ciphertext->begin(), ciphertext->end());
  const auto mac = detail::hmac_sha256(std::span(*keys).last(32), body);
  body.insert(body.end(), mac.begin(), mac.end());
  const std::string lines = mux::bytes::every(mux::bytes::base64_padded(body), 96, '\n') | std::ranges::to<std::string>();
  return std::format("{}\n{}\n{}\n", detail::kExportHeader, lines, detail::kExportFooter);
}
// A key file opened: none where it is not one, its passphrase another, or
// it was changed (its HMAC). Its rounds bounded, as anyone could write them.
[[nodiscard]] inline std::optional<std::vector<exported_session>> import_file(std::string_view text, std::string_view passphrase) {
  const auto begin = text.find(detail::kExportHeader);
  const auto end = text.find(detail::kExportFooter);
  if (begin == std::string_view::npos || end == std::string_view::npos || end < begin)
    return std::nullopt;
  const std::string encoded = text.substr(begin + detail::kExportHeader.size(), end - begin - detail::kExportHeader.size()) |
                              std::views::filter([](char c) { return std::isspace(static_cast<unsigned char>(c)) == 0; }) |
                              std::ranges::to<std::string>();
  const auto body = from_base64(encoded);
  if (!body || body->size() < 1 + 16 + 16 + 4 + 32 || (*body)[0] != 1)
    return std::nullopt;
  const std::span<const std::uint8_t> all(*body);
  const auto salt = all.subspan(1, 16);
  const auto iv = all.subspan(17, 16);
  const std::uint32_t rounds = (std::uint32_t{all[33]} << 24) | (std::uint32_t{all[34]} << 16) | (std::uint32_t{all[35]} << 8) |
                               std::uint32_t{all[36]};
  if (rounds < 1 || rounds > 10'000'000)
    return std::nullopt;
  const auto signed_part = all.first(all.size() - 32);
  const auto mac = all.last(32);
  const auto keys = detail::export_keys_of(passphrase, salt, rounds);
  if (!keys)
    return std::nullopt;
  const auto expected = detail::hmac_sha256(std::span(*keys).last(32), signed_part);
  if (CRYPTO_memcmp(expected.data(), mac.data(), 32) != 0)
    return std::nullopt;
  const auto plain = detail::aes_ctr(std::span(*keys).first(32), iv, all.subspan(37, all.size() - 37 - 32));
  if (!plain)
    return std::nullopt;
  auto read = knot::try_read<std::vector<exported_session>>(mux::bytes::chars(*plain));
  if (!read)
    return std::nullopt;
  return std::move(*read);
}

// Cross-signing's private keys: 32-byte Ed25519 seeds, unpadded base64.
struct cross_signing_secrets {
  std::string master;
  std::string self_signing;
  std::string user_signing;
  friend consteval auto json_schema(knot::type<cross_signing_secrets>) { return knot::schema<cross_signing_secrets>(); }
};
namespace detail {
struct pkey_free {
  void operator()(EVP_PKEY* key) const { EVP_PKEY_free(key); }
};
// An Ed25519 key from its seed; its public half, and a signature made with it.
template <byte_range Seed>
[[nodiscard]] inline std::unique_ptr<EVP_PKEY, pkey_free> ed25519_of(Seed&& seed) {
  const auto exact = mux::bytes::exactly<32>(std::forward<Seed>(seed));
  if (!exact)
    return nullptr;
  return std::unique_ptr<EVP_PKEY, pkey_free>(EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, exact->data(), exact->size()));
}
[[nodiscard]] inline std::optional<std::string> ed25519_public(std::string_view seed_b64) {
  const auto seed = from_base64(seed_b64);
  if (!seed || seed->size() != 32)
    return std::nullopt;
  const auto key = ed25519_of(*seed);
  std::array<std::uint8_t, 32> out{};
  std::size_t size = out.size();
  if (!key || EVP_PKEY_get_raw_public_key(key.get(), out.data(), &size) != 1 || size != out.size())
    return std::nullopt;
  return mux::bytes::base64_text(out);
}
[[nodiscard]] inline std::optional<std::string> ed25519_sign(std::string_view seed_b64, std::string_view message) {
  const auto seed = from_base64(seed_b64);
  if (!seed || seed->size() != 32)
    return std::nullopt;
  const auto key = ed25519_of(*seed);
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> md(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
  const auto bytes = mux::bytes::buffer_of(mux::bytes::of(message));  // Ed25519 signs the message whole
  std::array<std::uint8_t, 64> out{};
  std::size_t size = out.size();
  if (!key || !md || EVP_DigestSignInit(md.get(), nullptr, nullptr, nullptr, key.get()) != 1 ||
      EVP_DigestSign(md.get(), out.data(), &size, bytes.data(), bytes.size()) != 1 || size != out.size())
    return std::nullopt;
  return mux::bytes::base64_text(out);
}
}  // namespace detail
// Three new seeds: master, self-signing, user-signing.
[[nodiscard]] inline cross_signing_secrets new_cross_signing() {
  return {.master = mux::bytes::base64_text(mux::vault::vault::random(32)),
          .self_signing = mux::bytes::base64_text(mux::vault::vault::random(32)),
          .user_signing = mux::bytes::base64_text(mux::vault::vault::random(32))};
}
// A device's keys with signatures, as /keys/signatures/upload takes them.
struct signed_device_part {
  std::vector<std::string> algorithms;
  std::string device_id;
  std::map<std::string, std::string> keys;
  std::string user_id;
  signatures_t signatures;
  knot::raw rest;
  friend consteval auto json_schema(knot::type<signed_device_part>) {
    return knot::schema<signed_device_part>().member<"rest">(knot::rest);
  }
};
// A cross-signing key with signatures, likewise.
template <class Key>
struct signed_key_part {
  std::string user_id;
  std::vector<typename Key::usage_item_t> usage;
  std::map<std::string, std::string> keys;
  signatures_t signatures;
  knot::raw rest;
  friend consteval auto json_schema(knot::type<signed_key_part>) {
    return knot::schema<signed_key_part>().template member<"rest">(knot::rest);
  }
};

// Secret storage (the spec's m.secret_storage.v1.aes-hmac-sha2): secrets
// kept in account data, each sealed under a key only the user holds -- the
// recovery key, written down as 48 base58 letters. With it, a new device
// takes the cross-signing keys back from the server it cannot read them on.
struct storage_key_info {
  std::string algorithm = "m.secret_storage.v1.aes-hmac-sha2";
  std::string iv;
  std::string mac;
  friend consteval auto json_schema(knot::type<storage_key_info>) { return knot::schema<storage_key_info>(); }
};
struct default_storage_key {
  std::string key;
  friend consteval auto json_schema(knot::type<default_storage_key>) { return knot::schema<default_storage_key>(); }
};
struct sealed_secret {
  std::string iv;
  std::string ciphertext;
  std::string mac;
  friend consteval auto json_schema(knot::type<sealed_secret>) { return knot::schema<sealed_secret>(); }
};
struct stored_secret {
  std::map<std::string, sealed_secret> encrypted;
  friend consteval auto json_schema(knot::type<stored_secret>) { return knot::schema<stored_secret>(); }
};
namespace detail {
// HKDF-SHA-256 with a zero salt, 64 bytes: AES key then HMAC key.
template <byte_range Key>
[[nodiscard]] inline std::array<std::uint8_t, 64> hkdf64(Key&& key, std::string_view info) {
  const std::array<std::uint8_t, 32> salt{};
  const auto prk = hmac_sha256(salt, key);
  const auto first = hmac_sha256(prk, mux::bytes::of(info), std::array<std::uint8_t, 1>{1});
  const auto second = hmac_sha256(prk, first, mux::bytes::of(info), std::array<std::uint8_t, 1>{2});
  std::array<std::uint8_t, 64> out{};
  std::ranges::copy(first, out.begin());
  std::ranges::copy(second, out.begin() + 32);
  return out;
}
inline constexpr std::string_view kBase58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
template <byte_range Bytes>
[[nodiscard]] inline std::string base58(Bytes&& bytes) {
  std::vector<std::uint8_t> digits;  // base 58, least significant first
  for (const std::uint8_t byte : bytes) {
    std::uint32_t carry = byte;
    for (std::uint8_t& digit : digits) {
      carry += std::uint32_t{digit} << 8;
      digit = static_cast<std::uint8_t>(carry % 58);
      carry /= 58;
    }
    for (; carry > 0; carry /= 58)
      digits.push_back(static_cast<std::uint8_t>(carry % 58));
  }
  const auto zeros = std::ranges::distance(bytes | std::views::take_while([](std::uint8_t b) { return b == 0; }));  // walked again
  return std::string(static_cast<std::size_t>(zeros), '1') +
         (digits | std::views::reverse | std::views::transform([](std::uint8_t d) { return kBase58[d]; }) |
          std::ranges::to<std::string>());
}
[[nodiscard]] inline std::optional<std::vector<std::uint8_t>> from_base58(std::string_view text) {
  std::vector<std::uint8_t> bytes;  // base 256, least significant first
  for (const char c : text) {
    const auto at = kBase58.find(c);
    if (at == std::string_view::npos)
      return std::nullopt;
    std::uint32_t carry = static_cast<std::uint32_t>(at);
    for (std::uint8_t& byte : bytes) {
      carry += std::uint32_t{byte} * 58;
      byte = static_cast<std::uint8_t>(carry & 0xFF);
      carry >>= 8;
    }
    for (; carry > 0; carry >>= 8)
      bytes.push_back(static_cast<std::uint8_t>(carry & 0xFF));
  }
  const auto zeros = std::ranges::distance(text | std::views::take_while([](char c) { return c == '1'; }));
  std::vector<std::uint8_t> out(static_cast<std::size_t>(zeros), 0);
  out.insert(out.end(), bytes.rbegin(), bytes.rend());
  return out;
}
// A secret sealed under a storage key, for its name.
template <byte_range Key>
[[nodiscard]] inline sealed_secret seal_secret(Key&& key, std::string_view name, std::string_view secret) {
  const auto keys = hkdf64(key, name);
  auto iv = mux::vault::vault::random(16);
  iv[8] &= 0x7F;
  const auto ciphertext = aes_ctr(std::span(keys).first(32), iv, mux::bytes::of(secret));
  if (!ciphertext)
    throw std::runtime_error("the secret could not be encrypted");
  const auto mac = hmac_sha256(std::span(keys).last(32), *ciphertext);
  return sealed_secret{.iv = mux::bytes::base64_text(iv), .ciphertext = mux::bytes::base64_text(*ciphertext), .mac = mux::bytes::base64_text(mac)};
}
template <byte_range Key>
[[nodiscard]] inline std::optional<std::string> open_secret(Key&& key, std::string_view name,
                                                            const sealed_secret& sealed) {
  const auto keys = hkdf64(key, name);
  const auto iv = from_base64(sealed.iv);
  const auto ciphertext = from_base64(sealed.ciphertext);
  const auto mac = from_base64(sealed.mac);
  if (!iv || !ciphertext || !mac || mac->size() != 32)
    return std::nullopt;
  const auto expected = hmac_sha256(std::span(keys).last(32), *ciphertext);
  if (CRYPTO_memcmp(expected.data(), mac->data(), 32) != 0)
    return std::nullopt;
  const auto plain = aes_ctr(std::span(keys).first(32), *iv, *ciphertext);
  if (!plain)
    return std::nullopt;
  return mux::bytes::text_of(*plain);  // the secret, kept as text
}
}  // namespace detail
// A new storage key: its 32 bytes, its ID, what account data says of it (an
// empty secret sealed, by which a key typed later is checked), and the
// recovery key the user writes down.
struct new_storage_key {
  std::vector<std::uint8_t> key;
  std::string id;
  storage_key_info info;
  std::string recovery;
};
template <detail::byte_range Key>
[[nodiscard]] inline std::string recovery_key_of(Key&& key) {
  std::vector<std::uint8_t> bytes{0x8B, 0x01};
  bytes.insert(bytes.end(), key.begin(), key.end());
  bytes.push_back(std::ranges::fold_left(bytes, std::uint8_t{0}, [](std::uint8_t a, std::uint8_t b) { return static_cast<std::uint8_t>(a ^ b); }));
  const std::string plain = detail::base58(bytes);
  return mux::bytes::every(plain, 4, ' ') | std::ranges::to<std::string>();
}
[[nodiscard]] inline std::optional<std::vector<std::uint8_t>> key_of_recovery(std::string_view recovery) {
  const std::string plain = recovery | std::views::filter([](char c) { return std::isspace(static_cast<unsigned char>(c)) == 0; }) |
                            std::ranges::to<std::string>();
  const auto bytes = detail::from_base58(plain);
  if (!bytes || bytes->size() != 35 || (*bytes)[0] != 0x8B || (*bytes)[1] != 0x01)
    return std::nullopt;
  const auto parity = std::ranges::fold_left(*bytes, std::uint8_t{0}, [](std::uint8_t a, std::uint8_t b) { return static_cast<std::uint8_t>(a ^ b); });
  if (parity != 0)
    return std::nullopt;
  return std::vector<std::uint8_t>(bytes->begin() + 2, bytes->begin() + 34);
}
[[nodiscard]] inline new_storage_key make_storage_key() {
  auto key = mux::vault::vault::random(32);
  const std::string id = mux::bytes::base64_text(mux::vault::vault::random(24));
  const auto check = detail::seal_secret(key, "", std::string(32, '\0'));
  return new_storage_key{.key = key, .id = id, .info = {.iv = check.iv, .mac = check.mac}, .recovery = recovery_key_of(key)};
}
// Whether a key is the one account data describes: the empty secret it
// sealed opens to zeros under it.
template <detail::byte_range Key>
[[nodiscard]] inline bool is_storage_key(Key&& key, const storage_key_info& info) {
  const auto keys = detail::hkdf64(key, "");
  const auto zeros = detail::aes_ctr(std::span(keys).first(32), from_base64(info.iv).value_or(std::vector<std::uint8_t>{}),
                                     std::vector<std::uint8_t>(32, 0));
  const auto mac = from_base64(info.mac);
  if (!zeros || !mac || mac->size() != 32)
    return false;
  const auto expected = detail::hmac_sha256(std::span(keys).last(32), *zeros);
  return CRYPTO_memcmp(expected.data(), mac->data(), 32) == 0;
}

// Server-side key backup (the spec's m.megolm_backup.v1.curve25519-aes-sha2):
// each room key sealed to the backup's Curve25519 public key -- an
// ephemeral key, ECDH, HKDF-SHA-256 to an AES-256-CBC key, an HMAC key and
// an IV -- so that only whoever has the backup's private key (kept in
// secret storage, under the recovery key) reads it.
struct backup_session_data {
  std::string ephemeral;
  std::string ciphertext;
  std::string mac;
  friend consteval auto json_schema(knot::type<backup_session_data>) { return knot::schema<backup_session_data>(); }
};
struct backup_plaintext {
  std::string algorithm = "m.megolm.v1.aes-sha2";
  std::vector<std::string> forwarding_curve25519_key_chain;
  std::map<std::string, std::string> sender_claimed_keys;
  std::string sender_key;
  std::string session_key;
  friend consteval auto json_schema(knot::type<backup_plaintext>) { return knot::schema<backup_plaintext>(); }
};
struct backup_auth_data {
  std::string public_key;
  signatures_t signatures;
  friend consteval auto json_schema(knot::type<backup_auth_data>) { return knot::schema<backup_auth_data>(); }
};
struct backup_auth_signed_part {
  std::string public_key;
  friend consteval auto json_schema(knot::type<backup_auth_signed_part>) { return knot::schema<backup_auth_signed_part>(); }
};
namespace detail {
template <byte_range Secret>
[[nodiscard]] inline std::optional<std::array<std::uint8_t, 32>> x25519_public(Secret&& secret_bytes) {
  const auto secret = mux::bytes::exactly<32>(std::forward<Secret>(secret_bytes));
  if (!secret)
    return std::nullopt;
  const std::unique_ptr<EVP_PKEY, pkey_free> key(EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, secret->data(), secret->size()));
  std::array<std::uint8_t, 32> out{};
  std::size_t size = out.size();
  if (!key || EVP_PKEY_get_raw_public_key(key.get(), out.data(), &size) != 1 || size != out.size())
    return std::nullopt;
  return out;
}
template <byte_range Secret, byte_range Theirs>
[[nodiscard]] inline std::optional<std::array<std::uint8_t, 32>> x25519_shared(Secret&& secret_bytes, Theirs&& their_bytes) {
  const auto secret = mux::bytes::exactly<32>(std::forward<Secret>(secret_bytes));
  const auto theirs = mux::bytes::exactly<32>(std::forward<Theirs>(their_bytes));
  if (!secret || !theirs)
    return std::nullopt;
  const std::unique_ptr<EVP_PKEY, pkey_free> ours(EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, secret->data(), secret->size()));
  const std::unique_ptr<EVP_PKEY, pkey_free> other(EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, theirs->data(), theirs->size()));
  if (!ours || !other)
    return std::nullopt;
  std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(EVP_PKEY_CTX_new(ours.get(), nullptr), &EVP_PKEY_CTX_free);
  std::array<std::uint8_t, 32> out{};
  std::size_t size = out.size();
  if (!ctx || EVP_PKEY_derive_init(ctx.get()) != 1 || EVP_PKEY_derive_set_peer(ctx.get(), other.get()) != 1 ||
      EVP_PKEY_derive(ctx.get(), out.data(), &size) != 1 || size != out.size())
    return std::nullopt;
  return out;
}
// HKDF-SHA-256, zero salt, empty info, 80 bytes: AES key, HMAC key, IV.
template <byte_range Shared>
[[nodiscard]] inline std::array<std::uint8_t, 80> hkdf80(Shared&& shared) {
  const std::array<std::uint8_t, 32> salt{};
  const auto prk = hmac_sha256(salt, shared);
  std::array<std::uint8_t, 80> out{};
  std::vector<std::uint8_t> previous;
  for (std::uint8_t round = 1, at = 0; at < out.size(); ++round) {
    const auto block = hmac_sha256(prk, previous, std::array<std::uint8_t, 1>{round});
    previous.assign(block.begin(), block.end());
    for (std::size_t i = 0; i < block.size() && at < out.size(); ++i, ++at)
      out[at] = block[i];
  }
  return out;
}
template <byte_range Key, byte_range Iv, byte_range In>
[[nodiscard]] inline std::optional<std::vector<std::uint8_t>> aes_cbc(bool encrypt, Key&& key, Iv&& iv, In&& in) {
  return cipher(EVP_aes_256_cbc(), encrypt, std::forward<Key>(key), std::forward<Iv>(iv), std::forward<In>(in));
}
}  // namespace detail
// A backup key pair: its private half, and its public half as auth_data has it.
[[nodiscard]] inline std::pair<std::string, std::string> new_backup_key() {
  const auto secret = mux::vault::vault::random(32);
  const auto public_key = detail::x25519_public(secret);
  if (!public_key)
    throw std::runtime_error("the backup key could not be made");
  return {mux::bytes::base64_text(secret), mux::bytes::base64_text(*public_key)};
}
[[nodiscard]] inline std::optional<std::string> backup_public_of(std::string_view secret_b64) {
  const auto secret = from_base64(secret_b64);
  if (!secret || secret->size() != 32)
    return std::nullopt;
  const auto public_key = detail::x25519_public(*secret);
  return public_key ? std::optional<std::string>(mux::bytes::base64_text(*public_key)) : std::nullopt;
}
// A room key sealed to the backup's public key. Its MAC, as libolm made it
// and every client checks it, is of nothing: the first 8 bytes of
// HMAC-SHA-256 of an empty message.
[[nodiscard]] inline std::optional<backup_session_data> seal_backup(std::string_view public_b64, const backup_plaintext& plain) {
  const auto theirs = from_base64(public_b64);
  const auto ephemeral = mux::vault::vault::random(32);
  const auto ephemeral_public = detail::x25519_public(ephemeral);
  if (!theirs || theirs->size() != 32 || !ephemeral_public)
    return std::nullopt;
  const auto shared = detail::x25519_shared(ephemeral, *theirs);
  if (!shared)
    return std::nullopt;
  const auto keys = detail::hkdf80(*shared);
  const auto ciphertext =
      detail::aes_cbc(true, std::span(keys).first(32), std::span(keys).subspan(64, 16), mux::bytes::of(knot::to_json(plain)));
  if (!ciphertext)
    return std::nullopt;
  const auto mac = detail::hmac_sha256(std::span(keys).subspan(32, 32));
  return backup_session_data{.ephemeral = mux::bytes::base64_text(*ephemeral_public),
                             .ciphertext = mux::bytes::base64_text(*ciphertext),
                             .mac = mux::bytes::base64_text(std::span(mac).first(8))};
}
[[nodiscard]] inline std::optional<backup_plaintext> open_backup(std::string_view secret_b64, const backup_session_data& sealed) {
  const auto secret = from_base64(secret_b64);
  const auto ephemeral = from_base64(sealed.ephemeral);
  const auto ciphertext = from_base64(sealed.ciphertext);
  const auto mac = from_base64(sealed.mac);
  if (!secret || secret->size() != 32 || !ephemeral || ephemeral->size() != 32 || !ciphertext || !mac || mac->size() != 8)
    return std::nullopt;
  const auto shared = detail::x25519_shared(*secret, *ephemeral);
  if (!shared)
    return std::nullopt;
  const auto keys = detail::hkdf80(*shared);
  // Of nothing, as libolm and those after it write it; or of the
  // ciphertext, as the spec's text says.
  const auto of_nothing = detail::hmac_sha256(std::span(keys).subspan(32, 32));
  const auto of_ciphertext = detail::hmac_sha256(std::span(keys).subspan(32, 32), *ciphertext);
  if (CRYPTO_memcmp(of_nothing.data(), mac->data(), 8) != 0 && CRYPTO_memcmp(of_ciphertext.data(), mac->data(), 8) != 0)
    return std::nullopt;
  const auto plain = detail::aes_cbc(false, std::span(keys).first(32), std::span(keys).subspan(64, 16), *ciphertext);
  if (!plain)
    return std::nullopt;
  auto read = knot::try_read<backup_plaintext>(mux::bytes::chars(*plain));
  if (!read)
    return std::nullopt;
  return std::move(*read);
}
// A room key to back up: its room, ID, first index and what is sealed.
struct backup_entry {
  std::string room;
  std::string session_id;
  std::int64_t first_index = 0;
  bool verified = false;
  backup_plaintext plain;
};

// What a start that offers SAS says beyond the start itself: read from its
// remainder, where it comes as a start of any method.
struct sas_offer {
  std::vector<std::string> key_agreement_protocols;
  std::vector<std::string> hashes;
  std::vector<std::string> message_authentication_codes;
  std::vector<std::string> short_authentication_string;
  friend consteval auto json_schema(knot::type<sas_offer>) { return knot::schema<sas_offer>(); }
};
// Whether an offer has what this client speaks.
[[nodiscard]] inline bool speaks(const sas_offer& offer) {
  return std::ranges::contains(offer.key_agreement_protocols, std::string_view("curve25519-hkdf-sha256")) &&
         std::ranges::contains(offer.hashes, std::string_view("sha256")) &&
         std::ranges::contains(offer.message_authentication_codes, std::string_view("hkdf-hmac-sha256.v2")) &&
         std::ranges::contains(offer.short_authentication_string, std::string_view("emoji"));
}

// One verification, by its transaction: with whom, which of the two sent
// the start, what was committed to, and the keys exchanged.
struct sas_state {
  std::string txn;              // to-device: its transaction ID; in a room: the request's event ID
  std::optional<std::string> room;  // in a room, as Element verifies people from a DM
  std::string their_user;
  std::string their_device;     // empty until one of theirs answers
  bool we_requested = false;
  bool we_started = false;      // this side sent m.key.verification.start
  std::string start_canonical;  // the start's content, canonical, for the commitment
  std::optional<std::string> commitment;  // theirs, where this side started
  std::optional<rust::Box<vodozemac::sas::Sas>> sas;
  std::optional<rust::Box<vodozemac::sas::EstablishedSas>> established;
  std::string our_key;
  std::optional<std::string> their_key;
  bool we_confirmed = false;
  std::optional<std::map<std::string, std::string>> their_mac;  // their MACs, where they came first
  std::string their_keys_mac;

  // This side's key pair made: its public key kept to send.
  void begin() {
    sas.emplace(vodozemac::sas::new_sas());
    our_key = std::string((*sas)->public_key()->to_base64());
  }
  // Their key taken: the shared secret, from which the emoji and the MACs
  // come. False where it is not a key.
  [[nodiscard]] bool establish(const std::string& theirs) {
    if (!sas)
      return false;
    try {
      auto key = vodozemac::types::curve_key_from_base64(theirs);
      established.emplace((*sas)->diffie_hellman(*key));
      their_key = theirs;
      return true;
    } catch (const rust::Error&) {
      return false;
    }
  }
  [[nodiscard]] std::array<int, 7> emoji(std::string_view info) const {
    const auto indices = (*established)->bytes(std::string(info))->emoji_indices();
    std::array<int, 7> out{};
    std::ranges::copy(indices | std::views::transform([](std::uint8_t i) { return static_cast<int>(i); }), out.begin());
    return out;
  }
  [[nodiscard]] std::string mac(std::string_view input, std::string_view info) const {
    return std::string((*established)->calculate_mac(std::string(input), std::string(info))->to_base64());
  }
  [[nodiscard]] bool mac_ok(std::string_view input, std::string_view info, std::string_view mac) const {
    try {
      auto read = vodozemac::sas::mac_from_base64(std::string(mac));
      (*established)->verify_mac(std::string(input), std::string(info), *read);
      return true;
    } catch (const rust::Error&) {
      return false;
    }
  }
};
// A verification request made as a message in a room: to whom, from which
// device, by which methods (read from the message's remainder).
struct room_request_fields {
  std::string to;
  std::string from_device;
  std::vector<std::string> methods;
  friend consteval auto json_schema(knot::type<room_request_fields>) { return knot::schema<room_request_fields>(); }
};
// The event an encrypted event refers to, from its cleartext relation: what
// a verification step in an encrypted room names its request by, where the
// relation is kept out of the ciphertext, as the spec has it.
struct reference_part {
  struct relation {
    std::optional<std::string> event_id;
    friend consteval auto json_schema(knot::type<relation>) { return knot::schema<relation>(); }
  };
  std::optional<relation> relates_to;
  friend consteval auto json_schema(knot::type<reference_part>) {
    return knot::schema<reference_part>().member<"relates_to">(knot::key("m.relates_to"));
  }
};
// The commitment to a key and a start: unpadded base64 of SHA-256 of the
// key's base64 then the start's canonical JSON.
[[nodiscard]] inline std::string commitment_of(std::string_view key, std::string_view start_canonical) {
  return mux::bytes::base64_text(detail::sha256(mux::bytes::of(key), mux::bytes::of(start_canonical)));
}

// A room's session as it is to be used now: its ID and key, and the
// devices that do not have it yet.
struct outbound_plan {
  std::string session_id;
  std::string session_key;
  std::vector<recipient> to_share;
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
  // A payload, for a device, over Olm: through its newest session, or one
  // made from its one-time key.
  [[nodiscard]] std::optional<olm_ciphertext> seal_olm(const recipient& to, const std::string& plaintext,
                                                       const std::optional<std::string>& one_time_key) {
    try {
      auto& sessions = kept_.olm[to.curve25519];
      if (sessions.empty()) {
        if (!one_time_key)
          return std::nullopt;
        auto config = vodozemac::olm::new_session_config_version_1();
        auto identity = vodozemac::types::curve_key_from_base64(to.curve25519);
        auto otk = vodozemac::types::curve_key_from_base64(*one_time_key);
        auto made = (*account_)->create_outbound_session(*config, *identity, *otk);
        sessions.push_back(std::string(made->pickle(key_)));
      }
      auto session = vodozemac::olm::session_from_pickle(sessions.back(), key_);
      auto message = session->encrypt(plaintext);
      sessions.back() = std::string(session->pickle(key_));
      this->save();
      const auto parts = message->to_parts();
      return olm_ciphertext{.type = static_cast<std::int64_t>(parts.message_type),
                            .body = mux::bytes::base64_text(parts.ciphertext)};
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
      auto config = vodozemac::olm::new_session_config_version_1();
      auto identity = vodozemac::types::curve_key_from_base64(to.curve25519);
      auto otk = vodozemac::types::curve_key_from_base64(one_time_key);
      auto made = (*account_)->create_outbound_session(*config, *identity, *otk);
      auto message = made->encrypt(knot::to_json_string(payload));
      kept_.olm[to.curve25519].push_back(std::string(made->pickle(key_)));
      this->save();
      const auto parts = message->to_parts();
      return olm_ciphertext{.type = static_cast<std::int64_t>(parts.message_type),
                            .body = mux::bytes::base64_text(parts.ciphertext)};
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
    const auto sealed = mux::vault::detail::seal(key_, mux::bytes::of(knot::to_json(secrets)), "cross-signing");
    kept_.cross_signing = mux::bytes::base64_text(sealed);
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
    auto read = knot::try_read<cross_signing_secrets>(mux::bytes::chars(*opened));
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
    kept_.backup_key = mux::bytes::base64_text(mux::vault::detail::seal(key_, mux::bytes::of(secret_b64), "key backup"));
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
    return std::pair(*kept_.backup_version, mux::bytes::text_of(*opened));
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
    if (!mux::vault::the().write_file(store_, knot::to_json_string(kept_), true))
      throw std::runtime_error("the encryption store cannot be written: " + store_.string());
  }

  [[nodiscard]] std::string sign(std::string_view canonical) const {
    const auto signed_bytes = mux::bytes::buffer_of(mux::bytes::of(canonical));  // the Slice is read whole
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

  std::filesystem::path store_;
  std::string user_id_;
  std::string device_id_;
  std::array<std::uint8_t, 32> key_{};
  kept_file kept_;
  std::optional<rust::Box<vodozemac::olm::Account>> account_;
};

}  // namespace mux::matrix::crypto
