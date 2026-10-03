// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.xmpp.client -- An XMPP account's client, made from what the
// account keeps: make_account(kept, ...), found by ADL where the program
// starts accounts. Its type is what the program holds it as.
export module mux.proto.xmpp.client;

import std;
import mux.core;
import mux.net;
import mux.xmpp;
import mux.proto.kept;

export namespace mux::proto::xmpp {

template <class Sink>
[[nodiscard]] std::unique_ptr<::mux::xmpp::account<Sink>> make_account(const kept& saved, ::mux::net::loop& loop,
                                                                       ::mux::net::tls& tls,
                                                                       std::optional<::mux::net::proxy> via, Sink sink) {
  ::mux::xmpp::settings how{.address = saved.address,
                            .password = saved.password,
                            .resource = saved.resource,
                            .host = saved.host,
                            .plain_without_tls = saved.plain_without_tls,
                            .proxy = std::move(via)};
  if (saved.port)
    how.port = static_cast<std::uint16_t>(*saved.port);
  return std::make_unique<::mux::xmpp::account<Sink>>(loop, tls, std::move(how), std::move(sink));
}

}  // namespace mux::proto::xmpp
