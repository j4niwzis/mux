// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:account_forms -- Each protocol's account form, as the accounts'
// panels show them: what its account_form_of makes of its draft, from the
// protocols' UI modules, which sit under this one.
module;
#include "skiff/named.h"
export module mux.ui:account_forms;

import std;
import splice;
import skia;
import skiff.scene;
import skiff.compose;
import mux.core;
import mux.config;
import mux.ui.base;
import mux.ui.proto;

export namespace mux::ui {
// Each protocol's account form, made over a draft of its own -- its
// account_form_of(draft, colours, editing), found by ADL on the draft's type
// -- and any of them, as the panels show them, made from the list of
// protocols.
template <class Tag>
using form_view_of_t = decltype(account_form_of(std::declval<const draft_of_t<Tag>&>(), std::declval<const palette&>(), false));
template <class>
struct form_views;
template <class... Tags>
struct form_views<protocol_list<Tags...>> {
  using type = spl::variant<form_view_of_t<Tags>...>;
};
using account_form_node = typename form_views<protocols>::type;

// A protocol's draft, blank: a new account's form.
[[nodiscard]] inline account_draft_t blank_draft(const protocol_t& speaks) {
  return spl::visit([](auto tag) { return account_draft_t(std::in_place_type<draft_of_t<decltype(tag)>>); }, speaks);
}
// An account's draft, as it is kept.
[[nodiscard]] inline account_draft_t draft_of_account(const config::account_t& saved) {
  return spl::visit([](const auto& kept) { return account_draft_t(draft_of(kept)); }, saved.own);
}
// What its protocol says of adding an account of it.
[[nodiscard]] inline std::string adding_note(const protocol_t& speaks) {
  return spl::visit([](const auto& draft) { return std::string(form_note(draft)); }, blank_draft(speaks));
}
// A form's facts as it opens: a new account's, or one kept, with what its
// protocol keeps beside the draft, fresh -- made again from the round on.
template <class List>
struct first_form_part {
  using type = proto::no_form_part;
};
template <class First, class... Rest>
struct first_form_part<proto::form_part_list<First, Rest...>> {
  using type = First;
};
template <class Tag>
using first_form_part_t = typename first_form_part<decltype(proto::form_parts_of(state_of<Tag>{}))>::type;
[[nodiscard]] inline account_form_facts form_opened(account_draft_t seed, const protocol_t& speaks, std::optional<std::string> editing,
                                                   std::uint32_t round) {
  account_form_facts facts{.seed = std::move(seed), .editing = std::move(editing), .round = round};
  spl::visit([&](auto tag) { facts.part = form_part_t(std::in_place_type<first_form_part_t<decltype(tag)>>); }, speaks);
  return facts;
}

// The form a form's facts make: made again where its round moves -- opened,
// saved, another account's -- and as it was, typing and all, otherwise.
struct form_seed {
  std::uint32_t round = 0;
  account_draft_t seed;
  bool editing = false;
  friend bool operator==(const form_seed& a, const form_seed& b) { return a.round == b.round; }
};
[[nodiscard]] inline account_form_node form_made(const palette& colours, const form_seed& now) {
  return spl::visit([&](const auto& draft) { return account_form_node(account_form_of(draft, colours, now.editing)); }, now.seed);
}
inline auto account_form_view(const ui_needs& n) {
  namespace c = skiff::compose;
  using facts = std::optional<account_form_facts>;
  return SKIFF_NAMED(account_form_view, c::projected<facts>([](const facts& now) { return now ? std::optional(form_seed{now->round, now->seed, now->editing.has_value()}) : std::nullopt; },
                             c::mount<account_form_node, form_seed>([at = n.colours](const form_seed& now) { return std::tuple{form_made(*at, now)}; },
                                                                    {.fillX = true, .autoSize = scene::axes::kY})));
}
using account_form_view_t = decltype(account_form_view(std::declval<const ui_needs&>()));

}  // namespace mux::ui
