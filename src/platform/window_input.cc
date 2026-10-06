// SPDX-License-Identifier: AGPL-3.0-only
// mux.platform.window:input -- What the host makes of SDL's input: pointer shapes, keys and modifiers.
export module mux.platform.window:input;

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

export namespace mux::platform::window {
namespace detail {

// SDL's shape for each of skiff's pointer shapes.
inline sdl::SDL_SystemCursor system_cursor(skiff::scene::cursor::arrow) { return sdl::SDL_SYSTEM_CURSOR_DEFAULT; }
inline sdl::SDL_SystemCursor system_cursor(skiff::scene::cursor::text) { return sdl::SDL_SYSTEM_CURSOR_TEXT; }
inline sdl::SDL_SystemCursor system_cursor(skiff::scene::cursor::hand) { return sdl::SDL_SYSTEM_CURSOR_POINTER; }
inline sdl::SDL_SystemCursor system_cursor(skiff::scene::cursor::resize_horizontal) { return sdl::SDL_SYSTEM_CURSOR_EW_RESIZE; }
inline sdl::SDL_SystemCursor system_cursor(skiff::scene::cursor::resize_vertical) { return sdl::SDL_SYSTEM_CURSOR_NS_RESIZE; }

// The pointer's shape, made once each and set when it changes.
class pointer_shapes {
 public:
  pointer_shapes() = default;
  pointer_shapes(const pointer_shapes&) = delete;
  pointer_shapes& operator=(const pointer_shapes&) = delete;
  ~pointer_shapes() {
    for (sdl::SDL_Cursor* one : made_)
      if (one)
        sdl::SDL_DestroyCursor(one);
  }
  void show(const skiff::scene::Cursor& shape) {
    const sdl::SDL_SystemCursor which = spl::visit([](auto one) { return system_cursor(one); }, shape);
    if (which == shown_)
      return;
    sdl::SDL_Cursor*& made = made_[static_cast<std::size_t>(which)];
    if (!made)
      made = sdl::SDL_CreateSystemCursor(which);
    if (made)
      sdl::SDL_SetCursor(made);
    shown_ = which;
  }

 private:
  std::array<sdl::SDL_Cursor*, sdl::SDL_SYSTEM_CURSOR_COUNT> made_{};
  sdl::SDL_SystemCursor shown_ = sdl::SDL_SYSTEM_CURSOR_DEFAULT;
};

inline skiff::scene::Key key_of(sdl::SDL_Keycode key) {
  namespace keys = skiff::scene::keys;
  switch (key) {
    case sdl::kKeyTab: return keys::kTab;
    case sdl::kKeyReturn:
    case sdl::kKeyKpEnter: return keys::kEnter;
    case sdl::kKeySpace: return keys::kSpace;
    // Android's Back (button or gesture) closes what Escape closes.
    case sdl::kKeyBack:
    case sdl::kKeyEscape: return keys::kEscape;
    case sdl::kKeyLeft: return keys::kLeft;
    case sdl::kKeyRight: return keys::kRight;
    case sdl::kKeyUp: return keys::kUp;
    case sdl::kKeyDown: return keys::kDown;
    case sdl::kKeyHome: return keys::kHome;
    case sdl::kKeyEnd: return keys::kEnd;
    case sdl::kKeyBackspace: return keys::kBackspace;
    case sdl::kKeyDelete: return keys::kDelete;
    case sdl::kKeyA: return keys::kA;
    case sdl::kKeyB: return keys::kB;
    case sdl::kKeyC: return keys::kC;
    case sdl::kKeyD: return keys::kD;
    case sdl::kKeyE: return keys::kE;
    case sdl::kKeyF: return keys::kF;
    case sdl::kKeyG: return keys::kG;
    case sdl::kKeyH: return keys::kH;
    case sdl::kKeyI: return keys::kI;
    case sdl::kKeyJ: return keys::kJ;
    case sdl::kKeyK: return keys::kK;
    case sdl::kKeyL: return keys::kL;
    case sdl::kKeyM: return keys::kM;
    case sdl::kKeyN: return keys::kN;
    case sdl::kKeyO: return keys::kO;
    case sdl::kKeyP: return keys::kP;
    case sdl::kKeyQ: return keys::kQ;
    case sdl::kKeyR: return keys::kR;
    case sdl::kKeyS: return keys::kS;
    case sdl::kKeyT: return keys::kT;
    case sdl::kKeyU: return keys::kU;
    case sdl::kKeyV: return keys::kV;
    case sdl::kKeyW: return keys::kW;
    case sdl::kKeyX: return keys::kX;
    case sdl::kKeyY: return keys::kY;
    case sdl::kKeyZ: return keys::kZ;
    case sdl::kKeyPageup: return keys::kPageUp;
    case sdl::kKeyPagedown: return keys::kPageDown;
    case sdl::kKey0: return keys::k0;
    case sdl::kKey1: return keys::k1;
    case sdl::kKey2: return keys::k2;
    case sdl::kKey3: return keys::k3;
    case sdl::kKey4: return keys::k4;
    case sdl::kKey5: return keys::k5;
    case sdl::kKey6: return keys::k6;
    case sdl::kKey7: return keys::k7;
    case sdl::kKey8: return keys::k8;
    case sdl::kKey9: return keys::k9;
    case sdl::kKeyPeriod: return keys::kPeriod;
    default: return keys::kUnknown;
  }
}

// What SDL says of the modifier keys.
inline skiff::scene::Modifiers modifiers_of(sdl::SDL_Keymod held) {
  namespace modifier = skiff::scene::modifier;
  return skiff::scene::Modifiers{}
      .with<modifier::shift>((held & sdl::kKmodShift) != 0)
      .with<modifier::control>((held & sdl::kKmodCtrl) != 0)
      .with<modifier::alt>((held & sdl::kKmodAlt) != 0)
      .with<modifier::super>((held & sdl::kKmodGui) != 0);
}

}  // namespace detail
}  // namespace mux::platform::window
