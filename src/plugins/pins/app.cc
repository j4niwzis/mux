// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.pins.app -- The pins' part of the program: the pin the bar
// shows, worked out as the view moves -- Telegram's: the newest pinned
// above the view, else the oldest; the bar pressed (to it, and the bar on
// to the one before) or swiped; a message pinned or unpinned, from its menu.
export module mux.plugin.pins.app;

import std;
import splice;
import mux.composition;
import mux.core;
import mux.ui;
import mux.app.network;
import mux.app.services;
import mux.plugin.pins.types;
import mux.plugin.pins.ui;
import mux.plugin.pins.matrix;

export namespace mux::plugins::pins {

struct part {
  app::services* s_ = nullptr;

  // Where the bar is in the pins, once it was pressed or swiped: which, of
  // how many, in which chat. Let go of in another chat, or as the pins
  // change.
  struct cycle {
    conversation_id in;
    std::size_t at = 0;
    std::size_t of = 0;
  };
  std::optional<cycle> cycled_;
  // What the bar's pin was worked out from, last: worked out again only
  // where any of it moved -- at every frame, each pin was looked for through
  // the whole timeline, a room of many pins and a long history slowed down
  // with nothing changing.
  struct seen_t {
    conversation_id in;
    std::size_t held = 0;
    std::string front, back;
    std::optional<std::string> top;
    std::vector<std::string> pinned;
    std::optional<std::size_t> cycled;
    std::size_t quoted = 0;  // pins fetched aside since: their lines
    friend bool operator==(const seen_t&, const seen_t&) = default;
  };
  std::optional<std::pair<seen_t, std::optional<pin_view>>> kept_;

  [[nodiscard]] const conversation* chat_shown() const {
    const auto& chosen = s_->showing->look<ui::chat_shown>()->chosen;
    return chosen ? s_->model->find(*chosen) : nullptr;
  }
  void put(shown now) {
    if (*s_->showing->look<shown>() != now)
      ui::show(*s_->showing, std::move(now));
  }
  // Between events: the bar as the chat in view and the view are now.
  void tick() { this->put(shown{this->pin_of(this->chat_shown())}); }

  // The bar pressed: to the pin it shows, as any jump -- and, the view gone
  // up to it, the bar on to the one pinned before it. The pins gone through
  // from there, each press the next before: in the window the jump opens,
  // the pins not loaded have no place to be found above it by, and all of
  // them tied -- the bar went back to the first at every press, however
  // many there were.
  template <class Asked>
  void apply(const pressed&, const Asked& asked) {
    const auto& now = s_->showing->look<shown>()->pin;
    if (!now)
      return;
    if (const conversation* chat = this->chat_shown())
      if (const auto at = std::ranges::find(chat->pinned, now->id); at != chat->pinned.end()) {
        const auto count = chat->pinned.size();
        const auto index = static_cast<std::size_t>(at - chat->pinned.begin());
        cycled_ = cycle{chat->id, (index + count - 1) % count, count};
      }
    asked(ui::request::jump_to_message{now->id});
  }
  // The bar swiped: the pin before or after the one it shows, in the bar
  // only -- as a press moves it on, without the jump.
  void apply(const step& one) {
    const auto& now = s_->showing->look<shown>()->pin;
    const conversation* chat = this->chat_shown();
    if (!now || !chat)
      return;
    const auto at = std::ranges::find(chat->pinned, now->id);
    if (at == chat->pinned.end())
      return;
    const auto count = chat->pinned.size();
    const auto index = static_cast<std::size_t>(at - chat->pinned.begin());
    cycled_ = cycle{chat->id, one.older ? (index + count - 1) % count : (index + 1) % count, count};
    this->put(shown{this->pin_of(chat)});
  }
  // Pinned or unpinned, from a message's menu: by the account the chat in
  // view is of.
  void apply(const pin& one) {
    ui::show<ui::menu_facts>(*s_->showing, std::nullopt);
    const auto& chosen = s_->chosen();
    if (!chosen || s_->demo())
      return;
    s_->net->ask(chosen->account, pin_asked{chosen->id, one.message, one.on});
  }

  std::optional<pin_view> pin_of(const conversation* chat) {
    if (!chat || chat->pinned.empty())
      return std::nullopt;
    if (cycled_ && (cycled_->in != chat->id || cycled_->of != chat->pinned.size()))
      cycled_.reset();
    seen_t seen{chat->id, chat->timeline.size(), chat->timeline.empty() ? std::string() : chat->timeline.front().id,
                chat->timeline.empty() ? std::string() : chat->timeline.back().id, s_->list.top, chat->pinned,
                cycled_ ? std::optional(cycled_->at) : std::nullopt, chat->quoted.size()};
    if (kept_ && kept_->first == seen)
      return kept_->second;
    auto made = this->worked_out(chat);
    kept_ = std::pair{std::move(seen), made};
    return made;
  }
  std::optional<pin_view> worked_out(const conversation* chat) {
    const auto place = [&](const std::string& id) -> std::ptrdiff_t {
      const auto found = std::ranges::find(chat->timeline, id, &message::id);
      return found == chat->timeline.end() ? -1 : found - chat->timeline.begin();
    };
    const std::ptrdiff_t top = s_->list.top ? place(*s_->list.top) : std::numeric_limits<std::ptrdiff_t>::max();
    // Not const: a filter_view caches its begin, so begin() and end() are not.
    auto above = chat->pinned | std::views::enumerate | std::views::filter([&](const auto& one) { return place(std::get<1>(one)) < top; });
    const auto best = std::ranges::max_element(above, {}, [&](const auto& one) { return place(std::get<1>(one)); });
    const std::size_t count = chat->pinned.size();
    const std::size_t at = cycled_ ? cycled_->at : best == std::ranges::end(above) ? 0 : static_cast<std::size_t>(std::get<0>(*best));
    const std::string& id = chat->pinned[at];
    pin_view made{id, count == 1 ? std::string("Pinned message") : std::format("Pinned message #{} of {}", at + 1, count), "A message"};
    if (const message* said = ui::held_message(*chat, id)) {
      made.line = said->body.plain.empty() && said->attachment ? std::string("Photo") : said->body.plain;
      std::ranges::replace(made.line, '\n', ' ');
    }
    return made;
  }
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::pins
