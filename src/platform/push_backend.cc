// SPDX-License-Identifier: AGPL-3.0-only
// Where the platform has no push service: said so, once.
export module mux.platform.push.backend;

import std;
import mux.platform.push.types;

export namespace mux::platform::push::backend {
template <class Sink>
void run(std::stop_token, std::string, std::string, std::string, std::shared_ptr<const std::atomic<bool>>, Sink sink) {
  sink(event{no_bus{}});
}
}  // namespace mux::platform::push::backend
