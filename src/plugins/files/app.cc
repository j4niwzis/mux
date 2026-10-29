// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.files.app -- The files plugin's part of the program: its page
// opened in Settings, and its settings put in force.
export module mux.plugin.files.app;

import std;
import mux.composition;
import mux.core;
import mux.config;
import mux.ui;
import mux.app.kept;
import mux.app.services;
import mux.plugin.files.types;

export namespace mux::plugins::files {

struct part {
  app::services* s_ = nullptr;

  void apply(const open_files&) {
    if (s_->settings_open())
      s_->settings_page(files_page{s_->kept->plugin_settings<config::sending_settings>()});
  }
  // Each frame: what is done to a picture dropped, as kept, in force -- at
  // the start, once local data is opened, and as the page changes it.
  void tick() {
    if (const auto& mine = s_->kept->plugin_settings<config::sending_settings>(); s_->sending != mine)
      s_->sending = mine;
  }
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::files
