// SPDX-License-Identifier: AGPL-3.0-only
// The accounts screens, made for the program's actions: one of mux.app.screens's units.
module mux.app.screens;

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
