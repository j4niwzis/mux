// SPDX-License-Identifier: AGPL-3.0-only
// mux.matrix: a Matrix account, run by fibers on mux.net's loop -- loom's
// typed requests over mux.http, /sync long-polled into loom::client::state,
// and what each sync brought said as mux.core's changes.
export module mux.matrix;

export import :base;
export import :names;
export import :account;
export import :events;
export import :media;
export import :requests;
export import :sync;
