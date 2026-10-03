// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix -- What Matrix says when the client asks (mux.proto):
// its overloads, on its tag, found by ADL.
export module mux.proto.matrix;

import std;
import mux.core;
import mux.proto;
import mux.logic.links;

export namespace mux::proto::matrix {

constexpr bool offers(const state&, feature::people_directory) { return true; }
constexpr bool offers(const state&, feature::room_directory) { return true; }
constexpr bool offers(const state&, feature::room_creation) { return true; }
constexpr bool offers(const state&, feature::sticker_packs) { return true; }
constexpr bool offers(const state&, feature::history_context) { return true; }

// matrix.to, for a person, a room and a message in it.
inline std::optional<std::string> share_link(const state&, std::string_view address) {
  return "https://matrix.to/#/" + std::string(address);
}
inline std::optional<std::string> person_link(const state&, std::string_view user) { return "https://matrix.to/#/" + std::string(user); }
inline std::optional<std::string> room_link(const state&, const conversation& chat) { return logic::room_link(chat); }
// Only an event the server named ($...): one still being sent has no link.
inline std::optional<std::string> message_link(const state&, const conversation& chat, std::string_view event) {
  if (!event.starts_with('$'))
    return std::nullopt;
  return logic::message_link(chat, event);
}
constexpr bool can_pin(const state&, std::string_view event) { return event.starts_with('$'); }

// As the room's power levels allow it: one's own where one may send a
// redaction; another's where one may also redact.
inline bool may_delete(const state&, const conversation& chat, bool outgoing) {
  const auto mine = chat.powers.find(chat.id.account.address);
  const std::int64_t level = mine != chat.powers.end() ? mine->second : chat.power_default;
  const auto redaction = chat.needs.events.find("m.room.redaction");
  const std::int64_t send = redaction != chat.needs.events.end() ? redaction->second : chat.needs.events_default;
  return level >= send && (outgoing || level >= chat.needs.redact);
}

}  // namespace mux::proto::matrix
