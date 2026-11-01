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

  // ---- the tab, kept with the room's settings (tab_kept) --------------------------
  // What the tab keeps, where the settings open are a room the tools are
  // for, changed as `change` says -- with the room -- and shown again.
  template <class Change>
  void change_tab(Change change) {
    this->change_tab_of(change, room_settings::room_kinds{});
  }
  template <class Change, class... Kinds>
  void change_tab_of(Change& change, ui::type_list<Kinds...>) {
    (this->change_tab_in<Kinds>(change), ...);
  }
  template <class Kind, class Change>
  void change_tab_in(Change& change) {
    const auto& open = *s_->showing->look<std::optional<room_settings::room_settings_of<Kind>>>();
    if (!open)
      return;
    auto now = *open;
    room_settings::each_kept(now.manage, spl::overloaded{[&](tab_kept& kept) { change(now.chat, kept); }, [](auto&) {}});
    ui::show(*s_->showing, std::optional(std::move(now)));
  }
  void ask_state(const conversation_id& in) {
    if (!s_->demo())
      s_->net->ask(in.account, state_asked{in.id});
  }
  void ask_data(const conversation_id& in) {
    if (!s_->demo())
      s_->net->ask(in.account, data_asked{in.id});
  }
  // Room State, Account Data: unfolded where they are, asked of the account
  // as they open.
  void apply(const flip_state_list&) {
    this->change_tab([&](const conversation_id& in, tab_kept& kept) {
      if ((kept.state_open = !kept.state_open))
        this->ask_state(in);
    });
  }
  void apply(const flip_data_list&) {
    this->change_tab([&](const conversation_id& in, tab_kept& kept) {
      if ((kept.data_open = !kept.data_open))
        this->ask_data(in);
    });
  }
  void apply(const unfold_type& one) {
    this->change_tab([&](const conversation_id&, tab_kept& kept) { kept.unfolded = kept.unfolded == one.type ? std::string() : one.type; });
  }
  // A state event read, as listed; edited from it; one composed.
  void apply(const view_state_event& one) {
    this->change_tab([&](const conversation_id&, tab_kept& kept) {
      const auto found = std::ranges::find_if(kept.state, [&](const state_entry& each) { return each.type == one.type && each.key == one.key; });
      if (kept.pending || found == kept.state.end())
        return;
      kept.type = found->type;
      kept.key = found->key;
      kept.source = found->json;
      kept.json = found->content;
      kept.page = tab_page::view{};
      kept.error.clear();
      ++kept.state_round;
    });
  }
  void apply(const edit_state_event&) {
    this->change_tab([&](const conversation_id&, tab_kept& kept) {
      if (kept.pending)
        return;
      kept.page = tab_page::edit{};
      kept.error.clear();
      ++kept.state_round;
    });
  }
  void apply(const compose_state_event& one) {
    this->change_tab([&](const conversation_id&, tab_kept& kept) {
      if (kept.pending)
        return;
      kept.type = one.type;
      kept.key.clear();
      kept.json = "{}";
      kept.page = tab_page::compose{};
      kept.error.clear();
      ++kept.state_round;
    });
  }
  // Written: on its way, its answer taken where it comes (take(told)).
  void apply(const write_state_event& one) {
    this->change_tab([&](const conversation_id& in, tab_kept& kept) {
      if (kept.pending || one.type.empty() || s_->demo())
        return;
      kept.pending = true;
      kept.error.clear();
      kept.type = one.type;
      kept.key = one.key;
      kept.json = one.json;
      s_->net->ask(in.account, save_state_asked{in.id, one.type, one.key, one.json});
    });
  }
  void apply(const write_data_entry& one) {
    this->change_tab([&](const conversation_id& in, tab_kept& kept) {
      if (kept.pending || one.type.empty() || s_->demo())
        return;
      kept.pending = true;
      kept.error.clear();
      kept.account_type = one.type;
      kept.account_json = one.json;
      s_->net->ask(in.account, save_data_asked{in.id, one.type, one.json});
    });
  }
  // Account data chosen to write, as listed; a new one; the form shut.
  void apply(const choose_data_entry& one) {
    this->change_tab([&](const conversation_id&, tab_kept& kept) {
      if (one.index < kept.data.size()) {
        kept.account_type = kept.data[one.index].type;
        kept.account_json = kept.data[one.index].content;
      }
      kept.account_editor = true;
      ++kept.account_round;
    });
  }
  void apply(const new_data_entry&) {
    this->change_tab([&](const conversation_id&, tab_kept& kept) {
      kept.account_type.clear();
      kept.account_json.clear();
      kept.account_editor = true;
      ++kept.account_round;
    });
  }
  void apply(const close_data_form&) {
    this->change_tab([&](const conversation_id&, tab_kept& kept) { kept.account_editor = false; });
  }
  // What an account told, in the tab of its room: a write done -- a state
  // event composed back to the overview, one edited to its reading, the
  // lists asked again -- or what went wrong; the lists as listed, the state
  // event read as it is now.
  void tab_told(const told& one) {
    this->change_tab([&](const conversation_id& in, tab_kept& kept) {
      if (in != one.in || !kept.pending)
        return;
      kept.pending = false;
      kept.error = one.done ? std::string() : one.text;
      if (!one.done)
        return;
      spl::visit(spl::overloaded{[&](tab_page::compose) { kept.page = tab_page::overview{}; },
                                 [&](tab_page::edit) { kept.page = tab_page::view{}; },
                                 [](const auto&) {}},
                 kept.page);
      kept.account_editor = false;
      ++kept.state_round;
      this->ask_state(in);
      this->ask_data(in);
    });
  }
  void tab_listed(const state_listed& one) {
    this->change_tab([&](const conversation_id& in, tab_kept& kept) {
      if (in != one.in)
        return;
      kept.state = one.entries;
      const auto found = std::ranges::find_if(kept.state, [&](const state_entry& each) { return each.type == kept.type && each.key == kept.key; });
      spl::visit(spl::overloaded{[&](tab_page::view) {
                                   if (found != kept.state.end()) {
                                     kept.source = found->json;
                                     kept.json = found->content;
                                     ++kept.state_round;
                                   }
                                 },
                                 [](const auto&) {}},
                 kept.page);
    });
  }
  void tab_listed(const data_listed& one) {
    this->change_tab([&](const conversation_id& in, tab_kept& kept) {
      if (in == one.in)
        kept.data = one.entries;
    });
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
      room_settings::close_room_settings(*s_->showing);
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
  void take(const told& one) {
    this->shown_for(one.in, screen::reading{one.title, one.text});
    this->tab_told(one);
  }
  void take(const state_listed& one) {
    this->shown_for(one.in, screen::state{one.entries});
    this->tab_listed(one);
  }
  void take(const data_listed& one) {
    this->shown_for(one.in, screen::data{one.entries});
    this->tab_listed(one);
  }
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::devtools
