// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.encryption.ui_top -- Its parts of a person's card
// (contract::person_card_parts), over mux.ui.cards: Verify with emoji,
// where their protocol verifies people and their identity is known and not
// verified; Withdraw verification, where it was reset (Element's); and
// their sessions, each verified or not.
module;
#include "../../inline_in_release.h"
#include "skiff/named.h"
export module mux.plugin.encryption.ui_top;

import std;
import splice;
import skiff.scene;
import skiff.compose;
import skiff.nodes.text;
import mux.composition;
import mux.core;
import mux.proto;
import mux.ui.base;
import mux.ui.cards;
import mux.plugin.encryption.types;

export namespace mux::plugins::encryption {
using namespace ::mux::ui;

struct person_verify_act {
  using Answer = std::tuple<verify_person, request::close_person_info>;
  conversation_id who;
  Answer operator()() const { return {verify_person{who}, request::close_person_info{}}; }
};
// Verified by comparing emoji with each of their devices that answers.
struct person_accept_act {
  using Answer = accept_identity;
  conversation_id who;
  Answer operator()() const { return accept_identity{who}; }
};
inline constexpr scene::Spec kTileSpec{.fillX = true, .margin = {8.0f, 22.0f, 0.0f, 22.0f}};
[[nodiscard]] inline bool unverified(const person_facts& facts) {
  return facts.trust && !spl::visit(spl::overloaded{[](trust::verified) { return true; }, [](const auto&) { return false; }}, *facts.trust);
}
// Their identity reset: taken as theirs now, unverified.
[[nodiscard]] inline bool reset(const person_facts& facts) {
  return facts.trust && spl::visit(spl::overloaded{[](trust::changed) { return true; }, [](const auto&) { return false; }}, *facts.trust);
}
// Verify, where their protocol verifies people -- known from its type, the
// tile not made for one that does not -- shown while they are not verified,
// or not any more; Withdraw verification, where their identity was reset.
template <class Protocol>
auto verify_tile(const palette&, const account_id&, const std::string&, const person_facts&) {
  return skiff::compose::column(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}));
}
template <class Protocol>
  requires(proto::offered_by<state_of<Protocol>, proto::feature::identity_verification>())
auto verify_tile(const palette& colours, const account_id& account, const std::string& key, const person_facts& facts) {
  namespace c = skiff::compose;
  return c::visible(unverified(facts), c::styled(kTileSpec, action_tile<person_verify_act>(
      colours, "Verify with emoji", icon::check{}, {conversation_id{account, key}})));
}
template <class Protocol>
auto extend(contract::person_card_parts, type_tag<feature>, const palette& colours, person_place::actions, type_tag<Protocol>,
            const account_id& account, const std::string& key, const person_facts& facts) {
  namespace c = skiff::compose;
  return c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      verify_tile<Protocol>(colours, account, key, facts),
      c::visible(reset(facts), c::styled(kTileSpec, action_tile<person_accept_act>(
          colours, "Withdraw verification", icon::close{}, {conversation_id{account, key}}))));
}
// Their sessions, each verified or not, where their keys were listed.
template <class Protocol>
auto extend(contract::person_card_parts, type_tag<feature>, const palette& colours, person_place::details, type_tag<Protocol>,
            const account_id&, const std::string&, const person_facts& facts) {
  namespace c = skiff::compose;
  auto sessions = facts.devices | std::views::transform([&](const change::device_view& one) {
                    return c::styled(
                        {.fillX = true, .margin = {2.0f, 22.0f, 0.0f, 22.0f}},
                        elided(nodes::Text(
                            std::format("{} {}{}", one.verified ? "✓" : "⚠",
                                        one.name.empty() ? one.id : one.name,
                                        one.verified ? std::string(" · Verified") : std::string(" · Not verified")),
                            13.0f, one.verified ? colours.text : colours.dim)));
                  }) | std::ranges::to<std::vector>();
  return c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::styled({.margin = {14.0f, 22.0f, 2.0f, 22.0f}},
          c::visible(!facts.devices.empty(), nodes::Text(
              facts.devices.empty() ? std::string() : std::format("SESSIONS ({})", facts.devices.size()),
              13.0f, colours.dim, true))),
      c::visible(!sessions.empty(), c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(sessions))));
}

}  // namespace mux::plugins::encryption
