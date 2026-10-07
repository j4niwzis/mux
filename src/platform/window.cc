// SPDX-License-Identifier: AGPL-3.0-only
// mux.platform.window: the window. SDL3 makes it and brings its input; Skia draws
// skiff's scene into it -- on the GPU through GL where there is a GL, and in
// software into SDL's own window surface where there is not. A frame is
// drawn when the scene says something changed or is still moving, and the
// thread otherwise sleeps in SDL_WaitEvent: the network wakes it with an
// event of its own.
module;

#include <cxxabi.h>  // names of the nodes a frame trace says

export module mux.platform.window;

import std;
import sdl;
import splice.bytes;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import mux.platform.pointing;
import mux.platform.events;
import mux.platform.fonts;
import mux.platform.window_setup;
import mux.platform.clipboard;
import mux.platform.dialogs;

export import :input;
export import :canvas;
export import :toasts;

namespace mux::platform::window {
using fonts::load_fonts;
}  // namespace mux::platform::window

export namespace mux::platform::window {

struct options {
  std::string title = "mux";
  int width = 1100;
  int height = 720;
  bool software = false;  // skip the GPU even where there is one
  // A window with an alpha channel the desktop blends: what is repainted
  // cleared first, the scene no longer opaque at its bottom.
  bool transparent = false;
  std::string fonts = window_setup::fonts;
};

namespace detail {

inline double now_ms() {
  return static_cast<double>(sdl::SDL_GetTicksNS()) / 1'000'000.0;
}

}  // namespace detail

// The window, until it is closed. What `App` is asked:
//   window()        the scene: a skiff::scene::Scene<...>, whatever its root
//   woken()         another thread woke the window
//   files_given(paths, dropped)  files chosen in the dialog, or dropped
//   save_path_chosen(path)  where to save a file, chosen in the dialog
//   dialog_failed(why)  the system's file dialog could not be shown
//   open_link(url)  a link pressed in a text
//   focus_changed(on)  the window given the keyboard's focus, or losing it
//   take_toasts()   the notifications to show in windows of their own
//                   (toast_due: chat, key, title, text; toast_card, the node)
//   open_notified(chat)  one of them pressed
//   before_frame()  between events: what the screens asked for, applied
//                   where no handler is running
//   closing()       the window is going away
//   system_dialogs  the system's dialogs (mux.platform.dialogs), put over
//                   the window once it is made
// What shows the windows, as SDL names it: read once, here.
namespace video_driver {
struct x11 {};
struct wayland {};
struct other {};
}  // namespace video_driver
using video_driver_t = spl::variant<video_driver::x11, video_driver::wayland, video_driver::other>;
inline video_driver_t video_driver_of(const char* name) {
  const std::string_view said = name ? name : "";
  if (said == "x11")
    return video_driver::x11{};
  if (said == "wayland")
    return video_driver::wayland{};
  return video_driver::other{};
}

template <class App>
int run(App& app, const options& how, const events::kinds& kinds) {
  // Android's Back comes to the app as a key, instead of Android closing the app.
  sdl::SDL_SetHint(sdl::kHintTrapBackButton, "1");
  if (!sdl::SDL_Init(sdl::kInitVideo | sdl::kInitEvents)) {
    std::println(std::cerr, "[mux] no window: {}", sdl::SDL_GetError());
    return 1;
  }
  // A window drawn in software shown as it is, where the system can: on X11
  // SDL has a framebuffer of its own (shared memory), where by default it
  // puts the window on a GL texture -- emulated on the processor without a
  // GPU, the whole window drawn again at each frame. Not on Wayland, which
  // has no such framebuffer in SDL: there the texture is the only way.
  if (how.software)
    spl::visit(spl::overloaded{[](video_driver::x11) { sdl::SDL_SetHint(sdl::kHintFramebufferAcceleration, "0"); },
                                     [](const auto&) {}},
                  video_driver_of(sdl::SDL_GetCurrentVideoDriver()));
  // What every Text draws with: this run's.
  skia::SkFont font;
  load_fonts(how.fonts, font);
  window_setup::graphics();
  sdl::SDL_GL_SetAttribute(sdl::SDL_GL_STENCIL_SIZE, 8);
  sdl::SDL_GL_SetAttribute(sdl::SDL_GL_DOUBLEBUFFER, 1);
  if (how.transparent)
    sdl::SDL_GL_SetAttribute(sdl::SDL_GL_ALPHA_SIZE, 8);
  const sdl::SDL_WindowFlags base = sdl::kWindowResizable | sdl::kWindowHighPixelDensity |
                               (how.transparent ? sdl::kWindowTransparent : sdl::SDL_WindowFlags{0});
#if defined(SK_GANESH)
  constexpr bool can_use_gl = true;
#else
  constexpr bool can_use_gl = false;  // a Skia without Ganesh draws in software
#endif
  sdl::SDL_Window* window = how.software || !can_use_gl
                           ? nullptr
                           : sdl::SDL_CreateWindow(how.title.c_str(), how.width, how.height, base | sdl::kWindowOpengl);
  if (!window)
    window = sdl::SDL_CreateWindow(how.title.c_str(), how.width, how.height, base);
  if (!window) {
    std::println(std::cerr, "[mux] no window: {}", sdl::SDL_GetError());
    sdl::SDL_Quit();
    return 1;
  }
  app.system_dialogs.over(window, kinds);
  // Pictures pasted, each in a file of its own.
  clipboard::paster paste;
  // The scale and what was settling at the frame before.
  float scale_before = 0.0f;
  std::string settling_before;
  // A phone's from the start: a touch screen and no mouse. Not known until
  // the first press otherwise, and the field given the focus meanwhile put
  // the on-screen keyboard up as the program opened.
  {
    int touch_screens = 0;
    sdl::SDL_free(sdl::SDL_GetTouchDevices(&touch_screens));  // the list SDL made, given back: only its count is read
    app.shared.by_touch = mux::platform::pointing::touch_first() || (touch_screens > 0 && !sdl::SDL_HasMouse());
  }
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
    // The system's clipboard, for pasting: read only as something is pasted
    // -- Ctrl+V, the text menu's Paste -- and only where it
    // changed since. Read at the start, at every change and whenever the
    // window came back, it was read without the user pasting anything, and
    // a phone said so ("mux pasted from your clipboard") at every start.
    bool clipboard_stale = true;
    const auto fresh_clipboard = [&clipboard_stale] {
      if (!std::exchange(clipboard_stale, false))
        return;
      char* text = sdl::SDL_GetClipboardText();
      skiff::scene::clipboardContents() = text ? text : "";
      sdl::SDL_free(text);
    };
    // Whether a key pastes: Ctrl+V, with Shift too.
    const auto pastes = [](const skiff::scene::key::down& press) {
      return press.key == skiff::scene::keys::kV && press.modifiers.template has<skiff::scene::modifier::control>();
    };
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
    // A long press, as a phone's (the user's, #13548): the main button held
    // where it went down for half a second is a right press there -- what is
    // under it given way first (a scroll, a selection begun), then its menu,
    // with a selection or without. Where it went down, when, and whether it
    // has been made a right press: its own lifting is then swallowed. From a
    // finger, or in a window shaped as a phone's (single, in the screen).
    struct held_t {
      float x = 0.0f, y = 0.0f;
      double since = 0.0;
      bool fired = false;
    };
    std::optional<held_t> held;
    constexpr double kHoldMs = 500.0;
    // How much taller the window grows as an on-screen keyboard goes away.
    constexpr float kKeyboardGone = 80.0f;
    constexpr float kHoldSlop = 8.0f;
    float last_width = 0.0f, last_height = 0.0f;
    // Fingers on the screen, where they are in the scene's points: two of
    // them drawn apart or together zoom -- a pinch, given to what is under
    // its middle as the wheel's zoom (the picture viewer's), a step of it
    // for each quarter of a step the distance went.
    std::map<sdl::SDL_FingerID, skia::SkPoint> fingers;
    float pinch_from = 0.0f;
    const auto pinch_distance = [&] {
      const auto first = fingers.begin();
      const auto second = std::next(first);
      return std::hypot(second->second.fX - first->second.fX, second->second.fY - first->second.fY);
    };
    // Where the field typed into was last told to the system.
    std::optional<skia::SkRect> typing_told;
    // Whether the window can be seen: off screen, no frames are made.
    bool on_screen = true;
    while (running) {
      sdl::SDL_Event event;
      const double hold_in = held && !held->fired ? held->since + kHoldMs - detail::now_ms()
                                                  : std::numeric_limits<double>::infinity();
      const double wake_in = std::min(wake_at - detail::now_ms(), hold_in);
      bool got = (redraw || animating)       ? sdl::SDL_WaitEventTimeout(&event, 16)
                 : std::isfinite(wake_in) ? sdl::SDL_WaitEventTimeout(&event, static_cast<std::int32_t>(
                                                                    std::clamp(wake_in, 1.0, 60000.0)))
                                          : sdl::SDL_WaitEvent(&event);
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
        // The interface's scale: the scene's points are larger than the
        // window's by it, and what the pointer says is taken in the scene's.
        const float to_scene = sdl::SDL_GetWindowPixelDensity(window) /
                               sdl::SDL_GetWindowDisplayScale(window) *
                               100.0f / static_cast<float>(std::max(1, app.interface_scale));
        // An event of a notification's window: a press opens its chat, in
        // the window brought up; nothing else of it reaches the scene.
        if (sdl::SDL_Window* over = sdl::SDL_GetWindowFromEvent(&event); over && over != window && shown_toasts.owns(over)) {
          if (event.type == sdl::SDL_EVENT_MOUSE_BUTTON_UP)
            if (const auto chat = shown_toasts.pressed(over)) {
              app.open_notified(*chat);
              sdl::SDL_RaiseWindow(window);
              redraw = true;
            }
          got = sdl::SDL_PollEvent(&event);
          continue;
        }
        if (event.type != sdl::SDL_EVENT_MOUSE_MOTION)
          give_motion();
        switch (event.type) {
          case sdl::SDL_EVENT_CLIPBOARD_UPDATE:
            clipboard_stale = true;
            break;
          case sdl::SDL_EVENT_WINDOW_FOCUS_GAINED:
            clipboard_stale = true;
            app.focus_changed(true);
            break;
          case sdl::SDL_EVENT_WINDOW_FOCUS_LOST:
            app.focus_changed(false);
            break;
          case sdl::SDL_EVENT_QUIT:
          case sdl::SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            running = false;
            break;
          // Off screen -- hidden, minimised, covered, suspended (a phone's
          // screen off): no frames, nothing but the connections.
          case sdl::SDL_EVENT_WINDOW_HIDDEN:
          case sdl::SDL_EVENT_WINDOW_MINIMIZED:
          case sdl::SDL_EVENT_WINDOW_OCCLUDED:
            on_screen = false;
            app.shown_changed(false);
            break;
          case sdl::SDL_EVENT_WINDOW_SHOWN:
          case sdl::SDL_EVENT_WINDOW_RESTORED:
            on_screen = true;
            app.shown_changed(true);
            scene.state().invalidateLayout();
            redraw = true;
            break;
          case sdl::SDL_EVENT_WINDOW_EXPOSED:
            on_screen = true;
            app.shown_changed(true);
            scene.state().invalidateLayout();
            redraw = true;
            break;
          case sdl::SDL_EVENT_RENDER_DEVICE_RESET:
            target.recover_graphics();
            kept_frame.reset();
            kept_frame_for = nullptr;
            overlays.clear();
            scene.state().invalidateLayout();
            redraw = true;
            break;
          case sdl::SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
          case sdl::SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
          case sdl::SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            scene.state().invalidateLayout();
            redraw = true;
            break;
          case sdl::SDL_EVENT_MOUSE_MOTION:
            motion = skiff::scene::pointer::move{event.motion.x * to_scene, event.motion.y * to_scene};
            // Moved off where it went down: no long press -- the press held
            // back is given now, where it was, and the move after it.
            if (held && !held->fired &&
                std::hypot(event.motion.x * to_scene - held->x, event.motion.y * to_scene - held->y) > kHoldSlop) {
              router.pointer(skiff::scene::pointer::down{held->x, held->y, sdl::kButtonLeft});
              held.reset();
            }
            break;
          case sdl::SDL_EVENT_MOUSE_BUTTON_DOWN:
            app.shared.by_touch = event.button.which == sdl::kTouchMouseid;
            // Where it may be a long press, the press is held back until it
            // is known not to be: given on a move or a lift, never where it
            // becomes the right press -- given at once, it began a selection
            // where it went down, and a selection held was gone before its
            // menu came (the user, #13637).
            if (event.button.button == sdl::kButtonLeft &&
                (event.button.which == sdl::kTouchMouseid || (last_width < 600.0f && last_height > last_width))) {
              held = held_t{event.button.x * to_scene, event.button.y * to_scene, detail::now_ms(), false};
              break;
            }
            held.reset();
            router.pointer(
                skiff::scene::pointer::down{event.button.x * to_scene, event.button.y * to_scene, event.button.button});
            break;
          case sdl::SDL_EVENT_MOUSE_BUTTON_UP:
            // Its lifting, where the hold was made a right press: that press
            // was all of it.
            if (event.button.button == sdl::kButtonLeft && held) {
              const held_t was = *std::exchange(held, std::nullopt);
              if (was.fired)
                break;
              // A tap: the press held back, then this.
              router.pointer(skiff::scene::pointer::down{was.x, was.y, sdl::kButtonLeft});
            }
            router.pointer(
                skiff::scene::pointer::up{event.button.x * to_scene, event.button.y * to_scene, event.button.button});
            break;
          case sdl::SDL_EVENT_FINGER_DOWN:
            app.shared.by_touch = true;
            fingers[event.tfinger.fingerID] = {event.tfinger.x * last_width, event.tfinger.y * last_height};
            if (fingers.size() == 2) {
              pinch_from = pinch_distance();
              held.reset();  // two fingers are no long press
            }
            break;
          case sdl::SDL_EVENT_FINGER_MOTION:
            if (const auto found = fingers.find(event.tfinger.fingerID); found != fingers.end()) {
              found->second = {event.tfinger.x * last_width, event.tfinger.y * last_height};
              if (fingers.size() == 2 && pinch_from > 0.0f) {
                const float now = pinch_distance();
                const float ticks = std::log(now / pinch_from) / std::log(1.25f);
                if (std::abs(ticks) >= 0.25f) {
                  const auto first = fingers.begin();
                  const auto second = std::next(first);
                  router.pointer(skiff::scene::pointer::scroll{(first->second.fX + second->second.fX) * 0.5f,
                                                               (first->second.fY + second->second.fY) * 0.5f, 0.0f, ticks});
                  pinch_from = now;
                }
              }
            }
            break;
          case sdl::SDL_EVENT_FINGER_UP:
          case sdl::SDL_EVENT_FINGER_CANCELED:
            fingers.erase(event.tfinger.fingerID);
            pinch_from = 0.0f;
            break;
          case sdl::SDL_EVENT_MOUSE_WHEEL:
            router.pointer(skiff::scene::pointer::scroll{event.wheel.mouse_x * to_scene, event.wheel.mouse_y * to_scene, event.wheel.x,
                                                         event.wheel.y});
            break;
          case sdl::SDL_EVENT_KEY_DOWN:
          case sdl::SDL_EVENT_KEY_UP: {
            const skiff::scene::Key key = detail::key_of(event.key.key);
            if (key == skiff::scene::keys::kUnknown)
              break;
            // Tab too: the router moves the focus on it itself.
            const skiff::scene::Modifiers held = detail::modifiers_of(event.key.mod);
            // Ctrl+V with a picture on the clipboard: the picture, as if
            // dropped on the window -- not the text a field would paste.
            if (event.type == sdl::SDL_EVENT_KEY_DOWN && event.key.key == sdl::kKeyV && (event.key.mod & sdl::kKmodCtrl) != 0 &&
                !event.key.repeat) {
              if (std::optional<std::string> picture = paste.picture()) {
                app.files_given(std::vector<std::string>{std::move(*picture)}, true);
                break;
              }
            }
            if (event.type == sdl::SDL_EVENT_KEY_DOWN) {
              const skiff::scene::key::down press{key, held, event.key.repeat};
              if (pastes(press))
                fresh_clipboard();
              router.key(press);
            } else
              router.key(skiff::scene::key::up{key, held});
            break;
          }
          case sdl::SDL_EVENT_DROP_FILE:
            if (event.drop.data)
              app.files_given(std::vector<std::string>{event.drop.data}, true);
            break;
          case sdl::SDL_EVENT_TEXT_INPUT:
            router.text(skiff::scene::text::commit{event.text.text});
            break;
          case sdl::SDL_EVENT_TEXT_EDITING:
            router.text(skiff::scene::text::compose{event.edit.text ? event.edit.text : "", event.edit.start,
                                                    event.edit.length});
            break;
          default:
            if (event.type == kinds.wake)
              app.woken();
            else if (event.type == kinds.files) {
              std::ranges::for_each(app.system_dialogs.take_files(),
                                    [&](std::vector<std::string>& paths) { app.files_given(std::move(paths), false); });
              std::ranges::for_each(app.system_dialogs.take_failures(),
                                    [&](std::string& why) { app.dialog_failed(std::move(why)); });
            }
            else if (event.type == kinds.save)
              std::ranges::for_each(app.system_dialogs.take_save_paths(),
                                    [&](std::string& path) { app.save_path_chosen(std::move(path)); });
            break;
        }
        got = sdl::SDL_PollEvent(&event);
      }
      give_motion();
      if (!running)
        break;
      // Held long enough where it went down: what is under it lets go of the
      // gesture, and it is pressed and let go of with the right button.
      if (held && !held->fired && detail::now_ms() - held->since >= kHoldMs) {
        held->fired = true;
        router.pointer(skiff::scene::pointer::down{held->x, held->y, sdl::kButtonRight});
        router.pointer(skiff::scene::pointer::up{held->x, held->y, sdl::kButtonRight});
      }

      // The display's scale, times the interface's (Settings, Appearance).
      const float scale = sdl::SDL_GetWindowDisplayScale(window) * static_cast<float>(app.interface_scale) / 100.0f;
      // Another: all of it laid out and painted again at it.
      if (scale != std::exchange(scale_before, scale)) {
        scene.state().invalidateLayout();
        redraw = true;
      }
      skiff::scene::pixelScale() = scale;
      int pixel_width = 0, pixel_height = 0;
      sdl::SDL_GetWindowSizeInPixels(window, &pixel_width, &pixel_height);
      const float width = static_cast<float>(pixel_width) / scale;
      const float height = static_cast<float>(pixel_height) / scale;
      // The on-screen keyboard put away by the user, the field still
      // focused: the field let go of, and with it the text input. Told where
      // SDL stopped the text input itself (Android's Back over the
      // keyboard), or by the window grown back as tall as before with its
      // width the same (postmarketOS: its keyboard tells the program
      // nothing). Left focused, the field's place was told to the system
      // again as the window grew, and that brought the keyboard straight back
      // up.
      if (typing && (!sdl::SDL_TextInputActive(window) ||
                     (app.shared.by_touch && width == last_width && height > last_height + kKeyboardGone))) {
        scene.clearFocus();
        redraw = true;
      }
      last_width = width;
      last_height = height;
      // What the scene left for the host: the text input started or stopped
      // as a field takes the focus or lets it go, what was copied put on
      // the clipboard, the links pressed followed.
      auto& work = skiff::scene::hostWork();
      if (work.typing != typing) {
        typing = work.typing;
        if (typing)
          sdl::SDL_StartTextInput(window);
        else
          sdl::SDL_StopTextInput(window);
      }
      // The field typed into, told to the system where it moved: in the
      // window's coordinates, the scene's points times the interface's scale.
      if (typing && work.typingAt && work.typingAt != typing_told) {
        typing_told = work.typingAt;
        const float to_window = scale / sdl::SDL_GetWindowPixelDensity(window);
        const sdl::SDL_Rect area{static_cast<int>(work.typingAt->fLeft * to_window), static_cast<int>(work.typingAt->fTop * to_window),
                            static_cast<int>(work.typingAt->width() * to_window),
                            static_cast<int>(work.typingAt->height() * to_window)};
        sdl::SDL_SetTextInputArea(window, &area, 0);
      } else if (!typing) {
        typing_told.reset();
      }
      if (auto copied = std::exchange(work.copied, std::nullopt)) {
        sdl::SDL_SetClipboardText(copied->c_str());
        skiff::scene::clipboardContents() = std::move(*copied);
      }
      for (auto& url : std::exchange(work.links, {}))
        app.open_link(std::move(url));
      // The notifications mux shows itself, put up; the old ones gone.
      // The newest three at most: a window made for each only to be closed
      // at once, where many came together, was the whole of a frame.
      auto due_now = app.take_toasts();
      for (std::size_t i = due_now.size() > 3 ? due_now.size() - 3 : 0; i < due_now.size(); ++i)
        shown_toasts.show(due_now[i], app.colours);
      shown_toasts.frame(app.painting());
      // Off screen: no frame -- not the program's either, which marks what
      // is in view as read. Woken only by events and the network.
      if (!on_screen) {
        animating = !shown_toasts.empty();
        wake_at = std::numeric_limits<double>::infinity();
        continue;
      }
      const double events_done = detail::now_ms();
      app.before_frame();
      // Keys the program's own controls stand for -- a text menu's Paste is
      // Ctrl+V -- given to what has the focus, down and up, before the frame.
      for (const skiff::scene::key::down& press : std::exchange(skiff::scene::hostWork().keys, {})) {
        if (pastes(press))
          fresh_clipboard();
        router.key(press);
        router.key(skiff::scene::key::up{press.key, press.modifiers});
      }
      const double app_done = detail::now_ms();
      scene.update(app_done);
      const double ticked = detail::now_ms();
      sdl::SDL_Rect safe{};
      auto bounds = skia::SkRect::MakeWH(width, height);
      if (sdl::SDL_GetWindowSafeArea(window, &safe) && safe.w > 0 && safe.h > 0) {
        const float to_points = sdl::SDL_GetWindowPixelDensity(window) / scale;
        bounds = skia::SkRect::MakeXYWH(safe.x * to_points, safe.y * to_points,
                                      safe.w * to_points, safe.h * to_points);
      }
      scene.layoutIfNeeded(bounds);
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
        // Whether what is repainted has something blurring what is under it,
        // live: all of the window, while one is shown; a part, where one is in it.
        // Only what reads what is under it, live -- not one drawn from what it
        // keeps as it moves -- keeps the frame from being played in bands.
        bool live_under = std::ranges::any_of(skiff::scene::detail::liveBackdrops(),
                                              [](const auto& one) { return !one.second.kept; });
        // What blurs live whose rect is repainted whole this frame: one not
        // drawn there is no longer shown, and is let go of once it is drawn.
        std::vector<skiff::scene::NodeId> live_repainted;
        bool all_repainted = true;
        // The rings round a live blur's piece, as they were: put back once
        // the frame is drawn (above).
        struct ring_kept {
          skia::Sp<skia::SkImage> before;
          skia::SkRect ring;     // where it was taken, on the device
          skia::SkRect reached;  // what of it was drawn anew, and stays
        };
        std::vector<ring_kept> put_back;
        if (!whole && !fresh) {
          show_all = false;
          live_under = false;
          all_repainted = false;
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
          // What blurs what is under it, live: repainted wherever any of what
          // is under it is -- else it blurred its own last pixels. Only as far
          // as a change under it reaches into what it shows (the user's,
          // #13523): that, and as far again round it for what that blurs,
          // drawn; and of the ring round it, which blurred what was cut at the
          // piece's edge, what was there before put back -- nothing under it
          // changed. Most of it reached: all of it, as before.
          const std::vector<skia::SkRect> damaged = pieces;
          for (const auto& [id, live] : skiff::scene::detail::liveBackdrops()) {
            skia::SkRect whole_of = live.rect;
            whole_of.roundOut(&whole_of);
            if (!whole_of.intersect(all))
              continue;
            skia::SkRect reached = skia::SkRect::MakeEmpty();
            for (const skia::SkRect& piece : damaged)
              if (skia::SkRect inside = piece; inside.intersect(whole_of))
                reached.join(inside);
            if (reached.isEmpty())
              continue;
            reached.outset(live.reach, live.reach);
            reached.roundOut(&reached);
            // A scroll under it moved all it shows there, not only its damage.
            const bool scrolled_under = std::ranges::any_of(frame.fMoves, [&](const skiff::scene::ScrollMove& move) {
              return skia::SkRect::Intersects(skia::SkRect::MakeLTRB(move.rect.fLeft * scale, move.rect.fTop * scale,
                                                                     move.rect.fRight * scale, move.rect.fBottom * scale),
                                              whole_of);
            });
            if (!scrolled_under && live.reach > 0.0f && reached.intersect(whole_of) &&
                reached.width() * reached.height() < 0.6f * whole_of.width() * whole_of.height()) {
              skia::SkRect drawn = reached.makeOutset(live.reach, live.reach);
              drawn.roundOut(&drawn);
              if (drawn.intersect(all)) {
                skia::SkRect ring = drawn;
                if (ring.intersect(whole_of))
                  if (auto before = kept->makeImageSnapshot(ring.roundOut()))
                    put_back.push_back({std::move(before), ring, reached});
                pieces.push_back(drawn);
                repainted.join(drawn);
                live_under = live_under || !live.kept;
              }
              continue;
            }
            {
              pieces.push_back(whole_of);
              repainted.join(whole_of);
              live_under = live_under || !live.kept;
              live_repainted.push_back(id);
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
        // Not in bands while something blurs what is under it, live, in what
        // is repainted: at a band's edge it would not see the band beside it.
        // A menu up elsewhere keeps the rest of the window in bands.
        if (area > 300000.0f && team_size > 1 && !live_under && kept->peekPixels(&pixels)) {
          skia::SkPictureRecorder recorder;
          skia::SkCanvas* record = recorder.beginRecording(all);
          for (const skia::SkRect& piece : pieces) {
            record->save();
            record->clipRect(piece);
            if (how.transparent)
              record->clear(skia::SkColor{0});
            record->scale(scale, scale);
            scene.draw(app.painting(), record);
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
            scene.draw(app.painting(), into);
            into->restore();
          }
        }
        // Each ring put back where the blur under it was cut: the four sides
        // of what was taken round what was drawn anew.
        for (const ring_kept& one : put_back) {
          const skia::SkRect& r = one.ring;
          const skia::SkRect& in = one.reached;
          for (const skia::SkRect& side : {skia::SkRect::MakeLTRB(r.fLeft, r.fTop, r.fRight, in.fTop),
                                           skia::SkRect::MakeLTRB(r.fLeft, in.fBottom, r.fRight, r.fBottom),
                                           skia::SkRect::MakeLTRB(r.fLeft, in.fTop, in.fLeft, in.fBottom),
                                           skia::SkRect::MakeLTRB(in.fRight, in.fTop, r.fRight, in.fBottom)}) {
            if (side.isEmpty())
              continue;
            into->save();
            into->clipRect(side);
            skia::SkPaint copy;
            copy.setBlendMode(skia::SkBlendMode::kSrc);
            into->drawImage(one.before, r.fLeft, r.fTop, skia::SkSamplingOptions(), &copy);
            into->restore();
          }
        }
        for (const skia::SkRect& piece : pieces)
          changed.push_back(piece.roundOut());
        std::erase_if(skiff::scene::detail::liveBackdrops(), [&](const auto& one) {
          return one.second.frame != skiff::scene::work::frameNumber() &&
                 (all_repainted || std::ranges::contains(live_repainted, one.first));
        });
        // Something that blurs live was drawn into the bands where it had to
        // read what is under it: all of it again at the next frame, not in
        // bands -- what it reads, it then reads.
        if (std::exchange(skiff::scene::detail::liveBackdropsStale(), false)) {
          redraw = true;
          for (auto& [id, one] : skiff::scene::detail::liveBackdrops())
            one.kept = false;
        }
        if (kept != surface) {
          // Put in place of what the buffer had, not over it: one copy -- a
          // see-through window's was a clear and then a blend of the whole
          // frame, twice its pixels at every frame of an animation.
          skia::SkPaint copy;
          copy.setBlendMode(skia::SkBlendMode::kSrc);
          canvas->drawImage(kept_frame->makeImageSnapshot(), 0.0f, 0.0f, skia::SkSamplingOptions(), &copy);
        }
      } else {
        kept_frame = nullptr;
        kept_frame_for = nullptr;
        canvas->clear(how.transparent ? skia::SkColor{0} : skia::colorSetARGB(255, 24, 27, 30));
        canvas->save();
        canvas->scale(scale, scale);
        scene.draw(app.painting(), canvas);
        canvas->restore();
        // All of it drawn: what blurs live and was not drawn is not shown.
        std::erase_if(skiff::scene::detail::liveBackdrops(),
                      [](const auto& one) { return one.second.frame != skiff::scene::work::frameNumber(); });
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
  sdl::SDL_DestroyWindow(window);
  sdl::SDL_Quit();
  return result;
}

}  // namespace mux::platform::window
