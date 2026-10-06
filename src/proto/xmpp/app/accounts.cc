// SPDX-License-Identifier: AGPL-3.0-only
// The xmpp account as the program runs it, instantiated here alone (see
// network.cc): its requests, its sync, its media, and all under them.
// Part of mux.app.network, where the account is declared extern.
module mux.app.network;
import std;
import mux.core;
import mux.proto.xmpp.client;

template class mux::proto::xmpp::client::account<mux::app::post_change>;
#if defined(MUX_SPLIT_ACCOUNTS)
// Let go here, where it is made: see app/sink.cc.
void mux::app::destroy_account(mux::proto::xmpp::client::account<mux::app::post_change>* one) { delete one; }
#endif
