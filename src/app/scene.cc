// SPDX-License-Identifier: AGPL-3.0-only
// The window's scene, instantiated here alone: every walk over its tree --
// routing, layout, drawing -- for every type of node in it. The other
// units see it declared extern (mux.app.requests) and call it.
import std;
import skiff.scene;
import mux.app.requests;

template class skiff::scene::Scene<mux::app::window_type>;
