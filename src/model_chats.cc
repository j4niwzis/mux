// SPDX-License-Identifier: AGPL-3.0-only
// The chats model, instantiated here alone; the other units see it declared extern.
module mux.core;

import std;
import skiff.model;

template void skiff::model::Model<mux::chats_root, mux::chats_reactions>::endBatch();
