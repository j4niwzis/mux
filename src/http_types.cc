// SPDX-License-Identifier: AGPL-3.0-only
export module mux.http:types;
import std;
import mux.net;
export namespace mux::http {
namespace asio = net::asio;
using error_code = net::error_code;
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

}
