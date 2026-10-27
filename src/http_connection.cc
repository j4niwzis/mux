// SPDX-License-Identifier: AGPL-3.0-only
module;

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <openssl/err.h>
#include <openssl/ssl.h>

export module mux.http:connection;
import std;
import mux.core;
import mux.net;
import :types;
export namespace mux::http {
namespace beast = boost::beast;
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
                   const Progress* progress = nullptr, std::string_view accept = "application/json",
                   std::span<const std::pair<std::string, std::string>> headers = {}) {
    const auto authorization = bearer.transform([](std::string_view value) { return std::string(value); });
    if (authorization)
      bearer = std::string_view(*authorization);
    const turn mine(*this);
    // Cut short by abort(): opened afresh, not the stream it left.
    if (std::exchange(aborted_, false))
      stream_.reset();
    // Idle a while: opened afresh too. A phone's network forgets a quiet
    // connection -- its NAT's mapping gone, the network changed -- and tells
    // neither end: a request on it was written into nothing and waited out
    // its whole timeout, a minute, before it was tried again on a new one.
    // That was a message "sending" for a minute or more, where every other
    // client sent at once. A handshake costs far less.
    if (stream_ && std::chrono::steady_clock::now() - last_done_ > kMostIdle) {
      log_line(where_.host, std::format("idle {} s: connecting afresh", since_ms(last_done_) / 1000));
      stream_.reset();
    }
    const bool reused = stream_.has_value();
    const struct done_at {
      connection& of;
      ~done_at() { of.last_done_ = std::chrono::steady_clock::now(); }
    } stamped{*this};
    try {
      return once(method, target, body, bearer, timeout, type, progress, accept, headers);
    } catch (const net::failure& failed) {
      // Stopped by its progress: not a closed connection, not tried again.
      if (!reused || failed.code == asio::error::operation_aborted)
        throw;
      stream_.reset();
      return once(method, target, body, bearer, timeout, type, progress, accept, headers);
    }
  }

  void close() {
    const turn mine(*this);
    if (stream_) {
      (void)owner_->await<>([&](auto done) { stream_->async_shutdown(std::move(done)); });
      stream_.reset();
    }
  }
  // The request on it now stopped where it is, from another fiber -- not
  // waiting its turn, as close() does: a long poll on a socket that died
  // while the machine slept is waited for until its timeout, minutes later.
  // It fails at once; the next request opens the connection again.
  void abort() {
    if (!stream_)
      return;
    aborted_ = true;
    beast::get_lowest_layer(*stream_).cancel();
    error_code ignored;
    beast::get_lowest_layer(*stream_).socket().close(ignored);
  }

 private:
  bool aborted_ = false;
  // How long a connection may lie unused and still be trusted; and when it
  // was last used.
  static constexpr std::chrono::seconds kMostIdle{20};
  std::chrono::steady_clock::time_point last_done_{};
  using stream_type = beast::ssl_stream<beast::tcp_stream>;

  void open() {
    // A new connection reads from nothing: what the last one left unread --
    // a long poll cut short mid-answer, a read that failed -- let go. Read
    // on as the start of this one's answer, it was "bad version" and the
    // account reconnecting.
    buffer_.consume(buffer_.size());
    stream_.emplace(owner_->io(), tls_->context());
    if (!::SSL_set_tlsext_host_name(stream_->native_handle(), where_.host.c_str()))
      throw net::failure("naming " + where_.host, error_code(static_cast<int>(::ERR_get_error()),
                                                               asio::error::get_ssl_category()));
    stream_->set_verify_mode(asio::ssl::verify_peer);
    stream_->set_verify_callback(net::peer_verification{where_.host});
    const auto started = std::chrono::steady_clock::now();
    beast::get_lowest_layer(*stream_).socket() = net::connect(*owner_, via_, where_.host, where_.port);
    log_line(where_.host, std::format("connected{}, in {} ms", via_ ? std::format(" through {}:{}", via_->host, via_->port) : "",
                                      since_ms(started)));
    beast::get_lowest_layer(*stream_).expires_after(std::chrono::seconds(30));
    const auto [shaken] = owner_->await<>([&](auto done) {
      stream_->async_handshake(asio::ssl::stream_base::client, std::move(done));
    });
    if (shaken) {
      const std::string reason = net::verify_reason(stream_->native_handle());
      stream_.reset();
      throw net::failure("TLS with " + where_.host + (reason.empty() ? std::string() : " (" + reason + ")"), shaken);
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
                const Progress* progress = nullptr, std::string_view accept = "application/json",
                   std::span<const std::pair<std::string, std::string>> headers = {}) {
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
    for (const auto& [name, value] : headers) out.insert(name, value);
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

}
