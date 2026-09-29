// SPDX-License-Identifier: AGPL-3.0-only
// The chat screens, made for the program's actions, where mux.app.screens
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

template struct mux::ui::conversations_screen<mux::app::actions>;
template struct mux::ui::conversation_row<mux::app::actions>;
template struct mux::ui::chat_header<mux::app::actions>;
template struct mux::ui::search_bar<mux::app::actions>;
template struct mux::ui::timeline_area<mux::app::actions>;
template struct mux::ui::jump_button<mux::app::actions>;
template struct mux::ui::composer_bar<mux::app::actions>;
template struct mux::ui::info_panel<mux::app::actions>;
template struct mux::ui::person_card<mux::app::actions>;
template struct mux::ui::menu_button<mux::app::actions>;
template struct mux::ui::drawer_panel<mux::app::actions>;
template struct mux::ui::drawer_account<mux::app::actions>;
