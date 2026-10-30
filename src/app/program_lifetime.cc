// SPDX-License-Identifier: AGPL-3.0-only
// Construct and destroy the application in its owning module. This keeps
// the UI tree's destructors and constructor exception cleanup out of main.
module mux.app.program;

import mux.ui;
import mux.ui.shaping;

namespace mux::app {
app::app(const mux::ui::palette& theme_colours, const mux::ui::window_look_t& window)
    : shared{.looks = {.window = window}}, colours(theme_colours) {
  shared.paint.looks = &shared.looks;
  shared.paint.colours = &colours;
  jumps.made_with(mux::ui::bubble_needs{.colours = &colours, .looks = &shared.looks, .shared = &shared.ui});
}
// Stopping: outside a release build, the shaping rules that never applied
// said (mux.ui:shaping).
app::~app() { mux::ui::a::report_unused(); }
}
