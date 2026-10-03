// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.kept -- What every protocol keeps of its own in the accounts
// file: the modules mux.config reads them through. A protocol is added here
// beside its tag (mux.proto.tags).
export module mux.proto.kept;

export import mux.proto.tags;
export import mux.proto.xmpp.kept;
export import mux.proto.matrix.kept;
