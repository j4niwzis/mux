// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.clients -- Every protocol's make_account: what the program
// imports to start an account of any protocol. A protocol is added here
// beside its tag.
export module mux.proto.clients;

export import mux.proto.xmpp.client;
export import mux.proto.matrix.client;
