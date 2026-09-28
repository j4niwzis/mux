// SPDX-License-Identifier: AGPL-3.0-only
// mux.host: the window. SDL3 makes it and brings its input; Skia draws
// skiff's scene into it -- on the GPU through GL where there is a GL, and in
// software into SDL's own window surface where there is not. A frame is
// drawn when the scene says something changed or is still moving, and the
// thread otherwise sleeps in SDL_WaitEvent: the network wakes it with an
// event of its own.
module;

#include <SDL3/SDL.h>

export module mux.host;

import std;
import skia;
import skiff.paint;
import skiff.scene;

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
  std::string fonts = "/usr/share/fonts";
};

// The system's fonts, until mux ships its own: a sans-serif face first, and
// faces for what it does not cover -- CJK, emoji -- behind it.
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
  if (primary)
    skiff::paint::fonts().setPrimary(primary);
  // What every Text and widget draws with. Without it they draw nothing:
  // the window was its boxes and no words.
  static skia::SkFont font(primary);
  skiff::paint::defaultFont() = &font;
  for (const std::int32_t sample : {0x3042, 0xAC00, 0x4E00, 0x0627, 0x05D0, 0x0915, 0x1F600}) {
    if (auto face = manager->matchFamilyStyleCharacter(nullptr, skia::SkFontStyle(), nullptr, 0, sample))
      skiff::paint::fonts().addFallback(std::move(face));
  }
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
    const SDL_SystemCursor which = std::visit([](auto one) { return system_cursor(one); }, shape);
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
class canvas_target {
 public:
  canvas_target(SDL_Window* window, bool software) : window_(window) {
    if (!software && (SDL_GetWindowFlags(window) & SDL_WINDOW_OPENGL)) {
      gl_ = SDL_GL_CreateContext(window);
      if (gl_) {
        SDL_GL_MakeCurrent(window, gl_);
        SDL_GL_SetSwapInterval(1);
        auto interface = skia::GrGLMakeNativeInterface();
        if (!interface)
          interface = skia::GrGLMakeAssembledInterface(nullptr, [](void*, const char name[]) -> skia::GrGLFuncPtr {
            return reinterpret_cast<skia::GrGLFuncPtr>(SDL_GL_GetProcAddress(name));
          });
        if (interface)
          context_ = skia::MakeGL(std::move(interface));
      }
      if (!context_ && gl_) {
        SDL_GL_DestroyContext(gl_);
        gl_ = nullptr;
      }
    }
    std::println(std::cerr, "[mux] drawing {}", context_ ? "with OpenGL" : "in software");
  }
  canvas_target(const canvas_target&) = delete;
  canvas_target& operator=(const canvas_target&) = delete;
  ~canvas_target() {
    surface_.reset();
    if (context_)
      context_->abandonContext();
    context_.reset();
    if (gl_)
      SDL_GL_DestroyContext(gl_);
  }

  // A surface of the window's size in pixels, made again when it changes.
  skia::SkSurface* surface() {
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(window_, &width, &height);
    if (surface_ && width == width_ && height == height_)
      return surface_.get();
    width_ = width;
    height_ = height;
    surface_.reset();
    if (width <= 0 || height <= 0)
      return nullptr;
    if (context_) {
      skia::GrGLFramebufferInfo info;
      info.fFBOID = 0;
      info.fFormat = skia::kGlRgba8;
      auto target = skia::MakeGL(width, height, 0, 8, info);
      surface_ = skia::WrapBackendRenderTarget(context_.get(), target, skia::kBottomLeft_GrSurfaceOrigin,
                                               skia::kRGBA_8888_SkColorType, nullptr, nullptr);
    } else {
      surface_ = skia::Raster(skia::SkImageInfo::MakeN32Premul(width, height));
    }
    return surface_.get();
  }

  // What was drawn, shown.
  void present() {
    if (!surface_)
      return;
    if (context_) {
      context_->flushAndSubmit(surface_.get());
      SDL_GL_SwapWindow(window_);
      return;
    }
    SDL_Surface* shown = SDL_GetWindowSurface(window_);
    if (!shown)
      return;
    // Skia's N32 is SDL's ARGB8888 on a little-endian machine: the same
    // bytes in the same order. Anything else is converted.
    const auto info = skia::SkImageInfo::MakeN32Premul(width_, height_);
    const std::size_t pitch = static_cast<std::size_t>(width_) * 4;
    std::vector<std::byte> pixels(pitch * static_cast<std::size_t>(height_));
    if (!surface_->readPixels(info, pixels.data(), pitch, 0, 0))
      return;
    SDL_ConvertPixels(width_, height_, SDL_PIXELFORMAT_ARGB8888, pixels.data(), static_cast<int>(pitch),
                      shown->format, shown->pixels, shown->pitch);
    SDL_UpdateWindowSurface(window_);
  }

 private:
  SDL_Window* window_;
  SDL_GLContext gl_ = nullptr;
  skia::Sp<skia::GrDirectContext> context_;
  skia::Sp<skia::SkSurface> surface_;
  int width_ = 0, height_ = 0;
};

inline double now_ms() {
  return static_cast<double>(SDL_GetTicksNS()) / 1'000'000.0;
}

}  // namespace detail

// The window, until it is closed. What `App` is asked:
//   window()        the scene: a skiff::scene::Scene<...>, whatever its root
//   woken()         another thread woke the window
//   before_frame()  between events: what the screens asked for, applied
//                   where no handler is running
//   closing()       the window is going away
template <class App>
int run(App& app, const options& how) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    std::println(std::cerr, "[mux] no window: {}", SDL_GetError());
    return 1;
  }
  (void)wake_event();
  load_fonts(how.fonts);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  const SDL_WindowFlags base = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
  SDL_Window* window =
      how.software ? nullptr : SDL_CreateWindow(how.title.c_str(), how.width, how.height, base | SDL_WINDOW_OPENGL);
  if (!window)
    window = SDL_CreateWindow(how.title.c_str(), how.width, how.height, base);
  if (!window) {
    std::println(std::cerr, "[mux] no window: {}", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  int result = 0;
  {
    detail::canvas_target target(window, how.software);
    detail::pointer_shapes shapes;
    auto& scene = app.window();
    skiff::scene::InputRouter router;
    const std::array layers{skiff::scene::InputRouter::Layer{scene.handle(), false}};
    router.setLayers(layers);
    // Kept here for as long as the hook is set: the hook keeps a pointer.
    auto keyboard = [window](bool on) {
      if (on)
        SDL_StartTextInput(window);
      else
        SDL_StopTextInput(window);
    };
    skiff::scene::setTextFocusHook(keyboard);

    bool running = true;
    bool redraw = true;
    bool animating = false;
    while (running) {
      SDL_Event event;
      bool got = (redraw || animating) ? SDL_WaitEventTimeout(&event, 16) : SDL_WaitEvent(&event);
      while (got) {
        const float scale = SDL_GetWindowDisplayScale(window);
        (void)scale;
        switch (event.type) {
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
            router.pointer(skiff::scene::pointer::move{event.motion.x, event.motion.y});
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
            if (event.type == SDL_EVENT_KEY_DOWN)
              router.key(skiff::scene::key::down{key, held, event.key.repeat});
            else
              router.key(skiff::scene::key::up{key, held});
            break;
          }
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
            break;
        }
        got = SDL_PollEvent(&event);
      }
      if (!running)
        break;

      const float scale = SDL_GetWindowDisplayScale(window);
      int pixel_width = 0, pixel_height = 0;
      SDL_GetWindowSizeInPixels(window, &pixel_width, &pixel_height);
      const float width = static_cast<float>(pixel_width) / scale;
      const float height = static_cast<float>(pixel_height) / scale;
      app.before_frame();
      scene.update(detail::now_ms());
      scene.layoutIfNeeded(skia::SkRect::MakeWH(width, height));
      shapes.show(scene.cursor());
      const skiff::scene::FrameResult frame = scene.finishFrame();
      animating = frame.fWantsAnotherFrame;
      if (frame.fDamage.isEmpty() && !redraw)
        continue;
      redraw = false;
      skia::SkSurface* surface = target.surface();
      if (!surface)
        continue;
      skia::SkCanvas* canvas = surface->getCanvas();
      canvas->clear(skia::colorSetARGB(255, 24, 27, 30));
      canvas->save();
      canvas->scale(scale, scale);
      scene.draw(canvas);
      canvas->restore();
      target.present();
    }
    app.closing();
    skiff::scene::clearTextFocusHook();
  }
  SDL_DestroyWindow(window);
  SDL_Quit();
  return result;
}

}  // namespace mux::host
