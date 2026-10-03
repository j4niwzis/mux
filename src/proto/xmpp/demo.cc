// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.xmpp.demo -- XMPP's part of `mux --demo`: its account, its chats.
export module mux.proto.xmpp.demo;

import std;
import mux.core;
import mux.config;

export namespace mux::proto::xmpp {

inline std::optional<config::account_t> demo_account(const state&) {
  return config::account_from("alice@wonderland.example", "demo");
}
template <class Maker>
void demo(const state&, Maker& make) {
  const account_id me{id<state>{}, "alice@wonderland.example"};
  make.online(me);
  make.chat(me, "hatter@wonderland.example", "The Hatter", conversation_kind::direct{}, std::nullopt, true, 2,
            {{"hatter@wonderland.example", "Why is a raven like a writing-desk?"},
             {"alice@wonderland.example", "I give up. What's the answer?", true},
             {"hatter@wonderland.example", "I haven't the slightest idea."},
             {"hatter@wonderland.example", "Tea at six, as always. Don't be late."}});
  make.chat(me, "rabbit@wonderland.example", "White Rabbit", conversation_kind::direct{}, std::nullopt, false, 0,
            {{"rabbit@wonderland.example", "Oh dear! Oh dear! I shall be too late!"},
             {"alice@wonderland.example", "Late for what?", true},
             {"rabbit@wonderland.example", "The Duchess! She'll have my head."}});
  make.chat(me, "croquet@rooms.wonderland.example", "Croquet club", conversation_kind::group{},
            "Flamingos provided. Hedgehogs bring their own.", false, 5,
            {{"queen@wonderland.example", "Who has been painting my roses red?"},
             {"two@wonderland.example", "Not us, Your Majesty."},
             {"queen@wonderland.example", "Off with their heads!"},
             {"king@wonderland.example", "My dear, let us have the trial first."}});
  make.members(me, "croquet@rooms.wonderland.example",
               {{"queen@wonderland.example", "The Queen of Hearts", "owner"},
                {"king@wonderland.example", "The King of Hearts", "admin"},
                {"alice@wonderland.example", "Alice", std::nullopt},
                {"two@wonderland.example", "Two of Spades", std::nullopt},
                {"rabbit@wonderland.example", "White Rabbit", std::nullopt}});
  make.is(me, "hatter@wonderland.example", availability::online{});
  make.is(me, "rabbit@wonderland.example", availability::away{});
  make.is(me, "queen@wonderland.example", availability::do_not_disturb{});
  make.is(me, "king@wonderland.example", availability::online{});
}

}  // namespace mux::proto::xmpp
