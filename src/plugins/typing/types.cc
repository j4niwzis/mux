// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.typing.types -- What the typing plugin names, and what it says
// it provides: the chat's status in its head while someone types; its
// choice at each level -- whether others are told one is typing -- kept as
// its own, with its rows; and a part of the program. Light: mux.ui sees it.
export module mux.plugin.typing.types;

import std;
import splice;
import knot;
import mux.core;
import mux.composition;
import mux.levels;

export namespace mux::plugins::typing {

struct feature {
  static constexpr std::string_view name = "typing";
};

consteval bool provides(contract::chat_status, type_tag<feature>) { return true; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

// Its choice at each level (mux.levels): whether others are told one is
// typing (m.typing, XEP-0085's chat states) -- never what; unsaid, as the
// level over it says; said nowhere, told.
struct choices {
  std::optional<bool> sent;
  friend bool operator==(const choices&, const choices&) = default;
  friend consteval auto json_schema(knot::type<choices>) { return knot::schema<choices>(); }
};
[[nodiscard]] inline choices filled_from(choices below, const choices& above) {
  if (!below.sent)
    below.sent = above.sent;
  return below;
}
consteval bool provides(contract::settings_kept, type_tag<feature>) { return true; }
constexpr type_tag<level_choices<choices>> settings_type(contract::settings_kept, type_tag<feature>) { return {}; }
consteval bool provides(contract::choice_rows, type_tag<feature>) { return true; }
// On an account's Privacy page: whether others are told one is typing.
consteval bool provides(contract::account_page_rows, type_tag<feature>) { return true; }

// The user typing in a chat, or not, asked of its account: told by each
// protocol's side of the plugin (told_typing, found by ADL on the ask --
// mux.plugin.typing.matrix, .xmpp), where the account is one it tells.
// What is said of the user's typing: that it started in a chat, or that it
// stopped in one.
namespace typing_said {
struct started {
  conversation_id in;
};
struct stopped {
  conversation_id in;
};
}  // namespace typing_said
using typing_said_t = spl::variant<typing_said::started, typing_said::stopped>;

// Where 'typing' was last said, and when.
struct typing_state {
  std::optional<conversation_id> in;
  std::chrono::steady_clock::time_point said{};
  friend bool operator==(const typing_state&, const typing_state&) = default;
};
struct typing_step {
  std::vector<typing_said_t> say;
  typing_state next;
};
// The field's text there or not, in the chat chosen, at a time: 'stopped'
// where it was said somewhere and has stopped or the chat was left;
// 'started' where there is text and it was not said here in the last twenty
// seconds -- each only where `allowed` says the account's privacy lets it.
template <std::predicate<const conversation_id&> Allowed>
[[nodiscard]] typing_step typing_after(typing_state now, bool on, const std::optional<conversation_id>& chosen,
                                       std::chrono::steady_clock::time_point at, const Allowed& allowed) {
  typing_step out;
  if (now.in && (!on || now.in != chosen)) {
    if (allowed(*now.in))
      out.say.emplace_back(typing_said::stopped{*now.in});
    now.in.reset();
  }
  if (on && chosen && allowed(*chosen) && (now.in != chosen || at - now.said > std::chrono::seconds(20))) {
    out.say.emplace_back(typing_said::started{*chosen});
    now.in = chosen;
    now.said = at;
  }
  out.next = now;
  return out;
}

struct typing_ask {
  std::string to;
  bool on = false;
  template <class Account>
    requires requires(Account& account, const typing_ask& ask) { told_typing(account, ask); }
  void operator()(Account& account) const {
    told_typing(account, *this);
  }
};

}  // namespace mux::plugins::typing
