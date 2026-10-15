// SPDX-License-Identifier: AGPL-3.0-only
// The permissions room page, instantiated independently of the settings dialog.
module mux.ui.proto.matrix;

import std;
import skiff.scene;
import skiff.compose;
import skiff.model;
import mux.ui.base;

namespace mux::proto::matrix::manage {
using permissions_page_t = decltype(manage_detail::permissions_page(std::declval<manage_detail::manage_box*>(), std::declval<const ui::room_settings_facts&>()));
// Check the actual external action and Local-handler return types against
// the page boundary's declared requests; OutOf<Local> alone is empty.
using permissions_answers = skiff::model::Types<
    manage_detail::permission_actions::apply::Answer::value_type,
    manage_detail::apply_tags::Answer::value_type,
    typename decltype(std::declval<manage_detail::power_events>().on(manage_detail::save_power{}, std::declval<const manage_detail::power_draft&>()).fEvent)::value_type,
    typename decltype(std::declval<manage_detail::power_events>().on(manage_detail::delete_power{}, std::declval<const manage_detail::power_draft&>()).fEvent)::value_type>;
static_assert(std::same_as<skiff::compose::Minus<permissions_answers, manage_out>, skiff::model::Types<>>);
erased_page manage_page_erased(roles, manage_detail::manage_box* box, const ui::room_settings_facts& facts) {
  erased_page shown(manage_detail::permissions_page(box, facts));
  // AnyNode owns this node on the heap; moving the wrapper keeps its root
  // address stable for immediate preservation through manage_box::draft.
  manage_detail::manage_page_placed(*box, *shown.shown().get<permissions_page_t>());
  return shown;
}
}
