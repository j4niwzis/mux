// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui.shaping -- The plugins' rules (contract::shapes) gathered, in plugin
// order, and applied where a piece is put into its parent: a::part(piece,
// arguments...) makes the piece and hands it, with the same arguments, to
// every rule on it in turn. A module of its own, above every plugin's UI_TOP
// layer: what imports it sees every plugin -- a module under them that tried
// would be an import cycle -- so a piece is shaped the same wherever it is
// put; and the modules that put pieces together above the plugins -- each
// small, of its own, not mux.ui grown -- import it.
export module mux.ui.shaping;

import std;
import skiff.scene;
import mux.core;
import mux.composition;
import mux.plugins.types;
import mux.plugins.ui.top;

export namespace mux::ui::a {

// A rule, with the plugin it is of: what never applied is said by it.
template <class Feature, class Rule>
struct owned {
  Rule rule;
};
template <class Feature, class... Rules>
constexpr std::tuple<owned<Feature, Rules>...> owned_by(std::tuple<Rules...> all) {
  return std::apply([](Rules... each) { return std::tuple<owned<Feature, Rules>...>{owned<Feature, Rules>{each}...}; }, all);
}
// Every enabled plugin's rules, in plugin order (their ORDER): made once, a
// constant.
template <class... Features>
constexpr auto rules_of(plugin_list<Features...>) {
  return std::tuple_cat(owned_by<Features>(shaping::as_tuple(shape_rules(contract::shapes{}, type_tag<Features>{})))...);
}
inline constexpr auto all_rules = rules_of(composition::providers<contract::shapes, enabled_plugins>{});
inline constexpr std::size_t kRules = std::tuple_size_v<std::remove_cvref_t<decltype(all_rules)>>;

// How often each rule applied, outside a release build: one that never did
// -- its piece never put through a::part above the plugins -- is said as
// the program stops (report_unused), not left to do nothing unseen.
inline std::array<std::atomic<std::size_t>, kRules>& applications() {
  static std::array<std::atomic<std::size_t>, kRules> counted{};
  return counted;
}

// A rule on this piece: its verb applied to what the ones before made; one
// on another piece: what was made, as it was.
template <std::size_t I, class Piece, class Feature, class F, class Node, class... Args>
auto apply_rule(const owned<Feature, shaping::rule<Piece, shaping::around_t<F>>>& one, Node node, const Args&... args) {
  static_assert(std::invocable<const F&, Node, const Args&...>,
                "a plugin's around(...) on this piece does not take the piece's node and the piece's own arguments");
  if constexpr (skiff::scene::kErasedCombinators)
    applications()[I].fetch_add(1, std::memory_order_relaxed);
  return one.rule.verb.f(std::move(node), args...);
}
template <std::size_t I, class Piece, class Feature, class Other, class Verb, class Node, class... Args>
Node apply_rule(const owned<Feature, shaping::rule<Other, Verb>>&, Node node, const Args&...) {
  return node;
}
template <class Piece, std::size_t I = 0, class Node, class... Args>
auto shaped_from(Node node, const Args&... args) {
  if constexpr (I == kRules)
    return node;
  else
    return shaped_from<Piece, I + 1>(apply_rule<I, Piece>(std::get<I>(all_rules), std::move(node), args...), args...);
}

// What never applied, outside a release build: said, by its plugin and its
// place among the rules.
template <class Feature, class Rule>
Feature feature_of(const owned<Feature, Rule>&);
template <std::size_t... I>
void report_each(std::index_sequence<I...>) {
  const auto said = [](std::string_view plugin, std::size_t at) {
    log_line("shaping", std::format("{}: rule {} never applied -- its piece was not put through a::part above the plugins", plugin, at));
  };
  ((applications()[I].load() == 0 ? said(decltype(feature_of(std::get<I>(all_rules)))::name, I) : void()), ...);
}
inline void report_unused() {
  if constexpr (skiff::scene::kErasedCombinators)
    report_each(std::make_index_sequence<kRules>{});
}

// A piece, put where it stands: made from its arguments, then every rule on
// it in turn, each given what the one before made and the same arguments.
template <class Piece, class... Args>
auto part(const Piece& piece, const Args&... args) {
  return shaped_from<Piece>(piece(args...), args...);
}

}  // namespace mux::ui::a
