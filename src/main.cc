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
// With no accounts at all it opens all the same, and says how to add one.
import std;
import mux.core;
import mux.config;
import mux.net;
import mux.xmpp;
import mux.matrix;
import mux.host;
import mux.ui;
import skiff.paint;
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
struct back {};
struct open_accounts {};
struct open_new_account {};
struct add_xmpp {};
struct add_matrix {};
struct select_account {
  std::string address;
};
struct toggle_advanced {};
struct toggle_plain {};
struct submit_login {};
struct flip_enabled {
  std::string address;
};
struct remove_account {
  std::string address;
};
}  // namespace request

using request_t =
    std::variant<request::choose, request::back, request::open_accounts, request::open_new_account,
                 request::add_xmpp, request::add_matrix, request::select_account, request::toggle_advanced,
                 request::toggle_plain, request::submit_login, request::flip_enabled, request::remove_account>;

// What the screens ask: each a request, kept until the program applies it
// between events -- except a message, which goes to the network at once.
struct actions {
  network* net = nullptr;
  std::vector<request_t> requests;

  void choose(const mux::conversation_id& which) { requests.emplace_back(request::choose{which}); }
  void send(const mux::conversation_id& to, std::string text) { net->send(to, std::move(text)); }
  void back() { requests.emplace_back(request::back{}); }
  void open_accounts() { requests.emplace_back(request::open_accounts{}); }
  void open_new_account() { requests.emplace_back(request::open_new_account{}); }
  void add_xmpp() { requests.emplace_back(request::add_xmpp{}); }
  void add_matrix() { requests.emplace_back(request::add_matrix{}); }
  void select_account(std::string address) { requests.emplace_back(request::select_account{std::move(address)}); }
  void toggle_advanced() { requests.emplace_back(request::toggle_advanced{}); }
  void toggle_plain() { requests.emplace_back(request::toggle_plain{}); }
  void submit_login() { requests.emplace_back(request::submit_login{}); }
  void flip_enabled(std::string address) { requests.emplace_back(request::flip_enabled{std::move(address)}); }
  void remove_account(std::string address) { requests.emplace_back(request::remove_account{std::move(address)}); }
};

using window_type = mux::ui::window<actions>;

// How much moves, as the accounts file says: "none", "reduced" or "full"
// (and full where it says nothing, or what it says is none of these).
skiff::paint::Motion motion_of(const std::optional<std::string>& said) {
  if (!said || *said == "full")
    return skiff::paint::motion::full{};
  if (*said == "reduced")
    return skiff::paint::motion::reduced{};
  if (*said == "none")
    return skiff::paint::motion::none{};
  std::println(std::cerr, "[mux] motion is \"none\", \"reduced\" or \"full\", not \"{}\": full it is", *said);
  return skiff::paint::motion::full{};
}

// What the program does to the window between events.
struct app {
  using adding = mux::ui::add_account_panel<actions>;
  using accounts = mux::ui::accounts_panel<actions>;
  using xmpp_form = mux::ui::xmpp_form<actions>;
  using matrix_form = mux::ui::matrix_form<actions>;

  mailbox_type* box = nullptr;
  mux::model* model = nullptr;
  network* net = nullptr;
  std::filesystem::path config_path;
  std::vector<mux::config::account_t> saved;
  // How much moves, as read, to be written back as it was.
  std::optional<std::string> motion;
  // Why the accounts file could not be read, when it could not: then it is
  // not written over either.
  std::optional<std::string> config_error;
  // The account being added that a login is waiting to hear about.
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
    root().drop_closed();
    auto pending = std::exchange(ask.requests, {});
    for (const request_t& one : pending)
      std::visit([this](const auto& each) { this->apply(each); }, one);
  }

  void closing() { net->shutdown(); }

  // -- the window
  window_type& root() { return scene.root(); }

  void show_conversations() {
    pending_login.reset();
    root().close();
    this->refresh();
  }
  accounts& show_accounts() {
    pending_login.reset();
    auto& panel = root().open<accounts>();
    if (config_error)
      panel.say(*config_error);
    this->refresh();
    return panel;
  }
  // The accounts, with this one's settings up beside them.
  accounts& show_account(const std::string& address) {
    auto& panel = this->show_accounts();
    if (const auto found = this->find(address); found != saved.end()) {
      panel.select(*found, *model);
      panel.show(saved, *model);
    }
    return panel;
  }
  void show_adding() {
    pending_login.reset();
    root().open<adding>();
    this->refresh();
  }

  std::vector<mux::config::account_t>::iterator find(std::string_view address) {
    return std::ranges::find(saved, address, [](const auto& one) -> std::string_view {
      return mux::config::address_of(one);
    });
  }

  // The panel that is up, if one is, and the XMPP form in it, if there is one.
  [[nodiscard]] mux::ui::xmpp_form<actions>* xmpp_form_up() {
    auto* up = root().open_panel();
    if (!up)
      return nullptr;
    return std::visit([](auto& panel) { return panel.xmpp(); }, *up);
  }

  // Everything brought up to date with the model: each panel by its own
  // overload.
  void refresh() {
    root().bar.show(*model);
    root().main().show(*model);
    if (auto* up = root().open_panel())
      std::visit([this](auto& panel) { this->bring_up_to_date(panel); }, *up);
  }
  void bring_up_to_date(accounts& panel) { panel.show(saved, *model); }
  void bring_up_to_date(adding& panel) {
    std::visit([this](auto& form) { this->watch_login(form); }, panel.form);
  }

  // A new account waiting to log in: online is done, failed is said.
  template <class Form>
  void watch_login(Form& form) {
    if (!pending_login)
      return;
    const auto found = model->accounts().find(mux::account_id{mux::ui::protocol_of(*pending_login), *pending_login});
    if (found == model->accounts().end())
      return;
    std::visit(mux::overloaded{[&](const mux::connection::online&) { this->show_conversations(); },
                               [&](const mux::connection::failed& why) {
                                 form.say(why.error.empty() ? "The server said no." : why.error, true);
                                 pending_login.reset();
                               },
                               [&](const auto&) { form.say("Connecting…", false); }},
               found->second.state);
  }

  void apply(const request::choose& one) {
    root().main().chosen = one.which;
    root().main().show(*model);
  }
  void apply(const request::back&) { this->show_conversations(); }
  void apply(const request::open_accounts&) { (void)this->show_accounts(); }
  void apply(const request::open_new_account&) { this->show_adding(); }
  void apply(const request::add_xmpp&) { this->switch_form(&adding::show_xmpp); }
  void apply(const request::add_matrix&) { this->switch_form(&adding::show_matrix); }
  void apply(const request::select_account& one) {
    auto* up = root().open_panel();
    if (!up)
      return;
    std::visit(mux::overloaded{[&](accounts& panel) {
                                 if (const auto found = this->find(one.address); found != saved.end()) {
                                   panel.select(*found, *model);
                                   panel.show(saved, *model);
                                 }
                               },
                               [](adding&) {}},
               *up);
  }
  void apply(const request::toggle_advanced&) {
    if (auto* form = this->xmpp_form_up())
      form->show_advanced(!form->advanced);
  }
  void apply(const request::toggle_plain&) {
    if (auto* form = this->xmpp_form_up())
      form->flip_plain();
  }
  void apply(const request::submit_login&) {
    auto* up = root().open_panel();
    if (!up)
      return;
    std::visit(mux::overloaded{[this](adding& panel) {
                                 std::visit([this](auto& form) { this->add(form); }, panel.form);
                               },
                               [this](accounts& panel) {
                                 if (auto* editor = panel.editor())
                                   std::visit([this](auto& form) { this->edit(form); }, editor->form);
                               }},
               *up);
  }
  void apply(const request::flip_enabled& one) { this->flip_enabled(one.address); }
  void apply(const request::remove_account& one) { this->remove(one.address); }

  void switch_form(void (adding::*to)()) {
    auto* up = root().open_panel();
    if (!up)
      return;
    pending_login.reset();
    std::visit(mux::overloaded{[to](adding& panel) { (panel.*to)(); }, [](accounts&) {}}, *up);
  }

  // A new account: saved, and started; the panel waits to hear how it went.
  template <class Form>
  void add(Form& form) {
    auto typed = form.account();
    if (!typed) {
      form.say(typed.error(), true);
      return;
    }
    const mux::config::account_t account{std::move(*typed)};
    const std::string address = mux::config::address_of(account);
    if (this->find(address) != saved.end()) {
      form.say("That account is already here.", true);
      return;
    }
    saved.push_back(account);
    if (auto failed = this->write()) {
      form.say(*failed, true);
      return;
    }
    net->add(account);
    pending_login = address;
    form.say("Connecting…", false);
  }

  // An account's settings changed: the old one stops, and the new one, on
  // or off as the old one was, takes its place in the list.
  template <class Form>
  void edit(Form& form) {
    auto typed = form.account();
    if (!typed) {
      form.say(typed.error(), true);
      return;
    }
    mux::config::account_t account{std::move(*typed)};
    const std::string address = mux::config::address_of(account);
    const std::string was = form.editing.value_or(address);
    const auto old = this->find(was);
    if (old == saved.end())
      return;
    if (address != was && this->find(address) != saved.end()) {
      form.say("That account is already here.", true);
      return;
    }
    mux::config::enabled_of(account) = mux::config::enabled_of(*old);
    *old = account;
    const auto failed = this->write();
    net->remove(was);
    if (mux::config::enabled_of(account))
      net->add(account);
    // The form is rebuilt from what was saved: `form` is gone after this.
    auto& panel = this->show_account(address);
    if (auto* editor = panel.editor())
      editor->say(failed ? *failed : std::string("Saved."), failed.has_value());
  }

  void flip_enabled(const std::string& address) {
    const auto found = this->find(address);
    if (found == saved.end())
      return;
    bool& enabled = mux::config::enabled_of(*found);
    enabled = !enabled;
    if (enabled)
      net->add(*found);
    else
      net->remove(address);
    this->save_from_accounts();
    this->refresh();
  }

  void remove(const std::string& address) {
    if (std::erase_if(saved, [&](const auto& one) { return mux::config::address_of(one) == address; }) == 0)
      return;
    net->remove(address);
    this->save_from_accounts();
    this->refresh();
  }

  // Written, unless the file there could not be read: that one is the
  // user's to look at, not to lose.
  [[nodiscard]] std::optional<std::string> write() {
    if (config_error)
      return "Not saved: " + *config_error;
    auto file = mux::config::file_of(saved);
    file.motion = motion;
    if (auto done = mux::config::save(config_path, file); !done)
      return "Not saved: " + done.error();
    return std::nullopt;
  }
  void save_from_accounts() {
    if (auto failed = this->write())
      if (auto* up = root().open_panel())
        std::visit(mux::overloaded{[&](accounts& panel) { panel.say(*failed); }, [](adding&) {}}, *up);
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
  program.motion = saved.motion;
  skiff::paint::motionLevel() = motion_of(saved.motion);
  program.config_error = std::move(config_error);
  program.refresh();

  const int code = mux::host::run(program, {});
  net.thread.join();
  return code;
}
