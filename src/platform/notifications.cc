// SPDX-License-Identifier: AGPL-3.0-only
// mux.platform.notifications -- A notification shown by the system, where
// the platform has a way to: its backend says how (notifications_backend*.cc).
export module mux.platform.notifications;

import std;
import mux.platform.notifications.backend;

export namespace mux::platform::notifications {
// False where the platform has nowhere to show it, or did not take it.
// `sound`: with the system's own sound for a message, or silent.
[[nodiscard]] inline bool notify(std::string_view title, std::string_view text, bool sound) {
  return backend::notify(title, text, sound);
}
// The same for a chat: pressed, the chat `chat` (its account's address and
// its id, a line each) asked to be opened -- where the platform can say so
// (take_opened); else as notify.
[[nodiscard]] inline bool notify_chat(std::string_view title, std::string_view text, bool sound, std::string_view chat) {
  if constexpr (requires { backend::notify_chat(title, text, sound, chat); })
    return backend::notify_chat(title, text, sound, chat);
  else
    return backend::notify(title, text, sound);
}
// A chat a notification pressed asks to open, as notify_chat said it; none.
[[nodiscard]] inline std::optional<std::string> take_opened() {
  if constexpr (requires { backend::take_opened(); })
    return backend::take_opened();
  else
    return std::nullopt;
}
}  // namespace mux::platform::notifications
