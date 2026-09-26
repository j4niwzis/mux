// SPDX-License-Identifier: AGPL-3.0-only
// mux: the program. The network half runs on a thread of its own -- every
// account a fiber on one loop -- and the window on the main thread; what the
// accounts say crosses to the window in a mailbox, and what the window asks
// for (a message to send) is posted back to the loop.
//
//   mux <account>...      user@domain for XMPP, @user:server for Matrix
//
// The password is read from MUX_PASSWORD for now; saved accounts and a
// login screen come after.
import std;
import mux.core;
import mux.net;
import mux.xmpp;
import mux.matrix;
import mux.host;
import mux.ui;

namespace {

// The window's side of the mailbox: wake it.
struct wake_window {
  void operator()() const { mux::host::wake(); }
};
using mailbox_type = mux::mailbox<wake_window>;

// What an account says, put in the mailbox.
struct post_change {
  mailbox_type* box = nullptr;
  void operator()(mux::change_t one) const { box->push(std::move(one)); }
};

using xmpp_account = mux::xmpp::account<post_change>;
using matrix_account = mux::matrix::account<post_change>;
using any_account = std::variant<std::unique_ptr<xmpp_account>, std::unique_ptr<matrix_account>>;

struct network {
  mux::net::loop loop;
  mux::net::asio::ssl::context tls = mux::net::client_tls();
  std::vector<any_account> accounts;
  std::thread thread;

  void send(const mux::conversation_id& to, std::string text) {
    loop.post([this, to, text = std::move(text)] {
      for (auto& one : accounts)
        std::visit(
            [&](auto& account) {
              if (account->id() == to.account)
                account->send(to.id, text);
            },
            one);
    });
  }
};

// What the window asks of the program.
struct app;
struct sender {
  network* net = nullptr;
  void operator()(const mux::conversation_id& to, std::string text) const { net->send(to, std::move(text)); }
};

struct app {
  mailbox_type* box = nullptr;
  mux::model* model = nullptr;
  mux::ui::screen<sender>* screen = nullptr;
  network* net = nullptr;

  skiff::scene::Drawable& root() { return screen->root(); }
  void woken() {
    auto changes = box->take();
    if (changes.empty())
      return;
    for (const auto& one : changes)
      model->apply(one);
    screen->show(*model);
  }
  void closing() {
    net->loop.post([n = net] {
      for (auto& one : n->accounts)
        std::visit([](auto& account) { account->stop(); }, one);
      n->loop.stop();
    });
  }
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::println(stderr, "usage: {} <user@domain | @user:server>...; the password in MUX_PASSWORD", argv[0]);
    return 2;
  }
  const char* password = std::getenv("MUX_PASSWORD");
  if (!password) {
    std::println(stderr, "MUX_PASSWORD is not set");
    return 2;
  }

  mailbox_type box{wake_window{}};
  mux::model model;
  network net;
  for (int at = 1; at < argc; ++at) {
    const std::string address = argv[at];
    if (address.starts_with('@'))
      net.accounts.emplace_back(std::make_unique<matrix_account>(
          net.loop, net.tls, mux::matrix::settings{.user_id = address, .password = password}, post_change{&box}));
    else
      net.accounts.emplace_back(std::make_unique<xmpp_account>(
          net.loop, net.tls, mux::xmpp::settings{.address = address, .password = password}, post_change{&box}));
  }
  for (auto& one : net.accounts)
    std::visit([](auto& account) { account->start(); }, one);
  net.thread = std::thread([&net] {
    try {
      net.loop.run_forever();
    } catch (const std::exception& failed) {
      std::println(stderr, "[mux] the network stopped: {}", failed.what());
    }
  });

  mux::ui::screen<sender> screen(sender{&net});
  screen.show(model);
  app program{&box, &model, &screen, &net};
  const int code = mux::host::run(program, {});
  net.thread.join();
  return code;
}
