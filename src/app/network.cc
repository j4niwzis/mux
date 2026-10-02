// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.network: The accounts, on their loop of their own thread, and the mailbox to the window.
export module mux.app.network;

import std;
import splice;
import mux.core;
import mux.config;
import mux.net;
import mux.preview;
import mux.xmpp;
import mux.matrix;
import mux.host;
import mux.ui;

export namespace mux::app {

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
}  // namespace mux::app
// The accounts themselves -- their requests, sync, media, and the HTTP and
// TLS under them -- instantiated in units of their own (accounts_*.cc), in
// parallel: they were most of this one's four and a half minutes. Outside a
// release build only: a release build makes them here, where they are used,
// so that the optimiser sees all of them in one unit.
#if defined(MUX_SPLIT_ACCOUNTS)
extern template class mux::xmpp::account<mux::app::post_change>;
extern template class mux::matrix::account<mux::app::post_change>;
#endif
export namespace mux::app {
using any_account = splice::variant<std::unique_ptr<xmpp_account>, std::unique_ptr<matrix_account>>;

struct running_account {
  std::string address;
  any_account account;
  std::shared_ptr<std::atomic<bool>> live;
  // The proxy it goes through: what is fetched for its chats from elsewhere
  // -- a link's page, its picture -- goes through it as well.
  std::optional<mux::net::proxy> via;
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
    const auto& named = mux::config::proxy_of(saved);
    const auto* via = mux::config::find_proxy(proxies, named);
    // A proxy named and not there: not connected at all -- never straight
    // to the server instead, which would show it this machine's address.
    if (named && !via) {
      const std::string& address = mux::config::address_of(saved);
      box->push(mux::change_t{mux::change::connection_changed{
          {mux::ui::protocol_of(address), address},
          mux::connection::failed{std::format("Not connected: its proxy \"{}\" is gone. Choose a proxy for it, or none.",
                                              *named)}}});
      return;
    }
    splice::visit([this, via](const auto& each) { this->start_one(each, proxy_of(via)); }, saved);
  }
  // The proxy a profile names, as mux.net takes it.
  static std::optional<mux::net::proxy> proxy_of(const mux::config::proxy_settings* kept) {
    if (!kept)
      return std::nullopt;
    return mux::net::proxy{.kind = splice::visit(splice::overloaded{[](mux::config::proxy_kind::socks5) {
                                                                 return mux::net::proxy_kind_t{mux::net::proxy_kind::socks5{}};
                                                               },
                                                               [](mux::config::proxy_kind::http) {
                                                                 return mux::net::proxy_kind_t{mux::net::proxy_kind::http{}};
                                                               }},
                                              mux::config::proxy_kind_of(kept->kind)),
                           .host = kept->host,
                           .port = static_cast<std::uint16_t>(kept->port),
                           .username = kept->username,
                           .password = kept->password,
                           .srv = mux::net::srv_lookup_of(kept->srv_resolver)};
  }

  void start_one(const mux::config::xmpp_account& saved, std::optional<mux::net::proxy> via) {
    auto live = std::make_shared<std::atomic<bool>>(true);
    mux::xmpp::settings how{.address = saved.address,
                            .password = saved.password,
                            .resource = saved.resource,
                            .host = saved.host,
                            .plain_without_tls = saved.plain_without_tls,
                            .proxy = via};
    if (saved.port)
      how.port = static_cast<std::uint16_t>(*saved.port);
    this->run(saved.address, std::make_unique<xmpp_account>(loop, tls, std::move(how), post_change{box, live}), live,
              std::move(via));
  }
  void start_one(const mux::config::matrix_account& saved, std::optional<mux::net::proxy> via) {
    auto live = std::make_shared<std::atomic<bool>>(true);
    mux::matrix::settings how{.user_id = saved.user_id,
                              .password = saved.password,
                              .homeserver = saved.homeserver,
                              .device_name = saved.device_name,
                              .proxy = via,
                              .access_token = saved.access_token,
                              .device_id = saved.device_id,
                              // Named by the user ID with what a file name cannot hold put
                              // aside: ':' is not one on Windows.
                              .crypto_store = mux::config::state_path("crypto") /
                                              ((saved.user_id | std::views::transform([](char c) {
                                                  return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_' ? c : '_';
                                                }) | std::ranges::to<std::string>()) + ".json")};
    this->run(saved.user_id, std::make_unique<matrix_account>(loop, tls, std::move(how), post_change{box, live}),
              live, std::move(via));
  }
  void run(const std::string& address, any_account account, std::shared_ptr<std::atomic<bool>> live,
           std::optional<mux::net::proxy> via) {
    running_account entry{address, std::move(account), std::move(live), std::move(via)};
    splice::visit([](auto& one) { one->start(); }, entry.account);
    accounts.push_back(std::move(entry));
  }

  void stop(const std::string& address) {
    const auto found = std::ranges::find(accounts, address, &running_account::address);
    if (found == accounts.end())
      return;
    found->live->store(false);
    splice::visit([](auto& account) { account->stop(); }, found->account);
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
  void send(const mux::conversation_id& to, std::string text, std::optional<std::string> reply_to = std::nullopt,
            std::vector<mux::mention> mentions = {}) {
    loop.post([this, to, text = std::move(text), reply_to = std::move(reply_to), mentions = std::move(mentions)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == to.account)
                account->send(to.id, text, reply_to, mentions);
            },
            one.account);
    });
  }
  void edit(const mux::conversation_id& in, std::string id, std::string text) {
    loop.post([this, in, id = std::move(id), text = std::move(text)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == in.account)
                account->edit(in.id, id, text);
            },
            one.account);
    });
  }
  // A picture's caption, edited.
  void edit_caption(const mux::conversation_id& in, std::string id, std::string caption, mux::attachment picture) {
    loop.post([this, in, id = std::move(id), caption = std::move(caption), picture = std::move(picture)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == in.account)
                account->edit_caption(in.id, id, caption, picture);
            },
            one.account);
    });
  }
  void remove_message(const mux::conversation_id& in, std::string id) {
    loop.post([this, in, id = std::move(id)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == in.account)
                account->remove(in.id, id);
            },
            one.account);
    });
  }
  // For the account a conversation is of: read up to `event`, or left.
  void mark_read(const mux::conversation_id& in, std::string event) {
    loop.post([this, in, event = std::move(event)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == in.account)
                account->mark_read(in.id, event);
            },
            one.account);
    });
  }
  // A file sent into a chat by the account it is of.
  void send_file(const mux::conversation_id& in, std::string local, std::string bytes, std::string name,
                 std::string mimetype, bool image, int width, int height, std::string caption,
                 std::optional<std::string> reply_to = std::nullopt, std::optional<mux::thread_place> thread = std::nullopt,
                 std::optional<mux::video_look> video = std::nullopt) {
    loop.post([this, in, local = std::move(local), bytes = std::move(bytes), name = std::move(name),
               mimetype = std::move(mimetype), image, width, height, caption = std::move(caption),
               reply_to = std::move(reply_to), thread = std::move(thread), video = std::move(video)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == in.account)
                account->send_file(in.id, local, bytes, name, mimetype, image, width, height, caption, reply_to, thread, video);
            },
            one.account);
    });
  }
  // A reaction to a message put or taken back, by the account it is of.
  void react(const mux::conversation_id& in, std::string target, std::string key, bool on) {
    loop.post([this, in, target = std::move(target), key = std::move(key), on] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == in.account)
                account->react(in.id, target, key, on);
            },
            one.account);
    });
  }
  // The developer tools, by the account the chat is of.
  template <class Ask>
  void on_account_of(const mux::conversation_id& in, Ask ask) {
    loop.post([this, in, ask = std::move(ask)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == in.account)
                ask(*account);
            },
            one.account);
    });
  }
  void view_source(const mux::conversation_id& in, std::string event) {
    on_account_of(in, [room = in.id, event = std::move(event)](auto& account) { account.view_source(room, event); });
  }
  void list_state(const mux::conversation_id& in) {
    on_account_of(in, [room = in.id](auto& account) { account.list_state(room); });
  }
  void send_custom(const mux::conversation_id& in, std::string type, std::optional<std::string> key, std::string json) {
    on_account_of(in, [room = in.id, type = std::move(type), key = std::move(key), json = std::move(json)](auto& account) {
      account.send_custom(room, type, key, json);
    });
  }
  // A sticker sent into a chat by the account it is of.
  void send_sticker(const mux::conversation_id& to, mux::emote sticker, std::optional<std::string> reply_to = std::nullopt) {
    loop.post([this, to, sticker = std::move(sticker), reply_to = std::move(reply_to)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == to.account)
                account->send_sticker(to.id, sticker, reply_to);
            },
            one.account);
    });
  }
  // The proxy of an account: none where it has none, or is gone.
  [[nodiscard]] std::optional<mux::net::proxy> via_of(const mux::account_id& by) const {
    const auto found = std::ranges::find(accounts, by.address, &running_account::address);
    return found == accounts.end() ? std::nullopt : found->via;
  }
  // A preview's picture its site's page named: from the site, through the
  // account's proxy -- asked for only where the chat fetches previews from
  // sites. Never through fetch_avatar: an https address anything else
  // carries (a message's, a room's avatar) is not fetched at all.
  void fetch_preview_picture(const mux::account_id& of, std::string source) {
    loop.post([this, of, source = std::move(source)] {
      loop.spawn([this, via = this->via_of(of), source] {
        if (auto bytes = mux::preview::fetch_picture(loop, tls, via, source))
          box->push(mux::change_t{mux::change::avatar_loaded{mux::media_use::avatar{source}, source, std::move(*bytes)}});
      });
    });
  }
  // A link's preview: asked of the account's server, or, where the chat
  // chose so, of the site itself, through the account's proxy.
  void fetch_preview(const mux::account_id& by, std::string url, bool direct) {
    if (direct) {
      loop.post([this, by, url = std::move(url)] {
        loop.spawn([this, via = this->via_of(by), url] {
          if (auto made = mux::preview::fetch_preview(loop, tls, via, url))
            box->push(mux::change_t{mux::change::preview_loaded{url, std::move(*made)}});
        });
      });
      return;
    }
    loop.post([this, by, url = std::move(url)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == by)
                account->fetch_preview(url);
            },
            one.account);
    });
  }
  // A room made by an account: a direct chat with someone, or a group.
  void create_direct(const mux::account_id& by, std::string user) {
    loop.post([this, by, user = std::move(user)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == by)
                account->create_direct(user);
            },
            one.account);
    });
  }
  void create_group(const mux::account_id& by, std::string name) {
    loop.post([this, by, name = std::move(name)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == by)
                account->create_group(name);
            },
            one.account);
    });
  }
  // A message forwarded, within the account it is of.
  void forward(const mux::conversation_id& from, std::string event, const mux::conversation_id& to) {
    loop.post([this, from, event = std::move(event), to] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == from.account)
                account->forward(from.id, event, to.id);
            },
            one.account);
    });
  }
  // Something done to a room, by the account it is of.
  void manage(const mux::conversation_id& in, mux::room_action_t action) {
    loop.post([this, in, action = std::move(action)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == in.account)
                account->manage(in.id, action);
            },
            one.account);
    });
  }
  // A message a reply quotes, fetched beside the timeline.
  void fetch_quoted(const mux::conversation_id& in, std::string target) {
    loop.post([this, in, target = std::move(target)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == in.account)
                account->fetch_quoted(in.id, target);
            },
            one.account);
    });
  }
  // A message pinned or unpinned in its chat, by the account it is of.
  void pin(const mux::conversation_id& in, std::string target, bool on) {
    loop.post([this, in, target = std::move(target), on] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == in.account)
                account->pin(in.id, target, on);
            },
            one.account);
    });
  }
  // Whether the user is typing in a chat, told to it.
  void typing(const mux::conversation_id& in, bool on) {
    loop.post([this, in, on] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == in.account)
                account->typing(in.id, on);
            },
            one.account);
    });
  }
  // A server's public directory, searched by the account named.
  void search_directory(const mux::account_id& by, std::string server, std::string query) {
    loop.post([this, by, server = std::move(server), query = std::move(query)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == by)
                account->search_directory(server, query);
            },
            one.account);
    });
  }
  // The room being read, told to its account: a sliding sync follows it.
  void follow_room(const mux::account_id& by, std::optional<std::string> room) {
    loop.post([this, by, room = std::move(room)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == by)
                account->follow(room);
            },
            one.account);
    });
  }
  // A space's rooms, asked by the account named.
  void explore_space(const mux::account_id& by, std::string room) {
    loop.post([this, by, room = std::move(room)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == by)
                account->explore_space(room);
            },
            one.account);
    });
  }
  // A room made by the account named, as Element's Create room.
  void create_room(const mux::account_id& by, std::string name, std::string topic, bool open, std::string alias,
                   bool federate = true, bool encrypted = false) {
    loop.post([this, by, name = std::move(name), topic = std::move(topic), open, alias = std::move(alias), federate,
               encrypted] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == by)
                account->create_room(name, topic, open, alias, federate, encrypted);
            },
            one.account);
    });
  }
  // Threads: a room's listed, one loaded, an answer sent in one.
  void list_threads(const mux::conversation_id& in) {
    loop.post([this, in] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == in.account)
                account->list_threads(in.id);
            },
            one.account);
    });
  }
  void load_thread(const mux::conversation_id& in, std::string root) {
    loop.post([this, in, root = std::move(root)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == in.account)
                account->load_thread(in.id, root);
            },
            one.account);
    });
  }
  void send_in_thread(const mux::conversation_id& in, std::string body, std::string root, std::string latest,
                      std::optional<std::string> reply_to) {
    loop.post([this, in, body = std::move(body), root = std::move(root), latest = std::move(latest),
               reply_to = std::move(reply_to)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == in.account)
                account->send_in_thread(in.id, body, root, latest, reply_to);
            },
            one.account);
    });
  }
  // Packs: listed, saved, taken away, an image uploaded -- by the account named.
  void list_packs(const mux::account_id& by, std::optional<std::string> room) {
    loop.post([this, by, room = std::move(room)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == by)
                account->list_packs(room);
            },
            one.account);
    });
  }
  void save_pack(const mux::account_id& by, emote_pack pack) {
    loop.post([this, by, pack = std::move(pack)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == by)
                account->save_pack(pack);
            },
            one.account);
    });
  }
  void delete_pack(const mux::account_id& by, std::string room, std::string state_key) {
    loop.post([this, by, room = std::move(room), state_key = std::move(state_key)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == by)
                account->delete_pack(room, state_key);
            },
            one.account);
    });
  }
  void upload_pack_picture(const mux::account_id& by, pack_picture picture, std::string bytes) {
    loop.post([this, by, picture = std::move(picture), bytes = std::move(bytes)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == by)
                account->upload_pack_picture(picture, bytes);
            },
            one.account);
    });
  }
  // The user directory of the account named, searched.
  // What the account knows of a person's encryption identity, asked for.
  void ask_trust(const mux::account_id& by, std::string user) {
    loop.post([this, by, user = std::move(user)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == by)
                account->tell_trust(user);
            },
            one.account);
    });
  }
  void search_people(const mux::account_id& by, std::string term) {
    loop.post([this, by, term = std::move(term)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == by)
                account->search_people(term);
            },
            one.account);
    });
  }
  // A room not joined, looked up by the account named.
  void preview_room(const mux::account_id& by, std::string room, std::vector<std::string> via) {
    loop.post([this, by, room = std::move(room), via = std::move(via)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == by)
                account->preview_room(room, via);
            },
            one.account);
    });
  }
  // The sessions of the Matrix account named: listed, one renamed, some
  // signed out.
  template <class F>
  void with_matrix(const mux::account_id& by, F f) {
    loop.post([this, by, f = std::move(f)] {
      for (auto& one : accounts)
        splice::visit(splice::overloaded{[&](std::unique_ptr<matrix_account>& account) {
                                           if (account->id() == by)
                                             f(*account);
                                         },
                                         [](auto&) {}},
                      one.account);
    });
  }
  void export_room_keys(const mux::account_id& by, std::string path, std::string passphrase) {
    this->with_matrix(by, [path = std::move(path), passphrase = std::move(passphrase)](matrix_account& account) {
      account.export_room_keys(path, passphrase);
    });
  }
  void import_room_keys(const mux::account_id& by, std::string path, std::string passphrase) {
    this->with_matrix(by, [path = std::move(path), passphrase = std::move(passphrase)](matrix_account& account) {
      account.import_room_keys(path, passphrase);
    });
  }
  void restore_cross_signing(const mux::account_id& by, std::string recovery) {
    this->with_matrix(by, [recovery = std::move(recovery)](matrix_account& account) { account.restore_cross_signing(recovery); });
  }
  void setup_cross_signing(const mux::account_id& by, std::string password) {
    this->with_matrix(by, [password = std::move(password)](matrix_account& account) { account.setup_cross_signing(password); });
  }
  void verify_start(const mux::account_id& by, std::string user, std::optional<std::string> device) {
    this->with_matrix(by, [user = std::move(user), device = std::move(device)](matrix_account& account) {
      account.verify_start(user, device);
    });
  }
  void verify_accept(const mux::account_id& by, std::string txn) {
    this->with_matrix(by, [txn = std::move(txn)](matrix_account& account) { account.verify_accept(txn); });
  }
  void verify_confirm(const mux::account_id& by, std::string txn, bool match) {
    this->with_matrix(by, [txn = std::move(txn), match](matrix_account& account) { account.verify_confirm(txn, match); });
  }
  void verify_cancel(const mux::account_id& by, std::string txn) {
    this->with_matrix(by, [txn = std::move(txn)](matrix_account& account) { account.verify_cancel(txn); });
  }
  void list_sessions(const mux::account_id& by) {
    this->with_matrix(by, [](matrix_account& account) { account.list_sessions(); });
  }
  void rename_session(const mux::account_id& by, std::string device, std::string name) {
    this->with_matrix(by, [device = std::move(device), name = std::move(name)](matrix_account& account) {
      account.rename_session(device, name);
    });
  }
  void sign_out_sessions(const mux::account_id& by, std::vector<std::string> devices, std::string password) {
    this->with_matrix(by, [devices = std::move(devices), password = std::move(password)](matrix_account& account) {
      account.sign_out_sessions(devices, password);
    });
  }
  // A person's profile, asked of the Matrix account named.
  void fetch_profile(const mux::account_id& by, std::string user) {
    loop.post([this, by, user = std::move(user)] {
      for (auto& one : accounts)
        splice::visit(splice::overloaded{[&](std::unique_ptr<matrix_account>& account) {
                                           if (account->id() == by)
                                             account->fetch_profile(user);
                                         },
                                         [](auto&) {}},
                      one.account);
    });
  }
  // A room joined by the account named, through the servers named.
  void join(const mux::account_id& by, std::string room, std::vector<std::string> via) {
    loop.post([this, by, room = std::move(room), via = std::move(via)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == by)
                account->join(room, via);
            },
            one.account);
    });
  }
  // Asked to be let in (Matrix's knock), with a reason where one is given.
  void knock(const mux::account_id& by, std::string room, std::vector<std::string> via, std::string reason) {
    loop.post([this, by, room = std::move(room), via = std::move(via), reason = std::move(reason)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == by)
                account->knock(room, via, reason);
            },
            one.account);
    });
  }
  // All the members of a room, from its server.
  void fetch_members(const mux::conversation_id& in) {
    loop.post([this, in] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == in.account)
                account->fetch_members(in.id);
            },
            one.account);
    });
  }
  // What a message carries, fetched by the account it is of, for `use`: a
  // picture's thumbnail at `size`, or all of a file where `size` is 0.
  void fetch_media(const mux::account_id& of, std::string source, mux::media_use_t use, int size) {
    loop.post([this, of, source = std::move(source), use = std::move(use), size] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == of)
                account->fetch_media(source, use, size);
            },
            one.account);
    });
  }
  // A whole download stopped, by the account doing it.
  void cancel_media(const mux::account_id& of, std::string source) {
    loop.post([this, of, source = std::move(source)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == of)
                account->cancel_media(source);
            },
            one.account);
    });
  }
  // An avatar's picture, fetched by the account it is of, for `key`.
  void fetch_avatar(const mux::account_id& of, std::string source, std::string key) {
    loop.post([this, of, source = std::move(source), key = std::move(key)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == of)
                account->fetch_avatar(source, key);
            },
            one.account);
    });
  }
  // Older messages of a conversation, from `from` back.
  void load_context(const mux::conversation_id& in, std::string target) {
    loop.post([this, in, target = std::move(target)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == in.account)
                account->load_context(in.id, target);
            },
            one.account);
    });
  }
  void load_newer(const mux::conversation_id& in, std::string from) {
    loop.post([this, in, from = std::move(from)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == in.account)
                account->load_newer(in.id, from);
            },
            one.account);
    });
  }
  void load_older(const mux::conversation_id& in, std::string from) {
    loop.post([this, in, from = std::move(from)] {
      for (auto& one : accounts)
        splice::visit(
            [&](auto& account) {
              if (account->id() == in.account)
                account->load_older(in.id, from);
            },
            one.account);
    });
  }
  void leave(const mux::conversation_id& in) {
    loop.post([this, in] {
      for (auto& one : accounts)
        splice::visit(
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
        splice::visit([](auto& account) { account->stop(); }, one.account);
      }
      loop.stop();
    });
  }
};

}  // namespace mux::app
