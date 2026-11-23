// SPDX-License-Identifier: AGPL-3.0-only
// The irc account as the program runs it, instantiated here alone (see
// network.cc). Part of mux.app.network, where the account is declared extern.
module mux.app.network;
import std;
import mux.core;
import mux.proto.irc.client;
// What the plugins hear of its lines, found where it is made.
import mux.plugins.irc;

template class mux::proto::irc::client::account<mux::app::post_change>;
#if defined(MUX_SPLIT_ACCOUNTS)
// Let go here, where it is made: see app/sink.cc.
void mux::app::destroy_account(mux::proto::irc::client::account_of<mux::app::post_change>* one) { delete one; }
#endif
