export module mux.platform.notifications.backend;

export import mux.platform.freedesktop.notifications_backend;
import std;

// A notification here opens no chat when pressed: the chat's said as one
// without it, and none asked to be opened.
export namespace mux::platform::notifications::backend {
[[nodiscard]] inline bool notify_chat(std::string_view title, std::string_view text, bool sound, std::string_view) {
  return notify(title, text, sound);
}
[[nodiscard]] inline std::optional<std::string> take_opened() { return std::nullopt; }
}  // namespace mux::platform::notifications::backend
