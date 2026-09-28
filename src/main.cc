// SPDX-License-Identifier: AGPL-3.0-only
// mux: the program. The network half runs on a thread of its own -- every
// account a fiber on one loop -- and the window on the main thread; what the
// accounts say crosses to the window in a mailbox, and what the window asks
// for is posted back to the loop.
//
//   mux                   the accounts saved in ~/.config/mux/accounts.json
//   mux <account>...      and these too, for this run, with the password in
//                         MUX_PASSWORD: user@domain for XMPP, @user:server for
//                         Matrix
//
// With no accounts at all it opens on the choice of protocol for a new one.
import std;
import mux.core;
import mux.config;
import mux.net;
import mux.xmpp;
import mux.matrix;
import mux.host;
import mux.ui;
import skiff.scene;

namespace {

// The window's side of the mailbox: wake it.
struct wake_window {
  void operator()() const { mux::host::wake(); }
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

using xmpp_account = mux::xmpp::account<post_change>;
using matrix_account = mux::matrix::account<post_change>;
using any_account = std::variant<std::unique_ptr<xmpp_account>, std::unique_ptr<matrix_account>>;

struct running_account {
  std::string address;
  any_account account;
  std::shared_ptr<std::atomic<bool>> live;
};

// The accounts, on their loop. Everything here runs on the loop's thread;
// the window reaches it through post().
struct network {
  mux::net::loop loop;
  mux::net::tls tls = mux::net::client_tls();
  mailbox_type* box = nullptr;
  std::vector<running_account> accounts;
  // Accounts taken away, kept until the program ends: their fibers may still
  // be finishing, and they must not be destroyed under them.
  std::vector<running_account> retired;
  std::thread thread;

  void start(const mux::config::account_t& saved) {
    std::visit([this](const auto& each) { this->start_one(each); }, saved);
  }
  void start_one(const mux::config::xmpp_account& saved) {
    auto live = std::make_shared<std::atomic<bool>>(true);
    mux::xmpp::settings how{.address = saved.address,
                            .password = saved.password,
                            .resource = saved.resource,
                            .host = saved.host,
                            .plain_without_tls = saved.plain_without_tls};
    if (saved.port)
      how.port = static_cast<std::uint16_t>(*saved.port);
    this->run(saved.address, std::make_unique<xmpp_account>(loop, tls, std::move(how), post_change{box, live}), live);
  }
  void start_one(const mux::config::matrix_account& saved) {
    auto live = std::make_shared<std::atomic<bool>>(true);
    mux::matrix::settings how{.user_id = saved.user_id,
                              .password = saved.password,
                              .homeserver = saved.homeserver,
                              .device_name = saved.device_name};
    this->run(saved.user_id, std::make_unique<matrix_account>(loop, tls, std::move(how), post_change{box, live}),
              live);
  }
  void run(const std::string& address, any_account account, std::shared_ptr<std::atomic<bool>> live) {
    running_account entry{address, std::move(account), std::move(live)};
    std::visit([](auto& one) { one->start(); }, entry.account);
    accounts.push_back(std::move(entry));
  }

  void stop(const std::string& address) {
    const auto found = std::ranges::find(accounts, address, &running_account::address);
    if (found == accounts.end())
      return;
    found->live->store(false);
    std::visit([](auto& account) { account->stop(); }, found->account);
    retired.push_back(std::move(*found));
    accounts.erase(found);
  }

  // From the window's thread.
  void add(mux::config::account_t saved) {
    loop.post([this, saved = std::move(saved)] { this->start(saved); });
  }
  void remove(std::string address) {
    loop.post([this, address = std::move(address)] {
      this->stop(address);
      box->push(mux::change_t{mux::change::account_removed{{mux::ui::protocol_of(address), address}}});
    });
  }
  void send(const mux::conversation_id& to, std::string text) {
    loop.post([this, to, text = std::move(text)] {
      for (auto& one : accounts)
        std::visit(
            [&](auto& account) {
              if (account->id() == to.account)
                account->send(to.id, text);
            },
            one.account);
    });
  }
  void shutdown() {
    loop.post([this] {
      for (auto& one : accounts) {
        one.live->store(false);
        std::visit([](auto& account) { account->stop(); }, one.account);
      }
      loop.stop();
    });
  }
};

// What the window asks: requests, applied between events.
namespace request {
struct choose {
  mux::conversation_id which;
};
struct open_accounts {};
struct open_new_account {};
struct add_xmpp {};
struct add_matrix {};
struct edit_account {
  std::string address;
};
struct toggle_advanced {};
struct toggle_plain {};
struct submit_login {};
struct cancel_login {};
struct set_enabled {
  std::string address;
  bool enabled = true;
};
struct remove_account {
  std::string address;
};
struct back {};
}  // namespace request

using request_t =
    std::variant<request::choose, request::open_accounts, request::open_new_account, request::add_xmpp,
                 request::add_matrix, request::edit_account, request::toggle_advanced, request::toggle_plain,
                 request::submit_login, request::cancel_login, request::set_enabled, request::remove_account,
                 request::back>;

// What the screens ask: each a request, kept until the program applies it
// between events -- except a message, which goes to the network at once.
struct actions {
  network* net = nullptr;
  std::vector<request_t> requests;

  void choose(const mux::conversation_id& which) { requests.emplace_back(request::choose{which}); }
  void send(const mux::conversation_id& to, std::string text) { net->send(to, std::move(text)); }
  void open_accounts() { requests.emplace_back(request::open_accounts{}); }
  void open_new_account() { requests.emplace_back(request::open_new_account{}); }
  void add_xmpp() { requests.emplace_back(request::add_xmpp{}); }
  void add_matrix() { requests.emplace_back(request::add_matrix{}); }
  void edit_account(std::string address) { requests.emplace_back(request::edit_account{std::move(address)}); }
  void toggle_advanced() { requests.emplace_back(request::toggle_advanced{}); }
  void toggle_plain() { requests.emplace_back(request::toggle_plain{}); }
  void submit_login() { requests.emplace_back(request::submit_login{}); }
  void cancel_login() { requests.emplace_back(request::cancel_login{}); }
  void set_enabled(std::string address, bool enabled) {
    requests.emplace_back(request::set_enabled{std::move(address), enabled});
  }
  void remove_account(std::string address) { requests.emplace_back(request::remove_account{std::move(address)}); }
  void back() { requests.emplace_back(request::back{}); }
};

using window_type = mux::ui::window<actions>;

// What the program does to the window between events.
struct app {
  using conversations = mux::ui::conversations_screen<actions>;
  using choice = mux::ui::protocol_choice<actions>;
  using xmpp_login = mux::ui::xmpp_login<actions>;
  using matrix_login = mux::ui::matrix_login<actions>;
  using accounts = mux::ui::accounts_screen<actions>;

  mailbox_type* box = nullptr;
  mux::model* model = nullptr;
  network* net = nullptr;
  std::filesystem::path config_path;
  std::vector<mux::config::account_t> saved;
  // Why the accounts file could not be read, when it could not: then it is
  // not written over either.
  std::optional<std::string> config_error;
  // The account a login is waiting to hear about.
  std::optional<std::string> pending_login;
  actions ask;
  skiff::scene::Scene<window_type> scene{std::in_place, &ask};

  // -- what the host asks
  skiff::scene::Scene<window_type>& window() { return scene; }

  void woken() {
    auto changes = box->take();
    if (changes.empty())
      return;
    for (const auto& one : changes)
      model->apply(one);
    this->refresh();
  }

  void before_frame() {
    auto pending = std::exchange(ask.requests, {});
    for (const request_t& one : pending)
      std::visit([this](const auto& each) { this->apply(each); }, one);
  }

  void closing() { net->shutdown(); }

  // -- the screens
  auto& body() { return scene.root().body; }

  void show_conversations() {
    body().emplace<conversations>(&ask);
    this->refresh();
  }
  void show_accounts() {
    body().emplace<accounts>(&ask);
    if (config_error)
      std::get<accounts>(body()).say(*config_error);
    this->refresh();
  }
  void show_choice() {
    pending_login.reset();
    body().emplace<choice>(&ask, !saved.empty());
  }
  void show_login(mux::protocol::xmpp) {
    pending_login.reset();
    body().emplace<xmpp_login>(&ask, std::nullopt, true);
  }
  void show_login(mux::protocol::matrix) {
    pending_login.reset();
    body().emplace<matrix_login>(&ask, std::nullopt, true);
  }
  // An edit opens the screen of the account's own protocol.
  void show_edit(const std::string& address) {
    const auto found = this->find(address);
    if (found == saved.end())
      return;
    pending_login.reset();
    std::visit(mux::overloaded{[this](const mux::config::xmpp_account& one) {
                                 body().emplace<xmpp_login>(&ask, one, true);
                               },
                               [this](const mux::config::matrix_account& one) {
                                 body().emplace<matrix_login>(&ask, one, true);
                               }},
               *found);
  }

  std::vector<mux::config::account_t>::iterator find(std::string_view address) {
    return std::ranges::find(saved, address, [](const auto& one) -> std::string_view {
      return mux::config::address_of(one);
    });
  }

  // The current screen, brought up to date with the model: each by its own
  // overload.
  void refresh() {
    std::visit([this](auto& screen) { this->bring_up_to_date(screen); }, body());
  }
  void bring_up_to_date(conversations& screen) { screen.show(*model); }
  void bring_up_to_date(accounts& screen) { screen.show(saved, *model); }
  void bring_up_to_date(choice&) {}
  void bring_up_to_date(xmpp_login& screen) { this->watch_login(screen); }
  void bring_up_to_date(matrix_login& screen) { this->watch_login(screen); }

  // A login waiting for its account: online is done, failed is said.
  template <class Login>
  void watch_login(Login& screen) {
    if (!pending_login)
      return;
    const auto found = model->accounts().find(mux::account_id{mux::ui::protocol_of(*pending_login), *pending_login});
    if (found == model->accounts().end())
      return;
    std::visit(mux::overloaded{[&](const mux::connection::online&) {
                                 pending_login.reset();
                                 this->show_conversations();
                               },
                               [&](const mux::connection::failed& why) {
                                 screen.say(why.error.empty() ? "The server said no." : why.error, true);
                                 pending_login.reset();
                               },
                               [&](const auto&) { screen.say("Connecting…", false); }},
               found->second.state);
  }

  void apply(const request::choose& one) {
    if (auto* screen = std::get_if<conversations>(&body())) {
      screen->chosen = one.which;
      screen->show(*model);
    }
  }
  void apply(const request::open_accounts&) { this->show_accounts(); }
  void apply(const request::open_new_account&) { this->show_choice(); }
  void apply(const request::add_xmpp&) { this->show_login(mux::protocol::xmpp{}); }
  void apply(const request::add_matrix&) { this->show_login(mux::protocol::matrix{}); }
  void apply(const request::edit_account& one) { this->show_edit(one.address); }
  void apply(const request::toggle_advanced&) {
    std::visit(mux::overloaded{[](xmpp_login& screen) { screen.show_advanced(!screen.advanced); }, [](auto&) {}},
               body());
  }
  void apply(const request::toggle_plain&) {
    std::visit(mux::overloaded{[](xmpp_login& screen) { screen.flip_plain(); }, [](auto&) {}}, body());
  }
  void apply(const request::submit_login&) {
    std::visit(mux::overloaded{[this](xmpp_login& screen) { this->log_in(screen); },
                               [this](matrix_login& screen) { this->log_in(screen); }, [](auto&) {}},
               body());
  }
  // Back from a login: to the accounts after an edit, to the choice of
  // protocol when adding.
  void apply(const request::cancel_login&) {
    std::visit(mux::overloaded{[this](xmpp_login& screen) { this->leave(screen); },
                               [this](matrix_login& screen) { this->leave(screen); }, [](auto&) {}},
               body());
  }
  void apply(const request::set_enabled& one) { this->set_enabled(one.address, one.enabled); }
  void apply(const request::remove_account& one) { this->remove(one.address); }
  void apply(const request::back&) { this->show_conversations(); }

  template <class Login>
  void leave(const Login& screen) {
    if (screen.editing)
      this->show_accounts();
    else
      this->show_choice();
  }

  template <class Login>
  void log_in(Login& screen) {
    auto typed = screen.account();
    if (!typed) {
      screen.say(typed.error(), true);
      return;
    }
    mux::config::account_t account{std::move(*typed)};
    const std::string address = mux::config::address_of(account);
    if (this->find(address) != saved.end() && (!screen.editing || *screen.editing != address)) {
      screen.say("That account is already here.", true);
      return;
    }
    // An edit replaces the account it was of: the old one stops first, and
    // the new one is on or off as the old one was.
    if (screen.editing) {
      if (const auto old = this->find(*screen.editing); old != saved.end())
        mux::config::enabled_of(account) = mux::config::enabled_of(*old);
      std::erase_if(saved, [&](const auto& one) { return mux::config::address_of(one) == *screen.editing; });
      net->remove(*screen.editing);
    }
    saved.push_back(account);
    if (!this->save(screen))
      return;
    if (!mux::config::enabled_of(account)) {
      this->show_accounts();
      return;
    }
    net->add(account);
    pending_login = address;
    screen.say("Connecting…", false);
  }

  void set_enabled(const std::string& address, bool enabled) {
    const auto found = this->find(address);
    if (found == saved.end() || mux::config::enabled_of(*found) == enabled)
      return;
    mux::config::enabled_of(*found) = enabled;
    if (enabled)
      net->add(*found);
    else
      net->remove(address);
    (void)this->save_from_accounts();
    this->refresh();
  }

  void remove(const std::string& address) {
    if (std::erase_if(saved, [&](const auto& one) { return mux::config::address_of(one) == address; }) == 0)
      return;
    net->remove(address);
    (void)this->save_from_accounts();
    this->refresh();
  }

  // Written, unless the file there could not be read: that one is the
  // user's to look at, not to lose.
  [[nodiscard]] std::optional<std::string> write() {
    if (config_error)
      return "Not saved: " + *config_error;
    if (auto done = mux::config::save(config_path, mux::config::file_of(saved)); !done)
      return "Not saved: " + done.error();
    return std::nullopt;
  }
  template <class Login>
  bool save(Login& screen) {
    if (auto failed = this->write()) {
      screen.say(*failed, true);
      return false;
    }
    return true;
  }
  bool save_from_accounts() {
    if (auto failed = this->write()) {
      if (auto* screen = std::get_if<accounts>(&body()))
        screen->say(*failed);
      return false;
    }
    return true;
  }
};

}  // namespace

int main(int argc, char** argv) {
  mailbox_type box{wake_window{}};
  mux::model model;
  network net;
  net.box = &box;

  const std::filesystem::path config_path = mux::config::default_path();
  mux::config::file saved;
  std::optional<std::string> config_error;
  if (auto loaded = mux::config::load(config_path))
    saved = std::move(*loaded);
  else {
    config_error = loaded.error();
    std::println(std::cerr, "[mux] {}", loaded.error());
  }

  // Accounts named on the command line, for this run only.
  std::vector<mux::config::account_t> extra;
  if (argc > 1) {
    const char* password = std::getenv("MUX_PASSWORD");
    if (!password) {
      std::println(std::cerr, "accounts on the command line take their password from MUX_PASSWORD, which is not set");
      return 2;
    }
    for (int at = 1; at < argc; ++at)
      extra.push_back(mux::config::account_from(argv[at], password));
  }

  for (const auto& one : mux::config::accounts_of(saved))
    if (mux::config::enabled_of(one))
      net.start(one);
  for (const auto& one : extra)
    net.start(one);
  net.thread = std::thread([&net] {
    try {
      net.loop.run_forever();
    } catch (const std::exception& failed) {
      std::println(std::cerr, "[mux] the network stopped: {}", failed.what());
    }
  });

  app program;
  program.box = &box;
  program.model = &model;
  program.net = &net;
  program.ask.net = &net;
  program.config_path = config_path;
  program.saved = mux::config::accounts_of(saved);
  program.config_error = std::move(config_error);
  if (program.saved.empty() && extra.empty())
    program.show_choice();
  else
    program.show_conversations();

  const int code = mux::host::run(program, {});
  net.thread.join();
  return code;
}
