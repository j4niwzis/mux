// SPDX-License-Identifier: AGPL-3.0-only
#include <SDL3/SDL.h>
#include <GLES3/gl3.h>
import mux.platform.files;
import mux.platform.window_setup;

namespace {
void picked(void*, const char* const* paths, int) {
  if (!paths || !*paths) return;
  const auto bytes = mux::platform::files::read(*paths);
  SDL_Log("Document: %s (%zu bytes)", mux::platform::files::name(*paths).c_str(), bytes ? bytes->size() : 0);
}
}

int mux_main(int, char**) {
  if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
  mux::platform::window_setup::graphics();
  auto* window = SDL_CreateWindow("Mux Android bridge check", 640, 480, SDL_WINDOW_OPENGL);
  if (!window) { SDL_Quit(); return 2; }
  auto context = SDL_GL_CreateContext(window);
  if (!context) { SDL_DestroyWindow(window); SDL_Quit(); return 3; }
  SDL_StartTextInput(window);
  bool running = true;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) running = false;
      if (event.type == SDL_EVENT_RENDER_DEVICE_RESET) {
        const auto replacement = SDL_GL_GetCurrentContext();
        if (context != replacement) SDL_GL_DestroyContext(context);
        context = replacement;
      }
      if (event.type == SDL_EVENT_TEXT_INPUT) SDL_Log("text: %s", event.text.text);
      if (event.type == SDL_EVENT_TEXT_EDITING) SDL_Log("composition: %s", event.edit.text);
      if (event.type == SDL_EVENT_FINGER_DOWN) {
        SDL_Log("touch: %.3f, %.3f", event.tfinger.x, event.tfinger.y);
        if (event.tfinger.y < 0.25f)
          SDL_ShowOpenFileDialog(picked, nullptr, window, nullptr, 0, nullptr, false);
        else
          SDL_StartTextInput(window);
      }
    }
    if (!running) break;
    glClearColor(0.08f, 0.2f, 0.28f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    SDL_GL_SwapWindow(window);
    SDL_Delay(16);
  }
  SDL_GL_DestroyContext(context);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
