// SPDX-License-Identifier: AGPL-3.0-only
export module mux.http:types;
import std;
import mux.net;
export namespace mux::http {
namespace asio = net::asio;
using error_code = net::error_code;
// How a service is spoken to: over TLS, or in the clear (a server on a home
// network, a test one) -- each with its own port where none is said.
namespace scheme {
struct https {
  static constexpr std::string_view word = "https";
  static constexpr std::uint16_t port = 443;
  friend bool operator==(https, https) = default;
};
struct http {
  static constexpr std::string_view word = "http";
  static constexpr std::uint16_t port = 80;
  friend bool operator==(http, http) = default;
};
}  // namespace scheme
using scheme_t = std::variant<scheme::https, scheme::http>;

// Where an HTTP service is: how it is spoken to, its host, its port, and the
// path everything is under. "https://matrix.example.org:8448/prefix/",
// "http://192.168.1.2:8008" and "example.org" (https) alike.
struct url {
  scheme_t scheme = scheme::https{};
  std::string host;
  std::uint16_t port = 443;
  std::string path;  // without its trailing slash; empty for the root

  static std::optional<url> parse(std::string_view text) {
    url made;
    if (text.starts_with("https://"))
      text.remove_prefix(8);
    else if (text.starts_with("http://")) {
      text.remove_prefix(7);
      made.scheme = scheme::http{};
      made.port = scheme::http::port;
    } else if (text.find("://") != std::string_view::npos)
      return std::nullopt;  // no other scheme is spoken here
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
  // Whether the port is the scheme's own: left out of what is written.
  [[nodiscard]] bool default_port() const {
    return std::visit([&](auto how) { return port == decltype(how)::port; }, scheme);
  }
  // Its host as a URL or a Host header writes it: an IPv6 literal in
  // brackets; and its port, where it is not the scheme's own.
  [[nodiscard]] std::string authority() const {
    const std::string named = host.contains(':') ? "[" + host + "]" : host;
    return default_port() ? named : std::format("{}:{}", named, port);
  }
  // "https://host[:port]", without the path.
  [[nodiscard]] std::string origin() const {
    return std::format("{}://{}", std::visit([](auto how) { return decltype(how)::word; }, scheme), authority());
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

}
