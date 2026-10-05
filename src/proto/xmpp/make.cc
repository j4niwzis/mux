// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.xmpp.make -- An XMPP account's client, made from what the
// account keeps: make_account(kept, ...), found by ADL where the program
// starts accounts. Its type is what the program holds it as.
export module mux.proto.xmpp.make;

import std;
import mux.vault;
import mux.core;
import mux.net;
import tern;
import mux.proto.xmpp.client;
import mux.proto.kept;

export namespace mux::proto::xmpp {

template <class Sink>
[[nodiscard]] std::unique_ptr<::mux::proto::xmpp::client::account<Sink>> make_account(const kept& saved, ::mux::net::loop& loop,
                                                                       ::mux::net::tls& tls, ::mux::vault::vault&,
                                                                       std::optional<::mux::net::proxy> via, Sink sink) {
  ::mux::proto::xmpp::client::settings how{.address = saved.address,
                            .password = saved.password,
                            .resource = saved.resource,
                            .host = saved.host,
                            .plain_without_tls = saved.plain_without_tls,
                            .proxy = std::move(via)};
  if (saved.port)
    how.port = static_cast<std::uint16_t>(*saved.port);
  if (saved.create.value_or(false))
    how.create = saved.answers.value_or(std::vector<registration_answer>{}) |
                 std::views::transform([](const registration_answer& one) {
                   return tern::registration::answer{.var = one.var, .value = one.value};
                 }) |
                 std::ranges::to<std::vector<tern::registration::answer>>();
  return std::make_unique<::mux::proto::xmpp::client::account<Sink>>(loop, tls, std::move(how), std::move(sink));
}

}  // namespace mux::proto::xmpp
