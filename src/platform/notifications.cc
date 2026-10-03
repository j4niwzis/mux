// SPDX-License-Identifier: AGPL-3.0-only
// mux.platform.notifications -- A notification shown by the system, where
// the platform has a way to: its backend says how (notifications_backend*.cc).
export module mux.platform.notifications;

import std;
import mux.platform.notifications.backend;

export namespace mux::platform::notifications {
// False where the platform has nowhere to show it, or did not take it.
[[nodiscard]] inline bool notify(std::string_view title, std::string_view text) {
  return backend::notify(title, text);
}
}  // namespace mux::platform::notifications
