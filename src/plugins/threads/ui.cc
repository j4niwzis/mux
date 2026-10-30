// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.threads.ui -- The threads' button in the chat's head: the
// panel opened, or closed.
module;
#include "../../inline_in_release.h"
#include "skiff/named.h"
export module mux.plugin.threads.ui;

import std;
import skiff.scene;
import skiff.compose;
import mux.composition;
import mux.core;
import mux.ui.base;
import mux.plugin.threads.types;

export namespace mux::plugins::threads {
using namespace ::mux::ui;

// Its button in the chat's head (mux.ui:header), after Search.
MUX_INLINE_IN_RELEASE auto extend(contract::chat_header, type_tag<feature>, const palette& colours) {
  return SKIFF_NAMED(threads_button, skiff::compose::styled({.alignSelf = scene::align::kMiddle},
                                                            icon_button(colours, icon::threads{}, sends<request::toggle_threads>{})));
}

}  // namespace mux::plugins::threads
