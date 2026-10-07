// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix.make -- A Matrix account's client, made from what the
// account keeps: make_account(kept, ...), found by ADL where the program
// starts accounts. Its type is what the program holds it as.
export module mux.proto.matrix.make;

import std;
import mux.vault;
import mux.core;
import mux.config;
import mux.net;
import mux.proto.matrix.client;
import mux.proto.kept;

export namespace mux::proto::matrix {

template <class Sink>
[[nodiscard]] std::unique_ptr<::mux::proto::matrix::client::account<Sink>,
                              typename ::mux::account_deleter_of<Sink, ::mux::proto::matrix::client::account<Sink>>::type>
make_account(const kept& saved, ::mux::net::loop& loop,
                                                                         ::mux::net::tls& tls, ::mux::vault::vault& vault,
                                                                         std::optional<::mux::net::proxy> via, Sink sink) {
  ::mux::proto::matrix::client::settings how{.user_id = saved.user_id,
                              .password = saved.password,
                              .homeserver = saved.homeserver,
                              .device_name = saved.device_name,
                              .proxy = std::move(via),
                              .access_token = saved.access_token,
                              .device_id = saved.device_id,
                              // Named by the user ID with what a file name cannot hold put
                              // aside: ':' is not one on Windows.
                              .crypto_store = ::mux::config::state_path("crypto") /
                                              ((std::ranges::to<std::string>(std::views::transform(saved.user_id, [](char c) {
                                                  return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_' ? c : '_';
                                                }))) + ".json"),
                              .only_verified = saved.only_verified.value_or(false),
                              .mentions_shared = saved.mentions_shared.value_or(false),
                              .mentions_sealed = saved.mentions_sealed.value_or(false),
                              .vault = &vault,
                              .create = saved.create.value_or(false) && !saved.access_token,
                              .registration_token = saved.registration_token,
                              .accept_terms = saved.accept_terms.value_or(false),
                              .oauth = saved.oauth.value_or(false),
                              .oauth_client_id = saved.oauth_client_id,
                              .refresh_token = saved.refresh_token};
  // Held with the deleter the sink names: made here, let go where it says.
  using account_t = ::mux::proto::matrix::client::account<Sink>;
  return std::unique_ptr<account_t, typename ::mux::account_deleter_of<Sink, account_t>::type>(
      new account_t(loop, tls, std::move(how), std::move(sink)));
}

}  // namespace mux::proto::matrix
