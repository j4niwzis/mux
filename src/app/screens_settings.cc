// SPDX-License-Identifier: AGPL-3.0-only
// The settings screens, made for the program's actions, where mux.app.screens
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

template struct mux::ui::settings_dialog<mux::app::actions>;
template struct mux::ui::settings_home<mux::app::actions>;
template struct mux::ui::animations_page<mux::app::actions>;
template struct mux::ui::appearance_page<mux::app::actions>;
template struct mux::ui::theme_card<mux::app::actions>;
template struct mux::ui::accent_circle<mux::app::actions>;
template struct mux::ui::rendering_page<mux::app::actions>;
template struct mux::ui::storage_page<mux::app::actions>;
template struct mux::ui::files_page<mux::app::actions>;
template struct mux::ui::proxies_page<mux::app::actions>;
template struct mux::ui::proxy_editor<mux::app::actions>;
template struct mux::ui::kind_switch<mux::app::actions>;
