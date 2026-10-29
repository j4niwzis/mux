// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.typing.ui -- Who is typing in a chat, by their names, as
// Telegram says it in the chat's head: one, two ("A and B"), three ("A, B
// and C"); past three, the first two and how many more.
export module mux.plugin.typing.ui;

import std;
import mux.composition;
import mux.core;
import mux.ui.base;
import mux.plugin.typing.types;

export namespace mux::plugins::typing {

// The chat's status in its head, while someone types (mux.ui:header).
inline std::optional<std::string> extend(contract::chat_status, type_tag<feature>, const conversation& one) {
  if (one.typing.empty())
    return std::nullopt;
  const auto names = one.typing | std::views::transform([&](const std::string& who) { return ui::sender_name(one, who); }) |
                     std::ranges::to<std::vector>();
  switch (names.size()) {
    case 1: return names[0] + " is typing…";
    case 2: return std::format("{} and {} are typing…", names[0], names[1]);
    case 3: return std::format("{}, {} and {} are typing…", names[0], names[1], names[2]);
    default: return std::format("{}, {} and {} more are typing…", names[0], names[1], names.size() - 2);
  }
}

}  // namespace mux::plugins::typing
