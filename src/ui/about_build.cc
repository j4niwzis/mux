// SPDX-License-Identifier: AGPL-3.0-only
// The commit and branch mux is built from, as its About page says them: from
// a file written at every build where they changed (cmake/about_commit.py),
// read here alone.
module mux.ui.base;

import std;

namespace mux::ui {
#include "about-commit.inc"
std::string_view build_commit() { return about_build::commit; }
std::string_view build_branch() { return about_build::branch; }
}  // namespace mux::ui
