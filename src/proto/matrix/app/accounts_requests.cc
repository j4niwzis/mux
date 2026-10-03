// SPDX-License-Identifier: AGPL-3.0-only
// The Matrix account as the program runs it: the members defined in its
// requests partition, instantiated here alone -- one of four units the account
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
import mux.proto.matrix.requests;
import loom.cs.keys;
import mux.proto.matrix.crypto;

namespace mux::proto::matrix::client {
template auto account<mux::app::post_change>::id() const noexcept -> const account_id&;
template void account<mux::app::post_change>::start();
template void account<mux::app::post_change>::stop();
template void account<mux::app::post_change>::mark_read(std::string room, std::string event);
template void account<mux::app::post_change>::load_older(std::string room, std::string from);
template void account<mux::app::post_change>::manage(std::string room, room_action_t action);
template void account<mux::app::post_change>::change_room(std::string room, mux::proto::matrix::room_change_t change);
template void account<mux::app::post_change>::create_direct(std::string user);
template void account<mux::app::post_change>::send_sticker(std::string room, mux::emote sticker, std::optional<std::string> reply_to);
template void account<mux::app::post_change>::view_source(std::string room, std::string event);
template void account<mux::app::post_change>::list_state(std::string room);
template void account<mux::app::post_change>::send_custom(std::string room, std::string type, std::optional<std::string> state_key, std::string json);
template void account<mux::app::post_change>::fetch_preview(std::string url);
template void account<mux::app::post_change>::search_directory(std::string server, std::string query);
template void account<mux::app::post_change>::explore_space(std::string room);
template void account<mux::app::post_change>::follow(std::optional<std::string> room);
template void account<mux::app::post_change>::create_room(std::string name, std::string topic, bool open, std::string alias, bool federate,
                                                                  bool encrypted);
template void account<mux::app::post_change>::search_people(std::string term);
template void account<mux::app::post_change>::list_threads(std::string room);
template void account<mux::app::post_change>::load_thread(std::string room, std::string root);
template void account<mux::app::post_change>::send_in_thread(std::string room, std::string body, std::string root, std::string latest, std::optional<std::string> reply_to);
template void account<mux::app::post_change>::list_packs(std::optional<std::string> room);
template void account<mux::app::post_change>::save_pack(emote_pack pack);
template void account<mux::app::post_change>::delete_pack(emote_pack pack);
template void account<mux::app::post_change>::catch_up(std::string room, std::string from, std::string until);
template void account<mux::app::post_change>::preview_room(std::string room, std::vector<std::string> via);
template void account<mux::app::post_change>::fetch_profile(std::string user);
template void account<mux::app::post_change>::list_sessions();
template void account<mux::app::post_change>::rename_session(std::string device, std::string name);
template void account<mux::app::post_change>::sign_out_sessions(std::vector<std::string> devices, std::string password);
template void account<mux::app::post_change>::create_group(std::string name);
template void account<mux::app::post_change>::forward(std::string from, std::string event, std::string to);
template void account<mux::app::post_change>::fetch_quoted(std::string room, std::string target);
template void account<mux::app::post_change>::load_context(std::string room, std::string target);
template void account<mux::app::post_change>::load_newer(std::string room, std::string from);
template void account<mux::app::post_change>::fetch_avatar(std::string source, std::string of);
template void account<mux::app::post_change>::edit(std::string room, std::string event, std::string text);
template void account<mux::app::post_change>::edit_caption(std::string room, std::string event, std::string caption, mux::attachment picture);
template void account<mux::app::post_change>::remove(std::string room, std::string event);
template void account<mux::app::post_change>::react(std::string room, std::string target, std::string key, bool on);
template void account<mux::app::post_change>::pin(std::string room, std::string target, bool on);
template void account<mux::app::post_change>::leave(std::string room);
template void account<mux::app::post_change>::send(std::string room, std::string body, std::optional<std::string> reply_to, std::vector<mention> mentions);
template void account<mux::app::post_change>::typing(std::string room, bool on);
template void account<mux::app::post_change>::join(std::string room, std::vector<std::string> via);
template void account<mux::app::post_change>::knock(std::string room, std::vector<std::string> via, std::string reason);
template void account<mux::app::post_change>::fetch_members(std::string room);
// Cross-signing set up with the password, and restored with the recovery key:
// what they call -- the backup made and stored, devices and people signed --
// is instantiated through them.
template void account<mux::app::post_change>::setup_cross_signing(std::string password, bool reset);
template void account<mux::app::post_change>::reset_backup();
template void account<mux::app::post_change>::delete_backup();
template void account<mux::app::post_change>::sign_out_unverified(std::string password);
// Defined in the class, but not inline there -- a named module's class body
// makes nothing inline -- so made here, as the account is declared extern.
template std::expected<loom::cs::query_keys::response, failure> account<mux::app::post_change>::keys_of(const std::string& user);
template void account<mux::app::post_change>::send_text(const conversation_id& in, const std::string& room, const std::string& txn, knot::raw body);
template void account<mux::app::post_change>::restore_cross_signing(std::string recovery);
// What the members above call, defined in the same partition: made here
// too, for nothing instantiates them elsewhere.
template std::optional<std::string> account<mux::app::post_change>::make_backup(const crypto::cross_signing_secrets& secrets);
template void account<mux::app::post_change>::upload_backup();
template std::size_t account<mux::app::post_change>::restore_backup(const std::string& secret);
template std::optional<std::string> account<mux::app::post_change>::store_secrets(const crypto::cross_signing_secrets& secrets, const std::optional<std::string>& backup_secret);
template void account<mux::app::post_change>::cross_sign_device(const loom::cs::query_keys::response_t::device_information_t& info);
template void account<mux::app::post_change>::cross_sign_user(const std::string& user, const loom::cs::query_keys::response_t::cross_signing_key_t& master);
template void account<mux::app::post_change>::cut_long_poll();
template void account<mux::app::post_change>::sync_now();
template void account<mux::app::post_change>::set_pusher(std::optional<std::string> endpoint);
template void account<mux::app::post_change>::register_pusher();
}  // namespace mux::proto::matrix::client
