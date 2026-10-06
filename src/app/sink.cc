// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.network:sink -- What the accounts say, put in the window's mailbox.
export module mux.app.network:sink;

import std;
import mux.core;
import mux.platform.events;

export namespace mux::app {

// The window's side of the mailbox: wake it, with the kind of event main
// registered for it.
struct wake_window {
  std::uint32_t kind = 0;
  void operator()() const { mux::platform::events::push(kind); }
};
using mailbox_type = mux::mailbox<wake_window>;

// What an account says, put in the mailbox -- while the account is still one
// the program has. An account taken away keeps running a little while its
// fibers wind down; nothing it says after that reaches the window.
// An account let go: by destroy_account, declared with the accounts
// (:accounts) and defined in each account's own unit -- the one place its
// destructor is made. clang 23 crashed (EmitObjectDelete: no destructor
// found) making it in a unit that imports the account's extern template
// through this module.
struct account_deleter {
  template <class Account>
  void operator()(Account* one) const {
    destroy_account(one);
  }
};
struct post_change {
  using account_deleter = mux::app::account_deleter;
  mailbox_type* box = nullptr;
  std::shared_ptr<std::atomic<bool>> live;
  void operator()(mux::change_t one) const {
    if (live->load())
      box->push(std::move(one));
  }
};

}  // namespace mux::app
