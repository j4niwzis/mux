// SPDX-License-Identifier: AGPL-3.0-only
#include <pthread.h>
extern "C" unsigned mux_browser_rust_check();

int main() {
  unsigned result = 0;
  pthread_t thread;
  // pthread's C entry point is the only erased boundary in this fixture.
  if (pthread_create(&thread, nullptr, +[](void *state) -> void * {
        *static_cast<unsigned *>(state) = mux_browser_rust_check();
        return nullptr;
      }, &result) != 0) return 1;
  if (pthread_join(thread, nullptr) != 0) return 2;
  return result == 18 && mux_browser_rust_check() == 36 ? 0 : 3;
}
