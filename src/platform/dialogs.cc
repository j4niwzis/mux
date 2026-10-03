// SPDX-License-Identifier: AGPL-3.0-only
// mux.platform.dialogs -- The system's dialogs for opening files and for
// saving one: what is chosen comes back to the window as an event of its own
// (mux.platform.events).
export module mux.platform.dialogs;

import std;
import sdl;
import mux.platform.events;

namespace mux::platform::dialogs {
using events::files_event;
using events::save_event;
using events::the_window;
}  // namespace mux::platform::dialogs

export namespace mux::platform::dialogs {

// The system's dialog for opening files, several at once.
inline void choose_files() {
  sdl::SDL_ShowOpenFileDialog(
      +[](void*, const char* const* list, int) {
        if (!list)
          return;
        auto* chosen = new std::vector<std::string>();
        for (const char* const* one = list; *one; ++one)
          chosen->emplace_back(*one);
        if (chosen->empty()) {
          delete chosen;
          return;
        }
        sdl::SDL_Event event{};
        event.type = files_event();
        event.user.data1 = chosen;
        event.user.code = 0;  // chosen, not dropped
        sdl::SDL_PushEvent(&event);
      },
      nullptr, the_window(), nullptr, 0, nullptr, true);
}

// The system's dialog for saving a file, the name offered filled in (a path
// or a name). What is offered is kept until the dialog has read it.
inline void choose_save_path(std::string offered) {
  static std::string kept;
  kept = std::move(offered);
  sdl::SDL_ShowSaveFileDialog(
      +[](void*, const char* const* list, int) {
        if (!list || !*list)
          return;
        sdl::SDL_Event event{};
        event.type = save_event();
        event.user.data1 = new std::string(*list);
        sdl::SDL_PushEvent(&event);
      },
      nullptr, the_window(), nullptr, 0, kept.empty() ? nullptr : kept.c_str());
}

}  // namespace mux::platform::dialogs
