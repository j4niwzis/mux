// SPDX-License-Identifier: AGPL-3.0-only
module;
#include <SDL3/SDL.h>
export module mux.platform.window_setup;
export namespace mux::platform::window_setup {
inline constexpr const char* fonts = "/system/fonts";
inline void graphics() {
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  // GLES 2.0 asked for: EGL gives the newest version the driver has that
  // is compatible with it -- 3.x where there is one -- and a GLES 2.0 GPU
  // (a Mali-400) still gets a context.
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
}
}
