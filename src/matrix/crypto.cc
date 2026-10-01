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

class olm_machine {
 public:
  // The machine of this device: read from its file, else made anew.
  static olm_machine open(std::filesystem::path store, std::string user_id, std::string device_id) {
    olm_machine made(std::move(store), std::move(user_id), std::move(device_id));
    made.key_ = made.pickle_key();
    std::ifstream in(made.store_, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!text.empty())
      if (auto read = knot::try_read<kept_file>(text)) {
        made.kept_ = std::move(*read);
        try {
          made.account_.emplace(vodozemac::olm::account_from_pickle(made.kept_.account, made.key_));
        } catch (const rust::Error&) {
          made.kept_ = kept_file{};
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
  // read, and what it carries taken in -- a room key, kept for its room's
  // messages. Nothing where it is not for this device or cannot be read.
  void to_device(const std::string& sender, const loom::ev::m_room_encrypted_content_t& content) {
    const bool olm = splice::visit(
        splice::overloaded{[](loom::ev::m_room_encrypted_content_t::algorithm_values::m_olm_v1_curve25519_aes_sha2) { return true; },
                           [](const auto&) { return false; }},
        content.algorithm);
    if (!olm || !content.sender_key)
      return;
    auto ciphertexts = knot::try_read<std::map<std::string, olm_ciphertext>>(content.ciphertext.text);
    if (!ciphertexts)
      return;
    const auto mine = ciphertexts->find(this->curve25519());
    if (mine == ciphertexts->end())
      return;
    const auto bytes = from_base64(mine->second.body);
    if (!bytes)
      return;
    vodozemac::olm::OlmMessageParts parts{.message_type = static_cast<std::size_t>(mine->second.type), .ciphertext = {}};
    for (const std::uint8_t b : *bytes)
      parts.ciphertext.push_back(b);
    std::optional<std::string> plaintext;
    try {
      auto message = vodozemac::olm::olm_message_from_parts(parts);
      plaintext = this->olm_decrypt(*content.sender_key, *message, mine->second.type == 0);
    } catch (const rust::Error&) {
      return;
    }
    if (!plaintext)
      return;
    auto payload = knot::try_read<olm_payload>(*plaintext);
    if (!payload)
      return;
    (void)sender;
    splice::visit(splice::overloaded{[&](const loom::ev::m_room_key_content_t& key) { this->room_key(key); },
                                     [](const auto&) {}},
                  payload->content.data());
  }

  // A room's m.room.encrypted: its Megolm message read with the session it
  // names, where this device has it; the room event it was, read as any.
  [[nodiscard]] std::optional<megolm_payload> room_event(const std::string& room, const loom::ev::m_room_encrypted_content_t& content) {
    if (!content.session_id)
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
      const std::string text(clear.plaintext.begin(), clear.plaintext.end());
      auto read = knot::try_read<megolm_payload>(text);
      if (!read)
        return std::nullopt;
      return std::move(*read);
    } catch (const rust::Error&) {
      return std::nullopt;
    }
  }

 private:
  olm_machine(std::filesystem::path store, std::string user_id, std::string device_id)
      : store_(std::move(store)), user_id_(std::move(user_id)), device_id_(std::move(device_id)) {}

  // The store's key: 32 random bytes in a file of its own, readable by the
  // user alone -- made the first time.
  [[nodiscard]] std::array<std::uint8_t, 32> pickle_key() const {
    std::array<std::uint8_t, 32> key{};
    const auto path = std::filesystem::path(store_).concat(".key");
    if (std::ifstream in(path, std::ios::binary); in) {
      in.read(reinterpret_cast<char*>(key.data()), key.size());
      if (in.gcount() == static_cast<std::streamsize>(key.size()))
        return key;
    }
    std::random_device random;
    for (auto& b : key)
      b = static_cast<std::uint8_t>(random());
    std::error_code ignored;
    std::filesystem::create_directories(path.parent_path(), ignored);
    {
      std::ofstream out(path, std::ios::binary | std::ios::trunc);
      out.write(reinterpret_cast<const char*>(key.data()), key.size());
    }
    std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace, ignored);
    return key;
  }

  void save() {
    kept_.account = std::string((*account_)->pickle(key_));
    std::error_code ignored;
    std::filesystem::create_directories(store_.parent_path(), ignored);
    const auto fresh = std::filesystem::path(store_).concat(".new");
    {
      std::ofstream out(fresh, std::ios::binary | std::ios::trunc);
      out << knot::to_json_string(kept_);
    }
    std::filesystem::permissions(fresh, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace, ignored);
    std::filesystem::rename(fresh, store_, ignored);
  }

  [[nodiscard]] std::string sign(std::string_view canonical) const {
    const rust::Slice<const std::uint8_t> bytes(reinterpret_cast<const std::uint8_t*>(canonical.data()), canonical.size());
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
    this->save();
    return std::string(made.plaintext.begin(), made.plaintext.end());
  }

  // A room key: its Megolm session, kept for its room.
  void room_key(const loom::ev::m_room_key_content_t& key) {
    try {
      auto session_key = vodozemac::megolm::session_key_from_base64(key.session_key);
      auto session = vodozemac::megolm::new_inbound_group_session(*session_key);
      kept_.megolm[key.room_id][key.session_id] = std::string(session->pickle(key_));
      this->save();
    } catch (const rust::Error&) {
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
