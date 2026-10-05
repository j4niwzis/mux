// SPDX-License-Identifier: AGPL-3.0-only
// mux.platform.permissions -- What the system lets the program use only once
// the user has said so: the microphone, for a call. Where the platform asks
// nobody, it is had (permissions_android.cc asks).
export module mux.platform.permissions;

export namespace mux::platform::permissions {
// The microphone: true where it may be used now; else it is asked for, and
// the answer is the system's -- false until it is given.
[[nodiscard]] inline bool microphone() { return true; }
}  // namespace mux::platform::permissions
