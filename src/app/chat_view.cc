// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.chat_view: what the chat's own bands say, as the program has it --
// its head (who, how they are, who is typing), the pinned bar (the pin above
// what the list shows), the banner its protocol puts over the composer (and
// whether it was dismissed), and the connection notices over the list and
// the chat. Written to the shown model each frame where it changed; the
// bands read it and keep nothing. A part of the program: it reaches the
// rest through the services.
export module mux.app.chat_view;

import std;
import splice;
import mux.core;
import mux.config;
import mux.ui;
import mux.ui.proto;
import mux.proto;
import mux.protocols;
import mux.app.services;
import mux.app.requests;

export namespace mux::app {

class chat_view_part {
 public:
  explicit chat_view_part(services& shared) : s_(&shared) {}
  chat_view_part(const chat_view_part&) = delete;
  chat_view_part& operator=(const chat_view_part&) = delete;

  // What the bands say now: each written where it changed.
  void show() {
    const auto& shown = *s_->showing->look<mux::ui::chat_shown>();
    const auto& screen = s_->root().main();
    const mux::conversation* chat = shown.chosen ? s_->model->find(*shown.chosen) : nullptr;
    auto head = mux::ui::chat_head_of(s_->ui, chat, *s_->model);
    head.back = screen.single;
    head.hidden = s_->showing->look<mux::ui::search_found>()->open || s_->showing->look<mux::ui::selection_shown>()->count > 0;
    this->put(std::move(head));
    this->put(mux::ui::chats_shown{.none = s_->model->accounts().empty(), .chosen = chat != nullptr});
    this->put(mux::ui::pinned_shown{this->pin_of(chat)});
    this->put(this->banner_of(chat));
    // The account listed's, over the list; the chat's, over the chat --
    // where it is another's, or the two are not side by side.
    const auto notice = [&](const std::optional<mux::account_id>& of) {
      const mux::account* one = of ? s_->model->accounts().find(*of) : nullptr;
      return one ? mux::ui::connection_notice(one->state) : std::string();
    };
    const bool chat_band = chat && (screen.single || shown.current != chat->id.account);
    this->put(mux::ui::connection_shown{notice(shown.current), chat_band ? notice(chat->id.account) : std::string()});
  }

  // The pinned bar pressed: to the pin it shows, as any jump -- and, the
  // view gone up to it, the bar on to the one pinned before it.
  template <class Asked>
  void apply(const request::pinned_pressed&, const Asked& asked) {
    if (const auto& pin = s_->showing->look<mux::ui::pinned_shown>()->pin)
      asked(request::jump_to_message{pin->id});
  }
  // The banner's button: its protocol's; its close: let go until it says
  // something else.
  template <class Asked>
  void apply(const request::banner_button&, const Asked& asked) {
    if (const auto& chosen = s_->showing->look<mux::ui::chat_shown>()->chosen)
      asked(request::banner_pressed{*chosen});
  }
  void apply(const request::banner_close&) {
    if (const auto& chosen = s_->showing->look<mux::ui::chat_shown>()->chosen)
      dismissed_.insert_or_assign(*chosen, s_->showing->look<mux::ui::banner_shown>()->text);
  }

 private:
  template <class Part>
  void put(Part now) {
    if (*s_->showing->look<Part>() != now)
      mux::ui::show(*s_->showing, std::move(now));
  }
  // The pin the bar shows, as Telegram's: the newest pinned above the view
  // -- before the first message seen -- else the oldest; worked out again
  // as the view moves. A pin not loaded is taken as older than all that is.
  std::optional<mux::ui::pinned_view> pin_of(const mux::conversation* chat) {
    if (!chat || chat->pinned.empty())
      return std::nullopt;
    const auto place = [&](const std::string& id) -> std::ptrdiff_t {
      const auto found = std::ranges::find(chat->timeline, id, &mux::message::id);
      return found == chat->timeline.end() ? -1 : found - chat->timeline.begin();
    };
    const std::ptrdiff_t top = s_->list.top ? place(*s_->list.top) : std::numeric_limits<std::ptrdiff_t>::max();
    const auto above = chat->pinned | std::views::enumerate |
                       std::views::filter([&](const auto& pin) { return place(std::get<1>(pin)) < top; });
    const auto best = std::ranges::max_element(above, {}, [&](const auto& pin) { return place(std::get<1>(pin)); });
    const std::size_t count = chat->pinned.size();
    const std::size_t at = best == std::ranges::end(above) ? 0 : static_cast<std::size_t>(std::get<0>(*best));
    const std::string& id = chat->pinned[at];
    mux::ui::pinned_view shown{id, count == 1 ? std::string("Pinned message") : std::format("Pinned message #{} of {}", at + 1, count),
                               "A message"};
    if (const mux::message* said = mux::ui::held_message(*chat, id)) {
      shown.line = said->body.plain.empty() && said->attachment ? std::string("Photo") : said->body.plain;
      std::ranges::replace(shown.line, '\n', ' ');
    }
    return shown;
  }
  // What its protocol says over the composer, the first's button; nothing
  // where it was dismissed and says the same.
  mux::ui::banner_shown banner_of(const mux::conversation* chat) {
    if (!chat)
      return {};
    const auto banners = mux::proto::composer_banners(mux::ui::protocol_state_of(s_->ui, chat->id.account), *chat, *s_->model);
    mux::ui::banner_shown now{
        .text = std::ranges::to<std::string>(std::views::join_with(std::views::transform(banners, &mux::proto::part::banner::text), '\n')),
        .button = banners.empty() ? std::string() : banners.front().button,
        .tone = banners.empty() ? mux::proto::part::tone_t{mux::proto::part::tone::plain{}} : banners.front().tone};
    if (now.text.empty())
      dismissed_.erase(chat->id);
    else if (const auto gone = dismissed_.find(chat->id); gone != dismissed_.end() && gone->second == now.text)
      return {};
    return now;
  }

  services* s_;
  std::map<mux::conversation_id, std::string> dismissed_;
};

}  // namespace mux::app
