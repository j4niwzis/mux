// SPDX-License-Identifier: AGPL-3.0-only
// The layers screens, made for the program's actions, where mux.app.screens
// says they are made.
//
// A plain unit that imports what it instantiates, not a unit of
// mux.app.screens: one of those imports the module's own interface, with
// its extern template declarations, and clang read the specialisations
// those had made back out of the module file without their members --
// "no member named 'parts' in context_menu<actions>", then a crash. Here
// nothing is declared extern, and the instantiation is the ordinary kind.
import std;
import mux.ui;
import mux.app.requests;

template struct mux::ui::window<mux::app::actions>;
template struct mux::ui::context_menu<mux::app::actions>;
template struct mux::ui::emoji_popup<mux::app::actions>;
template struct mux::ui::picture_viewer<mux::app::actions>;
template struct mux::ui::send_box<mux::app::actions>;
template struct mux::ui::notice_box<mux::app::actions>;
