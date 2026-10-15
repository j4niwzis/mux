// SPDX-License-Identifier: AGPL-3.0-only
// The packs room page, instantiated independently of the settings dialog.
module mux.ui.proto.matrix;

import std;
import skiff.scene;
import skiff.compose;
import skiff.model;
import mux.ui.base;

namespace mux::proto::matrix::manage {
using packs_page_t = decltype(manage_detail::packs_page(std::declval<manage_detail::manage_box*>(), std::declval<const ui::room_settings_facts&>()));
// Check the actual external action and Local-handler return types against
// the page boundary's declared requests; OutOf<Local> alone is empty.
using packs_answers = skiff::model::Types<
    typename decltype(std::declval<manage_detail::pack_events>().on(manage_detail::create_pack{}, std::declval<const manage_detail::pack_draft&>()).fEvent)::value_type,
    ui::sends<ui::request::open_room_packs>::Answer>;
static_assert(std::same_as<skiff::compose::Minus<packs_answers, manage_out>, skiff::model::Types<>>);
erased_page manage_page_erased(packs, manage_detail::manage_box* box, const ui::room_settings_facts& facts) {
  erased_page shown(manage_detail::packs_page(box, facts));
  // AnyNode owns this node on the heap; moving the wrapper keeps its root
  // address stable for immediate preservation through manage_box::draft.
  manage_detail::manage_page_placed(*box, *shown.shown().get<packs_page_t>());
  return shown;
}
}
