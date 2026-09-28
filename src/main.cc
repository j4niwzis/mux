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
//   mux --demo            fake accounts and conversations: no network, and
//                         nothing kept
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

  // An account started, through the profile of `proxies` it names.
  void start(const mux::config::account_t& saved, const std::vector<mux::config::proxy_settings>& proxies) {
    const auto* via = mux::config::find_proxy(proxies, mux::config::proxy_of(saved));
    std::visit([this, via](const auto& each) { this->start_one(each, proxy_of(via)); }, saved);
  }
  // The proxy a profile names, as mux.net takes it.
  static std::optional<mux::net::proxy> proxy_of(const mux::config::proxy_settings* kept) {
    if (!kept)
      return std::nullopt;
    return mux::net::proxy{.kind = kept->kind,
                           .host = kept->host,
                           .port = static_cast<std::uint16_t>(kept->port),
                           .username = kept->username,
                           .password = kept->password};
  }

  void start_one(const mux::config::xmpp_account& saved, std::optional<mux::net::proxy> via) {
    auto live = std::make_shared<std::atomic<bool>>(true);
    mux::xmpp::settings how{.address = saved.address,
                            .password = saved.password,
                            .resource = saved.resource,
                            .host = saved.host,
                            .plain_without_tls = saved.plain_without_tls,
                            .proxy = std::move(via)};
    if (saved.port)
      how.port = static_cast<std::uint16_t>(*saved.port);
    this->run(saved.address, std::make_unique<xmpp_account>(loop, tls, std::move(how), post_change{box, live}), live);
  }
  void start_one(const mux::config::matrix_account& saved, std::optional<mux::net::proxy> via) {
    auto live = std::make_shared<std::atomic<bool>>(true);
    mux::matrix::settings how{.user_id = saved.user_id,
                              .password = saved.password,
                              .homeserver = saved.homeserver,
                              .device_name = saved.device_name,
                              .proxy = std::move(via)};
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
  void add(mux::config::account_t saved, std::vector<mux::config::proxy_settings> proxies) {
    loop.post([this, saved = std::move(saved), proxies = std::move(proxies)] { this->start(saved, proxies); });
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
  // For the account a conversation is of: read up to `event`, or left.
  void mark_read(const mux::conversation_id& in, std::string event) {
    loop.post([this, in, event = std::move(event)] {
      for (auto& one : accounts)
        std::visit(
            [&](auto& account) {
              if (account->id() == in.account)
                account->mark_read(in.id, event);
            },
            one.account);
    });
  }
  void leave(const mux::conversation_id& in) {
    loop.post([this, in] {
      for (auto& one : accounts)
        std::visit(
            [&](auto& account) {
              if (account->id() == in.account)
                account->leave(in.id);
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

// ---- the demo ----------------------------------------------------------------

// Fake accounts and conversations, for `mux --demo`: a window to look at and
// click through with no network and no accounts file.
namespace fake {

using namespace std::chrono_literals;

[[nodiscard]] inline std::vector<mux::config::account_t> accounts() {
  return {mux::config::xmpp_account{.address = "alice@wonderland.example", .password = "demo"},
          mux::config::matrix_account{.user_id = "@alice:matrix.example", .password = "demo"}};
}

struct said {
  std::string sender;
  std::string text;
  bool outgoing = false;
};

inline void conversation(mux::model& into, const mux::account_id& account, std::string id, std::string name,
                         mux::conversation_kind_t kind, std::optional<std::string> topic, bool encrypted,
                         std::int64_t unread, std::vector<said> lines) {
  const mux::conversation_id where{account, id};
  into.apply(mux::change_t{mux::change::conversation_updated{.id = where,
                                                             .kind = kind,
                                                             .name = std::move(name),
                                                             .topic = std::move(topic),
                                                             .encrypted = encrypted,
                                                             .unread = unread}});
  const auto now = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
  auto at = now - std::chrono::minutes(3 * static_cast<int>(lines.size()));
  int n = 0;
  for (said& line : lines) {
    mux::message one;
    one.in = where;
    one.id = std::format("{}-{}", id, n++);
    one.sender = std::move(line.sender);
    one.at = at;
    one.body.plain = std::move(line.text);
    one.outgoing = line.outgoing;
    into.apply(mux::change_t{mux::change::message_added{.message = std::move(one)}});
    at += 3min;
  }
}

inline void fill(mux::model& into) {
  const mux::account_id xmpp{mux::protocol::xmpp{}, "alice@wonderland.example"};
  const mux::account_id matrix{mux::protocol::matrix{}, "@alice:matrix.example"};
  into.apply(mux::change_t{mux::change::connection_changed{xmpp, mux::connection::online{}}});
  into.apply(mux::change_t{mux::change::connection_changed{matrix, mux::connection::online{}}});

  conversation(into, xmpp, "hatter@wonderland.example", "The Hatter", mux::conversation_kind::direct{}, std::nullopt,
               true, 2,
               {{"hatter@wonderland.example", "Why is a raven like a writing-desk?"},
                {"alice@wonderland.example", "I give up. What's the answer?", true},
                {"hatter@wonderland.example", "I haven't the slightest idea."},
                {"hatter@wonderland.example", "Tea at six, as always. Don't be late."}});
  conversation(into, xmpp, "rabbit@wonderland.example", "White Rabbit", mux::conversation_kind::direct{}, std::nullopt,
               false, 0,
               {{"rabbit@wonderland.example", "Oh dear! Oh dear! I shall be too late!"},
                {"alice@wonderland.example", "Late for what?", true},
                {"rabbit@wonderland.example", "The Duchess! She'll have my head."}});
  conversation(into, xmpp, "croquet@rooms.wonderland.example", "Croquet club", mux::conversation_kind::group{},
               "Flamingos provided. Hedgehogs bring their own.", false, 5,
               {{"queen@wonderland.example", "Who has been painting my roses red?"},
                {"two@wonderland.example", "Not us, Your Majesty."},
                {"queen@wonderland.example", "Off with their heads!"},
                {"king@wonderland.example", "My dear, let us have the trial first."}});
  conversation(into, matrix, "!tea:matrix.example", "Mad Tea Party", mux::conversation_kind::group{},
               "No room! No room!", true, 12,
               {{"@dormouse:matrix.example", "Twinkle, twinkle, little bat…"},
                {"@march.hare:matrix.example", "Have some wine."},
                {"@alice:matrix.example", "I don't see any wine.", true},
                {"@march.hare:matrix.example", "There isn't any."}});
  conversation(into, matrix, "!cheshire:matrix.example", "Cheshire Cat", mux::conversation_kind::direct{},
               std::nullopt, true, 0,
               {{"@cheshire:matrix.example", "We're all mad here."},
                {"@alice:matrix.example", "How do you know I'm mad?", true},
                {"@cheshire:matrix.example", "You must be, or you wouldn't have come here."}});

  // Who is in the groups, and how everyone is.
  const auto members = [&into](const mux::account_id& account, std::string id, std::vector<mux::member> who) {
    into.apply(mux::change_t{mux::change::members_changed{{account, std::move(id)}, std::move(who)}});
  };
  members(xmpp, "croquet@rooms.wonderland.example",
          {{"queen@wonderland.example", "The Queen of Hearts", "owner"},
           {"king@wonderland.example", "The King of Hearts", "admin"},
           {"alice@wonderland.example", "Alice", std::nullopt},
           {"two@wonderland.example", "Two of Spades", std::nullopt},
           {"rabbit@wonderland.example", "White Rabbit", std::nullopt}});
  members(matrix, "!tea:matrix.example",
          {{"@march.hare:matrix.example", "March Hare", "owner"},
           {"@hatter:matrix.example", "The Hatter", "admin"},
           {"@dormouse:matrix.example", "Dormouse", std::nullopt},
           {"@alice:matrix.example", "Alice", std::nullopt}});
  const auto is = [&into](const mux::account_id& account, std::string contact, mux::availability_t state) {
    into.apply(mux::change_t{mux::change::presence_changed{account, std::move(contact), mux::presence{state, std::nullopt}}});
  };
  is(xmpp, "hatter@wonderland.example", mux::availability::online{});
  is(xmpp, "rabbit@wonderland.example", mux::availability::away{});
  is(xmpp, "queen@wonderland.example", mux::availability::do_not_disturb{});
  is(xmpp, "king@wonderland.example", mux::availability::online{});
  is(matrix, "@cheshire:matrix.example", mux::availability::extended_away{});
  is(matrix, "@march.hare:matrix.example", mux::availability::online{});
  is(matrix, "@hatter:matrix.example", mux::availability::online{});
}

}  // namespace fake

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
struct open_drawer {};
struct show_account {
  std::string address;
};
struct set_motion {
  std::string level;
};
struct quit {};
struct open_settings {};
struct pop_panel {};
struct toggle_info {};
struct submit_message {
  std::string text;
};
struct send_typed {};
struct resize_sidebar {
  float x = 0.0f;
};
struct not_implemented {
  std::string what;
};
struct close_notice {};
struct resize_info {
  float x = 0.0f;
};
struct toggle_mute {};
struct close_account_pages {};
struct accounts_back {};
struct account_page {
  int page = 0;
};
struct flip_account_receipts {};
struct proxy_kind {
  int kind = 0;
};
struct choose_account_proxy {
  int index = -1;
};
struct manage_proxies {};
struct settings_proxies {};
struct add_proxy {};
struct edit_proxy {
  int index = 0;
};
struct save_proxy_profile {};
struct delete_proxy_profile {};
struct settings_appearance {};
struct set_theme {
  std::string name;
};
struct set_renderer {
  std::string name;
};
struct leave_chat {};
struct switch_account {
  std::string address;
};
struct close_settings {};
struct settings_home {};
struct settings_animations {};
}  // namespace request

using request_t =
    std::variant<request::choose, request::back, request::open_accounts, request::open_new_account,
                 request::add_xmpp, request::add_matrix, request::select_account, request::toggle_advanced,
                 request::toggle_plain, request::submit_login, request::flip_enabled, request::remove_account,
                 request::open_drawer, request::show_account, request::set_motion, request::quit,
                 request::open_settings, request::close_settings, request::settings_home,
                 request::settings_animations, request::pop_panel, request::toggle_info,
                 request::switch_account, request::submit_message, request::send_typed,
                 request::resize_sidebar, request::not_implemented, request::close_notice,
                 request::resize_info, request::toggle_mute, request::close_account_pages,
                 request::accounts_back, request::account_page, request::flip_account_receipts,
                 request::proxy_kind, request::choose_account_proxy, request::manage_proxies,
                 request::settings_proxies, request::add_proxy, request::edit_proxy, request::save_proxy_profile,
                 request::delete_proxy_profile, request::settings_appearance, request::set_theme,
                 request::set_renderer, request::leave_chat>;

// What the screens ask: each a request, kept until the program applies it
// between events -- except a message, which goes to the network at once.
struct actions {
  network* net = nullptr;
  // In the demo, a message sent is there at once, as sent.
  bool demo = false;
  mailbox_type* box = nullptr;
  int demo_sent = 0;
  std::vector<request_t> requests;

  void choose(const mux::conversation_id& which) { requests.emplace_back(request::choose{which}); }
  void send(const mux::conversation_id& to, std::string text) {
    if (!demo) {
      net->send(to, std::move(text));
      return;
    }
    mux::message one;
    one.in = to;
    one.id = std::format("demo-sent-{}", demo_sent++);
    one.sender = to.account.address;
    one.at = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
    one.body.plain = std::move(text);
    one.outgoing = true;
    box->push(mux::change_t{mux::change::message_added{.message = std::move(one)}});
  }
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
  void open_drawer() { requests.emplace_back(request::open_drawer{}); }
  void show_account(std::string address) { requests.emplace_back(request::show_account{std::move(address)}); }
  void set_motion(std::string level) { requests.emplace_back(request::set_motion{std::move(level)}); }
  void quit() { requests.emplace_back(request::quit{}); }
  void open_settings() { requests.emplace_back(request::open_settings{}); }
  void pop_panel() { requests.emplace_back(request::pop_panel{}); }
  void toggle_info() { requests.emplace_back(request::toggle_info{}); }
  void submit_message(std::string text) { requests.emplace_back(request::submit_message{std::move(text)}); }
  void send_typed() { requests.emplace_back(request::send_typed{}); }
  void resize_sidebar(float x) { requests.emplace_back(request::resize_sidebar{x}); }
  void not_implemented(std::string what) { requests.emplace_back(request::not_implemented{std::move(what)}); }
  void close_notice() { requests.emplace_back(request::close_notice{}); }
  void resize_info(float x) { requests.emplace_back(request::resize_info{x}); }
  void toggle_mute() { requests.emplace_back(request::toggle_mute{}); }
  void close_account_pages() { requests.emplace_back(request::close_account_pages{}); }
  void accounts_back() { requests.emplace_back(request::accounts_back{}); }
  void account_page(int page) { requests.emplace_back(request::account_page{page}); }
  void flip_account_receipts() { requests.emplace_back(request::flip_account_receipts{}); }
  void proxy_kind(int kind) { requests.emplace_back(request::proxy_kind{kind}); }
  void choose_account_proxy(int index) { requests.emplace_back(request::choose_account_proxy{index}); }
  void manage_proxies() { requests.emplace_back(request::manage_proxies{}); }
  void settings_proxies() { requests.emplace_back(request::settings_proxies{}); }
  void add_proxy() { requests.emplace_back(request::add_proxy{}); }
  void edit_proxy(int index) { requests.emplace_back(request::edit_proxy{index}); }
  void save_proxy_profile() { requests.emplace_back(request::save_proxy_profile{}); }
  void delete_proxy_profile() { requests.emplace_back(request::delete_proxy_profile{}); }
  void settings_appearance() { requests.emplace_back(request::settings_appearance{}); }
  void set_theme(std::string name) { requests.emplace_back(request::set_theme{std::move(name)}); }
  void set_renderer(std::string name) { requests.emplace_back(request::set_renderer{std::move(name)}); }
  void leave_chat() { requests.emplace_back(request::leave_chat{}); }
  void switch_account(std::string address) { requests.emplace_back(request::switch_account{std::move(address)}); }
  void close_settings() { requests.emplace_back(request::close_settings{}); }
  void settings_home() { requests.emplace_back(request::settings_home{}); }
  void settings_animations() { requests.emplace_back(request::settings_animations{}); }
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
  using adding = mux::ui::add_account_pane<actions>;
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
  // The theme and the renderer, for the next start: kept in the file.
  std::optional<std::string> theme;
  std::optional<std::string> renderer;
  // The chats muted, and the proxy profiles: kept in the file.
  std::set<mux::conversation_id> muted;
  std::vector<mux::config::proxy_settings> proxies;
  // Why the accounts file could not be read, when it could not: then it is
  // not written over either.
  std::optional<std::string> config_error;
  // The drawer, left open under a page coming in over it, to go when the
  // page is in.
  bool drawer_waits = false;
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
    if (drawer_waits && !root().pages_moving()) {
      root().close_drawer_now();
      drawer_waits = false;
    }
  }

  void closing() { net->shutdown(); }

  // -- the window
  window_type& root() { return scene.root(); }

  void show_conversations() {
    root().close_drawer();
    pending_login.reset();
    root().close();
    this->refresh();
  }
  accounts& show_accounts() {
    // From the drawer, the page comes in over it, and the drawer goes once
    // the page is in: not two things moving at once.
    if (root().drawer_open())
      drawer_waits = true;
    root().close_settings();
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
  // Adding an account: beside the list, on the accounts page.
  void show_adding() {
    auto& panel = this->show_accounts();
    panel.show_adding();
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
    root().main().muted = muted;
    root().show(saved, *model);
    root().main().show(*model);
    if (auto* up = root().open_panel())
      std::visit([this](auto& panel) { this->bring_up_to_date(panel); }, *up);
  }
  void bring_up_to_date(accounts& panel) {
    panel.show(saved, *model);
    if (auto* pane = panel.adding())
      std::visit([this](auto& form) { this->watch_login(form); }, pane->form);
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
    this->mark_read(one.which);
  }

  // A chat opened: read up to its last message from someone else, and the
  // people in it told so where read receipts are on.
  void mark_read(const mux::conversation_id& which) {
    if (ask.demo)
      return;
    if (const auto account = this->find(which.account.address);
        account != saved.end() && !mux::config::read_receipts_of(*account))
      return;
    const mux::conversation* one = model->find(which);
    if (!one)
      return;
    for (auto it = one->timeline.rbegin(); it != one->timeline.rend(); ++it)
      if (!it->outgoing && !it->id.empty()) {
        net->mark_read(which, it->id);
        return;
      }
  }

  // The chosen chat left: a Matrix room here; XMPP rooms are not there yet.
  void apply(const request::leave_chat&) {
    const auto& chosen = root().main().chosen;
    if (!chosen)
      return;
    const mux::conversation* one = model->find(*chosen);
    const bool room = one && mux::ui::is_group(*one);
    std::visit(mux::overloaded{[&](mux::protocol::xmpp) {
                                 if (!room) {
                                   root().show_notice("Leaving a direct XMPP chat");
                                   return;
                                 }
                                 if (!ask.demo)
                                   net->leave(*chosen);
                                 root().main().info_open = false;
                               },
                               [&](mux::protocol::matrix) {
                                 if (!ask.demo)
                                   net->leave(*chosen);
                                 root().main().info_open = false;
                               }},
               chosen->account.speaks);
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
    std::visit(
        [&](accounts& panel) {
          if (const auto found = this->find(one.address); found != saved.end()) {
            pending_login.reset();
            panel.select(*found, *model);
            panel.show(saved, *model);
          }
        },
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
    std::visit(
        [this](accounts& panel) {
          if (auto* editor = panel.editor())
            std::visit([this](auto& form) { this->edit(form); }, editor->form);
          else if (auto* pane = panel.adding())
            std::visit([this](auto& form) { this->add(form); }, pane->form);
        },
        *up);
  }
  void apply(const request::flip_enabled& one) { this->flip_enabled(one.address); }
  void apply(const request::remove_account& one) { this->remove(one.address); }
  void apply(const request::open_drawer&) { root().open_drawer(); }
  void apply(const request::show_account& one) { (void)this->show_account(one.address); }
  void apply(const request::set_motion& one) { this->set_motion(one.level); }
  void apply(const request::quit&) { mux::host::request_quit(); }
  void apply(const request::open_settings&) {
    root().close_drawer();
    root().open_settings(motion.value_or("full"));
  }
  void apply(const request::close_settings&) { root().close_settings(); }
  void apply(const request::toggle_info&) { root().main().toggle_info(); }
  void apply(const request::submit_message& one) { this->send_message(one.text); }
  void apply(const request::resize_sidebar& one) { root().main().resize_sidebar(one.x); }
  void apply(const request::not_implemented& one) { root().show_notice(one.what); }
  void apply(const request::close_notice&) { root().close_notice(); }
  void apply(const request::resize_info& one) { root().main().resize_info(one.x); }
  // The chosen chat muted, or not: kept in the file.
  void apply(const request::toggle_mute&) {
    auto& screen = root().main();
    if (!screen.chosen)
      return;
    if (!muted.erase(*screen.chosen))
      muted.insert(*screen.chosen);
    (void)this->write();
    this->refresh();
  }
  void apply(const request::close_account_pages&) {
    if (auto* up = root().open_panel())
      std::visit([](accounts& panel) { panel.close_pages(); }, *up);
  }
  // ← on the accounts page: from an account's pages to the list, from the
  // list to the chats.
  void apply(const request::accounts_back&) {
    auto* up = root().open_panel();
    if (!up)
      return;
    std::visit(
        [this](accounts& panel) {
          if (panel.pages_open())
            panel.close_pages();
          else
            this->apply(request::pop_panel{});
        },
        *up);
  }
  // The chosen account, and its accounts page, when they are up.
  template <class F>
  void with_chosen_account(F&& f) {
    auto* up = root().open_panel();
    if (!up)
      return;
    std::visit(
        [&](accounts& panel) {
          if (!panel.selected)
            return;
          if (const auto found = this->find(*panel.selected); found != saved.end())
            f(panel, *found);
        },
        *up);
  }
  void apply(const request::account_page& one) {
    this->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
      panel.show_page(one.page, account, *model, proxies);
    });
  }
  void apply(const request::flip_account_receipts&) {
    this->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
      auto& kept = mux::config::read_receipts_in(account);
      kept = !kept.value_or(true);
      if (auto* page = panel.privacy())
        page->show(*kept);
      (void)this->write();
    });
  }
  void apply(const request::proxy_kind& one) {
    if (auto* up = root().settings_up())
      if (auto* editor = up->editor())
        editor->set_kind(one.kind);
  }
  // The chosen account through a profile, or none: kept, and connected again.
  void apply(const request::choose_account_proxy& one) {
    this->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
      auto& kept = mux::config::proxy_in(account);
      if (one.index < 0 || static_cast<std::size_t>(one.index) >= proxies.size())
        kept.reset();
      else
        kept = proxies[static_cast<std::size_t>(one.index)].name;
      (void)this->write();
      this->reconnect(account);
      panel.show_page(2, account, *model, proxies);
    });
  }
  void reconnect(const mux::config::account_t& account) {
    if (!mux::config::enabled_of(account) || ask.demo)
      return;
    net->remove(mux::config::address_of(account));
    net->add(account, proxies);
  }
  // The accounts going through a profile, connected again.
  void reconnect_through(const std::string& name) {
    for (const auto& one : saved)
      if (mux::config::proxy_of(one) == name)
        this->reconnect(one);
  }
  void apply(const request::settings_appearance&) {
    if (auto* up = root().settings_up())
      up->show_appearance(theme.value_or("dark"), renderer.value_or("opengl"));
  }
  // The theme or the renderer chosen: kept, for the next start.
  // A theme chosen: its colours in place, and the window made again in them,
  // as it was -- the chats, the one chosen, the widths -- with Settings open
  // where it was.
  void apply(const request::set_theme& one) {
    theme = one.name;
    (void)this->write();
    auto& before = root().main();
    const auto chosen = before.chosen;
    const auto current = before.current;
    const float side_width = before.side_width;
    const float info_width = before.info_width;
    const bool info_open = before.info_open;
    mux::ui::use_theme(one.name);
    pending_login.reset();
    drawer_waits = false;
    root().rebuild();
    auto& after = root().main();
    after.chosen = chosen;
    after.current = current;
    after.side_width = side_width;
    after.info_width = info_width;
    after.info_open = info_open;
    this->refresh();
    root().open_settings(motion.value_or("full"));
    if (auto* up = root().settings_up())
      up->show_appearance(theme.value_or("dark"), renderer.value_or("opengl"));
  }
  void apply(const request::set_renderer& one) {
    renderer = one.name;
    this->show_appearance_choices();
    (void)this->write();
  }
  void show_appearance_choices() {
    if (auto* up = root().settings_up())
      if (auto* page = up->appearance())
        page->show(theme.value_or("dark"), renderer.value_or("opengl"));
  }
  void apply(const request::manage_proxies&) {
    root().open_settings(motion.value_or("full"));
    if (auto* up = root().settings_up())
      up->show_proxies(proxies);
  }
  void apply(const request::settings_proxies&) {
    if (auto* up = root().settings_up())
      up->show_proxies(proxies);
  }
  void apply(const request::add_proxy&) {
    if (auto* up = root().settings_up())
      up->show_proxy(std::nullopt, -1);
  }
  void apply(const request::edit_proxy& one) {
    if (auto* up = root().settings_up(); up && one.index >= 0 && static_cast<std::size_t>(one.index) < proxies.size())
      up->show_proxy(proxies[static_cast<std::size_t>(one.index)], one.index);
  }
  // A profile saved: a new one added, or one changed -- and renamed in the
  // accounts that use it.
  void apply(const request::save_proxy_profile&) {
    auto* up = root().settings_up();
    auto* editor = up ? up->editor() : nullptr;
    if (!editor)
      return;
    auto typed = editor->proxy();
    if (!typed) {
      editor->say(typed.error(), true);
      return;
    }
    for (std::size_t i = 0; i < proxies.size(); ++i)
      if (proxies[i].name == typed->name && static_cast<int>(i) != editor->index) {
        editor->say("There is a proxy of that name already.", true);
        return;
      }
    const std::string name = typed->name;
    if (editor->index < 0) {
      proxies.push_back(std::move(*typed));
    } else {
      auto& kept = proxies[static_cast<std::size_t>(editor->index)];
      for (auto& one : saved)
        if (auto& uses = mux::config::proxy_in(one); uses == kept.name)
          uses = name;
      kept = std::move(*typed);
    }
    if (auto failed = this->write()) {
      editor->say(*failed, true);
      return;
    }
    this->reconnect_through(name);
    up->show_proxies(proxies);
  }
  // A profile deleted: the accounts that used it connect directly.
  void apply(const request::delete_proxy_profile&) {
    auto* up = root().settings_up();
    auto* editor = up ? up->editor() : nullptr;
    if (!editor || editor->index < 0 || static_cast<std::size_t>(editor->index) >= proxies.size())
      return;
    const std::string name = proxies[static_cast<std::size_t>(editor->index)].name;
    proxies.erase(proxies.begin() + editor->index);
    for (auto& one : saved)
      if (auto& uses = mux::config::proxy_in(one); uses == name) {
        uses.reset();
        this->reconnect(one);
      }
    (void)this->write();
    up->show_proxies(proxies);
  }


  void apply(const request::send_typed&) { this->send_message(root().main().line.text()); }
  // What is in the message field, to the chosen chat; the field emptied.
  void send_message(std::string text) {
    auto& screen = root().main();
    const auto blank = [](unsigned char c) { return std::isspace(c) != 0; };
    if (!screen.chosen || std::ranges::all_of(text, blank))
      return;
    ask.send(*screen.chosen, std::move(text));
    screen.line.clear();
  }
  // Another account's chats listed: the drawer goes back, and no chat is
  // chosen.
  void apply(const request::switch_account& one) {
    auto& screen = root().main();
    screen.current = mux::account_id{mux::ui::protocol_of(one.address), one.address};
    screen.chosen.reset();
    root().close_drawer();
    this->refresh();
  }
  void apply(const request::pop_panel&) {
    pending_login.reset();
    root().back_panel();
    this->refresh();
  }
  void apply(const request::settings_home&) {
    if (auto* up = root().settings_up())
      up->show_home();
  }
  void apply(const request::settings_animations&) {
    if (auto* up = root().settings_up())
      up->show_animations();
  }

  // How much moves, from now on and in the file.
  void set_motion(std::string level) {
    skiff::paint::motionLevel() = motion_of(level);
    root().show_motion(level);
    motion = std::move(level);
    (void)this->write();
  }

  void switch_form(void (adding::*to)()) {
    auto* up = root().open_panel();
    if (!up)
      return;
    pending_login.reset();
    std::visit(
        [to](accounts& panel) {
          if (auto* pane = panel.adding())
            (pane->*to)();
        },
        *up);
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
    net->add(account, proxies);
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
      net->add(account, proxies);
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
      net->add(*found, proxies);
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
    if (ask.demo)
      return std::nullopt;  // the demo keeps nothing
    if (config_error)
      return "Not saved: " + *config_error;
    auto file = mux::config::file_of(saved);
    file.motion = motion;
    if (!proxies.empty())
      file.proxies = proxies;
    file.theme = theme;
    file.renderer = renderer;
    if (!muted.empty()) {
      std::vector<mux::config::muted_chat> kept;
      for (const auto& one : muted)
        kept.push_back({one.account.address, one.id});
      file.muted = std::move(kept);
    }
    if (auto done = mux::config::save(config_path, file); !done)
      return "Not saved: " + done.error();
    return std::nullopt;
  }
  void save_from_accounts() {
    if (auto failed = this->write())
      if (auto* up = root().open_panel())
        std::visit([&](accounts& panel) { panel.say(*failed); }, *up);
  }
};

}  // namespace

int main(int argc, char** argv) {
  mailbox_type box{wake_window{}};
  mux::model model;
  network net;
  net.box = &box;

  // `mux --demo`: fake accounts and conversations, no network, nothing kept.
  const bool demo = argc > 1 && std::string_view(argv[1]) == "--demo";
  const std::filesystem::path config_path = mux::config::default_path();
  mux::config::file saved;
  std::optional<std::string> config_error;
  if (demo) {
    saved = mux::config::file_of(fake::accounts());
    fake::fill(model);
  } else if (auto loaded = mux::config::load(config_path))
    saved = std::move(*loaded);
  else {
    config_error = loaded.error();
    std::println(std::cerr, "[mux] {}", loaded.error());
  }

  // Accounts named on the command line, for this run only.
  std::vector<mux::config::account_t> extra;
  if (argc > 1 && !demo) {
    const char* password = std::getenv("MUX_PASSWORD");
    if (!password) {
      std::println(std::cerr, "accounts on the command line take their password from MUX_PASSWORD, which is not set");
      return 2;
    }
    for (int at = 1; at < argc; ++at)
      extra.push_back(mux::config::account_from(argv[at], password));
  }

  const auto proxies = saved.proxies.value_or(std::vector<mux::config::proxy_settings>{});
  for (const auto& one : mux::config::accounts_of(saved))
    if (mux::config::enabled_of(one) && !demo)
      net.start(one, proxies);
  for (const auto& one : extra)
    net.start(one, proxies);
  net.thread = std::thread([&net] {
    try {
      net.loop.run_forever();
    } catch (const std::exception& failed) {
      std::println(std::cerr, "[mux] the network stopped: {}", failed.what());
    }
  });

  // The theme first: what is made takes its colours from it.
  mux::ui::use_theme(saved.theme.value_or("dark"));
  app program;
  program.box = &box;
  program.model = &model;
  program.net = &net;
  program.ask.net = &net;
  program.ask.demo = demo;
  program.ask.box = &box;
  program.config_path = config_path;
  program.saved = mux::config::accounts_of(saved);
  program.motion = saved.motion;
  program.theme = saved.theme;
  program.renderer = saved.renderer;
  program.proxies = proxies;
  for (const auto& one : saved.muted.value_or(std::vector<mux::config::muted_chat>{}))
    program.muted.insert({{mux::ui::protocol_of(one.account), one.account}, one.conversation});
  skiff::paint::motionLevel() = motion_of(saved.motion);
  program.root().show_motion(saved.motion.value_or("full"));
  program.config_error = std::move(config_error);
  program.refresh();

  const int code = mux::host::run(program, {.software = saved.renderer == "software"});
  net.thread.join();
  return code;
}
