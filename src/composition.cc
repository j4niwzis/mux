// SPDX-License-Identifier: AGPL-3.0-only
// mux.composition -- How a plugin extends what something else owns.
//
// A plugin is a folder, src/plugins/<name>/ (cmake/plugins.cmake), known by
// its feature type, mux::plugins::<name>::feature, which says its name; the
// ones turned on are mux::enabled_plugins (mux.plugins.types, written by
// CMake). A contract is a type owned by what is extended, which says its name
// too. A plugin that extends one says so --
//   provides(contract, type_tag<feature>) -> true
// -- and gives what the contract asks, overloads on the same two found by ADL:
// what a contract's owner asks of each of providers<Contract, List>, where the
// owner is instantiated. What a plugin does not say it provides it does not;
// what it says it provides must be there, or the build stops on broken(),
// which names both.
export module mux.composition;

import std;
export import mux.proto.identity;

export namespace mux {

// Plugins, by their feature types.
template <class... Features>
struct plugin_list {};

// The contracts the client's own parts own, named low enough that a
// plugin's types can say they provide them; each what it asks is said where
// it is used.
namespace contract {
// The media panel's pages beside its emoji and stickers (mux.ui:emoji_popup):
//   page_type(c, f)  -> type_tag<its page>: in popup_page_t, what is shown
//   shown_type(c, f) -> type_tag<what is shown on it>: in emoji_facts
//   tab_name(page), shown_where(page, facts), opened(page): of its page
//   extend(c, f, needs, shown) -> its node, in the window
struct media_pages {
  static constexpr std::string_view name = "the media panel's pages";
};
// The program's parts (mux.app.program): extend(c, f, services&) -> a part,
// offered the requests as the program's own are.
struct program_parts {
  static constexpr std::string_view name = "the program's parts";
};
}  // namespace contract

}  // namespace mux

export namespace mux::composition {

namespace defaults {
consteval bool provides(const auto&, const auto&) { return false; }
}  // namespace defaults
template <class Contract, class Feature>
consteval bool provides() {
  using defaults::provides;
  return provides(Contract{}, type_tag<Feature>{});
}

// Lists joined, in order.
template <class... Lists>
struct joined {
  using type = plugin_list<>;
};
template <class... A>
struct joined<plugin_list<A...>> {
  using type = plugin_list<A...>;
};
template <class... A, class... B, class... Rest>
struct joined<plugin_list<A...>, plugin_list<B...>, Rest...> : joined<plugin_list<A..., B...>, Rest...> {};

// The plugins of a list that say they provide a contract, in the list's order.
template <class Contract, class List>
struct providers_of;
template <class Contract, class... Features>
struct providers_of<Contract, plugin_list<Features...>> {
  using type = typename joined<std::conditional_t<provides<Contract, Features>(), plugin_list<Features>, plugin_list<>>...>::type;
};
template <class Contract, class List>
using providers = typename providers_of<Contract, List>::type;

// What the build says where a plugin says it provides a contract and what it
// gives does not compile: a static_assert's message, made while compiling.
struct message {
  std::array<char, 256> chars{};
  std::size_t length = 0;
  [[nodiscard]] constexpr std::size_t size() const { return length; }
  [[nodiscard]] constexpr const char* data() const { return chars.data(); }
};
template <class Contract, class Feature>
consteval message broken() {
  const std::array<std::string_view, 5> said{"the ", Feature::name, " plugin says it provides ", Contract::name,
                                             ", but what it gives for it does not compile"};
  message out;
  const auto end = std::ranges::copy(said | std::views::join | std::views::take(out.chars.size()), out.chars.begin()).out;
  out.length = static_cast<std::size_t>(end - out.chars.begin());
  return out;
}

}  // namespace mux::composition
