// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.stickers.app -- The stickers' part of the program: a sticker
// made a favourite, or no longer one, among those kept with the emoji.
export module mux.plugin.stickers.app;

import std;
import mux.ui;
import mux.app.services;
import mux.plugin.stickers.types;

export namespace mux::plugins::stickers {

struct part {
  app::services* s_ = nullptr;
  void apply(const favourite& one) {
    ui::show<ui::menu_facts>(*s_->showing, std::nullopt);
    s_->emoji.flip_favourite(one.sticker);
  }
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::stickers
