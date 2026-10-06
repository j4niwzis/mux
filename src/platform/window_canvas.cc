// SPDX-License-Identifier: AGPL-3.0-only
// mux.platform.window:canvas -- What the window draws on: OpenGL, hardware or emulated, or Skia's software renderer.
export module mux.platform.window:canvas;

import std;
import sdl;
import splice.bytes;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import mux.platform.events;
import mux.platform.fonts;
import mux.platform.clipboard;
import mux.platform.dialogs;
import :input;

export namespace mux::platform::window {
namespace detail {

// What draws: a GL context and Skia's context over it, or nothing of the
// GPU and a surface in memory.
// What draws OpenGL: a graphics card, or the processor pretending to be one
// -- Mesa's llvmpipe or softpipe, SwiftShader -- where Skia's own software
// renderer is much the faster. Read once from the driver's name.
namespace gl_kind {
struct hardware {};
struct emulated {};
}  // namespace gl_kind
using gl_kind_t = spl::variant<gl_kind::hardware, gl_kind::emulated>;
inline gl_kind_t gl_kind_of(std::string_view renderer) {
  for (const std::string_view emulator : {"llvmpipe", "softpipe", "SwiftShader", "Software Rasterizer"})
    if (renderer.contains(emulator))
      return gl_kind::emulated{};
  return gl_kind::hardware{};
}
// Said at the start, where it is seen: which renderer drawing got. And
// whether GL is kept: emulated on the processor, Skia's own software
// renderer draws much the faster, and the window is drawn with it instead.
inline bool keep_gl_renderer(const std::string& name) {
  const std::string_view renderer = name.empty() ? std::string_view("unknown") : std::string_view(name);
  std::println(std::cerr, "[render] OpenGL renderer: {}", renderer);
  return spl::visit(spl::overloaded{[](gl_kind::hardware) { return true; },
                                          [](gl_kind::emulated) {
                                            std::println(std::cerr,
                                                         "[render] OpenGL is emulated on the processor here: drawn "
                                                         "with Skia's software renderer instead");
                                            return false;
                                          }},
                       gl_kind_of(renderer));
}
// What the window's own pixels are, as Skia reads them: by their bytes'
// order, as SDL names it -- not Skia's N32, which is another order on
// another platform. Nothing, where Skia cannot draw into them as they are.
inline std::optional<skia::SkColorType> colour_type_of(sdl::SDL_PixelFormat format) {
  switch (format) {
    case sdl::SDL_PIXELFORMAT_XRGB8888:
    case sdl::SDL_PIXELFORMAT_ARGB8888:
      return skia::kBGRA_8888_SkColorType;
    case sdl::SDL_PIXELFORMAT_XBGR8888:
    case sdl::SDL_PIXELFORMAT_ABGR8888:
      return skia::kRGBA_8888_SkColorType;
    default:
      return std::nullopt;
  }
}

class canvas_target {
 public:
  canvas_target(sdl::SDL_Window* window, bool software) : window_(window) {
    // GL where the Skia has Ganesh -- its own define says -- and the window
    // was made for it.
#if defined(SK_GANESH)
    if (!software && (sdl::SDL_GetWindowFlags(window) & sdl::kWindowOpengl)) {
      gl_ = sdl::SDL_GL_CreateContext(window);
      if (gl_) {
        sdl::SDL_GL_MakeCurrent(window, gl_);
        sdl::SDL_GL_SetSwapInterval(1);
        auto interface = skia::GrGLMakeNativeInterface();
        if (!interface)
          interface = skia::GrGLMakeAssembledInterface(nullptr, [](void*, const char name[]) -> skia::GrGLFuncPtr {
            // The one cast a C API asks for: a loader gives
            // every GL function as one pointer type, Skia takes another.
            return reinterpret_cast<skia::GrGLFuncPtr>(sdl::SDL_GL_GetProcAddress(name));
          });
        // Asked of the context SDL made, through SDL's own loader: Skia's
        // native interface goes through GLX's, which answers nothing for a
        // context made through EGL (Wayland, GLES) -- the renderer said
        // "unknown" while drawing went on.
        using get_string_t = const unsigned char* (*)(unsigned int);
        // As above: the loader's one pointer type, made the function's.
        const auto get_string = reinterpret_cast<get_string_t>(sdl::SDL_GL_GetProcAddress("glGetString"));
        const bool kept = keep_gl_renderer(
            get_string ? spl::bytes::text_of_terminated(get_string(0x1F01 /* GL_RENDERER */))
            : interface && interface->fFunctions.fGetString
                ? spl::bytes::text_of_terminated(interface->fFunctions.fGetString(0x1F01 /* GL_RENDERER */))
                : std::string());
        if (interface && kept)
          context_ = skia::MakeGL(std::move(interface));
      }
      if (!context_ && gl_) {
        sdl::SDL_GL_DestroyContext(gl_);
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
      sdl::SDL_GL_DestroyContext(gl_);
#endif
  }

  // SDL can replace a lost EGL context while the activity resumes. Discard
  // Skia's resources from the old context and adopt the replacement.
  void recover_graphics() {
#if defined(SK_GANESH)
    if (!gl_) return;
    if (context_) context_->abandonContext();
    surface_.reset();
    context_.reset();
    const auto restored = sdl::SDL_GL_GetCurrentContext();
    if (gl_ != restored) sdl::SDL_GL_DestroyContext(gl_);
    gl_ = restored;
    if (gl_) {
      auto interface = skia::GrGLMakeAssembledInterface(nullptr, [](void*, const char name[]) -> skia::GrGLFuncPtr {
        return reinterpret_cast<skia::GrGLFuncPtr>(sdl::SDL_GL_GetProcAddress(name));
      });
      if (interface) context_ = skia::MakeGL(std::move(interface));
    }
    if (!context_ && gl_) {
      sdl::SDL_GL_DestroyContext(gl_);
      gl_ = nullptr;
    }
    width_ = height_ = 0;
    fresh_ = true;
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
      sdl::SDL_GL_MakeCurrent(window_, gl_);
#endif
    // In software, Skia draws straight into the window's own pixels where
    // they are laid out as its own are: no copy of the frame at each present,
    // and they stay from one frame to the next -- the kept frame themselves.
    if (!this->on_gpu()) {
      sdl::SDL_Surface* shown = sdl::SDL_GetWindowSurface(window_);
      const auto colour_type = shown ? colour_type_of(shown->format) : std::nullopt;
      if (shown && shown->pixels && colour_type) {
        if (!direct_ || shown != shown_ || shown->pixels != shown_pixels_ || shown->w != width_ || shown->h != height_) {
          surface_ = skia::WrapPixels(skia::SkImageInfo::Make(shown->w, shown->h, *colour_type, skia::kPremul_SkAlphaType),
                                      shown->pixels, static_cast<std::size_t>(shown->pitch));
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
    sdl::SDL_GetWindowSizeInPixels(window_, &width, &height);
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
      sdl::SDL_GL_SetSwapInterval(on ? 1 : 0);
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
        sdl::SDL_UpdateWindowSurface(window_);
        return;
      }
      std::vector<sdl::SDL_Rect> rects;
      rects.reserve(parts.size());
      for (const skia::SkIRect& one : parts)
        if (!one.isEmpty())
          rects.push_back(sdl::SDL_Rect{one.fLeft, one.fTop, one.width(), one.height()});
      if (rects.empty())
        return;
      sdl::SDL_UpdateWindowSurfaceRects(window_, rects.data(), static_cast<int>(rects.size()));
      return;
    }
#if defined(SK_GANESH)
    if (context_) {
      context_->flushAndSubmit(surface_.get());
      sdl::SDL_GL_SwapWindow(window_);
      return;
    }
#endif
    sdl::SDL_Surface* shown = sdl::SDL_GetWindowSurface(window_);
    if (!shown)
      return;
    // Read out as SDL's ARGB8888 -- Skia's BGRA, the same bytes in the same
    // order on a little-endian machine -- and converted to the window's.
    const auto info = skia::SkImageInfo::Make(width_, height_, skia::kBGRA_8888_SkColorType, skia::kPremul_SkAlphaType);
    const std::size_t pitch = static_cast<std::size_t>(width_) * 4;
    // Not zeroed: every byte is written by the read.
    const auto pixels = std::make_unique_for_overwrite<std::byte[]>(pitch * static_cast<std::size_t>(height_));
    if (!surface_->readPixels(info, pixels.get(), pitch, 0, 0))
      return;
    sdl::SDL_ConvertPixels(width_, height_, sdl::SDL_PIXELFORMAT_ARGB8888, pixels.get(), static_cast<int>(pitch),
                      shown->format, shown->pixels, shown->pitch);
    sdl::SDL_UpdateWindowSurface(window_);
  }

 private:
  sdl::SDL_Window* window_;
#if defined(SK_GANESH)
  sdl::SDL_GLContext gl_ = nullptr;
  skia::Sp<skia::GrDirectContext> context_;
#endif
  skia::Sp<skia::SkSurface> surface_;
  int width_ = 0, height_ = 0;
  // The window's own pixels drawn into, and which: a surface SDL makes
  // again, or moves, is wrapped again -- and painted whole.
  sdl::SDL_Surface* shown_ = nullptr;
  void* shown_pixels_ = nullptr;
  bool direct_ = false;
  bool fresh_ = true;
};

}  // namespace detail
}  // namespace mux::platform::window
