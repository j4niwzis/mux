// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.forward.app -- Forwarding's part of the program: what is to be
// forwarded -- a message from its menu, or the messages selected, handed
// over by the selection's owner -- the box of the account's chats opened for
// it; and, a chat chosen, sent there.
export module mux.plugin.forward.app;

import std;
import mux.composition;
import mux.core;
import mux.ui;
import mux.app.network;
import mux.app.services;
import mux.logic.messages;
import mux.plugin.forward.types;

export namespace mux::plugins::forward {

struct part {
  app::services* s_ = nullptr;
  // What is to be forwarded: from which chat, its events.
  std::optional<std::pair<conversation_id, std::vector<std::string>>> pending_;

  void apply(const show& one) {
    ui::show<ui::menu_facts>(*s_->showing, std::nullopt);
    if (const auto& chosen = s_->chosen())
      this->ask(*chosen, {one.message});
  }
  void apply(const ui::request::selection_taken<feature>& one) { this->ask(one.in, one.events); }
  void apply(const to& one) {
    ui::show<shown>(*s_->showing, std::nullopt);
    if (!pending_ || s_->demo())
      return;
    const auto [from, events] = *std::exchange(pending_, std::nullopt);
    const conversation* chat = s_->model->find(from);
    auto sources = events | std::views::transform([&](const std::string& id) {
      const message* kept = chat ? ui::held_message(*chat, id) : nullptr;
      return kept ? logic::forward_source_of(*kept) : forward_source{.event = id};
    }) | std::ranges::to<std::vector>();
    s_->net->forward(from, std::move(sources), one.chat);
    const conversation* there = s_->model->find(one.chat);
    s_->notice("Forward", "Forwarded to " + (there ? ui::display_name(*there) : one.chat.id));
  }
  void apply(const ui::request::close_shown<shown>&) {
    pending_.reset();
    ui::show<shown>(*s_->showing, std::nullopt);
  }

 private:
  // The box of the account's chats, by name, opened for these.
  void ask(const conversation_id& from, std::vector<std::string> events) {
    if (events.empty())
      return;
    pending_ = std::pair{from, std::move(events)};
    std::vector<target> chats;
    if (const account* in = s_->model->accounts().find(from.account))
      for (const auto& [key, one] : in->conversations)
        chats.push_back({one.id, ui::display_name(one)});
    std::ranges::sort(chats, {}, &target::name);
    ui::show(*s_->showing, std::optional(shown{std::move(chats)}));
  }
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::forward
