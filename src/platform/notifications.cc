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
}  // namespace mux::platform::notifications
