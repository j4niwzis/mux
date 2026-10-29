// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.catalog.app -- The catalog plugin's part of the program: its
// pages opened in Settings.
export module mux.plugin.catalog.app;

import std;
import mux.composition;
import mux.core;
import mux.ui;
import mux.app.services;
import mux.plugins.info;
import mux.plugin.catalog.types;

export namespace mux::plugins::catalog {

struct part {
  app::services* s_ = nullptr;

  void apply(const open_list&) {
    if (s_->settings_open())
      s_->settings_page(list_page{});
  }
  void apply(const open_plugin& one) {
    if (s_->settings_open() && one.index < plugin_infos.size())
      s_->settings_page(plugin_page{one.index});
  }
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::catalog
