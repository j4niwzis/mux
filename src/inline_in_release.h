// SPDX-License-Identifier: AGPL-3.0-only
// MUX_INLINE_IN_RELEASE: what a view is declared with -- inline in a release
// build, where the window is folded whole and its views inline with it;
// outside one, not: a view's code, and the nodes it makes, emitted once, in
// the unit it is written in, and only called by the window's -- not all of
// them made again in scene.cc, the unit the window is built in (half a
// thousand seconds of a debug build's compile, the user's limit a hundred).
// A macro, as the user said: only the preprocessor makes a keyword depend on
// the build. Included in a unit's global module fragment.
#pragma once

#if defined(SKIFF_ERASED_WALKS) && !defined(SKIFF_CONCRETE_COMBINATORS)
#define MUX_INLINE_IN_RELEASE
#else
#define MUX_INLINE_IN_RELEASE inline
#endif
