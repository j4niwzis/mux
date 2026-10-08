// SPDX-License-Identifier: AGPL-3.0-only
// Construct and destroy the application in its owning module. This keeps
// the UI tree's destructors and constructor exception cleanup out of main.
module mux.app.program;

import mux.ui;

namespace mux::app {
app::app(const mux::ui::palette& theme_colours, const mux::ui::window_look_t& window)
    : shared{.looks = {.window = window}}, colours(theme_colours) {
  shared.paint.looks = &shared.looks;
  shared.paint.colours = &colours;
}
app::~app() = default;
}
