// SPDX-License-Identifier: AGPL-3.0-only
// mux.host: the window. SDL3 makes it and brings its input; Skia draws
// skiff's scene into it -- on the GPU through GL where there is a GL, and in
// software into SDL's own window surface where there is not. A frame is
// drawn when the scene says something changed or is still moving, and the
// thread otherwise sleeps in SDL_WaitEvent: the network wakes it with an
// event of its own.
module;

#include <SDL3/SDL.h>
#include <cxxabi.h>  // names of the nodes a frame trace says

export module mux.host;

import std;
import mux.bytes;
import splice;
import skia;
import skiff.paint;
import skiff.scene;

// And emoji, as tdesktop has its own rather than the system's: Google's
// Noto Color Emoji (SIL Open Font License, fonts/OFL-NotoColorEmoji.txt),
// fetched at a pinned commit by the build, so they are the same on every
// machine and never boxes. Its 25 MB are in a translation unit of their
// own (emoji_font.cc), not in this module's interface, which every
// importer reads.
extern "C++" {
extern const unsigned char mux_noto_color_emoji[];
extern const decltype(sizeof 0) mux_noto_color_emoji_size;
}

namespace mux::host::shipped {
// Telegram Desktop's own faces, in the binary: Open Sans, regular and
// semibold, as it draws its text with (SIL Open Font License, fonts/OFL.txt).
constexpr unsigned char open_sans_regular[] = {
#embed "../fonts/OpenSans-Regular.ttf"
};
constexpr unsigned char open_sans_semibold[] = {
#embed "../fonts/OpenSans-SemiBold.ttf"
};

}  // namespace mux::host::shipped

export namespace mux::host {

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

struct options {
  std::string title = "mux";
  int width = 1100;
  int height = 720;
  bool software = false;  // skip the GPU even where there is one
  // A window with an alpha channel the desktop blends: what is repainted
  // cleared first, the scene no longer opaque at its bottom.
  bool transparent = false;
  std::string fonts = "/usr/share/fonts";
};

// Open Sans, shipped, as Telegram Desktop's text is: regular, and semibold
// for what is bold -- two faces, not one thickened. The system's fonts are
// behind them for what they do not cover -- CJK, emoji.
inline void load_fonts(const std::string& directory) {
  auto manager = skia::SkFontMgr_New_Custom_Directory(directory.c_str());
  if (!manager) {
    // No fonts found: the default face, rather than no text at all.
    static skia::SkFont font;
    skiff::paint::defaultFont() = &font;
    return;
  }
  skia::Sp<skia::SkTypeface> primary;
  for (const char* family : {"Inter", "Noto Sans", "DejaVu Sans", "Liberation Sans", "Cantarell"}) {
    primary = manager->matchFamilyStyle(family, skia::SkFontStyle());
    if (primary)
      break;
  }
  if (!primary && manager->countFamilies() > 0)
    primary = manager->createStyleSet(0)->createTypeface(0);
  const auto shipped_face = [&](const unsigned char* bytes, std::size_t size) {
    return manager->makeFromData(skia::SkData::MakeWithoutCopy(bytes, size));
  };
  auto regular = shipped_face(shipped::open_sans_regular, sizeof shipped::open_sans_regular);
  auto semibold = shipped_face(shipped::open_sans_semibold, sizeof shipped::open_sans_semibold);
  if (regular && semibold) {
    primary = regular;
    skiff::paint::fonts().setPrimary(std::move(regular), std::move(semibold));
  } else if (primary) {
    skiff::paint::fonts().setPrimary(primary);
  }
  // Emoji from the face shipped for them, before anything of the system's.
  if (auto emoji = shipped_face(mux_noto_color_emoji, mux_noto_color_emoji_size))
    skiff::paint::fonts().addFallback(std::move(emoji));
  // Where a character no face loaded here has is looked for: the system's.
  skiff::paint::fonts().setFontManager(manager);
  // Code in a monospace face of the system's, as Telegram draws it: the
  // first of the usual ones that is there.
  for (const char* family : {"DejaVu Sans Mono", "Noto Sans Mono", "Liberation Mono", "Ubuntu Mono", "JetBrains Mono",
                             "Fira Mono", "Source Code Pro", "Cascadia Mono", "Consolas", "Menlo", "Courier New"})
    if (auto face = manager->matchFamilyStyle(family, skia::SkFontStyle())) {
      skiff::paint::fonts().setMonospace(std::move(face));
      break;
    }
  // What every Text and widget draws with. Without it they draw nothing:
  // the window was its boxes and no words.
  static skia::SkFont font(primary);
  // Smoothed, and fitted to the pixels only lightly, as Qt draws on Linux:
  // full hinting is what made the text look heavier than Telegram's.
  font.setEdging(skia::SkFont::Edging::kAntiAlias);
  font.setHinting(skia::SkFontHinting::kSlight);
  font.setSubpixel(true);
  skiff::paint::defaultFont() = &font;
  for (const std::int32_t sample : {0x3042, 0xAC00, 0x4E00, 0x0627, 0x05D0, 0x0915, 0x1F600}) {
    if (auto face = manager->matchFamilyStyleCharacter(nullptr, skia::SkFontStyle(), nullptr, 0, sample))
      skiff::paint::fonts().addFallback(std::move(face));
  }
}

// A link opened in what the system opens links with.
inline void open_url(const std::string& url) { SDL_OpenURL(url.c_str()); }

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
// The system's dialog for opening files, several at once.
inline void choose_files() {
  SDL_ShowOpenFileDialog(
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
        SDL_Event event{};
        event.type = files_event();
        event.user.data1 = chosen;
        event.user.code = 0;  // chosen, not dropped
        SDL_PushEvent(&event);
      },
      nullptr, the_window(), nullptr, 0, nullptr, true);
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

// A path chosen to save a file to, handed to the window's thread as an event
// of its own, as the files opened are.
inline std::uint32_t save_event() {
  static const std::uint32_t registered = SDL_RegisterEvents(1);
  return registered;
}
// The system's dialog for saving a file, the name offered filled in (a path
// or a name). What is offered is kept until the dialog has read it.
inline void choose_save_path(std::string offered) {
  static std::string kept;
  kept = std::move(offered);
  SDL_ShowSaveFileDialog(
      +[](void*, const char* const* list, int) {
        if (!list || !*list)
          return;
        SDL_Event event{};
        event.type = save_event();
        event.user.data1 = new std::string(*list);
        SDL_PushEvent(&event);
      },
      nullptr, the_window(), nullptr, 0, kept.empty() ? nullptr : kept.c_str());
}

// The window asked to close, as its close button would: from the window's
// thread, between events or in a handler.
inline void request_quit() {
  SDL_Event quit{};
  quit.type = SDL_EVENT_QUIT;
  SDL_PushEvent(&quit);
}

namespace detail {

// SDL's shape for each of skiff's pointer shapes.
inline SDL_SystemCursor system_cursor(skiff::scene::cursor::arrow) { return SDL_SYSTEM_CURSOR_DEFAULT; }
inline SDL_SystemCursor system_cursor(skiff::scene::cursor::text) { return SDL_SYSTEM_CURSOR_TEXT; }
inline SDL_SystemCursor system_cursor(skiff::scene::cursor::hand) { return SDL_SYSTEM_CURSOR_POINTER; }
inline SDL_SystemCursor system_cursor(skiff::scene::cursor::resize_horizontal) { return SDL_SYSTEM_CURSOR_EW_RESIZE; }
inline SDL_SystemCursor system_cursor(skiff::scene::cursor::resize_vertical) { return SDL_SYSTEM_CURSOR_NS_RESIZE; }

// The pointer's shape, made once each and set when it changes.
class pointer_shapes {
 public:
  pointer_shapes() = default;
  pointer_shapes(const pointer_shapes&) = delete;
  pointer_shapes& operator=(const pointer_shapes&) = delete;
  ~pointer_shapes() {
    for (SDL_Cursor* one : made_)
      if (one)
        SDL_DestroyCursor(one);
  }
  void show(const skiff::scene::Cursor& shape) {
    const SDL_SystemCursor which = splice::visit([](auto one) { return system_cursor(one); }, shape);
    if (which == shown_)
      return;
    SDL_Cursor*& made = made_[static_cast<std::size_t>(which)];
    if (!made)
      made = SDL_CreateSystemCursor(which);
    if (made)
      SDL_SetCursor(made);
    shown_ = which;
  }

 private:
  std::array<SDL_Cursor*, SDL_SYSTEM_CURSOR_COUNT> made_{};
  SDL_SystemCursor shown_ = SDL_SYSTEM_CURSOR_DEFAULT;
};

inline skiff::scene::Key key_of(SDL_Keycode key) {
  namespace keys = skiff::scene::keys;
  switch (key) {
    case SDLK_TAB: return keys::kTab;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return keys::kEnter;
    case SDLK_SPACE: return keys::kSpace;
    case SDLK_ESCAPE: return keys::kEscape;
    case SDLK_LEFT: return keys::kLeft;
    case SDLK_RIGHT: return keys::kRight;
    case SDLK_UP: return keys::kUp;
    case SDLK_DOWN: return keys::kDown;
    case SDLK_HOME: return keys::kHome;
    case SDLK_END: return keys::kEnd;
    case SDLK_BACKSPACE: return keys::kBackspace;
    case SDLK_DELETE: return keys::kDelete;
    case SDLK_A: return keys::kA;
    case SDLK_B: return keys::kB;
    case SDLK_C: return keys::kC;
    case SDLK_D: return keys::kD;
    case SDLK_E: return keys::kE;
    case SDLK_F: return keys::kF;
    case SDLK_G: return keys::kG;
    case SDLK_H: return keys::kH;
    case SDLK_I: return keys::kI;
    case SDLK_J: return keys::kJ;
    case SDLK_K: return keys::kK;
    case SDLK_L: return keys::kL;
    case SDLK_M: return keys::kM;
    case SDLK_N: return keys::kN;
    case SDLK_O: return keys::kO;
    case SDLK_P: return keys::kP;
    case SDLK_Q: return keys::kQ;
    case SDLK_R: return keys::kR;
    case SDLK_S: return keys::kS;
    case SDLK_T: return keys::kT;
    case SDLK_U: return keys::kU;
    case SDLK_V: return keys::kV;
    case SDLK_W: return keys::kW;
    case SDLK_X: return keys::kX;
    case SDLK_Y: return keys::kY;
    case SDLK_Z: return keys::kZ;
    case SDLK_PAGEUP: return keys::kPageUp;
    case SDLK_PAGEDOWN: return keys::kPageDown;
    case SDLK_0: return keys::k0;
    case SDLK_1: return keys::k1;
    case SDLK_2: return keys::k2;
    case SDLK_3: return keys::k3;
    case SDLK_4: return keys::k4;
    case SDLK_5: return keys::k5;
    case SDLK_6: return keys::k6;
    case SDLK_7: return keys::k7;
    case SDLK_8: return keys::k8;
    case SDLK_9: return keys::k9;
    default: return keys::kUnknown;
  }
}

// What SDL says of the modifier keys.
inline skiff::scene::Modifiers modifiers_of(SDL_Keymod held) {
  namespace modifier = skiff::scene::modifier;
  return skiff::scene::Modifiers{}
      .with<modifier::shift>((held & SDL_KMOD_SHIFT) != 0)
      .with<modifier::control>((held & SDL_KMOD_CTRL) != 0)
      .with<modifier::alt>((held & SDL_KMOD_ALT) != 0)
      .with<modifier::super>((held & SDL_KMOD_GUI) != 0);
}

// What draws: a GL context and Skia's context over it, or nothing of the
// GPU and a surface in memory.
// What draws OpenGL: a graphics card, or the processor pretending to be one
// -- Mesa's llvmpipe or softpipe, SwiftShader -- where Skia's own software
// renderer is much the faster. Read once from the driver's name.
namespace gl_kind {
struct hardware {};
struct emulated {};
}  // namespace gl_kind
using gl_kind_t = splice::variant<gl_kind::hardware, gl_kind::emulated>;
inline gl_kind_t gl_kind_of(std::string_view renderer) {
  for (const std::string_view emulator : {"llvmpipe", "softpipe", "SwiftShader", "Software Rasterizer"})
    if (renderer.contains(emulator))
      return gl_kind::emulated{};
  return gl_kind::hardware{};
}
// Said at the start, where it is seen: which renderer drawing got.
inline void say_gl_renderer(const std::string& name) {
  const std::string_view renderer = name.empty() ? std::string_view("unknown") : std::string_view(name);
  std::println(std::cerr, "[render] OpenGL renderer: {}", renderer);
  splice::visit(splice::overloaded{[](gl_kind::hardware) {},
                                   [](gl_kind::emulated) {
                                     std::println(std::cerr,
                                                  "[render] OpenGL is emulated on the processor here: Settings, "
                                                  "Rendering, Software draws faster");
                                   }},
                gl_kind_of(renderer));
}

class canvas_target {
 public:
  canvas_target(SDL_Window* window, bool software) : window_(window) {
    // GL where the Skia has Ganesh -- its own define says -- and the window
    // was made for it.
#if defined(SK_GANESH)
    if (!software && (SDL_GetWindowFlags(window) & SDL_WINDOW_OPENGL)) {
      gl_ = SDL_GL_CreateContext(window);
      if (gl_) {
        SDL_GL_MakeCurrent(window, gl_);
        SDL_GL_SetSwapInterval(1);
        auto interface = skia::GrGLMakeNativeInterface();
        if (!interface)
          interface = skia::GrGLMakeAssembledInterface(nullptr, [](void*, const char name[]) -> skia::GrGLFuncPtr {
            // The one cast a C API asks for: a loader gives
            // every GL function as one pointer type, Skia takes another.
            return reinterpret_cast<skia::GrGLFuncPtr>(SDL_GL_GetProcAddress(name));
          });
        // Asked of the context SDL made, through SDL's own loader: Skia's
        // native interface goes through GLX's, which answers nothing for a
        // context made through EGL (Wayland, GLES) -- the renderer said
        // "unknown" while drawing went on.
        using get_string_t = const unsigned char* (*)(unsigned int);
        // As above: the loader's one pointer type, made the function's.
        if (const auto get_string = reinterpret_cast<get_string_t>(SDL_GL_GetProcAddress("glGetString")))
          say_gl_renderer(mux::bytes::text_of_terminated(get_string(0x1F01 /* GL_RENDERER */)));
        else if (interface && interface->fFunctions.fGetString)
          say_gl_renderer(mux::bytes::text_of_terminated(interface->fFunctions.fGetString(0x1F01 /* GL_RENDERER */)));
        if (interface)
          context_ = skia::MakeGL(std::move(interface));
      }
      if (!context_ && gl_) {
        SDL_GL_DestroyContext(gl_);
        gl_ = nullptr;
      }
    }
#else
    (void)software;
#endif
    if (!this->on_gpu())
      std::println(std::cerr, "[render] Skia software renderer");
  }
  [[nodiscard]] bool on_gpu() const {
#if defined(SK_GANESH)
    return context_ != nullptr;
#else
    return false;
#endif
  }
  canvas_target(const canvas_target&) = delete;
  canvas_target& operator=(const canvas_target&) = delete;
  ~canvas_target() {
    surface_.reset();
#if defined(SK_GANESH)
    if (context_)
      context_->abandonContext();
    context_.reset();
    if (gl_)
      SDL_GL_DestroyContext(gl_);
#endif
  }

  // A surface of the window's size in pixels, made again when it changes.
  // Its GL context made current first: another window's drawing -- a
  // notification's, through SDL's window surface, which SDL may accelerate
  // with a GL context of its own -- leaves that one current, and the window
  // then drew into nothing it shows.
  skia::SkSurface* surface() {
#if defined(SK_GANESH)
    if (gl_)
      SDL_GL_MakeCurrent(window_, gl_);
#endif
    // In software, Skia draws straight into the window's own pixels where
    // they are laid out as its own are: no copy of the frame at each present,
    // and they stay from one frame to the next -- the kept frame themselves.
    if (!this->on_gpu()) {
      SDL_Surface* shown = SDL_GetWindowSurface(window_);
      if (shown && shown->pixels &&
          (shown->format == SDL_PIXELFORMAT_XRGB8888 || shown->format == SDL_PIXELFORMAT_ARGB8888)) {
        if (!direct_ || shown != shown_ || shown->pixels != shown_pixels_ || shown->w != width_ || shown->h != height_) {
          surface_ = skia::WrapPixels(skia::SkImageInfo::MakeN32Premul(shown->w, shown->h), shown->pixels,
                                      static_cast<std::size_t>(shown->pitch));
          shown_ = shown;
          shown_pixels_ = shown->pixels;
          width_ = shown->w;
          height_ = shown->h;
          direct_ = surface_ != nullptr;
          fresh_ = true;
        }
        if (direct_)
          return surface_.get();
      }
      if (direct_) {
        direct_ = false;
        surface_.reset();
      }
    }
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(window_, &width, &height);
    if (surface_ && width == width_ && height == height_)
      return surface_.get();
    width_ = width;
    height_ = height;
    surface_.reset();
    if (width <= 0 || height <= 0)
      return nullptr;
#if defined(SK_GANESH)
    if (context_) {
      skia::GrGLFramebufferInfo info;
      info.fFBOID = 0;
      info.fFormat = skia::kGlRgba8;
      auto target = skia::MakeGL(width, height, 0, 8, info);
      surface_ = skia::WrapBackendRenderTarget(context_.get(), target, skia::kBottomLeft_GrSurfaceOrigin,
                                               skia::kRGBA_8888_SkColorType, nullptr, nullptr);
      return surface_.get();
    }
#endif
    surface_ = skia::Raster(skia::SkImageInfo::MakeN32Premul(width, height));
    return surface_.get();
  }

  // Shown in step with the screen's refresh, or as soon as drawn.
  void set_vsync(bool on) {
#if defined(SK_GANESH)
    if (gl_)
      SDL_GL_SetSwapInterval(on ? 1 : 0);
#else
    (void)on;
#endif
  }

  // Whether what is drawn stays in the window's pixels from frame to frame;
  // and whether they are new since last asked -- to be painted whole.
  [[nodiscard]] bool keeps_pixels() const noexcept { return direct_; }
  [[nodiscard]] bool take_fresh() noexcept { return std::exchange(fresh_, false); }

  // What was drawn, shown: where the window keeps its pixels, only the
  // parts said -- all of it where none are.
  void present(std::span<const skia::SkIRect> parts = {}) {
    if (!surface_)
      return;
    if (direct_) {
      if (parts.empty()) {
        SDL_UpdateWindowSurface(window_);
        return;
      }
      std::vector<SDL_Rect> rects;
      rects.reserve(parts.size());
      for (const skia::SkIRect& one : parts)
        if (!one.isEmpty())
          rects.push_back(SDL_Rect{one.fLeft, one.fTop, one.width(), one.height()});
      if (rects.empty())
        return;
      SDL_UpdateWindowSurfaceRects(window_, rects.data(), static_cast<int>(rects.size()));
      return;
    }
#if defined(SK_GANESH)
    if (context_) {
      context_->flushAndSubmit(surface_.get());
      SDL_GL_SwapWindow(window_);
      return;
    }
#endif
    SDL_Surface* shown = SDL_GetWindowSurface(window_);
    if (!shown)
      return;
    // Skia's N32 is SDL's ARGB8888 on a little-endian machine: the same
    // bytes in the same order. Anything else is converted.
    const auto info = skia::SkImageInfo::MakeN32Premul(width_, height_);
    const std::size_t pitch = static_cast<std::size_t>(width_) * 4;
    // Not zeroed: every byte is written by the read.
    const auto pixels = std::make_unique_for_overwrite<std::byte[]>(pitch * static_cast<std::size_t>(height_));
    if (!surface_->readPixels(info, pixels.get(), pitch, 0, 0))
      return;
    SDL_ConvertPixels(width_, height_, SDL_PIXELFORMAT_ARGB8888, pixels.get(), static_cast<int>(pitch),
                      shown->format, shown->pixels, shown->pitch);
    SDL_UpdateWindowSurface(window_);
  }

 private:
  SDL_Window* window_;
#if defined(SK_GANESH)
  SDL_GLContext gl_ = nullptr;
  skia::Sp<skia::GrDirectContext> context_;
#endif
  skia::Sp<skia::SkSurface> surface_;
  int width_ = 0, height_ = 0;
  // The window's own pixels drawn into, and which: a surface SDL makes
  // again, or moves, is wrapped again -- and painted whole.
  SDL_Surface* shown_ = nullptr;
  void* shown_pixels_ = nullptr;
  bool direct_ = false;
  bool fresh_ = true;
};

// tdesktop's own notifications: a small window each, borderless, over the
// rest, stacked up from the screen's bottom right, gone after five seconds
// or when pressed -- the press opening the chat. Drawn in software: small,
// and no second GL context beside the window's.
template <class App>
class toasts {
 public:
  using chat_type = decltype(std::declval<typename App::toast_due>().chat);
  static constexpr int kWidth = 340, kHeight = 76, kMargin = 16, kGap = 8;
  toasts() = default;
  toasts(const toasts&) = delete;
  toasts& operator=(const toasts&) = delete;
  ~toasts() { shown_.clear(); }

  void show(const typename App::toast_due& due) {
    SDL_Window* made = SDL_CreateWindow("mux", kWidth, kHeight,
                                        SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_UTILITY |
                                            SDL_WINDOW_NOT_FOCUSABLE | SDL_WINDOW_HIDDEN);
    if (!made)
      return;
    shown_.push_back(std::make_unique<one>(made, due));
    // Three at most, as tdesktop keeps: the oldest goes.
    while (shown_.size() > 3)
      shown_.pop_front();
    this->place();
    SDL_ShowWindow(made);
  }
  [[nodiscard]] bool empty() const { return shown_.empty(); }
  // Whether an event is one of theirs; a press on one, the chat it is of.
  [[nodiscard]] bool owns(SDL_Window* window) const {
    return std::ranges::any_of(shown_, [&](const auto& each) { return each->window == window; });
  }
  [[nodiscard]] std::optional<chat_type> pressed(SDL_Window* window) {
    const auto found = std::ranges::find_if(shown_, [&](const auto& each) { return each->window == window; });
    if (found == shown_.end())
      return std::nullopt;
    chat_type chat = (*found)->chat;
    shown_.erase(found);
    this->place();
    return chat;
  }
  // Each frame: the old ones gone, the rest drawn.
  void frame() {
    const auto now = std::chrono::steady_clock::now();
    const auto before = shown_.size();
    std::erase_if(shown_, [&](const auto& each) { return each->until <= now; });
    if (shown_.size() != before)
      this->place();
    for (auto& each : shown_)
      each->draw();
  }

 private:
  struct one {
    SDL_Window* window;
    canvas_target target;
    skiff::scene::Scene<typename App::toast_card> scene;
    chat_type chat;
    std::chrono::steady_clock::time_point until;
    one(SDL_Window* made, const typename App::toast_due& due)
        : window(made), target(made, true), scene(std::in_place, due.key, due.title, due.text), chat(due.chat),
          until(std::chrono::steady_clock::now() + std::chrono::seconds(5)) {}
    one(const one&) = delete;
    one& operator=(const one&) = delete;
    ~one() { SDL_DestroyWindow(window); }
    void draw() {
      const float scale = SDL_GetWindowDisplayScale(window);
      scene.layoutIfNeeded(skia::SkRect::MakeWH(static_cast<float>(kWidth), static_cast<float>(kHeight)));
      skia::SkSurface* surface = target.surface();
      if (!surface)
        return;
      skia::SkCanvas* canvas = surface->getCanvas();
      canvas->clear(skia::colorSetARGB(255, 24, 27, 30));
      canvas->save();
      canvas->scale(scale, scale);
      scene.draw(canvas);
      canvas->restore();
      target.present();
    }
  };
  // Stacked from the bottom right of the screen's usable part, the newest
  // lowest, as tdesktop stacks them.
  void place() {
    SDL_Rect area{};
    if (!SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &area))
      return;
    int index = 0;
    for (auto it = shown_.rbegin(); it != shown_.rend(); ++it, ++index)
      SDL_SetWindowPosition((*it)->window, area.x + area.w - kMargin - kWidth,
                            area.y + area.h - kMargin - kHeight - index * (kHeight + kGap));
  }
  std::list<std::unique_ptr<one>> shown_;
};

inline double now_ms() {
  return static_cast<double>(SDL_GetTicksNS()) / 1'000'000.0;
}

}  // namespace detail

// The window, until it is closed. What `App` is asked:
//   window()        the scene: a skiff::scene::Scene<...>, whatever its root
//   woken()         another thread woke the window
//   files_given(paths, dropped)  files chosen in the dialog, or dropped
//   save_path_chosen(path)  where to save a file, chosen in the dialog
//   open_link(url)  a link pressed in a text
//   focus_changed(on)  the window given the keyboard's focus, or losing it
//   toasts_due      the notifications to show in windows of their own
//                   (toast_due: chat, key, title, text; toast_card, the node)
//   open_notified(chat)  one of them pressed
//   before_frame()  between events: what the screens asked for, applied
//                   where no handler is running
//   closing()       the window is going away
// What shows the windows, as SDL names it: read once, here.
namespace video_driver {
struct x11 {};
struct wayland {};
struct other {};
}  // namespace video_driver
using video_driver_t = splice::variant<video_driver::x11, video_driver::wayland, video_driver::other>;
inline video_driver_t video_driver_of(const char* name) {
  const std::string_view said = name ? name : "";
  if (said == "x11")
    return video_driver::x11{};
  if (said == "wayland")
    return video_driver::wayland{};
  return video_driver::other{};
}

template <class App>
int run(App& app, const options& how) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    std::println(std::cerr, "[mux] no window: {}", SDL_GetError());
    return 1;
  }
  // A window drawn in software shown as it is, where the system can: on X11
  // SDL has a framebuffer of its own (shared memory), where by default it
  // puts the window on a GL texture -- emulated on the processor without a
  // GPU, the whole window drawn again at each frame. Not on Wayland, which
  // has no such framebuffer in SDL: there the texture is the only way.
  if (how.software)
    splice::visit(splice::overloaded{[](video_driver::x11) { SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0"); },
                                     [](const auto&) {}},
                  video_driver_of(SDL_GetCurrentVideoDriver()));
  (void)wake_event();
  (void)files_event();
  (void)save_event();
  load_fonts(how.fonts);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  if (how.transparent)
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
  const SDL_WindowFlags base = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                               (how.transparent ? SDL_WINDOW_TRANSPARENT : SDL_WindowFlags{0});
#if defined(SK_GANESH)
  constexpr bool can_use_gl = true;
#else
  constexpr bool can_use_gl = false;  // a Skia without Ganesh draws in software
#endif
  SDL_Window* window = how.software || !can_use_gl
                           ? nullptr
                           : SDL_CreateWindow(how.title.c_str(), how.width, how.height, base | SDL_WINDOW_OPENGL);
  if (!window)
    window = SDL_CreateWindow(how.title.c_str(), how.width, how.height, base);
  if (!window) {
    std::println(std::cerr, "[mux] no window: {}", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  the_window() = window;
  int result = 0;
  {
    detail::canvas_target target(window, how.software);
    // Said once, for the window -- not for each notification's, drawn in
    // software on purpose.
    std::println(std::cerr, "[mux] drawing {}", target.on_gpu() ? "with OpenGL" : "in software");
    detail::pointer_shapes shapes;
    auto& scene = app.window();
    skiff::scene::InputRouter router;
    const std::array layers{skiff::scene::InputRouter::Layer{scene.handle(), false}};
    router.setLayers(layers);
    // Kept here for as long as the hook is set: the hook keeps a pointer.
    // The system's clipboard, for pasting: read now, and again whenever it
    // changes.
    const auto read_clipboard = [] {
      char* text = SDL_GetClipboardText();
      skiff::scene::clipboardContents() = text ? text : "";
      SDL_free(text);
    };
    read_clipboard();
    bool typing = false;  // the text input, as last started or stopped

    bool running = true;
    bool redraw = true;
    bool animating = false;
    // When a node next wants a frame on its own (a caret's blink): slept
    // until then where nothing animates, rather than a frame at a time.
    double wake_at = std::numeric_limits<double>::infinity();
    detail::toasts<App> shown_toasts;
    // Partial redraw's frame, kept between frames; and which one the last
    // frame drew into (a new one is painted whole).
    skia::Sp<skia::SkSurface> kept_frame;
    skia::SkSurface* kept_frame_for = nullptr;
    // When the last frames were shown, for the counter; and whether they wait for the screen.
    std::deque<double> shown_times;
    // The last frame shown, by phase, in milliseconds: the events, the app's
    // own work, the tick, the layout, finding the damage, drawing, showing.
    std::array<double, 7> phase_ms{};
    // What the counter and the outline were drawn over, where the window
    // keeps its pixels: put back before the next frame is drawn.
    std::vector<std::pair<skia::Sp<skia::SkImage>, skia::SkIRect>> overlays;
    bool vsync_on = true;
    while (running) {
      SDL_Event event;
      const double wake_in = wake_at - detail::now_ms();
      bool got = (redraw || animating)       ? SDL_WaitEventTimeout(&event, 16)
                 : std::isfinite(wake_in) ? SDL_WaitEventTimeout(&event, static_cast<std::int32_t>(
                                                                    std::clamp(wake_in, 1.0, 60000.0)))
                                          : SDL_WaitEvent(&event);
      // When the frame's work began: the events that woke it, first.
      const double frame_began = detail::now_ms();
      // The pointer's moves, one a frame: only the last of them before the
      // frame is given to the scene -- each ran the hover walk, a restyle
      // and a hit test, and a quick mouse sends several a frame. Given
      // before any other event, so that order is kept.
      std::optional<skiff::scene::pointer::move> motion;
      const auto give_motion = [&] {
        if (motion)
          router.pointer(*std::exchange(motion, std::nullopt));
      };
      while (got) {
        const float scale = SDL_GetWindowDisplayScale(window);
        (void)scale;
        // An event of a notification's window: a press opens its chat, in
        // the window brought up; nothing else of it reaches the scene.
        if (SDL_Window* over = SDL_GetWindowFromEvent(&event); over && over != window && shown_toasts.owns(over)) {
          if (event.type == SDL_EVENT_MOUSE_BUTTON_UP)
            if (const auto chat = shown_toasts.pressed(over)) {
              app.open_notified(*chat);
              SDL_RaiseWindow(window);
              redraw = true;
            }
          got = SDL_PollEvent(&event);
          continue;
        }
        if (event.type != SDL_EVENT_MOUSE_MOTION)
          give_motion();
        switch (event.type) {
          case SDL_EVENT_CLIPBOARD_UPDATE:
            read_clipboard();
            break;
          case SDL_EVENT_WINDOW_FOCUS_GAINED:
            app.focus_changed(true);
            break;
          case SDL_EVENT_WINDOW_FOCUS_LOST:
            app.focus_changed(false);
            break;
          case SDL_EVENT_QUIT:
          case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            running = false;
            break;
          case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
          case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
          case SDL_EVENT_WINDOW_EXPOSED:
            scene.state().invalidateLayout();
            redraw = true;
            break;
          case SDL_EVENT_MOUSE_MOTION:
            motion = skiff::scene::pointer::move{event.motion.x, event.motion.y};
            break;
          case SDL_EVENT_MOUSE_BUTTON_DOWN:
            router.pointer(skiff::scene::pointer::down{event.button.x, event.button.y, event.button.button});
            break;
          case SDL_EVENT_MOUSE_BUTTON_UP:
            router.pointer(skiff::scene::pointer::up{event.button.x, event.button.y, event.button.button});
            break;
          case SDL_EVENT_MOUSE_WHEEL:
            router.pointer(skiff::scene::pointer::scroll{event.wheel.mouse_x, event.wheel.mouse_y, event.wheel.x,
                                                         event.wheel.y});
            break;
          case SDL_EVENT_KEY_DOWN:
          case SDL_EVENT_KEY_UP: {
            const skiff::scene::Key key = detail::key_of(event.key.key);
            if (key == skiff::scene::keys::kUnknown)
              break;
            // Tab too: the router moves the focus on it itself.
            const skiff::scene::Modifiers held = detail::modifiers_of(event.key.mod);
            // Ctrl+V with a picture on the clipboard: the picture, as if
            // dropped on the window -- not the text a field would paste.
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_V && (event.key.mod & SDL_KMOD_CTRL) != 0 &&
                !event.key.repeat) {
              if (std::optional<std::string> picture = pasted_picture()) {
                app.files_given(std::vector<std::string>{std::move(*picture)}, true);
                break;
              }
            }
            if (event.type == SDL_EVENT_KEY_DOWN)
              router.key(skiff::scene::key::down{key, held, event.key.repeat});
            else
              router.key(skiff::scene::key::up{key, held});
            break;
          }
          case SDL_EVENT_DROP_FILE:
            if (event.drop.data)
              app.files_given(std::vector<std::string>{event.drop.data}, true);
            break;
          case SDL_EVENT_TEXT_INPUT:
            router.text(skiff::scene::text::commit{event.text.text});
            break;
          case SDL_EVENT_TEXT_EDITING:
            router.text(skiff::scene::text::compose{event.edit.text ? event.edit.text : "", event.edit.start,
                                                    event.edit.length});
            break;
          default:
            if (event.type == wake_event())
              app.woken();
            else if (event.type == files_event()) {
              std::unique_ptr<std::vector<std::string>> chosen(static_cast<std::vector<std::string>*>(event.user.data1));
              app.files_given(std::move(*chosen), false);
            } else if (event.type == save_event()) {
              std::unique_ptr<std::string> chosen(static_cast<std::string*>(event.user.data1));
              app.save_path_chosen(std::move(*chosen));
            }
            break;
        }
        got = SDL_PollEvent(&event);
      }
      give_motion();
      if (!running)
        break;

      const float scale = SDL_GetWindowDisplayScale(window);
      skiff::scene::pixelScale() = scale;
      int pixel_width = 0, pixel_height = 0;
      SDL_GetWindowSizeInPixels(window, &pixel_width, &pixel_height);
      const float width = static_cast<float>(pixel_width) / scale;
      const float height = static_cast<float>(pixel_height) / scale;
      // What the scene left for the host: the text input started or stopped
      // as a field takes the focus or lets it go, what was copied put on
      // the clipboard, the links pressed followed.
      auto& work = skiff::scene::hostWork();
      if (work.typing != typing) {
        typing = work.typing;
        if (typing)
          SDL_StartTextInput(window);
        else
          SDL_StopTextInput(window);
      }
      if (auto copied = std::exchange(work.copied, std::nullopt)) {
        SDL_SetClipboardText(copied->c_str());
        skiff::scene::clipboardContents() = std::move(*copied);
      }
      for (auto& url : std::exchange(work.links, {}))
        app.open_link(std::move(url));
      // The notifications mux shows itself, put up; the old ones gone.
      // The newest three at most: a window made for each only to be closed
      // at once, where many came together, was the whole of a frame.
      auto due_now = std::exchange(app.toasts_due, {});
      for (std::size_t i = due_now.size() > 3 ? due_now.size() - 3 : 0; i < due_now.size(); ++i)
        shown_toasts.show(due_now[i]);
      shown_toasts.frame();
      const double events_done = detail::now_ms();
      app.before_frame();
      const double app_done = detail::now_ms();
      scene.update(app_done);
      const double ticked = detail::now_ms();
      scene.layoutIfNeeded(skia::SkRect::MakeWH(width, height));
      const double laid_out = detail::now_ms();
      // What the layout put past the edge of its parent, said where it can
      // be seen: once a node, until it fits again.
      for (const auto& one : std::exchange(skiff::scene::overflows(), {}))
        std::println(std::cerr, "[layout] {} sticks out of {} by {:.1f} across, {:.1f} down", one.node, one.parent,
                     one.x, one.y);
      shapes.show(scene.cursor());
      // Scroll views copied rather than repainted, where the frame is kept.
      skiff::scene::blitScrolling() = app.partial_redraw;
      const skiff::scene::FrameResult frame = scene.finishFrame();
      const double damage_found = detail::now_ms();
      // Frames said, where MUX_TRACE_FRAMES is set: what each repaints, and
      // whether more are asked for.
      static const bool traced = std::getenv("MUX_TRACE_FRAMES") != nullptr;
      if (traced && !frame.fDamage.isEmpty())
        std::println(std::cerr, "[frame] damage {:.0f},{:.0f} {:.0f}x{:.0f}{}", frame.fDamage.fLeft, frame.fDamage.fTop,
                     frame.fDamage.width(), frame.fDamage.height(), frame.fWantsAnotherFrame ? " (another wanted)" : "");
      // What made the frame lay out: the nodes found not laid out, and why.
      if (traced)
        for (const auto& one : std::exchange(skiff::scene::dirtiers(), {})) {
          int status = 0;
          char* name = abi::__cxa_demangle(one.type->name(), nullptr, nullptr, &status);
          std::cerr << "    laid out because of " << (name ? name : one.type->name()) << " at " << one.bounds.fLeft << ","
                    << one.bounds.fTop << (one.children ? " (its children changed)" : " (its layout undone)") << "\n";
          std::free(name);
        }
      // How many nodes each walk touched since the last frame said: what the
      // frame cost, walk by walk.
      if (traced) {
        const auto counts = std::exchange(skiff::scene::walkCounts(), {});
        const auto drawn = std::exchange(skiff::scene::drawnCount(), 0);
        const auto seen = std::exchange(skiff::scene::visitedCount(), 0);
        if (!frame.fDamage.isEmpty() || counts.laidOut != 0)
          std::cerr << "    walked: tick " << counts.tick << ", restyle " << counts.restyle << ", dirty " << counts.dirty
                    << ", layout " << counts.layout << " (" << counts.laidOut << " laid out), damage " << counts.damage
                    << ", hover " << counts.hover << ", animating " << counts.animating << ", draw " << seen << " ("
                    << drawn << " drawn), made " << counts.made << "\n";
      }
      // And which nodes marked it, by type and where.
      if (traced)
        for (const auto& one : std::exchange(skiff::scene::damagers(), {})) {
          int status = 0;
          char* name = abi::__cxa_demangle(one.type->name(), nullptr, nullptr, &status);
          std::cerr << "    damaged by " << (name ? name : one.type->name()) << " at " << one.rect.fLeft << ","
                    << one.rect.fTop << " " << one.rect.width() << "x" << one.rect.height()
                    << (one.moved ? " (moved: laid out elsewhere, or a child gone)" : one.relaid ? " (laid out again)" : " (its look)")
                    << "\n";
          std::free(name);
        }
      // And which nodes keep asking for frames, by type: said when that
      // changes. skiff notes them as it asks, where asked to.
      skiff::scene::traceSettling() = traced;
      if (traced) {
        std::string settling;
        for (const auto& one : std::exchange(skiff::scene::settlers(), {})) {
          int status = 0;
          char* name = abi::__cxa_demangle(one.type->name(), nullptr, nullptr, &status);
          settling += std::string("\n    ") + (name ? name : one.type->name()) +
                      (one.transform ? " (transform)" : " (settling)");
          std::free(name);
        }
        static std::string settling_before;
        if (settling != settling_before)
          std::println(std::cerr, "[frame] settling:{}", settling.empty() ? std::string(" nothing") : settling);
        settling_before = std::move(settling);
      }
      // Frames go on while a notification is up: it goes when its time is.
      animating = frame.fWantsAnotherFrame || !shown_toasts.empty();
      wake_at = frame.fWakeAtMs;
      if (frame.fDamage.isEmpty() && !redraw)
        continue;
      const bool whole = std::exchange(redraw, false);
      skia::SkSurface* surface = target.surface();
      if (!surface)
        continue;
      skia::SkCanvas* canvas = surface->getCanvas();
      // What is repainted, in the window's pixels: the damage, a pixel out
      // for antialiasing -- or all of it.
      const skia::SkRect all = skia::SkRect::MakeWH(static_cast<float>(surface->width()), static_cast<float>(surface->height()));
      skia::SkRect repainted = all;
      // Drawn straight into pixels the window keeps: those are the kept frame,
      // and only what changed in them is shown again. What was drawn over
      // them last -- the counter, the outline -- is put back first, before a
      // scroll's copy could carry it along.
      const bool keeps = target.keeps_pixels();
      std::vector<skia::SkIRect> changed;
      // What is repainted, rect by rect: all of it, unless only the damage.
      std::vector<skia::SkRect> pieces{all};
      bool show_all = true;
      for (auto& [pixels, at] : std::exchange(overlays, {}))
        if (keeps) {
          canvas->drawImage(pixels, static_cast<float>(at.fLeft), static_cast<float>(at.fTop));
          changed.push_back(at);
        }
      if (app.partial_redraw) {
        // Else into a frame of its own kept between frames (the window's
        // buffers are not): only the damage repainted there, then the frame
        // shown whole.
        skia::SkSurface* kept = surface;
        bool fresh = false;
        if (keeps) {
          kept_frame = nullptr;
          kept_frame_for = nullptr;
          fresh = target.take_fresh();
        } else {
          if (!kept_frame || kept_frame->width() != surface->width() || kept_frame->height() != surface->height())
            kept_frame = surface->makeSurface(surface->imageInfo());
          if (!kept_frame)
            continue;
          fresh = std::exchange(kept_frame_for, kept_frame.get()) != kept_frame.get();
          kept = kept_frame.get();
        }
        // Each rect of the damage on its own, in the window's pixels -- a pixel
        // out for antialiasing: their union could be the whole view.
        if (!whole && !fresh) {
          show_all = false;
          pieces.clear();
          repainted.setEmpty();
          const std::vector<skia::SkRect> said =
              frame.fDamageRects.empty() ? std::vector<skia::SkRect>{frame.fDamage} : frame.fDamageRects;
          for (const skia::SkRect& one : said) {
            skia::SkRect piece = skia::SkRect::MakeLTRB(one.fLeft * scale - 1.0f, one.fTop * scale - 1.0f,
                                                        one.fRight * scale + 1.0f, one.fBottom * scale + 1.0f);
            piece.roundOut(&piece);
            if (!piece.intersect(all))
              continue;
            pieces.push_back(piece);
            repainted.join(piece);
          }
          // What blurs what is under it, live: all of it repainted wherever
          // any of what is under it is -- else it blurred its own last pixels.
          for (const auto& [id, live] : skiff::scene::detail::liveBackdrops()) {
            skia::SkRect whole_of = live;
            whole_of.roundOut(&whole_of);
            if (!whole_of.intersect(all))
              continue;
            if (std::ranges::any_of(pieces, [&](const skia::SkRect& piece) { return skia::SkRect::Intersects(piece, whole_of); })) {
              pieces.push_back(whole_of);
              repainted.join(whole_of);
            }
          }
        }
        skia::SkCanvas* into = kept->getCanvas();
        // A scroll view that only moved: last frame's pixels of it copied to
        // where they go now, and only what came into view, and what is over
        // it, repainted -- not the whole view at every step of a scroll.
        if (!whole && !fresh)
          for (const skiff::scene::ScrollMove& move : frame.fMoves) {
            const skia::SkIRect to = skia::SkRect::MakeLTRB(move.rect.fLeft * scale, move.rect.fTop * scale,
                                                            move.rect.fRight * scale, move.rect.fBottom * scale)
                                         .round();
            const int dy = static_cast<int>(std::lround(move.dy * scale));
            skia::SkIRect from = to.makeOffset(0, -dy);
            if (!from.intersect(to))
              continue;
            // The window's own pixels: their rows moved where they are, one
            // copy -- not copied out and drawn back.
            skia::SkPixmap in_place;
            if (keeps && kept->peekPixels(&in_place)) {
              const std::size_t bytes = static_cast<std::size_t>(from.width()) * 4u;
              const auto row = [&](int y) { return static_cast<std::byte*>(in_place.writable_addr(from.fLeft, y)); };
              if (dy > 0)
                for (int y = from.fBottom - 1; y >= from.fTop; --y)
                  std::memmove(row(y + dy), row(y), bytes);
              else
                for (int y = from.fTop; y < from.fBottom; ++y)
                  std::memmove(row(y + dy), row(y), bytes);
              changed.push_back(to);
              continue;
            }
            if (auto pixels = kept->makeImageSnapshot(from)) {
              into->drawImage(pixels, static_cast<float>(from.fLeft), static_cast<float>(from.fTop + dy));
              changed.push_back(to);
            }
          }
        // Not cleared first: the window's backdrop, the bottom of every
        // scene, fills all of it opaque -- a clear under it was the same
        // pixels filled twice, the most costly part of a repainted frame.
        // Much to paint: the scene drawn on this thread into a recording --
        // walking it is not for several threads -- and the recording played
        // back into the pixels in bands, one a thread, as a browser's raster
        // workers play back what its main thread recorded. Little: drawn
        // straight in.
        float area = 0.0f;
        for (const skia::SkRect& piece : pieces)
          area += piece.width() * piece.height();
        const unsigned team_size = std::min(4u, std::max(1u, std::thread::hardware_concurrency()));
        skia::SkPixmap pixels;
        // Not in bands while something blurs what is under it, live: at a
        // band's edge it would not see the band beside it.
        if (area > 300000.0f && team_size > 1 && skiff::scene::detail::liveBackdrops().empty() && kept->peekPixels(&pixels)) {
          skia::SkPictureRecorder recorder;
          skia::SkCanvas* record = recorder.beginRecording(all);
          for (const skia::SkRect& piece : pieces) {
            record->save();
            record->clipRect(piece);
            if (how.transparent)
              record->clear(skia::SkColor{0});
            record->scale(scale, scale);
            scene.draw(record);
            record->restore();
          }
          const auto picture = recorder.finishRecordingAsPicture();
          if (picture) {
            std::vector<std::jthread> team;
            team.reserve(team_size);
            for (unsigned band = 0; band < team_size; ++band)
              team.emplace_back([&pixels, &picture, band, team_size] {
                const int top = pixels.height() * static_cast<int>(band) / static_cast<int>(team_size);
                const int bottom = pixels.height() * static_cast<int>(band + 1) / static_cast<int>(team_size);
                if (bottom <= top)
                  return;
                auto rows = skia::WrapPixels(pixels.info().makeWH(pixels.width(), bottom - top),
                                             pixels.writable_addr(0, top), pixels.rowBytes());
                if (!rows)
                  return;
                skia::SkCanvas* band_canvas = rows->getCanvas();
                band_canvas->translate(0.0f, static_cast<float>(-top));
                band_canvas->drawPicture(picture);
              });
          }  // the team joined here
        } else {
          for (const skia::SkRect& piece : pieces) {
            into->save();
            into->clipRect(piece);
            // See-through: nothing opaque at the bottom to cover last
            // frame's pixels -- cleared, one fill of what is repainted.
            if (how.transparent)
              into->clear(skia::SkColor{0});
            into->scale(scale, scale);
            scene.draw(into);
            into->restore();
          }
        }
        for (const skia::SkRect& piece : pieces)
          changed.push_back(piece.roundOut());
        if (kept != surface) {
          // Drawn over what the buffer had: see-through, that cleared first.
          if (how.transparent)
            canvas->clear(skia::SkColor{0});
          canvas->drawImage(kept_frame->makeImageSnapshot(), 0.0f, 0.0f);
        }
      } else {
        kept_frame = nullptr;
        kept_frame_for = nullptr;
        canvas->clear(how.transparent ? skia::SkColor{0} : skia::colorSetARGB(255, 24, 27, 30));
        canvas->save();
        canvas->scale(scale, scale);
        scene.draw(canvas);
        canvas->restore();
        if (!whole)
          repainted = skia::SkRect::MakeLTRB(frame.fDamage.fLeft * scale, frame.fDamage.fTop * scale,
                                             frame.fDamage.fRight * scale, frame.fDamage.fBottom * scale);
        pieces = {repainted};
      }
      // What is drawn over the frame, where the window keeps its pixels:
      // what was under it kept, to be put back at the next frame.
      const auto keep_under = [&](const skia::SkRect& area) {
        if (!keeps)
          return;
        skia::SkIRect at = area.roundOut();
        if (!at.intersect(skia::SkIRect::MakeWH(surface->width(), surface->height())))
          return;
        if (auto pixels = surface->makeImageSnapshot(at)) {
          overlays.emplace_back(std::move(pixels), at);
          changed.push_back(at);
        }
      };
      // What this frame repainted, outlined, where that is asked for.
      if (app.flash_redraws)
        for (const skia::SkRect& piece : pieces) {
        if (piece.isEmpty())
          continue;
        skia::SkPaint outline;
        outline.setStyle(skia::kStrokeStyle);
        outline.setStrokeWidth(2.0f);
        outline.setColor(skia::colorSetARGB(220, 255, 0, 160));
        const skia::SkRect edge = piece.makeInset(1.0f, 1.0f);
        // Only its four sides kept: what is inside is the frame's own.
        for (const skia::SkRect& side : {skia::SkRect::MakeLTRB(edge.fLeft - 2, edge.fTop - 2, edge.fRight + 2, edge.fTop + 2),
                                         skia::SkRect::MakeLTRB(edge.fLeft - 2, edge.fBottom - 2, edge.fRight + 2, edge.fBottom + 2),
                                         skia::SkRect::MakeLTRB(edge.fLeft - 2, edge.fTop - 2, edge.fLeft + 2, edge.fBottom + 2),
                                         skia::SkRect::MakeLTRB(edge.fRight - 2, edge.fTop - 2, edge.fRight + 2, edge.fBottom + 2)})
          keep_under(side);
        canvas->drawRect(edge, outline);
      }
      // Frames a second over the last second, and the last frame's time, in
      // the top right corner: counted as shown, so an idle window stays at
      // what it last was.
      const double shown_at = detail::now_ms();
      if (app.show_fps) {
        while (!shown_times.empty() && shown_at - shown_times.front() > 1000.0)
          shown_times.pop_front();
        const double since = shown_times.empty() ? 0.0 : shown_at - shown_times.back();
        if (skia::SkFont* base = skiff::paint::defaultFont()) {
          skia::SkFont font = *base;
          font.setSize(13.0f * scale);
          // And the last frame shown, by its phases: what the time went on.
          const std::string text =
              std::format("{} fps  {:.1f} ms | events {:.1f}  app {:.1f}  tick {:.1f}  layout {:.1f}  damage {:.1f}  "
                          "draw {:.1f}  present {:.1f}",
                          shown_times.size() + 1, since, phase_ms[0], phase_ms[1], phase_ms[2], phase_ms[3],
                          phase_ms[4], phase_ms[5], phase_ms[6]);
          const float wide = font.measureText(text.data(), text.size(), skia::SkTextEncoding::kUTF8);
          const float x = all.width() - wide - 12.0f * scale;
          const float y = 20.0f * scale;
          const skia::SkRect box =
              skia::SkRect::MakeLTRB(x - 6.0f * scale, y - 15.0f * scale, x + wide + 6.0f * scale, y + 5.0f * scale);
          keep_under(box);
          skia::SkPaint back;
          back.setColor(skia::colorSetARGB(170, 0, 0, 0));
          canvas->drawRect(box, back);
          skia::SkPaint ink;
          ink.setColor(skia::colorSetARGB(255, 120, 255, 140));
          canvas->drawSimpleText(text.data(), text.size(), skia::SkTextEncoding::kUTF8, x, y, font, ink);
        }
      }
      shown_times.push_back(shown_at);
      if (shown_times.size() > 2000)
        shown_times.pop_front();
      if (app.vsync != vsync_on) {
        vsync_on = app.vsync;
        target.set_vsync(vsync_on);
      }
      if (keeps && !show_all)
        target.present(changed);
      else
        target.present();
      phase_ms = {events_done - frame_began, app_done - events_done, ticked - app_done,  laid_out - ticked,
                  damage_found - laid_out,   shown_at - damage_found, detail::now_ms() - shown_at};
      // A slow frame said, where MUX_TRACE_FRAMES is set: by its phases, as the
      // corner shows the last one -- one that came and went is kept here.
      static const bool slow_traced = std::getenv("MUX_TRACE_FRAMES") != nullptr;
      if (slow_traced && detail::now_ms() - frame_began > 20.0)
        std::println(std::cerr, "[slow] {:.1f} ms | events {:.1f}  app {:.1f}  tick {:.1f}  layout {:.1f}  damage {:.1f}  draw {:.1f}  present {:.1f}",
                     detail::now_ms() - frame_began, phase_ms[0], phase_ms[1], phase_ms[2], phase_ms[3], phase_ms[4],
                     phase_ms[5], phase_ms[6]);
    }
    app.closing();
  }
  SDL_DestroyWindow(window);
  SDL_Quit();
  return result;
}

}  // namespace mux::host
