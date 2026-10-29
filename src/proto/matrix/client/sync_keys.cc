// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix.client:sync_keys -- The encrypted rooms remembered (no downgrade), and the sending of nothing in the clear into them; the rest of end-to-end encryption is the encryption plugin's (src/plugins/encryption).
export module mux.proto.matrix.client:sync_keys;

import mux.vault;
import std;
import splice.bytes;
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

// The members defined here are declared in :account, and exported there.
namespace mux::proto::matrix::client {

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
  if (!now)
    now = this->whole().store_says_encrypted(room);
  if (now)
    this->remember_encrypted(room);
  return now;
}
template <class Sink>
void account<Sink>::remember_encrypted(std::string_view room) {
  if (!encrypted_rooms_.emplace(room).second)
    return;
  this->save_encrypted();
  this->whole().store_encrypted(room);
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
template <class Sink>
void account<Sink>::set_only_verified(bool on) {
  how_.only_verified = on;
}
}  // namespace mux::proto::matrix::client
