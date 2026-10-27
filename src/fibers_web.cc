// SPDX-License-Identifier: AGPL-3.0-only
export module mux.net.fibers;
import std;
import skiff.platform.fibers;
export namespace mux::net::fibers {
inline constexpr bool verifies_sockets = false;
using skiff::platform::fibers::fiber;
using skiff::platform::fibers::forced_unwind;
template <class Body> fiber make(Body body, std::size_t stack) {
  return skiff::platform::fibers::make(std::move(body), stack);
}
template <class Io> void run(Io &io) { skiff::platform::fibers::run(io); }
}
