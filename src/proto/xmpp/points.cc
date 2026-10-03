// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.xmpp -- What XMPP says when the client asks (mux.proto): its
// overloads, on its tag, found by ADL. What it leaves out is mux.proto's
// default.
export module mux.proto.xmpp;

import std;
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

// An xmpp: URI (RFC 5122).
inline std::optional<std::string> share_link(const state&, std::string_view address) { return "xmpp:" + std::string(address); }

}  // namespace mux::proto::xmpp
