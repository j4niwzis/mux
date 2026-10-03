// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix.crypto:sas -- Emoji verification's state, and the references in a room's verification.
module;
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/core_names.h>
#include <openssl/params.h>
#include <rust/cxx.h>
#include <vodozemac/src/lib.rs.h>
export module mux.proto.matrix.crypto:sas;

import std;
import splice;
import knot;
import loom.ev;
import loom.cs.keys;
import mux.vault;
import mux.bytes;
import :wire;
import :keys;

export namespace mux::proto::matrix::client::crypto {
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

}  // namespace mux::proto::matrix::client::crypto
