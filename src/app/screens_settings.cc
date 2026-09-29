// SPDX-License-Identifier: AGPL-3.0-only
// The settings screens, made for the program's actions: one of mux.app.screens's units.
module mux.app.screens;

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
