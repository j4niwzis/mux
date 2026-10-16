// SPDX-License-Identifier: AGPL-3.0-only
// The window's scene, instantiated here alone: every walk over its tree --
// routing, layout, drawing -- for every type of node in it. The other
// units see it declared extern (mux.app.requests) and call it.
module mux.app.requests;

import std;
import skiff.scene;
import skiff.bind;
// Static routing must see the program's startCarry overload here. It was
// only visible to callers of this extern-instantiated scene, so release
// builds discarded returned pointer, key and semantic handler requests.
import mux.app.program;
import mux.app.kept;
import mux.ui;
import mux.ui.proto;

// Outside a release build every node's table is made here, once: the
// window is constructed here, and a stack takes its parts' tables as it is
// made (see requests.cc).

namespace {
// Use the same unqualified lookup and ADL as Scene's routing. A missing
// import must fail here rather than silently selecting NoCarry again.
template <class Root> auto routing_carry(Root& root) {
  using skiff::scene::startCarry;
  return startCarry(root);
}
static_assert(std::same_as<decltype(routing_carry(std::declval<mux::app::window_type&>())),
                           skiff::bind::CarryRoot<mux::app::kept_model, mux::app::app>>);
}

template class skiff::scene::Scene<mux::app::window_type>;

namespace mux::app {
window_scene::window_scene(const mux::ui::ui_needs& needs)
    : fScene(std::in_place, mux::ui::window_view(needs)) {}
window_scene::~window_scene() { std::destroy_at(&fScene); }
}
