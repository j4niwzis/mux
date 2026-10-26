// SPDX-License-Identifier: AGPL-3.0-only
// mux --page, where mux is built without the page (MUX_PAGE off: Android, or
// by choice): said so, and nothing shown.
import std;
import skiff.platform.window;
import skiff.platform.events;
import mux.app.program;

int mux_run_on_page(mux::app::app&, const skiff::platform::window::options&, const skiff::platform::events::kinds&) {
  std::println(std::cerr, "[mux] built without the page (MUX_PAGE): --page is not here");
  return 2;
}
