// SPDX-License-Identifier: AGPL-3.0-only
// mux.logic.reading: what the user's reading and typing come to -- which
// message a chat is read up to, and what is said of typing, and when.
export module mux.logic.reading;

import std;
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
using typing_said_t = std::variant<typing_said::started, typing_said::stopped>;

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
[[nodiscard]] inline typing_step typing_after(typing_state now, bool on, const std::optional<conversation_id>& chosen,
                                              std::chrono::steady_clock::time_point at,
                                              const std::function<bool(const conversation_id&)>& allowed) {
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
