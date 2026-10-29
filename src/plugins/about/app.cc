// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.about.app -- The About plugin's part of the program: its pages
// opened in Settings.
export module mux.plugin.about.app;

import std;
import mux.composition;
import mux.core;
import mux.ui;
import mux.app.services;
import mux.plugin.about.types;

export namespace mux::plugins::about {

struct part {
  app::services* s_ = nullptr;

  void apply(const open_about&) { s_->settings_page(about_page{}); }
  void apply(const open_library& one) { s_->settings_page(library_page{one.name}); }
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::about
