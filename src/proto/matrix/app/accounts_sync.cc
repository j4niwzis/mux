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
template void account<mux::app::post_change>::save_kept() const;
template void account<mux::app::post_change>::load_kept();
template void account<mux::app::post_change>::tell(const loom::cs::sync::response& got);
template auto account<mux::app::post_change>::avatar_of(const std::string& room, const loom::client::joined_room& kept) const -> std::optional<std::string>;
template auto account<mux::app::post_change>::direct(const std::string& room) const -> bool;
template void account<mux::app::post_change>::conversation(const conversation_id& in, const loom::client::joined_room& kept);
template auto account<mux::app::post_change>::emotes_of(const loom::client::joined_room& kept, bool stickers) const -> std::vector<mux::emote>;
template auto account<mux::app::post_change>::emotes_in(const std::string& room) const -> std::vector<mux::emote>;
template void account<mux::app::post_change>::members(const conversation_id& in, const loom::client::joined_room& kept);
// What the members above call, defined in the same partition: made here
// too, for nothing instantiates them elsewhere.
template void account<mux::app::post_change>::start_crypto();
template void account<mux::app::post_change>::upload_keys(std::int64_t on_server);
template bool account<mux::app::post_change>::send_plain(std::string type, const std::string& user, const std::string& device, knot::raw content);
template void account<mux::app::post_change>::verification_said(const crypto::sas_state& state, verification_step_t step);
template void account<mux::app::post_change>::cancel_verification(const std::string& txn, std::string code, std::string reason);
template void account<mux::app::post_change>::verify_start(std::string user, std::optional<std::string> device);
template void account<mux::app::post_change>::verification_in(const std::string& sender, const loom::ev::m_key_verification_request_content_t& content);
template void account<mux::app::post_change>::verify_accept(std::string txn);
template void account<mux::app::post_change>::verification_in(const std::string& sender, const loom::ev::m_key_verification_ready_content_t& content);
template void account<mux::app::post_change>::sas_start(crypto::sas_state& state);
template void account<mux::app::post_change>::verification_in(const std::string& sender, const loom::ev::m_key_verification_start_content_t& content);
template void account<mux::app::post_change>::verification_in(const std::string& sender, const loom::ev::m_key_verification_accept_content_t& content);
template void account<mux::app::post_change>::verification_in(const std::string& sender, const loom::ev::m_key_verification_key_content_t& content);
template void account<mux::app::post_change>::sas_show(crypto::sas_state& state);
template void account<mux::app::post_change>::verify_confirm(std::string txn, bool match);
template void account<mux::app::post_change>::sas_send_mac(crypto::sas_state& state);
template void account<mux::app::post_change>::verification_in(const std::string& sender, const loom::ev::m_key_verification_mac_content_t& content);
template void account<mux::app::post_change>::sas_check_mac(crypto::sas_state& state);
template void account<mux::app::post_change>::verification_in(const std::string& sender, const loom::ev::m_key_verification_cancel_content_t& content);
template void account<mux::app::post_change>::verification_request_in_room(const conversation_id& in, const loom::ev::timeline_event& one, const crypto::room_request_fields& fields);
template bool account<mux::app::post_change>::verification_in_room(const conversation_id& in, const loom::ev::timeline_event& one, placement_t where);
template void account<mux::app::post_change>::verify_cancel(std::string txn);
template void account<mux::app::post_change>::mend_session(const std::string& user, const std::string& curve25519);
template void account<mux::app::post_change>::upload_fallback_key();
template void account<mux::app::post_change>::crypto_answer(const loom::cs::sliding_sync::response_t& got);
template void account<mux::app::post_change>::crypto_answer_now(const loom::cs::sliding_sync::response_t& got);
template void account<mux::app::post_change>::export_room_keys(std::string path, std::string passphrase);
template void account<mux::app::post_change>::import_room_keys(std::string path, std::string passphrase);
template bool account<mux::app::post_change>::owns_key(const std::string& user, const std::string& curve25519);
template std::optional<std::string> account<mux::app::post_change>::share_room_key(const std::string& room);
template void account<mux::app::post_change>::vet_room_key(const crypto::room_key_offer& offer);
template bool account<mux::app::post_change>::encrypted_room(std::string_view room);
template void account<mux::app::post_change>::remember_encrypted(std::string_view room);
template void account<mux::app::post_change>::save_encrypted();
template std::optional<typename account<mux::app::post_change>::since_t> account<mux::app::post_change>::encrypted_by(std::string_view room, std::optional<since_t> seen);
template std::filesystem::path account<mux::app::post_change>::encrypted_rooms_file() const;
template void account<mux::app::post_change>::load_encrypted();
template void account<mux::app::post_change>::tell_trust(std::string user);
template void account<mux::app::post_change>::tell_devices(std::string user);
template void account<mux::app::post_change>::check_own_sessions();
template auto account<mux::app::post_change>::own_sessions_now() -> std::optional<std::vector<own_session>>;
template void account<mux::app::post_change>::set_only_verified(bool on);
template void account<mux::app::post_change>::set_mentions_sharing(bool shared, bool sealed);
template void account<mux::app::post_change>::share_marks_seen(std::string room, std::vector<std::string> seen);
template std::filesystem::path account<mux::app::post_change>::mentions_key_file() const;
template const std::optional<std::vector<std::uint8_t>>& account<mux::app::post_change>::mentions_key();
template void account<mux::app::post_change>::keep_mentions_key(std::vector<std::uint8_t> key);
template void account<mux::app::post_change>::mentions_from(const conversation_id& in, const loom::ev::net_mux_mentions_read_content_t& content);
template void account<mux::app::post_change>::accept_identity(std::string user);
template void account<mux::app::post_change>::withheld_in(const loom::ev::m_room_key_withheld_content_t& content);
template void account<mux::app::post_change>::request_secrets(const std::string& device);
template void account<mux::app::post_change>::secret_in(const crypto::secret_got& got);
template void account<mux::app::post_change>::secret_request_in(const std::string& sender, const loom::ev::m_secret_request_content_t& content);
}  // namespace mux::proto::matrix::client

#if defined(MUX_SPLIT_ACCOUNTS)
// Let go here, among its members made here: see app/sink.cc.
void mux::app::destroy_account(mux::proto::matrix::client::account<mux::app::post_change>* one) { delete one; }
#endif
