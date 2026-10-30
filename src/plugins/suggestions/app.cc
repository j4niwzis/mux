// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.suggestions.app -- The suggestions' part of the program: the
// list over the chat's composer, as Telegram's -- the members matching what
// follows an @ at the end of what is written (a group's, by name or id), or
// the emoji matching what follows a ':' (its room's custom ones, then
// Unicode's by name and keyword); Up and Down through it, Enter or Tab or a
// press picks, Esc closes it until something else is asked. Worked out each
// frame from what is written; the one picked put in the field.
export module mux.plugin.suggestions.app;

import std;
import splice;
import mux.composition;
import mux.core;
import mux.logic.text;
import mux.logic.emoji;
import mux.ui;
import mux.proto;
import mux.protocols;
import mux.app.services;
import mux.app.commands;
import mux.plugin.suggestions.types;
import mux.plugin.suggestions.ui;

export namespace mux::plugins::suggestions {

struct part {
  app::services* s_ = nullptr;

  // What is written now asks: an @ or a ':' at its end -- one at its start
  // or after a space -- with what follows it, no space in it (two letters
  // at least, for a ':'). The same asked again keeps its list and what is
  // lit; one closed stays closed.
  void tick() {
    const auto& chosen = s_->showing->look<mux::ui::chat_shown>()->chosen;
    const mux::conversation* chat = chosen ? s_->model->find(*chosen) : nullptr;
    const std::optional<asked_t> now = chat ? asked_in(s_->chat_writing().text, *chat) : std::nullopt;
    if (now == asked_)
      return;
    asked_ = now;
    lit_ = 0;
    items_ = now && now != closed_ ? this->found(*now, *chat) : std::vector<suggestion>{};
    if (!now)
      closed_.reset();
    this->put();
  }

  void apply(const ui::request::suggestion_step& one) {
    if (items_.empty())
      return;
    const std::size_t n = items_.size();
    lit_ = one.down ? (lit_ + 1) % n : (lit_ + n - 1) % n;
    this->put();
  }
  void apply(const ui::request::suggestion_close&) {
    closed_ = asked_;
    items_.clear();
    this->put();
  }
  // One picked: the @ and what follows it made their name -- kept to be
  // mentioned when it is sent -- or the ':' and what follows it that emoji.
  void apply(const ui::request::suggestion_pick& one) {
    const std::size_t at = one.at.value_or(lit_);
    if (at >= items_.size() || !asked_)
      return;
    const suggestion picked = items_[at];
    s_->edit_chat_writing([&](mux::ui::composer_doc& now) {
      const std::string& text = now.text;
      spl::visit(spl::overloaded{[&](const mention_asked&) {
                                   if (const auto sign = text.rfind('@'); sign != std::string::npos)
                                     mux::ui::put_mention(now, sign, picked.title, picked.id);
                                 },
                                 [&](const alias_asked&) {
                                   // The # and what follows it made the room's address, and a space.
                                   if (const auto sign = text.rfind('#'); sign != std::string::npos)
                                     mux::ui::put_over_end(now, sign, picked.sub + " ");
                                 },
                                 [&](const command_asked&) {
                                   // The command's name, and a space for what follows it.
                                   now = mux::ui::plain_draft("/" + picked.title + " ");
                                 },
                                 [&](const emoji_asked&) {
                                   const auto colon = text.rfind(':');
                                   if (colon == std::string::npos)
                                     return;
                                   if (picked.picture.empty())
                                     mux::ui::put_over_end(now, colon, picked.glyph);
                                   else
                                     mux::ui::put_picture_over_end(now, colon, picked.picture, picked.glyph);
                                 }},
                 asked_->kind);
    });
    items_.clear();
    this->put();
  }

  struct mention_asked {
    friend bool operator==(mention_asked, mention_asked) = default;
  };
  struct emoji_asked {
    friend bool operator==(emoji_asked, emoji_asked) = default;
  };
  struct command_asked {
    friend bool operator==(command_asked, command_asked) = default;
  };
  struct alias_asked {
    friend bool operator==(alias_asked, alias_asked) = default;
  };
  struct asked_t {
    spl::variant<mention_asked, emoji_asked, command_asked, alias_asked> kind;
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
    // A command being named: a '/' first, and no space after it yet.
    if (text.starts_with('/') && !text.starts_with("//") && text.find_first_of(" \n") == std::string::npos)
      return asked_t{command_asked{}, text.substr(1), chat.id};
    if (auto query = after_sign(text, '@', " \n", 0); query && mux::ui::is_group(chat))
      return asked_t{mention_asked{}, std::move(*query), chat.id};
    if (auto query = after_sign(text, '#', " \n", 0))
      return asked_t{alias_asked{}, std::move(*query), chat.id};
    if (auto query = after_sign(text, ':', " \n:", 2))
      return asked_t{emoji_asked{}, std::move(*query), chat.id};
    return std::nullopt;
  }
  std::vector<suggestion> found(const asked_t& asked, const mux::conversation& chat) const {
    constexpr auto lower = mux::logic::folded;
    const std::string wanted = lower(asked.query);
    auto out = spl::visit(
        spl::overloaded{[&](const mention_asked&) {
                          // The chat's members, by name or id -- not the reader.
                          return chat.members | std::views::filter([&](const mux::member& one) {
                                   return one.id != chat.id.account.address &&
                                          (lower(one.name).contains(wanted) || lower(one.id).contains(wanted));
                                 }) | std::views::take(6) | std::views::transform([](const mux::member& one) {
                                   return suggestion{.person = true, .id = one.id, .title = one.name.empty() ? one.id : one.name,
                                                              .sub = one.id};
                                 }) | std::ranges::to<std::vector>();
                        },
                        [&](const alias_asked&) {
                          // The rooms of the chat's account with an address
                          // -- their canonical alias, or one of their others
                          // -- matching what is typed, by it or by name.
                          std::vector<suggestion> rooms;
                          if (const mux::account* account = s_->model->accounts().find(chat.id.account))
                            for (const mux::conversation& room : account->conversations.values()) {
                              if (rooms.size() == 6)
                                break;
                              const auto address = room.alias ? room.alias : room.other_aliases.empty()
                                                                                 ? std::optional<std::string>()
                                                                                 : std::optional(room.other_aliases.front());
                              if (!address || (!lower(*address).contains(wanted) && !lower(room.name).contains(wanted)))
                                continue;
                              rooms.push_back({.person = true, .id = room.id.id, .title = room.name.empty() ? *address : room.name,
                                               .sub = *address});
                            }
                          return rooms;
                        },
                        [&](const command_asked&) {
                          // The plugins' commands (contract::slash_commands)
                          // whose names start with what is typed. What is
                          // typed read first: the commands are a constant, and
                          // clang tried to fold the whole pipeline while
                          // compiling -- the case folding's Unicode tables
                          // evaluated, 77 of this unit's 108 s (CI's traces) --
                          // before it came to what is typed, which no constant
                          // knows.
                          return app::kCommands | std::views::filter([&](const app::command_said& one) {
                                   return one.name.size() >= wanted.size() && lower(std::string(one.name)).starts_with(wanted);
                                 }) | std::views::take(8) | std::views::transform([](const app::command_said& one) {
                                   return suggestion{.glyph = "/", .title = std::string(one.name),
                                                     .sub = std::format("{} -- {}", one.usage, one.summary)};
                                 }) | std::ranges::to<std::vector>();
                        },
                        [&](const emoji_asked&) {
                          // The room's own, where its protocol has packs; then
                          // Unicode's, by name and keyword (CLDR's).
                          std::vector<suggestion> emoji;
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
                                               return suggestion{.glyph = std::string(one->text), .title = std::string(one->name)};
                                             }));
                          return emoji;
                        }},
        asked.kind);
    for (auto pair : out | std::views::enumerate) { std::get<1>(pair).at = static_cast<std::size_t>(std::get<0>(pair)); }
    return out;
  }
  void put() {
    shown now{items_};
    for (suggestion& one : now.items) { one.lit = one.at == lit_; }
    // Its keys -- Up, Down, Enter, Tab, Esc -- the screen's to catch while
    // it is up.
    s_->composer_list_up = !now.items.empty();
    if (*s_->showing->look<shown>() != now)
      mux::ui::show(*s_->showing, std::move(now));
  }

  std::optional<asked_t> asked_, closed_;
  std::vector<suggestion> items_;
  std::size_t lit_ = 0;
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::suggestions
