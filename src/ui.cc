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

export import :base;
export import :icons;
export import :avatars;
export import :controls;
export import :themes;
export import :names;
export import :html;
export import :chat_list;
export import :message;
export import :header;
export import :info;
export import :room_settings;
export import :composer;
export import :timeline;
export import :conversations;
export import :forms;
export import :add_account;
export import :accounts;
export import :drawer;
export import :proxies;
export import :appearance;
export import :storage;
export import :settings;
export import :context_menu;
export import :sending;
export import :viewer;
export import :call_bar;
export import :window;
