// SPDX-License-Identifier: AGPL-3.0-only
// mux.platform.clipboard -- Pictures put on the system's clipboard, and
// taken from it.
export module mux.platform.clipboard;

import std;
import sdl;
import chevron.escape;

export namespace mux::platform::clipboard {

// A picture put on the system's clipboard: its PNG, offered as image/png --
// what every program that pastes a picture takes -- held until the
// clipboard lets it go. SDL asks for it through the two functions it is
// given, as its C interface has it.
inline void copy_picture(std::string png) {
  auto* held = new std::string(std::move(png));
  const char* types[] = {"image/png"};
  sdl::SDL_SetClipboardData(
      +[](void* data, const char*, std::size_t* size) -> const void* {
        const auto* bytes = static_cast<const std::string*>(data);
        *size = bytes->size();
        return bytes->data();
      },
      +[](void* data) { delete static_cast<std::string*>(data); }, held, types, 1);
}

// Offer both plain labels and Matrix-compatible HTML for inline custom
// emoji. Other clients can consume the HTML without the in-process atoms.
inline void copy_text(std::string text, std::string html) {
  struct contents { std::string text, html; };
  auto* held = new contents{std::move(text), std::move(html)};
  const char* types[] = {"text/plain;charset=utf-8", "text/plain", "text/html"};
  sdl::SDL_SetClipboardData(
      +[](void* data, const char* mime, std::size_t* size) -> const void* {
        const auto& value = *static_cast<const contents*>(data);
        const auto& bytes = std::string_view(mime) == "text/html" ? value.html : value.text;
        *size = bytes.size();
        return bytes.data();
      }, +[](void* data) { delete static_cast<contents*>(data); }, held, types, 3);
}
template <class Fragment> std::string emoji_html(const Fragment& fragment) {
  std::string html;
  const auto text = [&](std::string_view part) {
    for (std::size_t at = 0; at < part.size();) {
      const auto end = std::min(part.find('\n', at), part.size());
      html += chevron::escaped(part.substr(at, end - at));
      if (end < part.size()) html += "<br>";
      at = end + 1;
    }
  };
  std::size_t at = 0;
  for (const auto& atom : fragment.atoms) {
    text(std::string_view(fragment.display).substr(at, atom.first - at));
    if (atom.picture)
      html += std::format(R"(<img data-mx-emoticon src="{}" alt="{}">)", chevron::escaped(atom.target), chevron::escaped(atom.plain));
    else text(atom.plain);
    at = atom.last;
  }
  text(std::string_view(fragment.display).substr(at));
  return html;
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
// What pastes pictures: each in a file of its own, numbered.
class paster {
 public:
  [[nodiscard]] std::optional<std::string> picture() {
    for (const picture_kind& kind : kPictureKinds) {
      if (!sdl::SDL_HasClipboardData(kind.mime))
        continue;
      std::size_t size = 0;
      void* data = sdl::SDL_GetClipboardData(kind.mime, &size);
      if (!data)
        continue;
      std::error_code failed;
      const std::filesystem::path folder = std::filesystem::temp_directory_path(failed) / "mux-pasted";
      std::filesystem::create_directories(folder, failed);
      const std::filesystem::path path = folder / std::format("pasted-{}.{}", ++counter_, kind.extension);
      std::ofstream out(path, std::ios::binary | std::ios::trunc);
      out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
      sdl::SDL_free(data);
      if (!out)
        return std::nullopt;
      return path.string();
    }
    return std::nullopt;
  }

 private:
  unsigned counter_ = 0;
};

}  // namespace mux::platform::clipboard
