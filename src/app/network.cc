// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.network: The accounts, on their loop of their own thread, and the mailbox to the window.
export module mux.app.network;

import std;
import mux.core;
import mux.config;
import mux.net;
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
    return mux::net::proxy{.kind = std::visit(mux::overloaded{[](mux::config::proxy_kind::socks5) {
                                                                 return mux::net::proxy_kind_t{mux::net::proxy_kind::socks5{}};
                                                               },
                                                               [](mux::config::proxy_kind::http) {
                                                                 return mux::net::proxy_kind_t{mux::net::proxy_kind::http{}};
                                                               }},
                                              mux::config::proxy_kind_of(kept->kind)),
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
                              .proxy = std::move(via),
                              .access_token = saved.access_token,
                              .device_id = saved.device_id};
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
  void send(const mux::conversation_id& to, std::string text, std::optional<std::string> reply_to = std::nullopt,
            std::vector<mux::mention> mentions = {}) {
    loop.post([this, to, text = std::move(text), reply_to = std::move(reply_to), mentions = std::move(mentions)] {
      for (auto& one : accounts)
        std::visit(
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
        std::visit(
            [&](auto& account) {
              if (account->id() == in.account)
                account->edit(in.id, id, text);
            },
            one.account);
    });
  }
  void remove_message(const mux::conversation_id& in, std::string id) {
    loop.post([this, in, id = std::move(id)] {
      for (auto& one : accounts)
        std::visit(
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
        std::visit(
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
                 std::optional<std::string> reply_to = std::nullopt) {
    loop.post([this, in, local = std::move(local), bytes = std::move(bytes), name = std::move(name),
               mimetype = std::move(mimetype), image, width, height, caption = std::move(caption),
               reply_to = std::move(reply_to)] {
      for (auto& one : accounts)
        std::visit(
            [&](auto& account) {
              if (account->id() == in.account)
                account->send_file(in.id, local, bytes, name, mimetype, image, width, height, caption, reply_to);
            },
            one.account);
    });
  }
  // A reaction to a message put or taken back, by the account it is of.
  void react(const mux::conversation_id& in, std::string target, std::string key, bool on) {
    loop.post([this, in, target = std::move(target), key = std::move(key), on] {
      for (auto& one : accounts)
        std::visit(
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
        std::visit(
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
  void send_sticker(const mux::conversation_id& to, mux::emote sticker) {
    loop.post([this, to, sticker = std::move(sticker)] {
      for (auto& one : accounts)
        std::visit(
            [&](auto& account) {
              if (account->id() == to.account)
                account->send_sticker(to.id, sticker);
            },
            one.account);
    });
  }
  // A link's preview, asked of the account's server.
  void fetch_preview(const mux::account_id& by, std::string url) {
    loop.post([this, by, url = std::move(url)] {
      for (auto& one : accounts)
        std::visit(
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
        std::visit(
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
        std::visit(
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
        std::visit(
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
        std::visit(
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
        std::visit(
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
        std::visit(
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
        std::visit(
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
        std::visit(
            [&](auto& account) {
              if (account->id() == by)
                account->search_directory(server, query);
            },
            one.account);
    });
  }
  // A room made by the account named, as Element's Create room.
  void create_room(const mux::account_id& by, std::string name, std::string topic, bool open, std::string alias) {
    loop.post([this, by, name = std::move(name), topic = std::move(topic), open, alias = std::move(alias)] {
      for (auto& one : accounts)
        std::visit(
            [&](auto& account) {
              if (account->id() == by)
                account->create_room(name, topic, open, alias);
            },
            one.account);
    });
  }
  // A room not joined, looked up by the account named.
  void preview_room(const mux::account_id& by, std::string room, std::vector<std::string> via) {
    loop.post([this, by, room = std::move(room), via = std::move(via)] {
      for (auto& one : accounts)
        std::visit(
            [&](auto& account) {
              if (account->id() == by)
                account->preview_room(room, via);
            },
            one.account);
    });
  }
  // A room joined by the account named, through the servers named.
  void join(const mux::account_id& by, std::string room, std::vector<std::string> via) {
    loop.post([this, by, room = std::move(room), via = std::move(via)] {
      for (auto& one : accounts)
        std::visit(
            [&](auto& account) {
              if (account->id() == by)
                account->join(room, via);
            },
            one.account);
    });
  }
  // All the members of a room, from its server.
  void fetch_members(const mux::conversation_id& in) {
    loop.post([this, in] {
      for (auto& one : accounts)
        std::visit(
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
        std::visit(
            [&](auto& account) {
              if (account->id() == of)
                account->fetch_media(source, use, size);
            },
            one.account);
    });
  }
  // An avatar's picture, fetched by the account it is of, for `key`.
  void fetch_avatar(const mux::account_id& of, std::string source, std::string key) {
    loop.post([this, of, source = std::move(source), key = std::move(key)] {
      for (auto& one : accounts)
        std::visit(
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
        std::visit(
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
        std::visit(
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
        std::visit(
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

}  // namespace mux::app
