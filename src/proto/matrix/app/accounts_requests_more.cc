// SPDX-License-Identifier: AGPL-3.0-only
// The Matrix account as the program runs it: the members defined in one of
// its requests partitions (client/requests_more.cc), instantiated here alone -- one
// of six units the account is built in, in parallel (see network.cc). The
// three partitions were one unit, and took over a minute.
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
import mux.proto.matrix.requests;
import loom.cs.keys;
import loom.crypto;

namespace mux::proto::matrix::client {
template void account<mux::app::post_change>::catch_up(std::string room, std::string from, std::string until);
template void account<mux::app::post_change>::preview_room(std::string room, std::vector<std::string> via);
template void account<mux::app::post_change>::fetch_profile(std::string user);
template void account<mux::app::post_change>::set_profile(user_profile profile);
template void account<mux::app::post_change>::upload_profile_banner(std::string bytes, std::string name, std::string mime);
template void account<mux::app::post_change>::remove_profile_banner();
template void account<mux::app::post_change>::fetch_presence(std::string user);
template void account<mux::app::post_change>::set_presence(presence now);
template void account<mux::app::post_change>::list_sessions();
template void account<mux::app::post_change>::rename_session(std::string device, std::string name);
template void account<mux::app::post_change>::sign_out_sessions(std::vector<std::string> devices, std::string password);
template void account<mux::app::post_change>::continue_uia();
template account<mux::app::post_change>::uia_way_t account<mux::app::post_change>::uia_answer(const loom::error& said, const std::string& given);
template void account<mux::app::post_change>::finish_cross_signing(const crypto::cross_signing_secrets& secrets, const std::string& master_pub, const std::string& self_pub);
template void account<mux::app::post_change>::create_group(std::string name);
template void account<mux::app::post_change>::forward(std::string from, std::vector<forward_source> events, std::string to);
template void account<mux::app::post_change>::fetch_quoted(std::string room, std::string target);
template void account<mux::app::post_change>::load_context(std::string room, std::string target);
template void account<mux::app::post_change>::load_newer(std::string room, std::string from);
template void account<mux::app::post_change>::fetch_avatar(std::string source, std::string of);
template void account<mux::app::post_change>::knock(std::string room, std::vector<std::string> via, std::string reason);
// Cross-signing set up with the password, and restored with the recovery key:
// what they call -- the backup made and stored, devices and people signed --
// is instantiated through them.
template void account<mux::app::post_change>::setup_cross_signing(std::string password, bool reset);
template void account<mux::app::post_change>::restore_cross_signing(std::string recovery);
// What the members above call, defined in the same partition: made here
// too, for nothing instantiates them elsewhere.
template std::optional<std::string> account<mux::app::post_change>::make_backup(const crypto::cross_signing_secrets& secrets);
template void account<mux::app::post_change>::upload_backup();
template std::size_t account<mux::app::post_change>::restore_backup(const std::string& secret);
template std::optional<std::string> account<mux::app::post_change>::store_secrets(const crypto::cross_signing_secrets& secrets, const std::optional<std::string>& backup_secret);
template void account<mux::app::post_change>::cross_sign_device(const loom::cs::query_keys::response_t::device_information_t& info);
template void account<mux::app::post_change>::cross_sign_user(const std::string& user, const loom::cs::query_keys::response_t::cross_signing_key_t& master);
}  // namespace mux::proto::matrix::client
