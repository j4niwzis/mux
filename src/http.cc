// SPDX-License-Identifier: AGPL-3.0-only
// mux.http: request pooling and exchange, on the platform transport.
module;
#include <boost/asio/error.hpp>
export module mux.http;
export import :types;
export import :connection;
import std;
import mux.core;
import mux.net;
export namespace mux::http {
// What a request is for: what the user waits on -- a message sent, a page
// of history, a setting saved -- or what fills in behind it: avatars,
// thumbnails, downloads.
enum class lane : std::uint8_t { user, background };

// Several connections to one host, opened as they are needed, up to
// `most`: a request takes one that no other request holds, and waits only
// while all of them are held. What an account asks at once -- history
// paged back, receipts, messages sent -- goes at once.
//
// What fills in behind holds all but one of them at most, and waits behind
// what the user waits on: a message sent while a room list's avatars come
// in went, in one line, behind every one of them -- minutes of "sending".
class pool {
 public:
  pool(net::loop& owner, net::tls& settings, url where, std::optional<net::proxy> via = std::nullopt,
       std::size_t most = 4)
      : owner_(&owner), tls_(&settings), where_(std::move(where)), via_(std::move(via)), most_(std::max<std::size_t>(1, most)) {}
  pool(const pool&) = delete;
  pool& operator=(const pool&) = delete;

  const url& where() const noexcept { return where_; }

  template <class Progress = no_progress>
  response request(std::string_view method, std::string_view target, std::string_view body = {},
                   std::optional<std::string_view> bearer = std::nullopt,
                   std::chrono::seconds timeout = std::chrono::seconds(60), std::string_view type = {},
                   const Progress* progress = nullptr, lane way = lane::user) {
    // OAuth refresh can replace the account's token while this request
    // waits for a connection. Keep the credentials this request received.
    const auto authorization = bearer.transform([](std::string_view value) { return std::string(value); });
    if (authorization)
      bearer = std::string_view(*authorization);
    connection* free = this->take(way);
    if (free == nullptr) {
      // Counted while it waits: the pool is not let go under it (drain).
      struct waiting_while {
        pool& of;
        explicit waiting_while(pool& on) : of(on) { ++of.waiters_; }
        ~waiting_while() { --of.waiters_; }
      } const counted{*this};
      // A long wait for a connection said: "sending" while every
      // connection is held shows here.
      const auto began = std::chrono::steady_clock::now();
      const struct wait_said {
        pool& of;
        lane way;
        std::chrono::steady_clock::time_point began;
        ~wait_said() {
          const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began).count();
          if (ms > 1000)
            log_line(of.where_.host, std::format("waited {} ms for a connection ({} lane; {} in flight, {} of them background)", ms,
                                                 way == lane::user ? "user" : "background", of.in_flight_, of.background_));
        }
      } said{*this, way, began};
      while (free == nullptr) {
        if (closed_)
          throw net::failure("asking " + where_.host + " (the account stopped)", asio::error::operation_aborted);
        auto& queue = this->queue_of(way);
        if (std::ranges::find(queue, owner_->current()) == queue.end())
          queue.push_back(owner_->current());
        owner_->park();
        free = this->take(way);
      }
    }
    // Whatever becomes of the request, the next in line is told a
    // connection is free.
    ++in_flight_;
    if (way == lane::background)
      ++background_;
    struct next_in_line {
      pool& of;
      lane way;
      ~next_in_line() {
        --of.in_flight_;
        if (way == lane::background)
          --of.background_;
        of.wake_next();
        of.release_closed();
      }
    } const told{*this, way};
    return free->request(method, target, body, bearer, timeout, type, progress);
  }

  void close() {
    for (connection& one : all_)
      one.close();
  }

  // Stop taking requests, wake waiters, and cut short active connections.
  // This does not park, so it can also run during exception unwinding.
  void cancel() {
    if (std::exchange(closed_, true))
      return;
    for (auto* queue : {&waiting_user_, &waiting_background_}) {
      for (const net::loop::handle one : *queue)
        owner_->wake(one);
      queue->clear();
    }
    for (connection& one : all_)
      one.abort();
    release_closed();
  }
  // Wait for borrowers to leave before a normal return from the account.
  void drain() {
    cancel();
    while (in_flight_ > 0 || waiters_ > 0)
      owner_->sleep(std::chrono::milliseconds(20));
  }

 private:
  // A closed pool remains valid for borrowers, but its I/O objects must
  // leave while the loop is still alive, after the last active request.
  void release_closed() {
    if (closed_ && in_flight_ == 0)
      all_.clear();
  }
  // One no request holds, or a new one where there is room for it -- for
  // what fills in behind, never the last one.
  connection* take(lane way) {
    if (closed_)
      return nullptr;
    if (way == lane::background && most_ > 1 && background_ + 1 >= most_)
      return nullptr;
    for (connection& one : all_)
      if (!one.busy())
        return &one;
    if (all_.size() < most_)
      return &all_.emplace_back(*owner_, *tls_, where_, via_);
    return nullptr;
  }
  std::deque<net::loop::handle>& queue_of(lane way) {
    return way == lane::user ? waiting_user_ : waiting_background_;
  }
  // A connection free: what the user waits on first.
  void wake_next() {
    auto& queue = !waiting_user_.empty() ? waiting_user_ : waiting_background_;
    if (queue.empty())
      return;
    const net::loop::handle next = queue.front();
    queue.pop_front();
    owner_->wake(next);
  }

  net::loop* owner_;
  net::tls* tls_;
  url where_;
  std::optional<net::proxy> via_;
  std::size_t most_;
  std::deque<connection> all_;
  std::deque<net::loop::handle> waiting_user_;
  std::deque<net::loop::handle> waiting_background_;
  std::size_t in_flight_ = 0, background_ = 0, waiters_ = 0;
  bool closed_ = false;
};

// One exchange, made once, here: what a program calls instead of the
// request templates where it would rather not have them inlined into each
// of its callers (outside a release build, a protocol client's endpoints).
// The lane a request goes in, where it goes through a pool: what the user
// waits on, or what fills in behind -- one connection's own has none.
response exchange(pool& over, std::string_view method, std::string_view target, std::string_view body,
                  std::optional<std::string_view> bearer, std::chrono::seconds timeout, std::string_view type = {},
                  lane way = lane::user) {
  return over.request(method, target, body, bearer, timeout, type, static_cast<const no_progress*>(nullptr), way);
}
response exchange(connection& over, std::string_view method, std::string_view target, std::string_view body,
                  std::optional<std::string_view> bearer, std::chrono::seconds timeout, std::string_view type = {},
                  lane = lane::user) {
  return over.request(method, target, body, bearer, timeout, type);
}

}  // namespace mux::http
