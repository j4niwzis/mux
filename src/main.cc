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
// With no accounts at all it opens on the login screen.
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

  void start(const mux::config::saved_account& saved) {
    auto live = std::make_shared<std::atomic<bool>>(true);
    running_account entry{saved.address, {}, live};
    if (mux::config::is_matrix(saved.address)) {
      mux::matrix::settings how{.user_id = saved.address, .password = saved.password};
      how.homeserver = saved.homeserver;
      entry.account = std::make_unique<matrix_account>(loop, tls, std::move(how), post_change{box, live});
    } else {
      mux::xmpp::settings how{.address = saved.address, .password = saved.password};
      how.host = saved.host;
      if (saved.port)
        how.port = static_cast<std::uint16_t>(*saved.port);
      entry.account = std::make_unique<xmpp_account>(loop, tls, std::move(how), post_change{box, live});
    }
    std::visit([](auto& account) { account->start(); }, entry.account);
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
  void add(mux::config::saved_account saved) {
    loop.post([this, saved = std::move(saved)] { this->start(saved); });
  }
  void remove(std::string address) {
    loop.post([this, address = std::move(address)] {
      this->stop(address);
      box->push(mux::change_t{mux::change::account_removed{
          {mux::config::is_matrix(address) ? mux::protocol::matrix : mux::protocol::xmpp, address}}});
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
struct open_login {
  std::optional<std::string> editing;
};
struct toggle_advanced {};
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

using request_t = std::variant<request::choose, request::open_accounts, request::open_login,
                               request::toggle_advanced, request::submit_login, request::cancel_login,
                               request::set_enabled, request::remove_account, request::back>;

// What the window asks of the program, and what the program does to the
// window between events.
struct app {
  using window_type = mux::ui::window<app>;
  using conversations = mux::ui::conversations_screen<app>;
  using login = mux::ui::login_screen<app>;
  using accounts = mux::ui::accounts_screen<app>;

  mailbox_type* box = nullptr;
  mux::model* model = nullptr;
  network* net = nullptr;
  std::filesystem::path config_path;
  mux::config::file saved;
  // Why the accounts file could not be read, when it could not: then it is
  // not written over either.
  std::optional<std::string> config_error;
  // The account a login is waiting to hear about.
  std::optional<std::string> pending_login;
  // Made once app is complete: the screens name its member functions.
  std::unique_ptr<skiff::scene::Scene<window_type>> scene;

  std::vector<request_t> requests;

  void choose(const mux::conversation_id& which) { requests.emplace_back(request::choose{which}); }
  void send(const mux::conversation_id& to, std::string text) { net->send(to, std::move(text)); }
  void open_accounts() { requests.emplace_back(request::open_accounts{}); }
  void open_login(std::optional<std::string> editing) {
    requests.emplace_back(request::open_login{std::move(editing)});
  }
  void toggle_advanced() { requests.emplace_back(request::toggle_advanced{}); }
  void submit_login() { requests.emplace_back(request::submit_login{}); }
  void cancel_login() { requests.emplace_back(request::cancel_login{}); }
  void set_enabled(std::string address, bool enabled) {
    requests.emplace_back(request::set_enabled{std::move(address), enabled});
  }
  void remove_account(std::string address) { requests.emplace_back(request::remove_account{std::move(address)}); }
  void back() { requests.emplace_back(request::back{}); }

  // -- what the host asks
  skiff::scene::Scene<window_type>& window() { return *scene; }

  void woken() {
    auto changes = box->take();
    if (changes.empty())
      return;
    for (const auto& one : changes)
      model->apply(one);
    this->refresh();
  }

  void before_frame() {
    auto pending = std::exchange(requests, {});
    for (const request_t& one : pending)
      std::visit([this](const auto& each) { this->apply(each); }, one);
  }

  void closing() { net->shutdown(); }

  // -- the screens
  auto& body() { return scene->root().body; }

  void show_conversations() {
    body().emplace<conversations>(this);
    this->refresh();
  }
  void show_accounts() {
    body().emplace<accounts>(this);
    if (config_error)
      std::get<accounts>(body()).say(*config_error);
    this->refresh();
  }
  void show_login(const std::optional<std::string>& editing) {
    std::optional<mux::config::saved_account> from;
    if (editing) {
      const auto found = std::ranges::find(saved.accounts, *editing, &mux::config::saved_account::address);
      if (found != saved.accounts.end())
        from = *found;
    }
    pending_login.reset();
    body().emplace<login>(this, from, !saved.accounts.empty());
  }

  // The current screen, brought up to date with the model.
  void refresh() {
    std::visit(
        [&](auto& screen) {
          using screen_type = std::remove_cvref_t<decltype(screen)>;
          if constexpr (std::same_as<screen_type, conversations>)
            screen.show(*model);
          else if constexpr (std::same_as<screen_type, accounts>)
            screen.show(saved.accounts, *model);
          else
            this->watch_login(screen);
        },
        body());
  }

  // A login waiting for its account: online is done, failed is said.
  void watch_login(login& screen) {
    if (!pending_login)
      return;
    const mux::account_id id{mux::config::is_matrix(*pending_login) ? mux::protocol::matrix : mux::protocol::xmpp,
                             *pending_login};
    const auto found = model->accounts().find(id);
    if (found == model->accounts().end())
      return;
    switch (found->second.state) {
      case mux::connection::online:
        pending_login.reset();
        this->show_conversations();
        break;
      case mux::connection::failed:
        screen.say(found->second.error.value_or("The server said no."), true);
        pending_login.reset();
        break;
      default:
        screen.say("Connecting…", false);
        break;
    }
  }

  void apply(const request::choose& one) {
    if (auto* screen = std::get_if<conversations>(&body())) {
      screen->chosen = one.which;
      screen->show(*model);
    }
  }
  void apply(const request::open_accounts&) { this->show_accounts(); }
  void apply(const request::open_login& one) { this->show_login(one.editing); }
  void apply(const request::toggle_advanced&) {
    if (auto* screen = std::get_if<login>(&body()))
      screen->show_advanced(!screen->advanced);
  }
  void apply(const request::submit_login&) {
    if (auto* screen = std::get_if<login>(&body()))
      this->log_in(*screen);
  }
  void apply(const request::cancel_login&) {
    if (!saved.accounts.empty())  // otherwise there is nowhere to go back to
      this->show_accounts();
  }
  void apply(const request::set_enabled& one) { this->set_enabled(one.address, one.enabled); }
  void apply(const request::remove_account& one) { this->remove(one.address); }
  void apply(const request::back&) { this->show_conversations(); }

  void log_in(login& screen) {
    auto typed = screen.account();
    if (!typed) {
      screen.say(typed.error(), true);
      return;
    }
    const auto clash = std::ranges::find(saved.accounts, typed->address, &mux::config::saved_account::address);
    if (clash != saved.accounts.end() && (!screen.editing || *screen.editing != typed->address)) {
      screen.say("That account is already here.", true);
      return;
    }
    // An edit replaces the account it was of: the old one stops first.
    if (screen.editing) {
      std::erase_if(saved.accounts, [&](const auto& one) { return one.address == *screen.editing; });
      net->remove(*screen.editing);
    }
    saved.accounts.push_back(*typed);
    if (!this->save(screen))
      return;
    net->add(*typed);
    pending_login = typed->address;
    screen.say("Connecting…", false);
  }

  void set_enabled(const std::string& address, bool enabled) {
    const auto found = std::ranges::find(saved.accounts, address, &mux::config::saved_account::address);
    if (found == saved.accounts.end() || found->enabled == enabled)
      return;
    found->enabled = enabled;
    if (enabled)
      net->add(*found);
    else
      net->remove(address);
    (void)this->save_from_accounts();
    this->refresh();
  }

  void remove(const std::string& address) {
    if (std::erase_if(saved.accounts, [&](const auto& one) { return one.address == address; }) == 0)
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
    if (auto done = mux::config::save(config_path, saved); !done)
      return "Not saved: " + done.error();
    return std::nullopt;
  }
  bool save(login& screen) {
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
  std::vector<mux::config::saved_account> extra;
  if (argc > 1) {
    const char* password = std::getenv("MUX_PASSWORD");
    if (!password) {
      std::println(std::cerr, "accounts on the command line take their password from MUX_PASSWORD, which is not set");
      return 2;
    }
    for (int at = 1; at < argc; ++at)
      extra.push_back({.address = argv[at], .password = password});
  }

  for (const auto& one : saved.accounts)
    if (one.enabled)
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
  program.config_path = config_path;
  program.saved = std::move(saved);
  program.config_error = std::move(config_error);
  program.scene = std::make_unique<skiff::scene::Scene<app::window_type>>(std::in_place, &program);
  if (program.saved.accounts.empty() && extra.empty())
    program.show_login(std::nullopt);
  else
    program.show_conversations();

  const int code = mux::host::run(program, {});
  net.thread.join();
  return code;
}
