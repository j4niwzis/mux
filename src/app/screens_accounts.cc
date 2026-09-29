// SPDX-License-Identifier: AGPL-3.0-only
// The accounts screens, made for the program's actions, where mux.app.screens
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

template struct mux::ui::accounts_panel<mux::app::actions>;
template struct mux::ui::account_editor<mux::app::actions>;
template struct mux::ui::account_entry<mux::app::actions>;
template struct mux::ui::account_pages<mux::app::actions>;
template struct mux::ui::account_privacy<mux::app::actions>;
template struct mux::ui::account_proxy<mux::app::actions>;
template struct mux::ui::add_account_pane<mux::app::actions>;
template struct mux::ui::xmpp_form<mux::app::actions>;
template struct mux::ui::xmpp_advanced<mux::app::actions>;
template struct mux::ui::matrix_form<mux::app::actions>;
template struct mux::ui::form_end<mux::app::actions>;
