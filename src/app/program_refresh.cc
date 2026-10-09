// SPDX-License-Identifier: AGPL-3.0-only
// The program: what the window's parts read of the chats that the settings
// need as well -- the space each chat is in. The rest they read themselves.
module mux.app.program;

import std;
import splice;
import knot;
import skia;
import mux.core;
import mux.config;
import mux.net;
import mux.media;
import mux.ui;
import mux.kept_root;
import skiff.paint;
import skiff.scene;
import mux.app.network;
import mux.app.demo;
import mux.app.store;
import mux.app.requests;
import mux.app.words;

namespace mux::app {

// The space each chat is in: its choices, where the chat has none.
void app::note_spaces() { space_above = mux::space_above_of(model->accounts()); }

}  // namespace mux::app
