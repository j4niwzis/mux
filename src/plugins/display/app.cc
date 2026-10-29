// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.display.app -- The display plugin's part of the program: its
// pages opened in Settings, and how much the window moves put in force.
export module mux.plugin.display.app;

import std;
import mux.composition;
import mux.core;
import mux.ui;
import mux.config;
import skiff.paint;
import mux.app.words;
import mux.app.kept;
import mux.app.services;
import mux.plugin.display.types;

export namespace mux::plugins::display {

struct part {
  app::services* s_ = nullptr;

  void apply(const open_animations&) {
    if (s_->settings_open())
      s_->settings_page(animations_page{});
  }
  void apply(const open_rendering&) {
    if (s_->settings_open())
      s_->settings_page(rendering_page{s_->kept->plugin_settings<kept>().renderer});
  }
  // Each frame: how much the window moves, as kept, in force -- at the
  // start, once local data is opened, and as the page changes it.
  void tick() {
    if (const auto& mine = s_->kept->plugin_settings<kept>().motion; applied_ != mine) {
      skiff::paint::motionLevel() = app::motion_of(mine);
      applied_ = mine;
    }
  }
  std::optional<config::motion_t> applied_;
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::display
