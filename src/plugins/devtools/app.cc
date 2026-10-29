// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.devtools.app -- The developer tools' part of the program: a
// message's source, from its menu, for the chat in view; the state, the
// account data and an event composed, for the room whose settings are
// open -- each asked of its account, its answer shown in the dialog where
// it is up for that room.
export module mux.plugin.devtools.app;

import std;
import splice;
import mux.composition;
import mux.core;
import mux.protocols;
import mux.ui;
import mux.app.network;
import mux.app.kept;
import mux.app.services;
import mux.plugin.devtools.types;
import mux.plugin.room_settings.ui;
import mux.plugin.devtools.changes;
import mux.plugin.devtools.ui;
import mux.plugin.devtools.matrix;

export namespace mux::plugins::devtools {

struct part {
  app::services* s_ = nullptr;

  // The tools turned on or off: kept in its own settings, and shown.
  void apply(const flip_tools&) {
    auto now = s_->kept->plugin_settings<kept>();
    now.tools = !now.tools;
    s_->kept->set_plugin_settings(now);
    (void)s_->kept->write();
    ui::show(*s_->showing, now);
  }
  // Each frame: what the window shows of them as they are kept -- at the
  // start, once local data is opened.
  void tick() {
    const auto& now = s_->kept->plugin_settings<kept>();
    if (*s_->showing->look<kept>() != now)
      ui::show(*s_->showing, now);
  }
  std::uint64_t opened_ = 0;

  // The dialog open on a screen, for a room: made anew.
  void show_screen(const conversation_id& in, screen_t screen) {
    ui::show(*s_->showing, std::optional(shown{in, std::move(screen), ++opened_}));
  }
  // The room the tools are for: the one whose settings are open -- they
  // are closed for the dialog -- or the one the dialog is already for.
  [[nodiscard]] std::optional<conversation_id> room() {
    if (const auto& open = *s_->showing->look<std::optional<shown>>())
      return open->in;
    const auto chosen = s_->managed();
    if (chosen)
      ui::show<room_settings::room_settings_facts>(*s_->showing, std::nullopt);
    return chosen;
  }

  // A message's source, from its menu.
  void apply(const view_source& one) {
    ui::show<ui::menu_facts>(*s_->showing, std::nullopt);
    const auto& chosen = s_->chosen();
    if (!chosen || s_->demo())
      return;
    this->show_screen(*chosen, screen::reading{"Source of " + one.message, "Loading…"});
    s_->net->ask(chosen->account, source_asked{chosen->id, one.message});
  }
  // The room's state, its account data: listed as they open.
  void apply(const open_state&) {
    const auto in = this->room();
    if (!in || s_->demo())
      return;
    this->show_screen(*in, screen::state{});
    s_->net->ask(in->account, state_asked{in->id});
  }
  void apply(const open_data&) {
    const auto in = this->room();
    if (!in || s_->demo())
      return;
    this->show_screen(*in, screen::data{});
    s_->net->ask(in->account, data_asked{in->id});
  }
  void apply(const open_composer&) {
    if (const auto in = this->room())
      this->show_screen(*in, screen::composer{});
  }
  // A state event or account data opened to write: as listed, or a new one.
  void apply(const edit_state& one) {
    const auto& open = *s_->showing->look<std::optional<shown>>();
    if (!open)
      return;
    const auto entries = spl::visit(spl::overloaded{[](const screen::state& listed) { return listed.entries; },
                                                    [](const auto&) { return std::vector<state_entry>{}; }},
                                    open->screen);
    const auto found = std::ranges::find_if(entries, [&](const state_entry& each) { return each.type == one.type && each.key == one.key; });
    const bool fresh = one.type.empty() || found == entries.end();
    this->show_screen(open->in, screen::state_editor{one.type, one.key, fresh ? std::string("{}") : found->content, fresh});
  }
  void apply(const edit_data& one) {
    const auto& open = *s_->showing->look<std::optional<shown>>();
    if (!open)
      return;
    const auto entries = spl::visit(spl::overloaded{[](const screen::data& listed) { return listed.entries; },
                                                    [](const auto&) { return std::vector<data_entry>{}; }},
                                    open->screen);
    const auto found = std::ranges::find(entries, one.type, &data_entry::type);
    const bool fresh = one.type.empty() || found == entries.end();
    this->show_screen(open->in, screen::data_editor{one.type, fresh ? std::string("{}") : found->content, fresh});
  }
  // Written, sent: the answer read where it comes.
  template <class Asked>
  void write(std::string title, Asked asked) {
    const auto& open = *s_->showing->look<std::optional<shown>>();
    if (!open || s_->demo())
      return;
    const conversation_id in = open->in;
    this->show_screen(in, screen::reading{std::move(title), "Sending…"});
    s_->net->ask(in.account, std::move(asked));
  }
  void apply(const save_state& one) {
    if (const auto& open = *s_->showing->look<std::optional<shown>>(); open && !one.type.empty())
      this->write("Saved " + one.type, save_state_asked{open->in.id, one.type, one.key, one.content});
  }
  void apply(const save_data& one) {
    if (const auto& open = *s_->showing->look<std::optional<shown>>(); open && !one.type.empty())
      this->write("Saved " + one.type, save_data_asked{open->in.id, one.type, one.content});
  }
  void apply(const send_event& one) {
    if (const auto& open = *s_->showing->look<std::optional<shown>>(); open && !one.type.empty())
      this->write("Sent " + one.type, send_asked{open->in.id, one.type, one.key, one.content});
  }
  void apply(const ui::request::close_shown<shown>&) { ui::show<shown>(*s_->showing, std::nullopt); }

  // What an account told: in the dialog, where it is up for that room.
  void shown_for(const conversation_id& in, screen_t screen) {
    const auto& open = *s_->showing->look<std::optional<shown>>();
    if (open && open->in == in)
      this->show_screen(in, std::move(screen));
  }
  void take(const told& one) { this->shown_for(one.in, screen::reading{one.title, one.text}); }
  void take(const state_listed& one) { this->shown_for(one.in, screen::state{one.entries}); }
  void take(const data_listed& one) { this->shown_for(one.in, screen::data{one.entries}); }
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::devtools
