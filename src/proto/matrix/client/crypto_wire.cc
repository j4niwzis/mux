// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix.crypto:wire -- What goes over the wire and into the store: the kept file, Olm and Megolm messages, keys signed, devices, payloads, rotation.
module;
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/core_names.h>
#include <openssl/params.h>
#include <rust/cxx.h>
#include <vodozemac/src/lib.rs.h>
export module mux.proto.matrix.crypto:wire;

import std;
import splice;
import knot;
import loom.ev;
import loom.cs.keys;
import mux.vault;
import splice.bytes;

export namespace mux::proto::matrix::client::crypto {

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
    const auto bytes = splice::bytes::buffer_of(splice::bytes::of(canonical));  // the Slice is read whole
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

}  // namespace mux::proto::matrix::client::crypto
