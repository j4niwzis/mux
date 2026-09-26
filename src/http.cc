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

struct response {
  int status = 0;
  std::string body;
  // What Retry-After said, in milliseconds, where it said a number of
  // seconds (RFC 9110, 10.2.3).
  std::optional<std::int64_t> retry_after_ms;
};

// One connection to one host, used by one fiber at a time.
class connection {
 public:
  connection(net::loop& owner, net::tls& settings, url where)
      : owner_(&owner), tls_(&settings), where_(std::move(where)) {}
  connection(const connection&) = delete;
  connection& operator=(const connection&) = delete;

  const url& where() const noexcept { return where_; }

  // A request and its response. `target` is under the service's path; the
  // bearer token goes in Authorization where there is one. A connection the
  // server closed since the last request is opened again, once.
  response request(std::string_view method, std::string_view target, std::string_view body = {},
                   std::optional<std::string_view> bearer = std::nullopt,
                   std::chrono::seconds timeout = std::chrono::seconds(60)) {
    const bool reused = stream_.has_value();
    try {
      return once(method, target, body, bearer, timeout);
    } catch (const net::failure&) {
      if (!reused)
        throw;
      stream_.reset();
      return once(method, target, body, bearer, timeout);
    }
  }

  void close() {
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
    beast::get_lowest_layer(*stream_).socket() = net::connect(*owner_, where_.host, where_.port);
    beast::get_lowest_layer(*stream_).expires_after(std::chrono::seconds(30));
    const auto [shaken] = owner_->await<>([&](auto done) {
      stream_->async_handshake(asio::ssl::stream_base::client, std::move(done));
    });
    if (shaken) {
      stream_.reset();
      throw net::failure("TLS with " + where_.host, shaken);
    }
  }

  response once(std::string_view method, std::string_view target, std::string_view body,
                std::optional<std::string_view> bearer, std::chrono::seconds timeout) {
    if (!stream_)
      open();
    beast::http::request<beast::http::string_body> out;
    out.method(beast::http::string_to_verb(method));
    out.target(where_.path + std::string(target));
    out.version(11);
    out.set(beast::http::field::host,
            where_.port == 443 ? where_.host : where_.host + ":" + std::to_string(where_.port));
    out.set(beast::http::field::user_agent, "mux");
    out.set(beast::http::field::accept, "application/json");
    if (bearer)
      out.set(beast::http::field::authorization, "Bearer " + std::string(*bearer));
    if (!body.empty() || method == "POST" || method == "PUT") {
      out.set(beast::http::field::content_type, "application/json");
      out.body() = std::string(body);
    }
    out.prepare_payload();
    beast::get_lowest_layer(*stream_).expires_after(timeout);
    const auto [sent, sent_bytes] = owner_->await<std::size_t>([&](auto done) {
      beast::http::async_write(*stream_, out, std::move(done));
    });
    if (sent) {
      stream_.reset();
      throw net::failure("sending to " + where_.host, sent);
    }
    beast::http::response_parser<beast::http::string_body> in;
    in.body_limit(64 * 1024 * 1024);  // a first sync can be large
    const auto [read, read_bytes] = owner_->await<std::size_t>([&](auto done) {
      beast::http::async_read(*stream_, buffer_, in, std::move(done));
    });
    if (read) {
      stream_.reset();
      throw net::failure("reading from " + where_.host, read);
    }
    auto got = in.release();
    response made{static_cast<int>(got.result_int()), std::move(got.body()), std::nullopt};
    if (const auto after = got.find(beast::http::field::retry_after); after != got.end()) {
      std::int64_t seconds = 0;
      const std::string_view text = after->value();
      if (std::from_chars(text.data(), text.data() + text.size(), seconds).ec == std::errc{})
        made.retry_after_ms = seconds * 1000;
    }
    if (!got.keep_alive())
      stream_.reset();
    return made;
  }

  net::loop* owner_;
  net::tls* tls_;
  url where_;
  std::optional<stream_type> stream_;
  beast::flat_buffer buffer_;
};

}  // namespace mux::http
