// SPDX-License-Identifier: AGPL-3.0-only
// MUX_INLINE_IN_RELEASE: what a view is declared with. Meant to be inline in
// a release build and not outside one -- a view's code, and the nodes it
// makes, emitted once in the unit it is written in, not again in scene.cc.
//
// Inline in every build for now: a view's type is the class SKIFF_NAMED
// makes inside it, and a class local to a function that is not inline is
// no entity another unit can name -- the window, the toasts' window and
// the program, instantiating scenes and walks over those types elsewhere,
// crashed clang (Scene<Named<toast_card, Held>>, in window_toasts). Not
// inline outside a release build once what a view returns there names no
// class of its own (held, its name kept in its state).
#pragma once

#define MUX_INLINE_IN_RELEASE inline
