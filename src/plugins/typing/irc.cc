// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.typing.irc -- Typing over IRC, the plugin's own: the IRCv3
// typing client tag (+typing=active, paused or done, on a TAGMSG). Who is
// typing in a chat, as the tags that come say -- a line said ends its
// sender's typing -- told to the program as the plugin's change; and the
// user typing, or not, sent to the chat. Through the account's door for
// plugins (irc_message, irc_kept_type).
export module mux.plugin.typing.irc;

import std;
import splice;
import cairn;
import mux.core;
import mux.proto.irc.client;
import mux.plugin.typing.types;
export import mux.plugin.typing.changes;

export namespace mux::plugins::typing {

// What it keeps of an account while it is connected: who is typing in each
// chat, as their tags said; and what was said to each last.
struct irc_kept {
  std::map<std::string, std::set<std::string>> heard;
  std::map<std::string, bool> sent;
};
constexpr type_tag<irc_kept> irc_kept_type(contract::irc_events, type_tag<feature>) { return {}; }

// A line come: a TAGMSG's typing tag, or a message said -- which ends its
// sender's typing.
template <class Account>
void irc_message(contract::irc_events, type_tag<feature> f, const cairn::message& got, Account& account) {
  const auto& l = got.line;
  if (!l.from || account.mine_nick(l.from->nick))
    return;
  const std::string nick(l.from->nick);
  const auto target = spl::visit(spl::overloaded{[](const cairn::msg::tagmsg& one) { return std::optional<std::string_view>(one.target); },
                                                 [](const cairn::msg::privmsg& one) { return std::optional<std::string_view>(one.target); },
                                                 [](const cairn::msg::notice& one) { return std::optional<std::string_view>(one.target); },
                                                 [](const cairn::msg::action& one) { return std::optional<std::string_view>(one.target); },
                                                 [](const auto&) { return std::optional<std::string_view>(); }},
                                 got.what);
  if (!target)
    return;
  const bool tagged = spl::visit(spl::overloaded{[](const cairn::msg::tagmsg&) { return true; }, [](const auto&) { return false; }}, got.what);
  const auto state = l.tag_value("+typing");
  if (tagged && !state)
    return;
  const bool typing = state && std::ranges::equal(*state, std::string_view("active"));
  const std::string chat = account.chat_for(*target, nick);
  auto& who = account.kept_for(f).heard[chat];
  if (typing)
    who.insert(nick);
  else if (!who.erase(nick))
    return;
  account.tell_program(typing_now{{account.id(), chat}, who | std::ranges::to<std::vector>()});
}

template <class Account>
concept irc_account = requires(Account& account, std::string_view to) {
  account.send_tags(to, to);
  account.mine_nick(to);
};
// The user typing, or not (typing_ask, from the program's part): active, or
// paused, once each time it changes.
template <irc_account Account>
void told_typing(Account& account, const typing_ask& ask) {
  account.on_fiber([&account, ask] {
    if (!account.connected())
      return;
    auto& kept = account.kept_for(type_tag<feature>{});
    if (const auto previous = kept.sent.find(ask.to); previous != kept.sent.end() && previous->second == ask.on)
      return;
    kept.sent[ask.to] = ask.on;
    account.send_tags(ask.to, ask.on ? "+typing=active" : "+typing=paused");
  });
}

}  // namespace mux::plugins::typing
