// SPDX-License-Identifier: AGPL-3.0-only
// The Matrix account as the program runs it: the members defined in its
// media partition, instantiated here alone -- one of four units the account
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

namespace mux::proto::matrix::client {
template void account<mux::app::post_change>::fetch_media(std::string source, media_use_t use, int size, bool crop);
template std::expected<std::string, failure> account<mux::app::post_change>::upload_content(const std::string& bytes, std::string_view name, std::string_view mime);
template void account<mux::app::post_change>::upload_room_avatar(std::string room, std::string bytes, std::string name, std::string mime);
template void account<mux::app::post_change>::send_file(std::string room, std::string local, std::string bytes, std::string name, std::string mimetype, bool image, int width, int height, std::string caption, std::optional<std::string> reply_to, std::optional<mux::thread_place> thread, std::optional<mux::video_look> video);
}  // namespace mux::proto::matrix::client
