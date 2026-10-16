// SPDX-License-Identifier: AGPL-3.0-only
// The Matrix account as the program runs it: the members defined in one of
// its requests partitions (client/requests.cc), instantiated here alone -- one
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
template auto account<mux::app::post_change>::id() const noexcept -> const account_id&;
template void account<mux::app::post_change>::start();
template void account<mux::app::post_change>::stop();
template void account<mux::app::post_change>::mark_read(std::string room, std::string event);
template void account<mux::app::post_change>::load_older(std::string room, std::string from);
template void account<mux::app::post_change>::manage(std::string room, room_action_t action);
template void account<mux::app::post_change>::change_room(std::string room, mux::proto::matrix::room_change_t change);
template void account<mux::app::post_change>::fetch_room_settings(std::string room);
template void account<mux::app::post_change>::create_direct(std::string user);
template void account<mux::app::post_change>::send_sticker(std::string room, mux::emote sticker, std::optional<std::string> reply_to);
template void account<mux::app::post_change>::view_source(std::string room, std::string event);
template void account<mux::app::post_change>::list_state(std::string room);
template void account<mux::app::post_change>::send_custom(std::string room, std::string type, std::optional<std::string> state_key, std::string json);
template void account<mux::app::post_change>::fetch_preview(std::string url);
template void account<mux::app::post_change>::search_directory(std::string server, std::string query, std::optional<std::string> since);
template void account<mux::app::post_change>::explore_space(std::string room);
template void account<mux::app::post_change>::follow(std::optional<std::string> room);
template void account<mux::app::post_change>::create_room(std::string name, std::string topic, bool open, std::string alias, bool federate,
                                                                  bool encrypted, mux::room_place place);
template void account<mux::app::post_change>::search_people(std::string term);
template void account<mux::app::post_change>::list_packs(std::optional<std::string> room);
template void account<mux::app::post_change>::save_pack(emote_pack pack);
template void account<mux::app::post_change>::adopt_pack(emote_pack pack);
template void account<mux::app::post_change>::delete_pack(emote_pack pack);
template void account<mux::app::post_change>::cancel_uia();
template std::string account<mux::app::post_change>::server_of(const std::string& room) const;
// Defined in the class, but not inline there -- a named module's class body
// makes nothing inline -- so made here, as the account is declared extern.
template std::expected<loom::cs::query_keys::response, failure> account<mux::app::post_change>::keys_of(const std::string& user);
template void account<mux::app::post_change>::cut_long_poll();
template void account<mux::app::post_change>::sync_now();
template void account<mux::app::post_change>::flush_notifications();
template bool account<mux::app::post_change>::save_notifications(std::optional<std::string> room, mux::notification_choices choices, mux::notification_choices effective);
template void account<mux::app::post_change>::set_notifications(std::optional<std::string> room, mux::notification_choices choices, mux::notification_choices effective);
template void account<mux::app::post_change>::set_pusher(std::optional<std::string> endpoint);
template void account<mux::app::post_change>::register_pusher();
}  // namespace mux::proto::matrix::client
