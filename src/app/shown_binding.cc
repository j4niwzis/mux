// SPDX-License-Identifier: AGPL-3.0-only
// The shown model's binding walk, instantiated once for the whole window.
module mux.app.services;

import skiff.bind;
import mux.ui;
import mux.ui.proto;
import mux.app.requests;

namespace mux::app {
void services::read_shown_now() const {
  showing_binding->refresh(root(), *showing);
}
}
