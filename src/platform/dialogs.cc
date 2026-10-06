// SPDX-License-Identifier: AGPL-3.0-only
// mux.platform.dialogs -- The system's dialogs for opening files and for
// saving one.
export module mux.platform.dialogs;

import std;
import sdl;
import mux.platform.events;

export namespace mux::platform::dialogs {

// The dialogs of a window: the program's, put over the window as it is made.
// What they answer -- on a thread of SDL's choosing -- is kept here, and the
// window told with an event of its own; it takes the answers on its thread.
class dialogs {
 public:
  dialogs() = default;
  dialogs(const dialogs&) = delete;
  dialogs& operator=(const dialogs&) = delete;

  // The window they are put over, and the kinds of event they answer with.
  void over(sdl::SDL_Window* window, const events::kinds& kinds) {
    parent_ = window;
    kinds_ = kinds;
  }

  // Files to open, several at once.
  void choose_files() {
    sdl::SDL_ShowOpenFileDialog(
        // SDL's C interface gives back what it was given as a void*: this.
        +[](void* self, const char* const* list, int) {
          // None at all -- not an empty list, which is the dialog let go --
          // where SDL could not show one: why, told to the user.
          if (list)
            static_cast<dialogs*>(self)->chose(list);
          else
            static_cast<dialogs*>(self)->failed(sdl::SDL_GetError());
        },
        this, parent_, nullptr, 0, nullptr, true);
  }
  // A path to save a file to, the name offered filled in (a path or a
  // name): kept here until the dialog has read it.
  void choose_save_path(std::string offered) {
    offered_ = std::move(offered);
    sdl::SDL_ShowSaveFileDialog(
        +[](void* self, const char* const* list, int) {
          if (list && *list)
            static_cast<dialogs*>(self)->saved(*list);
          else if (!list)
            static_cast<dialogs*>(self)->failed(sdl::SDL_GetError());
        },
        this, parent_, nullptr, 0, offered_.empty() ? nullptr : offered_.c_str());
  }

  // What was chosen since, taken: on the window's thread, at its event.
  [[nodiscard]] std::vector<std::vector<std::string>> take_files() {
    const std::lock_guard held(lock_);
    return std::exchange(files_, {});
  }
  [[nodiscard]] std::vector<std::string> take_save_paths() {
    const std::lock_guard held(lock_);
    return std::exchange(save_paths_, {});
  }
  // Why a dialog could not be shown, since: SDL's words.
  [[nodiscard]] std::vector<std::string> take_failures() {
    const std::lock_guard held(lock_);
    return std::exchange(failures_, {});
  }

 private:
  void chose(const char* const* list) {
    auto paths = std::ranges::to<std::vector>(std::views::transform(std::views::take_while(std::views::iota(std::size_t{0}), [list](std::size_t at) { return list[at] != nullptr; }), [list](std::size_t at) { return std::string(list[at]); }));
    if (paths.empty())
      return;
    {
      const std::lock_guard held(lock_);
      files_.push_back(std::move(paths));
    }
    events::push(kinds_.files);
  }
  void failed(const char* why) {
    {
      const std::lock_guard held(lock_);
      failures_.emplace_back(why != nullptr && *why != '\0' ? why : "no reason given");
    }
    events::push(kinds_.files);
  }
  void saved(const char* path) {
    {
      const std::lock_guard held(lock_);
      save_paths_.emplace_back(path);
    }
    events::push(kinds_.save);
  }

  sdl::SDL_Window* parent_ = nullptr;
  events::kinds kinds_{0, 0, 0};
  std::string offered_;
  std::mutex lock_;
  std::vector<std::vector<std::string>> files_;
  std::vector<std::string> save_paths_;
  std::vector<std::string> failures_;
};

}  // namespace mux::platform::dialogs
