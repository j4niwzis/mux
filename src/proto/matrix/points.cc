// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix -- What Matrix says when the client asks (mux.proto):
// its overloads, on its tag, found by ADL.
export module mux.proto.matrix;

import std;
import splice;
import mux.core;
import mux.proto;
import mux.proto.matrix.links;
import mux.proto.matrix.requests;

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
inline std::optional<std::string> room_link(const state&, const conversation& chat) { return room_link_to(chat); }
// Only an event the server named ($...): one still being sent has no link.
inline std::optional<std::string> message_link(const state&, const conversation& chat, std::string_view event) {
  if (!event.starts_with('$'))
    return std::nullopt;
  return message_link_to(chat, event);
}
// Its own account pages: its encryption, and its sessions under
// cross-signing and the key backup.
namespace settings {
struct encryption {};
struct sessions {};
}  // namespace settings
constexpr account_page_list<settings::encryption, settings::sessions> account_pages(const state&) { return {}; }

// A direct chat is a room: whom it is with, the member who is not the account.
inline std::string direct_contact(const state&, const conversation& one) {
  const auto other = std::ranges::find_if(one.members, [&](const member& each) { return each.id != one.id.account.address; });
  return other == one.members.end() ? std::string() : other->id;
}
// A sender's role, as the room's power levels give it: 100 and 50.
inline std::string sender_role(const state&, const conversation& in, std::string_view who) {
  const auto level = in.powers.find(std::string(who));
  const std::int64_t power = level == in.powers.end() ? in.power_default : level->second;
  return power >= 100 ? std::string("admin") : power >= 50 ? std::string("mod") : std::string();
}
// What is known of the other person's identity, in a direct encrypted chat.
[[nodiscard]] inline std::optional<trust_t> other_trust(const state& now, const conversation& one, const model& known) {
  const bool direct = splice::visit(
      splice::overloaded{[](const conversation_kind::direct&) { return true; }, [](const auto&) { return false; }}, one.kind);
  if (!one.encrypted || !direct)
    return std::nullopt;
  return known.trust_of(one.id.account, direct_contact(now, one));
}
// Encrypted, said after the chat's status, as Element's shield on its
// header: and in a direct chat, what is known of the other's identity.
inline std::vector<part::badge> header_badges(const state& now, const conversation& one, const model& known) {
  if (!one.encrypted || !one.typing.empty())
    return {};
  const auto trust = other_trust(now, one, known);
  const std::string after = !trust ? std::string()
                                   : splice::visit(splice::overloaded{[](trust::verified) { return std::string(" \u00b7 Verified"); },
                                                                      [](trust::unverified) { return std::string(" \u00b7 Not verified"); },
                                                                      [](trust::changed) { return std::string(" \u00b7 Identity reset"); }},
                                                   *trust);
  return {part::badge{"\U0001F512 Encrypted" + after}};
}
// Element's warning over the composer, in a direct encrypted chat: the
// other not verified -- messages are still encrypted to them -- or their
// identity reset, to be verified again or withdrawn on their card.
using banner = part::banner_of<request::verify_them>;
inline std::vector<banner> composer_banners(const state& now, const conversation& one, const model& known) {
  const auto trust = other_trust(now, one, known);
  if (!trust)
    return {};
  const std::string& name = one.name.empty() ? one.id.id : one.name;
  const request::verify_them them{one.id.account, direct_contact(now, one)};
  return splice::visit(
      splice::overloaded{
          [](trust::verified) { return std::vector<banner>{}; },
          [&](trust::unverified) {
            return std::vector<banner>{{std::format("\u26A0 {} is not verified. Messages are encrypted to them, but verify "
                                                    "them to be sure who reads them.",
                                                    name),
                                        part::tone::accent{}, "Verify", them}};
          },
          [&](trust::changed) {
            return std::vector<banner>{{std::format("\u26A0 {}'s identity was reset. Verify them again, or withdraw the "
                                                    "verification on their card.",
                                                    name),
                                        part::tone::danger{}, "Verify again", them}};
          }},
      *trust);
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
