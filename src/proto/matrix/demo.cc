// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix.demo -- Matrix's part of `mux --demo`: its account, its rooms.
export module mux.proto.matrix.demo;

import std;
import mux.core;
import mux.config;

export namespace mux::proto::matrix {

inline std::optional<config::account_t> demo_account(const state&) {
  return config::account_from("@alice:matrix.example", "demo");
}
template <class Maker>
void demo(const state&, Maker& make) {
  const account_id me{id<state>{}, "@alice:matrix.example"};
  make.online(me);
  make.chat(me, "!tea:matrix.example", "Mad Tea Party", conversation_kind::group{}, "No room! No room!", true, 12,
            {{"@dormouse:matrix.example", "Twinkle, twinkle, little bat\u2026"},
             {"@march.hare:matrix.example", "Have some wine."},
             {"@alice:matrix.example", "I don't see any wine.", true},
             {"@march.hare:matrix.example", "There isn't any."}});
  make.chat(me, "!cheshire:matrix.example", "Cheshire Cat", conversation_kind::direct{}, std::nullopt, true, 0,
            {{"@cheshire:matrix.example", "We're all mad here."},
             {"@alice:matrix.example", "How do you know I'm mad?", true},
             {"@cheshire:matrix.example", "You must be, or you wouldn't have come here."}});
  make.members(me, "!tea:matrix.example",
               {{"@march.hare:matrix.example", "March Hare", "owner"},
                {"@hatter:matrix.example", "The Hatter", "admin"},
                {"@dormouse:matrix.example", "Dormouse", std::nullopt},
                {"@alice:matrix.example", "Alice", std::nullopt}});
  make.is(me, "@cheshire:matrix.example", availability::extended_away{});
  make.is(me, "@march.hare:matrix.example", availability::online{});
  make.is(me, "@hatter:matrix.example", availability::online{});
}

}  // namespace mux::proto::matrix
