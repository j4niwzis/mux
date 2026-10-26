// SPDX-License-Identifier: AGPL-3.0-only
// mux in skiff's own window: laid out and painted by skiff, drawn with Skia
// (skiff.platform.window). Where mux is the page only (MUX_WINDOW off),
// window_host_off.cc stands here, and none of the window's loop is built.
import std;
import skiff.platform.window;
import skiff.platform.events;
import mux.app.program;

int mux_run_in_window(mux::app::app& program, const skiff::platform::window::options& how,
                      const skiff::platform::events::kinds& kinds) {
  return skiff::platform::window::run(program, how, kinds);
}
bool mux_has_window() { return true; }
