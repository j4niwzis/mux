// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.typing.app -- The typing's part of the program: the user typing
// in the chosen chat, or not, said as the logic of it says (at most every
// twenty seconds while it goes on, and its end), where the chat, its space,
// its account or every account lets it.
export module mux.plugin.typing.app;

import std;
import splice;
import mux.composition;
import mux.core;
import mux.ui;
import mux.logic.reading;
import mux.app.network;
import mux.app.kept;
import mux.app.services;
import mux.plugin.typing.types;

export namespace mux::plugins::typing {

struct part {
  app::services* s_ = nullptr;
  logic::typing_state typing_{};

  void apply(const ui::request::typing& one) {
    if (s_->demo())
      return;
    const auto allowed = [&](const conversation_id& in) { return s_->kept->typing_sent(in); };
    auto step = logic::typing_after(typing_, one.on, s_->chosen(), std::chrono::steady_clock::now(), allowed);
    for (const auto& said : step.say)
      spl::visit(spl::overloaded{[&](const logic::typing_said::started& it) { s_->net->typing(it.in, true); },
                                 [&](const logic::typing_said::stopped& it) { s_->net->typing(it.in, false); }},
                 said);
    typing_ = step.next;
  }
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::typing
