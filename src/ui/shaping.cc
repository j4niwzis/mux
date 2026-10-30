// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui.shaping -- The program's rules: every enabled plugin's
// (contract::shapes), in plugin order (their ORDER), gathered once as a
// constant -- what every piece (skiff.compose's piece<Self>) is built under:
// folded into it while compiling in a release build, applied from skiff's
// table outside one (register_all, as the program starts). A module of its
// own, above every plugin's UI_TOP layer: what imports it sees every plugin
// -- a module under them that tried would be an import cycle -- and a piece
// is built only under these rules, so it is shaped the same wherever it is.
export module mux.ui.shaping;

import std;
import skiff.scene;
import skiff.compose;
import mux.core;
import mux.composition;
import mux.plugins.types;
import mux.plugins.ui.top;

export namespace mux::ui::a {

template <class... Features>
constexpr auto rules_of(plugin_list<Features...>) {
  return skiff::compose::rules_of(std::tuple_cat(skiff::compose::rule_tuple(shape_rules(contract::shapes{}, type_tag<Features>{}))...));
}
inline constexpr skiff::compose::program_rules program{rules_of(composition::providers<contract::shapes, enabled_plugins>{})};

// What a builder makes, built under the program's rules.
template <class B>
auto built(B builder) {
  return std::move(builder).build(program);
}
// What a contract gave, built where it is a builder (it holds pieces); as it
// is where it is a node.
template <class T>
T built_any(T node) {
  return node;
}
template <skiff::compose::a_builder B>
auto built_any(B builder) {
  return built(std::move(builder));
}
// As the program starts, outside a release build: the rules in skiff's table.
inline void register_all() {
  if constexpr (skiff::scene::kErasedCombinators)
    skiff::compose::register_rules(program.rules);
}

}  // namespace mux::ui::a
