// SPDX-License-Identifier: AGPL-3.0-only
// mux.platform.pointing -- What the platform is pointed at with first,
// before any press says: a mouse, or a finger (pointing_android.cc).
export module mux.platform.pointing;

export namespace mux::platform::pointing {
// A finger from the start: true where the platform is one held in the hand,
// whatever the devices SDL has found yet.
[[nodiscard]] constexpr bool touch_first() { return false; }
}  // namespace mux::platform::pointing
