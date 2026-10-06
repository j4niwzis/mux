// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.network:accounts -- Each protocol's account, made for the program's
// sink: the one place the program names a protocol's client, a line each, as
// the other registries do (tags.cc, clients.cc, ui.cc, app.cc).
//
// The accounts themselves -- their requests, sync, media, and the HTTP and
// TLS under them -- are instantiated in units of their own (the protocols'
// app/accounts*.cc), in parallel, outside a release build; a release build
// makes them here, at namespace scope, before network.cc runs one: clang 23
// crashed (TemplateArgument::isPackExpansion, under libc++'s
// basic_format_string::__handles_) on every format string first made deep
// in the chain where network.cc first starts one -- and on none made first
// in a plain context.
export module mux.app.network:accounts;

import std;
import mux.proto.xmpp.client;
import mux.proto.matrix.client;
import :sink;

#if defined(MUX_SPLIT_ACCOUNTS)
extern template class mux::proto::xmpp::client::account<mux::app::post_change>;
extern template class mux::proto::matrix::client::account<mux::app::post_change>;
#else
template class mux::proto::xmpp::client::account<mux::app::post_change>;
template class mux::proto::matrix::client::account<mux::app::post_change>;
#endif

// Each account let go: in its own unit where accounts are split (xmpp's
// app/accounts.cc, matrix's app/accounts_sync.cc), else here.
export namespace mux::app {
void destroy_account(mux::proto::xmpp::client::account<post_change>* one);
void destroy_account(mux::proto::matrix::client::account<post_change>* one);
}  // namespace mux::app
#if !defined(MUX_SPLIT_ACCOUNTS)
void mux::app::destroy_account(mux::proto::xmpp::client::account<post_change>* one) { delete one; }
void mux::app::destroy_account(mux::proto::matrix::client::account<post_change>* one) { delete one; }
#endif
