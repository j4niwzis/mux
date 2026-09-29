// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.screens: the window's screens, made for this program's actions once
// -- in this module's units, side by side -- and not again in every unit of
// the program that reaches them. The declarations here say where they are
// made. They are not exported (an explicit instantiation names nothing), but
// whoever imports this module reaches them, and makes none of them itself.
export module mux.app.screens;

import std;
import mux.ui;
import mux.app.requests;

extern template struct mux::ui::conversations_screen<mux::app::actions>;
extern template struct mux::ui::conversation_row<mux::app::actions>;
extern template struct mux::ui::chat_header<mux::app::actions>;
extern template struct mux::ui::search_bar<mux::app::actions>;
extern template struct mux::ui::timeline_area<mux::app::actions>;
extern template struct mux::ui::jump_button<mux::app::actions>;
extern template struct mux::ui::composer_bar<mux::app::actions>;
extern template struct mux::ui::info_panel<mux::app::actions>;
extern template struct mux::ui::person_card<mux::app::actions>;
extern template struct mux::ui::menu_button<mux::app::actions>;
extern template struct mux::ui::drawer_panel<mux::app::actions>;
extern template struct mux::ui::drawer_account<mux::app::actions>;
extern template struct mux::ui::window<mux::app::actions>;
extern template struct mux::ui::context_menu<mux::app::actions>;
extern template struct mux::ui::emoji_popup<mux::app::actions>;
extern template struct mux::ui::picture_viewer<mux::app::actions>;
extern template struct mux::ui::send_box<mux::app::actions>;
extern template struct mux::ui::notice_box<mux::app::actions>;
extern template struct mux::ui::accounts_panel<mux::app::actions>;
extern template struct mux::ui::account_editor<mux::app::actions>;
extern template struct mux::ui::account_entry<mux::app::actions>;
extern template struct mux::ui::account_pages<mux::app::actions>;
extern template struct mux::ui::account_privacy<mux::app::actions>;
extern template struct mux::ui::account_proxy<mux::app::actions>;
extern template struct mux::ui::add_account_pane<mux::app::actions>;
extern template struct mux::ui::xmpp_form<mux::app::actions>;
extern template struct mux::ui::xmpp_advanced<mux::app::actions>;
extern template struct mux::ui::matrix_form<mux::app::actions>;
extern template struct mux::ui::form_end<mux::app::actions>;
extern template struct mux::ui::settings_dialog<mux::app::actions>;
extern template struct mux::ui::settings_home<mux::app::actions>;
extern template struct mux::ui::animations_page<mux::app::actions>;
extern template struct mux::ui::appearance_page<mux::app::actions>;
extern template struct mux::ui::theme_card<mux::app::actions>;
extern template struct mux::ui::accent_circle<mux::app::actions>;
extern template struct mux::ui::rendering_page<mux::app::actions>;
extern template struct mux::ui::storage_page<mux::app::actions>;
extern template struct mux::ui::files_page<mux::app::actions>;
extern template struct mux::ui::proxies_page<mux::app::actions>;
extern template struct mux::ui::proxy_editor<mux::app::actions>;
extern template struct mux::ui::kind_switch<mux::app::actions>;
