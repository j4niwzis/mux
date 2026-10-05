// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix.client: a Matrix account, run by fibers on mux.net's loop -- loom's
// typed requests over mux.http, /sync long-polled into loom::client::state,
// and what each sync brought said as mux.core's changes.
export module mux.proto.matrix.client;

export import :base;
export import :oauth;
export import :names;
export import :account;
export import :events;
export import :media;
export import :requests;
export import :requests_more;
export import :requests_send;
export import :sync;
export import :sync_crypto;
export import :sync_keys;
