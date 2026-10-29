// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.storage.app -- The storage plugin's part of the program: its
// pages opened in Settings, the seal's switch as the vault is.
export module mux.plugin.storage.app;

import std;
import mux.composition;
import mux.core;
import mux.vault;
import mux.ui;
import mux.app.kept;
import mux.app.services;
import mux.plugin.storage.types;

export namespace mux::plugins::storage {

struct part {
  app::services* s_ = nullptr;

  void apply(const open_storage&) {
    if (!s_->settings_open())
      return;
    ui::show(*s_->showing, ui::local_seal{s_->vault->on()});
    s_->settings_page(storage_page{s_->kept->limits(), s_->kept->history(), s_->vault->on()});
  }
  void apply(const open_files&) {
    if (s_->settings_open())
      s_->settings_page(files_page{s_->kept->sending()});
  }
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::storage
