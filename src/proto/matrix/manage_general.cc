// SPDX-License-Identifier: AGPL-3.0-only
// The general room page, instantiated independently of the settings dialog.
module mux.ui.proto.matrix;

import std;
import skiff.scene;
import skiff.compose;
import skiff.model;
import mux.ui.base;

namespace mux::proto::matrix::manage {
using general_page_t = decltype(manage_detail::general_page(std::declval<manage_detail::manage_box*>(), std::declval<const ui::room_settings_facts&>()));
// Check the actual external action and Local-handler return types against
// the page boundary's declared requests; OutOf<Local> alone is empty.
using general_answers = skiff::model::Types<
    manage_detail::general_actions::choose_access::Answer::value_type,
    manage_detail::general_actions::choose_history::Answer::value_type,
    manage_detail::general_actions::encryption::Answer::value_type,
    manage_detail::general_actions::directory::Answer::value_type,
    manage_detail::general_actions::address_action::Answer::value_type,
    manage_detail::selected_addresses::Answer::value_type,
    manage_detail::choose_child::Answer::value_type,
    manage_detail::remove_child::Answer::value_type,
    typename decltype(std::declval<manage_detail::general_events>().on(manage_detail::save_profile{}, std::declval<const manage_detail::general_draft&>()).fEvent)::value_type,
    typename decltype(std::declval<manage_detail::general_events>().on(manage_detail::create_address{}, std::declval<const manage_detail::general_draft&>()).fEvent)::value_type,
    typename decltype(std::declval<manage_detail::general_events>().on(manage_detail::upgrade_room{}, std::declval<const manage_detail::general_draft&>()).fEvent)::value_type,
    ui::sends<ui::request::open_url>::Answer,
    ui::sends<ui::request::pick_room_avatar>::Answer>;
static_assert(std::same_as<skiff::compose::Minus<general_answers, manage_out>, skiff::model::Types<>>);
erased_page manage_page_erased(room, manage_detail::manage_box* box, const ui::room_settings_facts& facts) {
  erased_page shown(manage_detail::general_page(box, facts));
  // AnyNode owns this node on the heap; moving the wrapper keeps its root
  // address stable for immediate preservation through manage_box::draft.
  manage_detail::manage_page_placed(*box, *shown.shown().get<general_page_t>());
  return shown;
}
}
