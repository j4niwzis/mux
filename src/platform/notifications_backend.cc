// SPDX-License-Identifier: AGPL-3.0-only
// Where the platform has no notifications: none shown.
export module mux.platform.notifications.backend;

import std;

export namespace mux::platform::notifications::backend {
[[nodiscard]] inline bool notify(std::string_view, std::string_view, bool) { return false; }
[[nodiscard]] inline bool notify_chat(std::string_view, std::string_view, bool, std::string_view) { return false; }
[[nodiscard]] inline std::optional<std::string> take_opened() { return std::nullopt; }
}  // namespace mux::platform::notifications::backend
