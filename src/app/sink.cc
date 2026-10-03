// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.network:sink -- What the accounts say, put in the window's mailbox.
export module mux.app.network:sink;

import std;
import mux.core;
import mux.platform.events;

export namespace mux::app {

// The window's side of the mailbox: wake it.
struct wake_window {
  void operator()() const { mux::platform::events::wake(); }
};
using mailbox_type = mux::mailbox<wake_window>;

// What an account says, put in the mailbox -- while the account is still one
// the program has. An account taken away keeps running a little while its
// fibers wind down; nothing it says after that reaches the window.
struct post_change {
  mailbox_type* box = nullptr;
  std::shared_ptr<std::atomic<bool>> live;
  void operator()(mux::change_t one) const {
    if (live->load())
      box->push(std::move(one));
  }
};

}  // namespace mux::app
