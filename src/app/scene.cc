// SPDX-License-Identifier: AGPL-3.0-only
// The window's scene, instantiated here alone: every walk over its tree --
// routing, layout, drawing -- for every type of node in it. The other
// units see it declared extern (mux.app.requests) and call it.
import std;
import skiff.scene;
import mux.ui;
import mux.ui.proto;
import mux.app.requests;

// The tables of the window's big subtrees -- and so the walks of all in them
// -- are made in walks_*.cc, outside a release build: requests.cc says so
// (skiff::scene::kOpsElsewhere), and here they are calls, not instantiated.

template class skiff::scene::Scene<mux::app::window_type>;
