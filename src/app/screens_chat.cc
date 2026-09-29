// SPDX-License-Identifier: AGPL-3.0-only
// The chat screens, made for the program's actions: one of mux.app.screens's units.
module mux.app.screens;

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
