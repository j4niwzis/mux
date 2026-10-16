// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.reactions.app -- The reactions' part of the program: a
// message's reactions listed as the events they are -- those that came
// without one, read back from the history, at the message's time -- and the
// lists opened from one another, gone back through.
export module mux.plugin.reactions.app;

import std;
import mux.composition;
import mux.core;
import mux.ui;
import mux.app.services;
import mux.plugin.reactions.types;

export namespace mux::plugins::reactions {

struct part {
  app::services* s_ = nullptr;
  // The lists under the one shown, opened from a reaction in it.
  std::vector<shown> pages_;

  void apply(const show& one) {
    ui::show<ui::menu_facts>(*s_->showing, std::nullopt);
    const auto& chosen = s_->chosen();
    const conversation* chat = chosen ? s_->model->find(*chosen) : nullptr;
    if (!chat)
      return;
    const message* said = ui::held_message(*chat, one.message);
    if (!said)
      return;
    auto events = said->reaction_events;
    std::ranges::stable_sort(events, {}, &message::reaction_event::at);
    auto entries = events | std::views::transform([&](const message::reaction_event& event) {
                     return entry{event.event, event.who, ui::sender_name(*chat, event.who), event.key, event.at,
                                  event.who == chat->id.account.address, one.message, event.shortcode};
                   }) | std::ranges::to<std::vector>();
    // Those whose reaction came without its event -- read back from the
    // history -- listed too, at the message's time.
    for (const auto& reacted : said->reactions) {
      const auto& [key, who] = reacted;
      entries.append_range(who | std::views::filter([&](const std::string& user) {
                             return std::ranges::none_of(events, [&](const auto& event) { return event.key == key && event.who == user; });
                           }) | std::views::transform([&](const std::string& user) {
                             return entry{std::string(), user, ui::sender_name(*chat, user), key, said->at,
                                          user == chat->id.account.address, one.message};
                           }));
    }
    const auto& current = *s_->showing->look<std::optional<shown>>();
    if (current && current->in == chat->id && current->event != one.message)
      pages_.push_back(*current);
    else if (!current || current->in != chat->id)
      pages_.clear();
    ui::show(*s_->showing, std::optional(shown{chat->id, std::move(entries), &*s_->model, one.message, !pages_.empty()}));
  }
  void apply(const ui::request::close_shown<shown>&) {
    pages_.clear();
    ui::show<shown>(*s_->showing, std::nullopt);
  }
  // The list under it: made again from its event, with the reactions that
  // came since it was opened.
  void apply(const back&) {
    if (pages_.empty())
      return;
    auto page = std::move(pages_.back());
    pages_.pop_back();
    ui::show<shown>(*s_->showing, std::nullopt);
    auto under = std::move(pages_);
    this->apply(show{page.event});
    pages_ = std::move(under);
    ui::change_shown<std::optional<shown>>(*s_->showing, [&](auto& now) {
      if (now)
        now->back = !pages_.empty();
    });
  }
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::reactions
