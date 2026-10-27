// SPDX-License-Identifier: AGPL-3.0-only
import std;
import skiff.platform.window;
import skiff.platform.events;
import skiff.platform.browser;
import mux.app.program;

// The scene host delegates pointer and input events itself, so listeners
// installed by the generic DOM writer have no additional action to perform.
extern "C" __attribute__((used)) void skiff_web_event(unsigned, const char *, const char *) {}
int mux_run_on_page(mux::app::app &program, const skiff::platform::window::options &how,
                    const skiff::platform::events::kinds &kinds) {
  return skiff::platform::browser::run(program, how, kinds);
}
