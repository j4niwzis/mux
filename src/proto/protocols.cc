// SPDX-License-Identifier: AGPL-3.0-only
// mux.protocols -- Every protocol's overloads, and the extension points that
// find them: what the rest of mux imports to ask a protocol anything. A
// protocol is added to the list in mux.proto.tags, and its module here.
export module mux.protocols;

import std;
import splice;
import mux.core;
export import mux.proto.tags;
export import mux.proto.kept;
export import mux.proto;
export import mux.proto.xmpp;
export import mux.proto.matrix;
export import mux.proto.matrix.requests;

export namespace mux::proto {

// The protocol an address is of: the first in the list that owns it -- the
// list's first where none does.
template <class... Tags>
[[nodiscard]] protocol_t protocol_of(protocol_list<Tags...>, std::string_view address) {
  std::optional<protocol_t> found;
  (void)((owns_address(state_of<Tags>{}, address) ? (found = protocol_t{Tags{}}, true) : false) || ...);
  return found.value_or(protocol_t{});
}
[[nodiscard]] inline protocol_t protocol_of(std::string_view address) { return protocol_of(protocols{}, address); }

// One of every protocol's own requests: what a banner's button asks.
template <class... Lists>
struct request_union;
template <class... Rs>
struct request_union<request_list<Rs...>> {
  using type = splice::variant<part::no_request, Rs...>;
};
template <class... As, class... Bs, class... Rest>
struct request_union<request_list<As...>, request_list<Bs...>, Rest...> : request_union<request_list<As..., Bs...>, Rest...> {};
template <class>
struct protocols_requests;
template <class... Tags>
struct protocols_requests<protocol_list<Tags...>> {
  using type = typename request_union<request_list<>, decltype(protocol_requests_of(state_of<Tags>{}))...>::type;
};
using any_request_t = typename protocols_requests<protocols>::type;
using any_banner = part::banner_of<any_request_t>;
// A protocol's request, as one of every protocol's: itself, or the one its
// own variant holds.
template <class R>
[[nodiscard]] std::optional<any_request_t> as_any(const std::optional<R>& one) {
  return one ? std::optional<any_request_t>(any_request_t{*one}) : std::nullopt;
}
template <class... Rs>
[[nodiscard]] std::optional<any_request_t> as_any(const std::optional<splice::variant<Rs...>>& one) {
  if (!one)
    return std::nullopt;
  return splice::visit([](const auto& each) { return std::optional<any_request_t>(any_request_t{each}); }, *one);
}

// No banners of its own, by default.
namespace banner_defaults {
inline std::vector<part::banner> composer_banners(const auto&, const conversation&, const auto&) { return {}; }
}  // namespace banner_defaults
// Over the composer, as its protocol says: its banners, each its button's
// request made one of every protocol's.
inline constexpr struct composer_banners_t {
  template <class State, class Model>
  std::vector<any_banner> operator()(const State& state, const conversation& chat, const Model& known) const {
    return splice::visit([&](const auto& now) {
      using banner_defaults::composer_banners;
      return composer_banners(now, chat, known) | std::views::transform([](auto one) {
               return any_banner{std::move(one.text), one.tone, std::move(one.button), as_any(one.asks)};
             }) |
             std::ranges::to<std::vector>();
    }, state);
  }
} composer_banners{};

}  // namespace mux::proto
