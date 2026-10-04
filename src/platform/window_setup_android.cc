// SPDX-License-Identifier: AGPL-3.0-only
module;
#include <SDL3/SDL.h>
export module mux.platform.window_setup;
export namespace mux::platform::window_setup {
inline constexpr const char* fonts = "/system/fonts";
inline void graphics() {
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
}
}
