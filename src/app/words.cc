// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.words: How much moves, as the window's paint takes it.
export module mux.app.words;

import std;
import splice;
import skiff.paint;
import mux.config;

export namespace mux::app {

[[nodiscard]] inline skiff::paint::Motion motion_of(mux::config::motion::full) { return skiff::paint::motion::full{}; }
[[nodiscard]] inline skiff::paint::Motion motion_of(mux::config::motion::reduced) { return skiff::paint::motion::reduced{}; }
[[nodiscard]] inline skiff::paint::Motion motion_of(mux::config::motion::none) { return skiff::paint::motion::none{}; }
[[nodiscard]] inline skiff::paint::Motion motion_of(const mux::config::motion_t& said) {
  return spl::visit([](auto one) { return motion_of(one); }, said);
}

}  // namespace mux::app
