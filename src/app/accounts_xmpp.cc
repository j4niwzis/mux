// SPDX-License-Identifier: AGPL-3.0-only
// The xmpp account as the program runs it, instantiated here alone (see
// network.cc): its requests, its sync, its media, and all under them.
import std;
import mux.core;
import mux.xmpp;
import mux.app.network;

template class mux::xmpp::account<mux::app::post_change>;
