// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui: the window's screens, as skiff nodes -- the conversations of every
// account down the side with the chosen one beside them, under a top bar
// that swaps in the panels for adding an account and for the accounts, all in
// the one window.
//
// The screens are views. What a control does is ask the program, through
// `Actions`, and the program changes the screens between events: a screen
// switched or a list rebuilt inside a click would destroy the control whose
// handler is still running.
export module mux.ui;

export import mux.ui.base;
export import mux.ui.messages;

export import mux.ui.lists;
export import :header;
export import :info;
export import :room_settings;
export import :conversations;
export import :add_account;
export import :account_forms;
export import :accounts;
export import :drawer;
export import :proxies;
export import :appearance;
export import :storage;
export import :settings;
export import :about;
export import :context_menu;
export import :sending;
export import :viewer;
export import :window;
