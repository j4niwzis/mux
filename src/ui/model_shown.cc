// SPDX-License-Identifier: AGPL-3.0-only
// What the window shows, as a model, instantiated here alone; the other units see it declared extern.
module mux.ui;

import std;
import skiff.model;

template void skiff::model::Model<mux::ui::shown_root, mux::ui::shown_reactions>::endBatch();
