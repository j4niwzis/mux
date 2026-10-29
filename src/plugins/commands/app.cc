// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.commands.app -- The commands' part of the program: each carried
// out with the actions plugins script with (mux.app.commands' script).
export module mux.plugin.commands.app;

import std;
import mux.composition;
import mux.core;
import mux.ui;
import mux.app.services;
import mux.app.commands;
import mux.plugin.commands.types;

export namespace mux::plugins::commands {

struct part {
  app::services* s_ = nullptr;

  [[nodiscard]] app::script act() const { return app::script{s_}; }

  void apply(const shrug& one) {
    if (const auto chat = this->act().chat())
      this->act().send(*chat, one.arguments.empty() ? std::string("¯\\_(ツ)_/¯") : one.arguments + " ¯\\_(ツ)_/¯");
  }
  template <class Asked>
  void apply(const open& one, const Asked& asked) {
    const auto found = this->act().chats_named(one.arguments);
    if (found.empty()) {
      this->act().notice("/open", std::format("No chat has \"{}\" in its name.", one.arguments));
      return;
    }
    this->act().open(found.front(), asked);
  }
  void apply(const join& one) {
    const auto chat = this->act().chat();
    if (!chat || one.arguments.empty()) {
      this->act().notice("/join", "Give a room's address or ID, with a chat of the account open.");
      return;
    }
    this->act().join(chat->account, one.arguments);
  }
  void apply(const react& one) {
    const auto chat = this->act().chat();
    if (!chat || one.arguments.empty())
      return;
    const auto messages = this->act().messages(*chat);
    if (messages.empty())
      return;
    this->act().react(*chat, messages.back().id, one.arguments);
  }
  void apply(const help&) {
    this->act().notice("Commands", app::kCommands | std::views::transform([](const app::command_said& one) {
                                     return std::format("{} -- {}", one.usage, one.summary);
                                   }) | std::views::join_with(std::string_view("\n")) |
                                       std::ranges::to<std::string>());
  }
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::commands
