// SPDX-License-Identifier: AGPL-3.0-only
// mux.platform.pointing on Android: a finger from the start. SDL lists a
// phone's touch screen only once it has been touched, and the field given
// the focus before that put the keyboard up as the program opened.
export module mux.platform.pointing;

export namespace mux::platform::pointing {
[[nodiscard]] constexpr bool touch_first() { return true; }
}  // namespace mux::platform::pointing
