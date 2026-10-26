// SPDX-License-Identifier: AGPL-3.0-only
// mux --page: the program laid out and painted by Blitz, called directly
// (skiff.platform.page) -- its scene mirrored into a document each frame,
// presses read back from the element pressed. For measuring the engine.
import std;
import skiff.platform.window;
import skiff.platform.events;
import skiff.platform.page;
import mux.app.program;

int mux_run_on_page(mux::app::app& program, const skiff::platform::window::options& how,
                    const skiff::platform::events::kinds& kinds) {
  return skiff::platform::page::run(program, how, kinds);
}
