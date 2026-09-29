// SPDX-License-Identifier: AGPL-3.0-only
// mux.matrix:base -- Settings, and reading values out of events.
export module mux.matrix:base;

import std;
import knot;
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
import loom.cs.typing;
import loom.cs.wellknown;
import mux.config;
import mux.core;
import mux.http;
import mux.net;

export namespace mux::matrix {

struct settings {
  std::string user_id;  // @user:example.org
  std::string password;
  // The client-server API's base URL, instead of what the server name's
  // .well-known says.
  std::optional<std::string> homeserver;
  std::string device_name = "mux";
  // A proxy to connect through, where there is one.
  std::optional<net::proxy> proxy;
  // The session kept from before: logged in with, rather than logging in
  // again -- a new device each time -- where it is still good.
  std::optional<std::string> access_token;
  std::optional<std::string> device_id;
  // How long a sync waits on the server for something to happen.
  std::chrono::milliseconds sync_timeout = std::chrono::seconds(30);
};

// A value of an object, by key; nothing where it is not an object or has no
// such key.
inline const knot::value* member(const knot::value& of, std::string_view key) {
  if (!of.is<knot::value::object>())
    return nullptr;
  const auto& all = of.as<knot::value::object>();
  const auto found = all.find(key);
  return found == all.end() ? nullptr : &found->second;
}

// A key of an event's content that its type does not name: in the content's
// rest, or where knot::tagged keeps what the alternative did not type.
template <class Tagged>
const knot::value* extra(const knot::value& rest, const Tagged& content, std::string_view key) {
  if (const knot::value* found = member(rest, key))
    return found;
  return member(content.unknown, key);
}

inline std::optional<std::string> text(const knot::value* of) {
  if (of && of->is<std::string>())
    return of->as<std::string>();
  return std::nullopt;
}

}  // namespace mux::matrix
