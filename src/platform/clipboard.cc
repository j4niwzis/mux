// SPDX-License-Identifier: AGPL-3.0-only
// mux.platform.clipboard -- Pictures put on the system's clipboard, and
// taken from it.
module;

#include <SDL3/SDL.h>

export module mux.platform.clipboard;

import std;

export namespace mux::platform::clipboard {

// A picture put on the system's clipboard: its PNG, offered as image/png --
// what every program that pastes a picture takes -- held until the
// clipboard lets it go. SDL asks for it through the two functions it is
// given, as its C interface has it.
inline void copy_picture(std::string png) {
  auto* held = new std::string(std::move(png));
  const char* types[] = {"image/png"};
  SDL_SetClipboardData(
      +[](void* data, const char*, std::size_t* size) -> const void* {
        const auto* bytes = static_cast<const std::string*>(data);
        *size = bytes->size();
        return bytes->data();
      },
      +[](void* data) { delete static_cast<std::string*>(data); }, held, types, 1);
}

// A picture on the clipboard, for Ctrl+V: its bytes put in a file of its
// own, to go where a picture dropped on the window goes. The first of the
// kinds a picture comes as that the clipboard has.
struct picture_kind {
  const char* mime;
  const char* extension;
};
inline constexpr std::array kPictureKinds{
    picture_kind{"image/png", "png"},   picture_kind{"image/jpeg", "jpg"}, picture_kind{"image/gif", "gif"},
    picture_kind{"image/webp", "webp"}, picture_kind{"image/bmp", "bmp"},
};
[[nodiscard]] inline std::optional<std::string> pasted_picture() {
  static unsigned counter = 0;
  for (const picture_kind& kind : kPictureKinds) {
    if (!SDL_HasClipboardData(kind.mime))
      continue;
    std::size_t size = 0;
    void* data = SDL_GetClipboardData(kind.mime, &size);
    if (!data)
      continue;
    std::error_code failed;
    const std::filesystem::path folder = std::filesystem::temp_directory_path(failed) / "mux-pasted";
    std::filesystem::create_directories(folder, failed);
    const std::filesystem::path path = folder / std::format("pasted-{}.{}", ++counter, kind.extension);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    SDL_free(data);
    if (!out)
      return std::nullopt;
    return path.string();
  }
  return std::nullopt;
}

}  // namespace mux::platform::clipboard
