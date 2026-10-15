// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.suggestions: the list over the chat's composer, as Telegram's --
// the members matching what follows an @ at the end of what is written (a
// group's, by name or id), or the emoji matching what follows a ':' (its
// room's custom ones, then Unicode's by name and keyword); Up and Down
// through it, Enter or Tab or a press picks, Esc closes it until something
// else is asked. Worked out each frame from what is written, written to the
// shown model where it changed; the one picked put in the field. A part of
// the program: it reaches the rest through the services.
export module mux.app.suggestions;

import std;
import splice;
import mux.core;
import mux.logic.text;
import mux.logic.emoji;
import mux.ui;
import mux.ui.proto;
import mux.proto;
import mux.protocols;
import mux.app.services;
import mux.app.requests;

export namespace mux::app {

class suggestions_part {
 public:
  explicit suggestions_part(services& shared) : s_(&shared) {}
  suggestions_part(const suggestions_part&) = delete;
  suggestions_part& operator=(const suggestions_part&) = delete;

  // What is written now asks: an @ or a ':' at its end -- one at its start
  // or after a space -- with what follows it, no space in it (two letters
  // at least, for a ':'). The same asked again keeps its list and what is
  // lit; one closed stays closed.
  void show() {
    auto& screen = s_->root().main();
    const auto& chosen = s_->showing->look<mux::ui::chat_shown>()->chosen;
    const mux::conversation* chat = chosen ? s_->model->find(*chosen) : nullptr;
    const std::optional<asked_t> now = chat ? asked_in(mux::ui::composer_text(screen.line), *chat) : std::nullopt;
    if (now == asked_)
      return;
    asked_ = now;
    lit_ = 0;
    items_ = now && now != closed_ ? this->found(*now, *chat) : std::vector<mux::ui::suggestion>{};
    if (!now)
      closed_.reset();
    this->put();
  }

  void apply(const request::suggestion_step& one) {
    if (items_.empty())
      return;
    const std::size_t n = items_.size();
    lit_ = one.down ? (lit_ + 1) % n : (lit_ + n - 1) % n;
    this->put();
  }
  void apply(const request::suggestion_close&) {
    closed_ = asked_;
    items_.clear();
    this->put();
  }
  // One picked: the @ and what follows it made their name -- kept to be
  // mentioned when it is sent -- or the ':' and what follows it that emoji.
  void apply(const request::suggestion_pick& one) {
    const std::size_t at = one.at.value_or(lit_);
    if (at >= items_.size() || !asked_)
      return;
    const mux::ui::suggestion picked = items_[at];
    auto& line = s_->root().main().line;
    const std::string& text = mux::ui::composer_text(line);
    spl::visit(spl::overloaded{[&](const mention_asked&) {
                                 if (const auto sign = text.rfind('@'); sign != std::string::npos)
                                   mux::ui::put_mention(line, sign, picked.title, picked.id);
                               },
                               [&](const emoji_asked&) {
                                 const auto colon = text.rfind(':');
                                 if (colon == std::string::npos)
                                   return;
                                 if (picked.picture.empty()) {
                                   mux::ui::put_over_end(line, colon, picked.glyph);
                                 } else {
                                   mux::ui::composer_field(line).select(colon, text.size());
                                   mux::ui::composer_field(line).insertAtom(" ", picked.picture, picked.glyph, true);
                                 }
                               }},
               asked_->kind);
    items_.clear();
    this->put();
  }

 private:
  struct mention_asked {
    friend bool operator==(mention_asked, mention_asked) = default;
  };
  struct emoji_asked {
    friend bool operator==(emoji_asked, emoji_asked) = default;
  };
  struct asked_t {
    spl::variant<mention_asked, emoji_asked> kind;
    std::string query;
    mux::conversation_id in;
    friend bool operator==(const asked_t&, const asked_t&) = default;
  };
  // What follows the last `sign` at the end of what is written, where it is
  // at its start or after a space or a line, and nothing after it breaks it.
  static std::optional<std::string> after_sign(const std::string& text, char sign, std::string_view breaks, std::size_t least) {
    const auto at = text.rfind(sign);
    if (at == std::string::npos || !(at == 0 || text[at - 1] == ' ' || text[at - 1] == '\n'))
      return std::nullopt;
    const std::string_view after = std::string_view(text).substr(at + 1);
    if (after.size() < least || after.find_first_of(breaks) != std::string_view::npos)
      return std::nullopt;
    return std::string(after);
  }
  static std::optional<asked_t> asked_in(const std::string& text, const mux::conversation& chat) {
    if (auto query = after_sign(text, '@', " \n", 0); query && mux::ui::is_group(chat))
      return asked_t{mention_asked{}, std::move(*query), chat.id};
    if (auto query = after_sign(text, ':', " \n:", 2))
      return asked_t{emoji_asked{}, std::move(*query), chat.id};
    return std::nullopt;
  }
  std::vector<mux::ui::suggestion> found(const asked_t& asked, const mux::conversation& chat) const {
    constexpr auto lower = mux::logic::folded;
    const std::string wanted = lower(asked.query);
    auto out = spl::visit(
        spl::overloaded{[&](const mention_asked&) {
                          // The chat's members, by name or id -- not the reader.
                          return chat.members | std::views::filter([&](const mux::member& one) {
                                   return one.id != chat.id.account.address &&
                                          (lower(one.name).contains(wanted) || lower(one.id).contains(wanted));
                                 }) |
                                 std::views::take(6) | std::views::transform([](const mux::member& one) {
                                   return mux::ui::suggestion{.person = true, .id = one.id, .title = one.name.empty() ? one.id : one.name,
                                                              .sub = one.id};
                                 }) |
                                 std::ranges::to<std::vector>();
                        },
                        [&](const emoji_asked&) {
                          // The room's own, where its protocol has packs; then
                          // Unicode's, by name and keyword (CLDR's).
                          std::vector<mux::ui::suggestion> emoji;
                          if (mux::proto::offers(mux::ui::protocol_state_of(s_->ui, chat.id.account), mux::proto::feature::sticker_packs{}))
                            for (const auto& one : chat.emotes) {
                              if (emoji.size() == 6)
                                break;
                              if (!lower(one.shortcode).contains(wanted) && !lower(one.body).contains(wanted))
                                continue;
                              const std::string label = ":" + one.shortcode + ":";
                              if (std::ranges::any_of(emoji, [&](const auto& had) { return had.picture == one.url && had.glyph == label; }))
                                continue;
                              emoji.push_back({.glyph = label, .picture = one.url, .title = one.shortcode, .sub = label});
                            }
                          emoji.append_range(mux::logic::emoji_found(asked.query, 6 - emoji.size()) | std::views::transform([](const auto* one) {
                                               return mux::ui::suggestion{.glyph = std::string(one->text), .title = std::string(one->name)};
                                             }));
                          return emoji;
                        }},
        asked.kind);
    std::ranges::for_each(std::views::enumerate(out), [](auto pair) { std::get<1>(pair).at = static_cast<std::size_t>(std::get<0>(pair)); });
    return out;
  }
  void put() {
    mux::ui::suggestions_shown now{items_};
    std::ranges::for_each(now.items, [&](mux::ui::suggestion& one) { one.lit = one.at == lit_; });
    if (*s_->showing->look<mux::ui::suggestions_shown>() != now)
      mux::ui::show(*s_->showing, std::move(now));
  }

  services* s_;
  std::optional<asked_t> asked_, closed_;
  std::vector<mux::ui::suggestion> items_;
  std::size_t lit_ = 0;
};

}  // namespace mux::app
