// SPDX-License-Identifier: AGPL-3.0-only
// mux.platform.events -- The window's own kinds of event: what another
// thread wakes it with, and what the system's dialogs answer with.
export module mux.platform.events;

import std;
import sdl;

export namespace mux::platform::events {

// The kinds, registered with SDL once -- by main, which makes the window --
// and handed to what wakes the window or answers it. A copy names the same
// kinds; only one made anew registers more.
struct kinds {
  std::uint32_t wake = sdl::SDL_RegisterEvents(1);
  std::uint32_t files = sdl::SDL_RegisterEvents(1);
  std::uint32_t save = sdl::SDL_RegisterEvents(1);
};

// An event of a kind pushed: from any thread -- SDL's queue is the one thing
// another thread may touch.
inline void push(std::uint32_t kind) {
  sdl::SDL_Event event{};
  event.type = kind;
  sdl::SDL_PushEvent(&event);
}

// The window asked to close, as its close button would: from the window's
// thread, between events or in a handler.
inline void request_quit() {
  sdl::SDL_Event quit{};
  quit.type = sdl::SDL_EVENT_QUIT;
  sdl::SDL_PushEvent(&quit);
}

}  // namespace mux::platform::events
