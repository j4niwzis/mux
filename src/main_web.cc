// SPDX-License-Identifier: AGPL-3.0-only
#include <cstdlib>
import std;
import skiff.platform.storage;
int mux_main(int argc, char **argv);
int main(int argc, char **argv) {
  if (auto error = skiff::platform::storage::mount("/mux")) {
    std::println(std::cerr, "Could not open browser storage: {}", *error);
    return 1;
  }
  ::setenv("XDG_CONFIG_HOME", "/mux/config", 1);
  ::setenv("XDG_STATE_HOME", "/mux/state", 1);
  ::setenv("XDG_CACHE_HOME", "/mux/cache", 1);
  const int result = mux_main(argc, argv);
  if (auto error = skiff::platform::storage::flush()) std::println(std::cerr, "Could not save browser storage: {}", *error);
  return result;
}
