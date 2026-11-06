// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.typing.app -- The typing's part of the program: the user typing
// in the chosen chat, or not, said as the logic of it says (at most every
// twenty seconds while it goes on, and its end), where the chat, its space,
// its account or every account lets it; and who is typing in a chat, past
// once thirty seconds go by with nothing said of it -- a protocol that
// says only that someone started (an XMPP client gone, a Matrix server
// that never said the end), the window woken for it, not drawing frames.
export module mux.plugin.typing.app;

import std;
import splice;
import mux.composition;
import mux.core;
import mux.ui;
import mux.app.network;
import mux.app.kept;
import mux.app.services;
import mux.levels;
import mux.plugin.typing.types;
import mux.plugin.typing.model;
import mux.plugin.typing.changes;

export namespace mux::plugins::typing {

struct part {
  static constexpr auto kPast = std::chrono::seconds(30);
  app::services* s_ = nullptr;
  typing_state typing_{};
  // When each chat's typers are past, unless told of again.
  std::map<conversation_id, std::chrono::steady_clock::time_point> until_;

  void apply(const ui::request::typing& one) {
    if (s_->demo())
      return;
    // Told where the chat, its space, its account or every chat lets it.
    const auto& mine = s_->kept->plugin_settings<level_choices<choices>>();
    const auto allowed = [&](const conversation_id& in) { return choices_in_effect(mine, s_->kept->space_above, in).sent.value_or(true); };
    auto step = typing_after(typing_, one.on, s_->chosen(), std::chrono::steady_clock::now(), allowed);
    for (const auto& said : step.say)
      spl::visit(spl::overloaded{[&](const typing_said::started& it) { s_->net->ask(it.in.account, typing_ask{it.in.id, true}); },
                                 [&](const typing_said::stopped& it) { s_->net->ask(it.in.account, typing_ask{it.in.id, false}); }},
                 said);
    typing_ = step.next;
  }
  // Who is typing, told: past in thirty seconds, unless told again.
  void take(const typing_now& one) {
    if (one.who.empty())
      until_.erase(one.in);
    else
      until_.insert_or_assign(one.in, std::chrono::steady_clock::now() + kPast);
  }
  // When the window must wake for the first to be past: never, where none is.
  [[nodiscard]] double wake_in() const {
    if (until_.empty())
      return std::numeric_limits<double>::infinity();
    const auto next = std::ranges::min(until_ | std::views::values);
    return std::max(0.0, std::chrono::duration<double, std::milli>(next - std::chrono::steady_clock::now()).count());
  }
  // Those past, no longer typing.
  void tick() {
    const auto now = std::chrono::steady_clock::now();
    const auto past = until_ | std::views::filter([&](const auto& one) { return one.second <= now; }) | std::views::keys |
                      std::ranges::to<std::vector>();
    for (const conversation_id& in : past) {
      until_.erase(in);
      if (const conversation* chat = s_->model->find(in); chat && !std::get<chat_part<message>>(chat->plugins).who.empty()) {
        s_->model->apply(change_t{typing_now{in, {}}});
        s_->refresh_due = true;
      }
    }
  }
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::typing
