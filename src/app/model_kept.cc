// SPDX-License-Identifier: AGPL-3.0-only
// The kept settings model, instantiated here alone; the other units see it declared extern.
module mux.app.kept;

import std;
import skiff.model;

template class skiff::model::Model<mux::app::kept_root, mux::app::kept_reactions>;
