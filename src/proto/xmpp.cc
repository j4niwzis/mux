// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.xmpp -- What XMPP says when the client asks (mux.proto): its
// overloads, on its tag, found by ADL. What it leaves out is mux.proto's
// default.
export module mux.proto.xmpp;

import std;
import mux.core;
import mux.proto;

export namespace mux::xmpp {

// A JID: anything a Matrix user ID is not (those begin with '@').
constexpr bool owns_address(tag, std::string_view address) { return !address.empty() && !address.starts_with('@'); }

// An xmpp: URI (RFC 5122).
inline std::optional<std::string> share_link(tag, std::string_view address) { return "xmpp:" + std::string(address); }

}  // namespace mux::xmpp
