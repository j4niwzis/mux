// SPDX-License-Identifier: AGPL-3.0-only
module;
#include <boost/context/fiber.hpp>
#include <boost/context/protected_fixedsize_stack.hpp>
export module mux.net.fibers;
import std;

export namespace mux::net::fibers {
inline constexpr bool verifies_sockets = true;
using fiber = boost::context::fiber;
using forced_unwind = boost::context::detail::forced_unwind;
template <class Body> fiber make(Body body, std::size_t stack) {
  return fiber(std::allocator_arg, boost::context::protected_fixedsize_stack(stack), std::move(body));
}
template <class Io> void run(Io &io) { io.run(); }
}
