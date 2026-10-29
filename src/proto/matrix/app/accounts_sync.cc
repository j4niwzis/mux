// SPDX-License-Identifier: AGPL-3.0-only
// The Matrix account as the program runs it: the members defined in its
// sync partition, instantiated here alone -- one of four units the account
// is built in, in parallel (see network.cc).
// Part of mux.app.network, where the account is declared extern.
module mux.app.network;
import std;
import knot;
import loom.api;
import loom.ev;
import loom.state;
import mux.core;
import mux.config;
import mux.http;
import mux.net;
import mux.proto.matrix.client;
// The account as made, end-to-end encryption's where its plugin is on
// (account_of): what the account's members call through whole().
import mux.plugins.matrix;
import loom.cs.sliding_sync;
import loom.crypto;

namespace mux::proto::matrix::client {
// Defined in the class, but a member of a module's class is not inline: the
// extern template in network.cc leaves them to be made here, as the rest.
template account<mux::app::post_change>::account(mux::net::loop& loop, mux::net::tls& tls, settings how, mux::app::post_change sink);
template auto account<mux::app::post_change>::transaction() -> std::string;
template void account<mux::app::post_change>::cancel_media(std::string source);
template void account<mux::app::post_change>::say(connection_t state);
template auto account<mux::app::post_change>::homeserver() -> std::optional<http::url>;
template void account<mux::app::post_change>::run();
template auto account<mux::app::post_change>::kept_file() const -> std::filesystem::path;
template bool account<mux::app::post_change>::save_kept() const;
template void account<mux::app::post_change>::load_kept();
template void account<mux::app::post_change>::receive_notifications(const loom::ev::account_data_event& event);
template void account<mux::app::post_change>::tell(const loom::cs::sync::response& got);
template void account<mux::app::post_change>::tell_presence(const loom::cs::sync::response& got);
template auto account<mux::app::post_change>::avatar_of(const std::string& room, const loom::client::joined_room& kept) const -> std::optional<std::string>;
template auto account<mux::app::post_change>::direct(const std::string& room) const -> bool;
template void account<mux::app::post_change>::conversation(const conversation_id& in, const loom::client::joined_room& kept);
template auto account<mux::app::post_change>::emotes_of(const loom::client::joined_room& kept, bool stickers) const -> mux::emote_list;
template auto account<mux::app::post_change>::emotes_in(const std::string& room) const -> mux::emote_list;
template void account<mux::app::post_change>::members(const conversation_id& in, const loom::client::joined_room& kept);
// The account as made, and what it does where no plugin encrypts: made
// here too, for the extern template leaves them to be.
template account_of<mux::app::post_change>& account<mux::app::post_change>::whole();
template void account<mux::app::post_change>::start_crypto();
template void account<mux::app::post_change>::device_extensions(loom::cs::sliding_sync::body_t::extensions_t&);
template void account<mux::app::post_change>::crypto_answer(const loom::cs::sliding_sync::response_t&);
template bool account<mux::app::post_change>::encrypting() const;
template bool account<mux::app::post_change>::store_says_encrypted(std::string_view);
template void account<mux::app::post_change>::store_encrypted(std::string_view);
template proto::matrix::security_state account<mux::app::post_change>::security_now();
template void account<mux::app::post_change>::mentions_from(const conversation_id&, const loom::ev::net_mux_mentions_read_content_t&);
template bool account<mux::app::post_change>::verification_in_room(const conversation_id&, const loom::ev::timeline_event&, placement_t);
template void account<mux::app::post_change>::verification_request_in_room(const conversation_id&, const loom::ev::timeline_event&, const crypto::room_request_fields&);
template void account<mux::app::post_change>::without_encryption();
template void account<mux::app::post_change>::tell_trust(std::string);
template void account<mux::app::post_change>::tell_devices(std::string);
template void account<mux::app::post_change>::set_mentions_sharing(bool, bool);
template void account<mux::app::post_change>::share_marks_seen(std::string, std::vector<std::string>);
template void account<mux::app::post_change>::accept_identity(std::string);
template void account<mux::app::post_change>::verify_start(std::string, std::optional<std::string>);
template void account<mux::app::post_change>::verify_accept(std::string);
template void account<mux::app::post_change>::verify_confirm(std::string, bool);
template void account<mux::app::post_change>::verify_cancel(std::string);
template void account<mux::app::post_change>::setup_cross_signing(std::string, bool);
template void account<mux::app::post_change>::finish_cross_signing(const crypto::cross_signing_secrets&, const std::string&, const std::string&);
template void account<mux::app::post_change>::restore_cross_signing(std::string);
template void account<mux::app::post_change>::reset_backup();
template void account<mux::app::post_change>::delete_backup();
template void account<mux::app::post_change>::sign_out_unverified(std::string);
template void account<mux::app::post_change>::export_room_keys(std::string, std::string);
template void account<mux::app::post_change>::import_room_keys(std::string, std::string);
// What the members above call, defined in the same partition: made here
// too, for nothing instantiates them elsewhere.
template bool account<mux::app::post_change>::encrypted_room(std::string_view room);
template void account<mux::app::post_change>::remember_encrypted(std::string_view room);
template void account<mux::app::post_change>::save_encrypted();
template std::optional<typename account<mux::app::post_change>::since_t> account<mux::app::post_change>::encrypted_by(std::string_view room, std::optional<since_t> seen);
template std::filesystem::path account<mux::app::post_change>::encrypted_rooms_file() const;
template void account<mux::app::post_change>::load_encrypted();
template void account<mux::app::post_change>::set_only_verified(bool on);
}  // namespace mux::proto::matrix::client

#if defined(MUX_SPLIT_ACCOUNTS)
// Let go here, among its members made here: see app/sink.cc.
void mux::app::destroy_account(mux::proto::matrix::client::account_of<mux::app::post_change>* one) { delete one; }
#endif
