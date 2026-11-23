// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.irc.make -- An IRC account's client, made from what the account
// keeps: make_account(kept, ...), found by ADL where the program starts
// accounts. Its type is what the program holds it as.
export module mux.proto.irc.make;

import std;
import mux.vault;
import mux.core;
import mux.net;
import mux.proto.irc.client;
import mux.proto.kept;

export namespace mux::proto::irc {

template <class Sink>
[[nodiscard]] std::unique_ptr<::mux::proto::irc::client::account<Sink>,
                              typename ::mux::account_deleter_of<Sink, ::mux::proto::irc::client::account<Sink>>::type>
make_account(const kept& saved, ::mux::net::loop& loop, ::mux::net::tls& tls, ::mux::vault::vault&,
             std::optional<::mux::net::proxy> via, Sink sink) {
  // What check() let through reads: the account is not made without it.
  const auto where = target_of(saved.address).value_or(target{});
  ::mux::proto::irc::client::settings how{.address = saved.address,
                                          .where = where,
                                          .password = saved.password,
                                          .mechanism = ::mux::proto::irc::client::mechanism_of(saved.mechanism),
                                          .login = saved.login,
                                          .server_password = saved.server_password,
                                          .username = saved.username,
                                          .realname = saved.realname,
                                          .channels = saved.channels,
                                          .bouncer_network = saved.bouncer_network.value_or(""),
                                          .proxy = std::move(via)};
  // Held with the deleter the sink names: made here, let go where it says.
  using account_t = ::mux::proto::irc::client::account<Sink>;
  return std::unique_ptr<account_t, typename ::mux::account_deleter_of<Sink, account_t>::type>(
      new account_t(loop, tls, std::move(how), std::move(sink)));
}

}  // namespace mux::proto::irc
