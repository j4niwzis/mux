// SPDX-License-Identifier: AGPL-3.0-only
// The developer room page, instantiated independently of the settings dialog.
module mux.ui.proto.matrix;

import std;
import skiff.scene;
import skiff.compose;
import skiff.model;
import mux.ui.base;

namespace mux::proto::matrix::manage {
using developer_page_t = decltype(manage_detail::developer_page(std::declval<manage_detail::manage_box*>(), std::declval<const ui::room_settings_facts&>()));
// Check the actual external action and Local-handler return types against
// the page boundary's declared requests; OutOf<Local> alone is empty.
using developer_answers = skiff::model::Types<
    typename decltype(std::declval<manage_detail::developer_events>().on(manage_detail::save_state_event{}, std::declval<const manage_detail::developer_draft&>()).fEvent)::value_type,
    typename decltype(std::declval<manage_detail::developer_events>().on(manage_detail::save_account_data{}, std::declval<const manage_detail::developer_draft&>()).fEvent)::value_type,
    manage_detail::toggle_developer_tools::Answer,
    ui::sends<ui::request::copy_text>::Answer,
    ui::sends<request::open_send_custom>::Answer>;
static_assert(std::same_as<skiff::compose::Minus<developer_answers, manage_out>, skiff::model::Types<>>);
erased_page manage_page_erased(advanced, manage_detail::manage_box* box, const ui::room_settings_facts& facts) {
  erased_page shown(manage_detail::developer_page(box, facts));
  // AnyNode owns this node on the heap; moving the wrapper keeps its root
  // address stable for immediate preservation through manage_box::draft.
  manage_detail::manage_page_placed(*box, *shown.shown().get<developer_page_t>());
  return shown;
}
}
