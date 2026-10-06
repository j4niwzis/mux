// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.xmpp -- What XMPP says when the client asks (mux.proto): its
// overloads, on its tag, found by ADL. What it leaves out is mux.proto's
// default.
export module mux.proto.xmpp;

import std;
import splice;
import mux.core;
import mux.proto;

export namespace mux::proto::xmpp {

// Edits as XMPP has them: one's own last message corrected, and no other
// (Last Message Correction, XEP-0308).
struct last_correction {};
[[nodiscard]] inline bool allows(last_correction, const conversation& chat, const message& one) {
  const auto last = std::ranges::find_if(chat.timeline.rbegin(), chat.timeline.rend(), own_text);
  return last != chat.timeline.rend() && last->id == one.id && own_text(one);
}
inline last_correction edit_rule(const state&) { return {}; }
// Older history asked of the server where it keeps an archive (XEP-0313);
// before it is known, asked -- as it was.
constexpr bool can_page_back(const state& now) { return now.archive.value_or(true); }

// The roster says who is there: one never heard of is offline.
inline std::string unheard_presence(const state&) { return "offline"; }

// A group chat is left; a direct chat, a contact's, is not: there is
// nothing to leave.
inline bool can_leave(const state&, const conversation& chat) {
  return spl::visit(spl::overloaded{[](const conversation_kind::direct&) { return false; }, [](const auto&) { return true; }},
                       chat.kind);
}

// A JID's local part: before its @.
inline std::string local_part(const state&, std::string_view address) {
  return std::string(address.substr(0, address.find('@')));
}

// An xmpp: URI (RFC 5122).
inline std::optional<std::string> share_link(const state&, std::string_view address) { return "xmpp:" + std::string(address); }

}  // namespace mux::proto::xmpp
