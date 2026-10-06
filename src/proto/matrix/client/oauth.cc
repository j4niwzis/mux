// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix.client:oauth -- Signing in in the browser, on the
// server's own page (Matrix's OAuth 2.0 API): what mux says of itself as a
// client (RFC 7591), the tokens the server gives (RFC 6749), PKCE (RFC
// 7636), and the forms and queries they travel in. The flow itself is the
// sync's (sync.cc), where the account logs in.
export module mux.proto.matrix.client:oauth;

import std;
import knot;
import tern.crypto;
import mux.http;
import mux.net;

export namespace mux::proto::matrix::client::oauth {

// What mux says of itself, registering as a client of the server's (RFC
// 7591, as MSC2966 has it): a native app, sent back to this machine.
struct client_metadata {
  std::string client_name;
  std::string client_uri;
  std::string application_type;
  std::vector<std::string> redirect_uris;
  std::vector<std::string> grant_types;
  std::vector<std::string> response_types;
  std::string token_endpoint_auth_method;
};
consteval auto json_schema(knot::type<client_metadata>) { return knot::schema<client_metadata>(); }
struct client_registered {
  std::string client_id;
  knot::raw rest;
};
consteval auto json_schema(knot::type<client_registered>) {
  return knot::schema<client_registered>().member<"rest">(knot::rest);
}
// What the token endpoint gives (RFC 6749, 5.1), or why not (5.2).
struct tokens {
  std::string access_token;
  std::optional<std::string> refresh_token;
  std::optional<std::int64_t> expires_in;
  knot::raw rest;
};
consteval auto json_schema(knot::type<tokens>) { return knot::schema<tokens>().member<"rest">(knot::rest); }
struct token_error {
  std::string error;
  std::optional<std::string> error_description;
  knot::raw rest;
};
consteval auto json_schema(knot::type<token_error>) { return knot::schema<token_error>().member<"rest">(knot::rest); }

// Text in a query or a form: RFC 3986's unreserved characters as they are,
// every other byte as %XX.
inline std::string escaped(std::string_view text) {
  return std::ranges::to<std::string>(std::views::join(std::views::transform(text, [](char c) {
           const auto byte = static_cast<unsigned char>(c);
           return std::isalnum(byte) || c == '-' || c == '.' || c == '_' || c == '~' ? std::string(1, c)
                                                                                     : std::format("%{:02X}", byte);
         })));
}
// And back: '+' a space, %XX its byte.
inline std::string unescaped(std::string_view text) {
  const std::string spaced = std::ranges::to<std::string>(std::views::transform(text, [](char c) { return c == '+' ? ' ' : c; }));
  return std::ranges::to<std::string>(std::views::join(std::views::transform(std::views::enumerate(std::views::split(spaced, '%')), [](const auto& numbered) {
           const auto& [at, piece] = numbered;
           const std::string_view part(piece.begin(), piece.end());
           unsigned value = 0;
           const bool hex = part.size() >= 2 && std::from_chars(part.data(), part.data() + 2, value, 16).ptr == part.data() + 2;
           return at == 0 ? std::string(part)
                  : hex   ? std::string(1, static_cast<char>(value)) + std::string(part.substr(2))
                          : "%" + std::string(part);
         })));
}
// A form's body, or a query: name=value, joined by '&'.
inline std::string form(std::initializer_list<std::pair<std::string_view, std::string_view>> fields) {
  return std::ranges::to<std::string>(std::views::join_with(std::views::transform(fields, [](const auto& one) { return std::string(one.first) + "=" + escaped(one.second); }), '&'));
}
// A query's value, by its name.
inline std::optional<std::string> query_value(std::string_view query, std::string_view name) {
  // Not const: a split is walked through itself.
  auto pairs = std::views::transform(std::views::split(query, '&'), [](const auto& piece) { return std::string_view(piece.begin(), piece.end()); });
  const auto found = std::ranges::find_if(pairs, [&](std::string_view one) {
    return one.starts_with(name) && one.size() > name.size() && one[name.size()] == '=';
  });
  if (found == pairs.end())
    return std::nullopt;
  return unescaped((*found).substr(name.size() + 1));
}

// Random text of a set of characters: PKCE's verifier, the state, a device.
inline constexpr std::string_view unreserved = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
inline constexpr std::string_view capitals = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
inline std::string random_text(std::size_t length, std::string_view alphabet = unreserved) {
  std::random_device entropy;
  std::uniform_int_distribution<std::size_t> pick(0, alphabet.size() - 1);
  return std::ranges::to<std::string>(std::views::transform(std::views::iota(std::size_t{0}, length), [&](std::size_t) { return alphabet[pick(entropy)]; }));
}
// PKCE's challenge (RFC 7636, 4.2): SHA-256 of the verifier, in unpadded
// URL-safe Base64.
inline std::string challenge_of(std::string_view verifier) {
  // Whole: the digest takes contiguous bytes.
  const auto bytes = std::ranges::to<std::vector<std::uint8_t>>(std::views::transform(verifier, [](char c) { return static_cast<std::uint8_t>(c); }));
  return std::ranges::to<std::string>(std::views::transform(std::views::filter(tern::crypto::base64_encode(tern::crypto::sha256::digest(bytes)), [](char c) { return c != '='; }), [](char c) { return c == '+' ? '-' : c == '/' ? '_' : c; }));
}

// A POST to an address of the server's sign-in service: its answer, or
// none where it could not be reached.
inline std::optional<http::response> post(net::loop& loop, net::tls& tls, const std::optional<net::proxy>& via,
                                          std::string_view endpoint, std::string_view body, std::string_view type) {
  const auto where = http::url::parse(endpoint);
  if (!where)
    return std::nullopt;
  http::connection to(loop, tls, *where, via);
  try {
    return to.request("POST", "", body, std::nullopt, std::chrono::seconds(60), type);
  } catch (const net::failure&) {
    return std::nullopt;
  }
}
// The token endpoint's answer, read: the tokens, or what it said is wrong.
inline std::expected<tokens, std::string> tokens_of(const std::optional<http::response>& got) {
  if (!got)
    return std::unexpected(std::string("the sign-in service did not answer"));
  if (got->status / 100 == 2) {
    if (auto read = knot::try_read<tokens>(got->body))
      return *read;
    return std::unexpected(std::string("the sign-in service's tokens were not understood"));
  }
  if (auto said = knot::try_read<token_error>(got->body))
    return std::unexpected(said->error + (said->error_description ? ": " + *said->error_description : std::string()));
  return std::unexpected(std::format("the sign-in service answered HTTP {}", got->status));
}
inline constexpr std::string_view form_type = "application/x-www-form-urlencoded";
// The session renewed with the refresh token (RFC 6749, 6).
inline std::expected<tokens, std::string> refreshed(net::loop& loop, net::tls& tls, const std::optional<net::proxy>& via,
                                                    std::string_view token_endpoint, std::string_view client_id,
                                                    std::string_view refresh_token) {
  return tokens_of(post(loop, tls, via, token_endpoint,
                        form({{"grant_type", "refresh_token"}, {"refresh_token", refresh_token}, {"client_id", client_id}}),
                        form_type));
}
// The page the browser shows, sent back to mux.
inline std::string page(std::string_view said) {
  const std::string body = "<!DOCTYPE html><meta charset=\"utf-8\"><title>mux</title><p style=\"font:16px sans-serif;margin:3em\">" +
                           std::string(said) + "</p>";
  return std::format("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: {}\r\n\r\n{}",
                     body.size(), body);
}

}  // namespace mux::proto::matrix::client::oauth
