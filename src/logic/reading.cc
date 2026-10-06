// SPDX-License-Identifier: AGPL-3.0-only
// mux.logic.reading: what the user's reading and typing come to -- which
// message a chat is read up to, and what is said of typing, and when.
export module mux.logic.reading;

import std;
import splice;
import mux.core;

export namespace mux::logic {

// The message a chat is read up to once it is read to its end: its newest
// from someone else -- none where there is none, or it is read already.
[[nodiscard]] inline std::optional<std::string> to_mark_read(const conversation& one) {
  for (auto it = one.timeline.rbegin(); it != one.timeline.rend(); ++it)
    if (!it->outgoing && !it->id.empty())
      return one.read_up_to == it->id ? std::nullopt : std::optional<std::string>(it->id);
  return std::nullopt;
}

// The message a chat is read up to once the user has seen as far as `seen`,
// as tdesktop counts it: the newest from someone else not after it -- none
// where that is not past what is read already. A window of history away
// from the newest, not holding what is read, says nothing: what is read may
// be past it.
[[nodiscard]] inline std::optional<std::string> read_up_to_seen(const conversation& one, std::string_view seen) {
  const auto index_of = [&](std::string_view id) -> std::ptrdiff_t {
    const auto it = std::ranges::find(one.timeline, id, &message::id);
    return it == one.timeline.end() ? -1 : it - one.timeline.begin();
  };
  const std::ptrdiff_t last = index_of(seen);
  if (last < 0)
    return std::nullopt;
  const std::ptrdiff_t done = one.read_up_to ? index_of(*one.read_up_to) : -1;
  if (done < 0 && one.read_up_to && one.detached)
    return std::nullopt;
  for (std::ptrdiff_t i = last; i > done; --i) {
    const message& each = one.timeline[static_cast<std::size_t>(i)];
    if (!each.outgoing && !each.id.empty())
      return each.id;
  }
  return std::nullopt;
}

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

}  // namespace mux::logic
