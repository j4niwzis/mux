// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui.proto -- Every protocol's overloads for the client's UI: what the
// program imports where it makes the window, so that the window, a template
// on the program's Actions, finds each protocol's by ADL. A protocol with UI
// of its own is added here; one with none is not.
export module mux.ui.proto;

export import mux.ui.proto.matrix;
