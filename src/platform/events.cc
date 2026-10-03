// SPDX-License-Identifier: AGPL-3.0-only
// mux.platform.events -- The window's own events: what another thread wakes
// it with, what the system's dialogs answer with, and the window those
// dialogs are put over.
module;

#include <SDL3/SDL.h>

export module mux.platform.events;

import std;

export namespace mux::platform::events {

// The event another thread pushes to wake the window: SDL's queue is the one
// thing that thread may touch.
inline std::uint32_t wake_event() {
  static const std::uint32_t registered = SDL_RegisterEvents(1);
  return registered;
}

// Callable from any thread.
inline void wake() {
  SDL_Event event{};
  event.type = wake_event();
  SDL_PushEvent(&event);
}

// Files the user picked or dropped, handed to the window's thread as an
// event of its own: the dialog answers on a thread of its choosing.
inline std::uint32_t files_event() {
  static const std::uint32_t registered = SDL_RegisterEvents(1);
  return registered;
}
inline SDL_Window*& the_window() {
  static SDL_Window* window = nullptr;
  return window;
}

// A path chosen to save a file to, handed to the window's thread as an event
// of its own, as the files opened are.
inline std::uint32_t save_event() {
  static const std::uint32_t registered = SDL_RegisterEvents(1);
  return registered;
}

// The window asked to close, as its close button would: from the window's
// thread, between events or in a handler.
inline void request_quit() {
  SDL_Event quit{};
  quit.type = SDL_EVENT_QUIT;
  SDL_PushEvent(&quit);
}

}  // namespace mux::platform::events
