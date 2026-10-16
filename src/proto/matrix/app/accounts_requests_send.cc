// SPDX-License-Identifier: AGPL-3.0-only
// The Matrix account as the program runs it: the members defined in one of
// its requests partitions (client/requests_send.cc), instantiated here alone -- one
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
template void account<mux::app::post_change>::list_threads(std::string room);
template void account<mux::app::post_change>::load_thread(std::string room, std::string root);
template void account<mux::app::post_change>::send_in_thread(std::string room, std::string body, std::string root, std::string latest, std::optional<std::string> reply_to, std::vector<styled_run> styles);
template void account<mux::app::post_change>::edit(std::string room, std::string event, std::string text, std::vector<styled_run> styles);
template void account<mux::app::post_change>::edit_caption(std::string room, std::string event, std::string caption, mux::attachment picture);
template void account<mux::app::post_change>::remove(std::string room, std::string event);
template void account<mux::app::post_change>::react(std::string room, std::string target, std::string key, bool on, std::optional<std::string> shortcode);
template void account<mux::app::post_change>::pin(std::string room, std::string target, bool on);
template void account<mux::app::post_change>::leave(std::string room);
template void account<mux::app::post_change>::send(std::string room, std::string body, std::optional<std::string> reply_to, std::vector<mention> mentions, std::vector<styled_run> styles);
template void account<mux::app::post_change>::typing(std::string room, bool on);
template void account<mux::app::post_change>::join(std::string room, std::vector<std::string> via);
template void account<mux::app::post_change>::fetch_members(std::string room);
template void account<mux::app::post_change>::reset_backup();
template void account<mux::app::post_change>::delete_backup();
template void account<mux::app::post_change>::sign_out_unverified(std::string password);
template void account<mux::app::post_change>::send_text(const conversation_id& in, const std::string& room, const std::string& txn, knot::raw body, std::optional<knot::raw> relates_to);
}  // namespace mux::proto::matrix::client
