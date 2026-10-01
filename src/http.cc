// SPDX-License-Identifier: AGPL-3.0-only
// mux.http: HTTPS requests from a fiber, with Boost.Beast over mux.net's
// loop -- one connection kept open to a host between requests, as HTTP/1.1
// keeps it, and opened again where the server closed it.
module;

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <openssl/err.h>
#include <openssl/ssl.h>

export module mux.http;

import std;
import mux.core;
import mux.net;

export namespace mux::http {

namespace asio = boost::asio;
namespace beast = boost::beast;
using error_code = boost::system::error_code;

// Where an HTTPS service is: its host, its port, and the path everything is
// under. "https://matrix.example.org:8448/prefix/" and "example.org" alike.
struct url {
  std::string host;
  std::uint16_t port = 443;
  std::string path;  // without its trailing slash; empty for the root

  static std::optional<url> parse(std::string_view text) {
    url made;
    if (text.starts_with("https://"))
      text.remove_prefix(8);
    else if (text.find("://") != std::string_view::npos)
      return std::nullopt;  // plain http is not spoken here
    const auto slash = text.find('/');
    std::string_view authority = text.substr(0, slash);
    if (slash != std::string_view::npos)
      made.path = std::string(text.substr(slash));
    while (made.path.ends_with('/'))
      made.path.pop_back();
    if (authority.starts_with('[')) {  // an IPv6 literal
      const auto close = authority.find(']');
      if (close == std::string_view::npos)
        return std::nullopt;
      made.host = std::string(authority.substr(1, close - 1));
      authority.remove_prefix(close + 1);
      if (authority.starts_with(':'))
        authority = authority.substr(0);
      else
        authority = {};
    } else {
      const auto colon = authority.rfind(':');
      made.host = std::string(authority.substr(0, colon));
      authority = colon == std::string_view::npos ? std::string_view() : authority.substr(colon);
    }
    if (authority.starts_with(':')) {
      unsigned port = 0;
      const auto digits = authority.substr(1);
      const auto [end, bad] = std::from_chars(digits.data(), digits.data() + digits.size(), port);
      if (bad != std::errc{} || end != digits.data() + digits.size() || port == 0 || port > 65535)
        return std::nullopt;
      made.port = static_cast<std::uint16_t>(port);
    }
    if (made.host.empty())
      return std::nullopt;
    return made;
  }
};

// How far a response's body has come: bytes read, of how many where the
// server said. Called as it comes, for a download's progress to be shown.
// Given as a type: any callable of (read, total) saying whether to go on;
// none by default.
struct no_progress {
  bool operator()(std::size_t, std::optional<std::size_t>) const { return true; }
};

struct response {
  int status = 0;
  std::string body;
  // What Retry-After said, in milliseconds, where it said a number of
  // seconds (RFC 9110, 10.2.3).
  std::optional<std::int64_t> retry_after_ms;
  // Where a redirect points.
  std::optional<std::string> location;
};

// One connection to one host, used by one fiber at a time.
class connection {
 public:
  connection(net::loop& owner, net::tls& settings, url where, std::optional<net::proxy> via = std::nullopt)
      : owner_(&owner), tls_(&settings), where_(std::move(where)), via_(std::move(via)) {}
  connection(const connection&) = delete;
  connection& operator=(const connection&) = delete;

  const url& where() const noexcept { return where_; }
  // Whether a request is on it now.
  bool busy() const noexcept { return busy_; }

  // A request and its response. `target` is under the service's path; the
  // bearer token goes in Authorization where there is one. A connection the
  // server closed since the last request is opened again, once.
  // `type` is the body's, where it is not JSON: an upload's.
  // `accept` is what is asked for: JSON, unless a page or a picture is.
  template <class Progress = no_progress>
  response request(std::string_view method, std::string_view target, std::string_view body = {},
                   std::optional<std::string_view> bearer = std::nullopt,
                   std::chrono::seconds timeout = std::chrono::seconds(60), std::string_view type = {},
                   const Progress* progress = nullptr, std::string_view accept = "application/json") {
    const turn mine(*this);
    const bool reused = stream_.has_value();
    try {
      return once(method, target, body, bearer, timeout, type, progress, accept);
    } catch (const net::failure& failed) {
      // Stopped by its progress: not a closed connection, not tried again.
      if (!reused || failed.code == asio::error::operation_aborted)
        throw;
      stream_.reset();
      return once(method, target, body, bearer, timeout, type, progress, accept);
    }
  }

  void close() {
    const turn mine(*this);
    if (stream_) {
      (void)owner_->await<>([&](auto done) { stream_->async_shutdown(std::move(done)); });
      stream_.reset();
    }
  }

 private:
  using stream_type = beast::ssl_stream<beast::tcp_stream>;

  void open() {
    stream_.emplace(owner_->io(), tls_->context());
    if (!::SSL_set_tlsext_host_name(stream_->native_handle(), where_.host.c_str()))
      throw net::failure("naming " + where_.host, error_code(static_cast<int>(::ERR_get_error()),
                                                               asio::error::get_ssl_category()));
    stream_->set_verify_mode(asio::ssl::verify_peer);
    stream_->set_verify_callback(asio::ssl::host_name_verification(where_.host));
    const auto started = std::chrono::steady_clock::now();
    beast::get_lowest_layer(*stream_).socket() = net::connect(*owner_, via_, where_.host, where_.port);
    log_line(where_.host, std::format("connected{}, in {} ms", via_ ? std::format(" through {}:{}", via_->host, via_->port) : "",
                                      since_ms(started)));
    beast::get_lowest_layer(*stream_).expires_after(std::chrono::seconds(30));
    const auto [shaken] = owner_->await<>([&](auto done) {
      stream_->async_handshake(asio::ssl::stream_base::client, std::move(done));
    });
    if (shaken) {
      stream_.reset();
      throw net::failure("TLS with " + where_.host, shaken);
    }
    log_line(where_.host, std::format("TLS up, in {} ms", since_ms(started)));
  }
  // How long since then, in whole milliseconds.
  static long long since_ms(std::chrono::steady_clock::time_point from) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - from).count();
  }
  // A target as it is logged: its path, without the query that may carry
  // a token or a filter.
  static std::string_view path_of(std::string_view target) { return target.substr(0, target.find('?')); }

  template <class Progress = no_progress>
  response once(std::string_view method, std::string_view target, std::string_view body,
                std::optional<std::string_view> bearer, std::chrono::seconds timeout, std::string_view type = {},
                const Progress* progress = nullptr, std::string_view accept = "application/json") {
    if (!stream_)
      open();
    beast::http::request<beast::http::string_body> out;
    // The method, read into Beast's verb once; what follows asks the verb.
    const beast::http::verb verb = beast::http::string_to_verb(method);
    out.method(verb);
    out.target(where_.path + std::string(target));
    out.version(11);
    out.set(beast::http::field::host,
            where_.port == 443 ? where_.host : where_.host + ":" + std::to_string(where_.port));
    out.set(beast::http::field::user_agent, "mux");
    out.set(beast::http::field::accept, accept);
    if (bearer)
      out.set(beast::http::field::authorization, "Bearer " + std::string(*bearer));
    if (!body.empty() || verb == beast::http::verb::post || verb == beast::http::verb::put) {
      out.set(beast::http::field::content_type, type.empty() ? std::string_view("application/json") : type);
      out.body() = std::string(body);
    }
    out.prepare_payload();
    beast::get_lowest_layer(*stream_).expires_after(timeout);
    const auto asked_at = std::chrono::steady_clock::now();
    const auto [sent, sent_bytes] = owner_->await<std::size_t>([&](auto done) {
      beast::http::async_write(*stream_, out, std::move(done));
    });
    if (sent) {
      stream_.reset();
      throw net::failure("sending to " + where_.host, sent);
    }
    beast::http::response_parser<beast::http::string_body> in;
    in.body_limit(64 * 1024 * 1024);  // a first sync can be large
    if (!progress) {
      const auto [read, read_bytes] = owner_->await<std::size_t>([&](auto done) {
        beast::http::async_read(*stream_, buffer_, in, std::move(done));
      });
      if (read) {
        stream_.reset();
        throw net::failure("reading from " + where_.host, read);
      }
    } else {
      // Read in pieces, how far it has come said after each.
      const auto [header, header_bytes] = owner_->await<std::size_t>([&](auto done) {
        beast::http::async_read_header(*stream_, buffer_, in, std::move(done));
      });
      if (header) {
        stream_.reset();
        throw net::failure("reading from " + where_.host, header);
      }
      const std::optional<std::size_t> total =
          in.content_length() ? std::optional<std::size_t>(static_cast<std::size_t>(*in.content_length())) : std::nullopt;
      while (!in.is_done()) {
        const auto [read, read_bytes] = owner_->await<std::size_t>([&](auto done) {
          beast::http::async_read_some(*stream_, buffer_, in, std::move(done));
        });
        if (read) {
          stream_.reset();
          throw net::failure("reading from " + where_.host, read);
        }
        // Stopped by what watches it: the download let go, the connection
        // with it (it is mid-answer).
        if (!(*progress)(in.get().body().size(), total)) {
          stream_.reset();
          throw net::failure("reading from " + where_.host, asio::error::operation_aborted);
        }
      }
    }
    auto got = in.release();
    // Slow answers said, and what they were: a long wait shows where it was.
    if (const auto took = since_ms(asked_at); took > 2000 || got.body().size() > 256 * 1024)
      log_line(where_.host, std::format("{} {}: {} after {} ms, {} KB", method, path_of(target), got.result_int(), took,
                                        got.body().size() / 1024));
    response made{static_cast<int>(got.result_int()), std::move(got.body()), std::nullopt};
    if (const auto after = got.find(beast::http::field::retry_after); after != got.end()) {
      std::int64_t seconds = 0;
      const std::string_view text = after->value();
      if (std::from_chars(text.data(), text.data() + text.size(), seconds).ec == std::errc{})
        made.retry_after_ms = seconds * 1000;
    }
    if (const auto to = got.find(beast::http::field::location); to != got.end())
      made.location = std::string(to->value());
    if (!got.keep_alive())
      stream_.reset();
    return made;
  }

  // One request at a time on the stream: Beast allows one read and one
  // write in flight, and fibers of one account -- the history paged back,
  // a receipt, a message sent -- ask at once. Each waits its turn, in the
  // order they asked.
  struct turn {
    connection& of;
    explicit turn(connection& on) : of(on) {
      while (of.busy_) {
        if (std::ranges::find(of.waiting_, of.owner_->current()) == of.waiting_.end())
          of.waiting_.push_back(of.owner_->current());
        of.owner_->park();
      }
      of.busy_ = true;
    }
    turn(const turn&) = delete;
    turn& operator=(const turn&) = delete;
    ~turn() {
      of.busy_ = false;
      if (!of.waiting_.empty()) {
        const net::loop::handle next = of.waiting_.front();
        of.waiting_.pop_front();
        of.owner_->wake(next);
      }
    }
  };
  bool busy_ = false;
  std::deque<net::loop::handle> waiting_;

  net::loop* owner_;
  net::tls* tls_;
  url where_;
  std::optional<net::proxy> via_;
  std::optional<stream_type> stream_;
  beast::flat_buffer buffer_;
};

// Several connections to one host, opened as they are needed, up to
// `most`: a request takes one that no other request holds, and waits only
// while all of them are held. What an account asks at once -- history
// paged back, receipts, messages sent -- goes at once.
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
                   const Progress* progress = nullptr) {
    connection* free = this->take();
    while (free == nullptr) {
      if (std::ranges::find(waiting_, owner_->current()) == waiting_.end())
        waiting_.push_back(owner_->current());
      owner_->park();
      free = this->take();
    }
    // Whatever becomes of the request, the next in line is told a
    // connection is free.
    struct next_in_line {
      pool& of;
      ~next_in_line() {
        if (!of.waiting_.empty()) {
          const net::loop::handle next = of.waiting_.front();
          of.waiting_.pop_front();
          of.owner_->wake(next);
        }
      }
    } const told{*this};
    return free->request(method, target, body, bearer, timeout, type, progress);
  }

  void close() {
    for (connection& one : all_)
      one.close();
  }

 private:
  // One no request holds, or a new one where there is room for it.
  connection* take() {
    for (connection& one : all_)
      if (!one.busy())
        return &one;
    if (all_.size() < most_)
      return &all_.emplace_back(*owner_, *tls_, where_, via_);
    return nullptr;
  }

  net::loop* owner_;
  net::tls* tls_;
  url where_;
  std::optional<net::proxy> via_;
  std::size_t most_;
  std::deque<connection> all_;
  std::deque<net::loop::handle> waiting_;
};

// One exchange, made once, here: what a program calls instead of the
// request templates where it would rather not have them inlined into each
// of its callers (outside a release build, mux::matrix's endpoints).
response exchange(pool& over, std::string_view method, std::string_view target, std::string_view body,
                  std::optional<std::string_view> bearer, std::chrono::seconds timeout) {
  return over.request(method, target, body, bearer, timeout);
}
response exchange(connection& over, std::string_view method, std::string_view target, std::string_view body,
                  std::optional<std::string_view> bearer, std::chrono::seconds timeout) {
  return over.request(method, target, body, bearer, timeout);
}

}  // namespace mux::http
