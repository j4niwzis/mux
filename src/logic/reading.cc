// SPDX-License-Identifier: AGPL-3.0-only
// mux.logic.reading: what the user's reading comes to -- which message a
// chat is read up to.
export module mux.logic.reading;

import std;
import mux.core;

export namespace mux::logic {

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

// The message a chat is read up to once it is read to its end: its newest
// from someone else -- none where there is none, or it is read already.
[[nodiscard]] inline std::optional<std::string> to_mark_read(const conversation& one) {
  for (auto it = one.timeline.rbegin(); it != one.timeline.rend(); ++it)
    if (!it->outgoing && !it->id.empty())
      return read_up_to_seen(one, it->id);
  return std::nullopt;
}

}  // namespace mux::logic
