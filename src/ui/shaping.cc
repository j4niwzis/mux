// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:shaping -- The plugins' rules (contract::shapes) gathered, in plugin
// order, and applied where a piece is put into its parent: a::part(piece,
// arguments...) makes the piece and hands it, with the same arguments, to
// every rule on it in turn. Here, above every plugin's UI_TOP layer: what can
// call it sees every plugin -- a module under them that tried would be an
// import cycle -- so a piece is shaped the same wherever it is put.
export module mux.ui:shaping;

import std;
import mux.composition;
import mux.plugins.types;
import mux.plugins.ui.top;

export namespace mux::ui::a {

// Every enabled plugin's rules, in plugin order: made once, a constant.
template <class... Features>
constexpr auto rules_of(plugin_list<Features...>) {
  return std::tuple_cat(shaping::as_tuple(shape_rules(contract::shapes{}, type_tag<Features>{}))...);
}
inline constexpr auto all_rules = rules_of(composition::providers<contract::shapes, enabled_plugins>{});

// A rule on this piece: its verb applied to what the ones before made; one
// on another piece: what was made, as it was.
template <class Piece, class F, class Node, class... Args>
auto apply_rule(const shaping::rule<Piece, shaping::around_t<F>>& one, Node node, const Args&... args) {
  static_assert(std::invocable<const F&, Node, const Args&...>,
                "a plugin's around(...) on this piece does not take the piece's node and the piece's own arguments");
  return one.verb.f(std::move(node), args...);
}
template <class Piece, class Other, class Verb, class Node, class... Args>
Node apply_rule(const shaping::rule<Other, Verb>&, Node node, const Args&...) {
  return node;
}
template <class Piece, std::size_t I = 0, class Node, class... Args>
auto shaped_from(Node node, const Args&... args) {
  if constexpr (I == std::tuple_size_v<std::remove_cvref_t<decltype(all_rules)>>)
    return node;
  else
    return shaped_from<Piece, I + 1>(apply_rule<Piece>(std::get<I>(all_rules), std::move(node), args...), args...);
}

// A piece, put where it stands: made from its arguments, then every rule on
// it in turn, each given what the one before made and the same arguments.
template <class Piece, class... Args>
auto part(const Piece& piece, const Args&... args) {
  return shaped_from<Piece>(piece(args...), args...);
}

}  // namespace mux::ui::a
