// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.typing.xmpp -- Typing over XMPP, the plugin's own: XEP-0085's
// chat states. Who is typing in a chat, as the states in the messages that
// come say (a person's, or a room's occupants' but one's own), told to the
// program as the plugin's change; and the user typing, or not, sent to a
// room, or to a person whose client says it understands chat states (asked
// once, by disco#info, or seen in what they sent). Through the account's
// door for plugins (xmpp_message, xmpp_kept_type).
export module mux.plugin.typing.xmpp;

import std;
import tern;
import mux.core;
import mux.proto.xmpp.client;
import mux.plugin.typing.types;
export import mux.plugin.typing.changes;

export namespace mux::plugins::typing {

// What it keeps of an account while it is connected: whether each person's
// client understands chat states; what was said to each last; and who is
// typing in each chat, as their states said.
struct xmpp_kept {
  std::map<std::string, bool> supported, sent;
  std::map<std::string, std::set<std::string>> heard;
};
constexpr type_tag<xmpp_kept> xmpp_kept_type(contract::xmpp_events, type_tag<feature>) { return {}; }

// A message come: the chat state it carries, where it carries one; a
// message with a body and none ends its sender's typing.
template <class Message, class Account>
void xmpp_message(contract::xmpp_events, type_tag<feature> f, const Message& got, bool group, Account& account) {
  if (!got.from)
    return;
  const std::string& from = *got.from;
  const auto slash = from.find('/');
  const std::string room = from.substr(0, slash);
  const std::string nick = slash == std::string::npos ? std::string() : from.substr(slash + 1);
  if (group) {
    const auto own = account.nick_in(room);
    if (!own || nick == *own || nick.empty())
      return;
  }
  const std::string sender = group ? from : room;
  bool said = false, composing = false;
  for (const auto& p : got.payload) {
    composing = composing || p.template is<tern::chat_states::composing>();
    said = said || p.template is<tern::chat_states::active>() || p.template is<tern::chat_states::composing>() ||
           p.template is<tern::chat_states::paused>() || p.template is<tern::chat_states::inactive>() ||
           (!group && p.template is<tern::chat_states::gone>());
  }
  auto& kept = account.kept_for(f);
  if (!group && said)
    kept.supported[room] = true;
  if (!said && !got.body)
    return;
  auto& who = kept.heard[room];
  if (composing)
    who.insert(sender);
  else
    who.erase(sender);
  account.tell_program(typing_now{{account.id(), room}, who | std::ranges::to<std::vector>()});
}

template <class Account>
concept xmpp_account = requires(Account& account, const std::string& room) {
  account.nick_in(room);
  account.connected();
};
// The user typing, or not (typing_ask, from the program's part): composing,
// or paused, once each time it changes.
template <xmpp_account Account>
void told_typing(Account& account, const typing_ask& ask) {
  account.on_fiber([&account, ask] {
    if (!account.connected())
      return;
    auto& kept = account.kept_for(type_tag<feature>{});
    const bool room = account.nick_in(ask.to).has_value();
    if (!room && !kept.supported.contains(ask.to)) {
      const auto info = account.template request<tern::query::disco_info>(tern::asking<tern::query::disco_info>{.to = ask.to});
      kept.supported[ask.to] = info && std::ranges::contains(info->features, tern::chat_states::ns, &tern::disco::feature::var);
    }
    if (!account.connected() || (!room && !kept.supported[ask.to]))
      return;
    if (const auto previous = kept.sent.find(ask.to); previous != kept.sent.end() && previous->second == ask.on)
      return;
    kept.sent[ask.to] = ask.on;
    if (ask.on)
      account.send_payload(ask.to, tern::chat_states::composing{});
    else
      account.send_payload(ask.to, tern::chat_states::paused{});
  });
}

}  // namespace mux::plugins::typing
