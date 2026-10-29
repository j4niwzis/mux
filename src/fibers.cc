// SPDX-License-Identifier: AGPL-3.0-only
module;
#include <boost/context/fiber.hpp>
#include <boost/context/protected_fixedsize_stack.hpp>
#include <boost/context/stack_context.hpp>
export module mux.net.fibers;
import std;

export namespace mux::net::fibers {
inline constexpr bool verifies_sockets = true;
using fiber = boost::context::fiber;
using forced_unwind = boost::context::detail::forced_unwind;

// Fibers' stacks, kept as a fiber ends for the next one to take: each made
// once -- mapped, with its guard page below it -- not mapped, guarded and
// unmapped again for every fiber the loop starts (each request an account
// makes, a search paging back). Kept by the size asked, a few of each, on
// the thread that runs the fibers; unmapped when that thread ends.
class kept_stacks {
 public:
  explicit kept_stacks(std::size_t size) : size_(size) {}
  boost::context::stack_context allocate() {
    auto& kept = stacks().kept;
    if (const auto found = std::ranges::find(kept, size_, &held::size); found != kept.end()) {
      const boost::context::stack_context one = found->stack;
      kept.erase(found);
      return one;
    }
    return boost::context::protected_fixedsize_stack(size_).allocate();
  }
  void deallocate(boost::context::stack_context& one) noexcept {
    auto& kept = stacks().kept;
    if (kept.size() < kKept && kept.capacity() > kept.size()) {
      kept.push_back(held{size_, one});
      return;
    }
    boost::context::protected_fixedsize_stack(size_).deallocate(one);
  }

 private:
  // How many are kept at most, of every size: what is let go past them is
  // unmapped.
  static constexpr std::size_t kKept = 16;
  struct held {
    std::size_t size;
    boost::context::stack_context stack;
  };
  struct all_kept {
    std::vector<held> kept;
    all_kept() { kept.reserve(kKept); }  // pushed to without allocating: deallocate cannot throw
    ~all_kept() {
      for (held& one : kept)
        boost::context::protected_fixedsize_stack(one.size).deallocate(one.stack);
    }
  };
  static all_kept& stacks() {
    thread_local all_kept kept;
    return kept;
  }
  std::size_t size_;
};

template <class Body> fiber make(Body body, std::size_t stack) {
  return fiber(std::allocator_arg, kept_stacks(stack), std::move(body));
}
template <class Io> void run(Io &io) { io.run(); }
}
