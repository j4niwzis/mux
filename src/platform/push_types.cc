// SPDX-License-Identifier: AGPL-3.0-only
// mux.platform.push.types -- What a push service tells the program.
export module mux.platform.push.types;

import std;
import splice;

export namespace mux::platform::push {

// The name mux owns on the session bus, and is activated by.
inline constexpr std::string_view kAppId = "io.github.j4niwzis.mux";

// What UnifiedPush tells the program -- read off the bus by
// push::run(), handed to its sink, drained by the program.
struct endpoint {  // where the push server takes this device's pushes
  std::string url;
};
struct message {};       // a push came: what it says is the server's to say, on the next sync
struct unregistered {};  // the distributor dropped the registration
struct registered {      // the distributor took it
  std::string distributor;
};
struct refused {  // the distributor would not
  std::string reason;
};
struct no_distributor {};  // none on the bus, running or to be started
struct no_bus {};          // no session bus at all
using event = spl::variant<endpoint, message, unregistered, registered, refused, no_distributor, no_bus>;

}  // namespace mux::platform::push
