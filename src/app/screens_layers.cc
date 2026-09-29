// SPDX-License-Identifier: AGPL-3.0-only
// The layers screens, made for the program's actions: one of mux.app.screens's units.
module mux.app.screens;

template struct mux::ui::window<mux::app::actions>;
template struct mux::ui::context_menu<mux::app::actions>;
template struct mux::ui::emoji_popup<mux::app::actions>;
template struct mux::ui::picture_viewer<mux::app::actions>;
template struct mux::ui::send_box<mux::app::actions>;
template struct mux::ui::notice_box<mux::app::actions>;
