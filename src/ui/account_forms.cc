// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:account_forms -- Each protocol's account form, as the accounts'
// panels hold them: what its form_type gives, from the protocols' UI
// modules, which sit under this one.
export module mux.ui:account_forms;

import std;
import splice;
import skia;
import skiff.scene;
import mux.core;
import mux.config;
import mux.ui.base;
import mux.ui.proto;

export namespace mux::ui {
// Each protocol's account form -- its form_type(state), found by ADL where the
// panels are made: a template on Actions, made in the program, which imports
// mux.ui.proto -- and any of them, as the panels hold them, made from the
// list of protocols.
template <class Tag>
using form_of_t = typename decltype(form_type(state_of<Tag>{}))::type;
template <class>
struct form_list;
template <class... Tags>
struct form_list<protocol_list<Tags...>> {
  using type = spl::variant<form_of_t<Tags>...>;
};
using account_form = typename form_list<protocols>::type;

// The form of an account's own protocol, filled in from what it keeps.
[[nodiscard]] account_form form_of(const palette& colours, const config::account_t& saved) {
  return spl::visit([&](const auto& kept) {
    using form = typename decltype(form_type_for(kept))::type;
    return account_form(std::in_place_type<form>, colours, std::optional(kept));
  }, saved.own);
}
// A form laid out in the column under `top`.
void place_form(account_form& form, const skia::SkRect& column, float top) {
  spl::visit(
      [&](auto& one) {
        one.fState.arrange(0.0f, 0.0f);
        scene::layout(one, skia::SkRect::MakeLTRB(column.fLeft, column.fTop + top, column.fRight, column.fBottom));
      },
      form);
}

}  // namespace mux::ui
