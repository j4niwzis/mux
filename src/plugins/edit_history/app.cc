// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.edit_history.app -- The edit history's part of the program: a
// message's versions shown, by its id -- in the timeline or a thread -- and
// let go.
export module mux.plugin.edit_history.app;

import std;
import mux.composition;
import mux.core;
import mux.ui;
import mux.app.services;
import mux.plugin.edit_history.types;

export namespace mux::plugins::edit_history {

struct part {
  app::services* s_ = nullptr;

  void apply(const show& one) {
    ui::show<ui::menu_facts>(*s_->showing, std::nullopt);
    const auto& chosen = s_->chosen();
    const conversation* chat = chosen ? s_->model->find(*chosen) : nullptr;
    if (!chat)
      return;
    const auto in_threads = chat->threads | std::views::values | std::views::join;
    const auto is_it = [&](const message& said) { return said.id == one.message; };
    const message* found = nullptr;
    if (const auto at = std::ranges::find_if(chat->timeline, is_it); at != chat->timeline.end())
      found = &*at;
    else if (const auto there = std::ranges::find_if(in_threads, is_it); there != std::ranges::end(in_threads))
      found = &*there;
    if (!found || found->versions.empty())
      return;
    ui::show(*s_->showing, std::optional(shown{chat->id, *found, &*s_->model}));
  }
  void apply(const ui::request::close_shown<shown>&) { ui::show<shown>(*s_->showing, std::nullopt); }
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::edit_history
