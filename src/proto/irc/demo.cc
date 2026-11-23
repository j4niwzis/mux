// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.irc.demo -- IRC's part of `mux --demo`: its account, its chats.
export module mux.proto.irc.demo;

import std;
import mux.core;
import mux.config;

export namespace mux::proto::irc {

inline std::optional<config::account_t> demo_account(const state&) {
  return config::account_from("ircs://alice@irc.wonderland.example", "");
}
template <class Maker>
void demo(const state&, Maker& make) {
  const account_id me{id<state>{}, "ircs://alice@irc.wonderland.example"};
  make.online(me);
  make.state(me, protocol_state_t{state{.online = true, .history = true, .network = "Wonderland"}});
  make.chat(me, "#tea-party", "#tea-party", conversation_kind::group{}, "Six o'clock, always.", false, 3,
            {{"hatter", "Have some wine."},
             {"alice", "I don't see any wine.", true},
             {"march_hare", "There isn't any."},
             {"dormouse", "Twinkle, twinkle, little bat..."}},
            room_part_t{channel_part{.joined = true}});
  make.members(me, "#tea-party",
               {{"hatter", "hatter", "operator"},
                {"march_hare", "march_hare", "voice"},
                {"dormouse", "dormouse", std::nullopt},
                {"alice", "alice", std::nullopt}});
}

}  // namespace mux::proto::irc
