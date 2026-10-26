// SPDX-License-Identifier: AGPL-3.0-only
// mux as the page only (MUX_WINDOW off): no window of skiff's own -- the
// program is shown through the page (page_host.cc), whatever it is asked.
import std;
import skiff.platform.window;
import skiff.platform.events;
import mux.app.program;

int mux_run_in_window(mux::app::app&, const skiff::platform::window::options&, const skiff::platform::events::kinds&) {
  std::println(std::cerr, "[mux] built as the page only (MUX_WINDOW off)");
  return 2;
}
bool mux_has_window() { return false; }
