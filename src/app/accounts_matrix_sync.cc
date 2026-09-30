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
import mux.matrix;

namespace mux::matrix {
template void account<mux::app::post_change>::say(connection_t state);
template auto account<mux::app::post_change>::homeserver() -> std::optional<http::url>;
template void account<mux::app::post_change>::run();
template auto account<mux::app::post_change>::kept_file() const -> std::filesystem::path;
template void account<mux::app::post_change>::save_kept() const;
template void account<mux::app::post_change>::load_kept();
template void account<mux::app::post_change>::tell(const loom::cs::sync::response& got);
template auto account<mux::app::post_change>::avatar_of(const std::string& room, const loom::client::joined_room& kept) const -> std::optional<std::string>;
template auto account<mux::app::post_change>::name_of(const std::string& room, const loom::client::joined_room& kept) -> std::string;
template auto account<mux::app::post_change>::direct(const std::string& room) const -> bool;
template void account<mux::app::post_change>::conversation(const conversation_id& in, const loom::client::joined_room& kept);
template auto account<mux::app::post_change>::emotes_of(const loom::client::joined_room& kept, bool stickers) const -> std::vector<mux::emote>;
template auto account<mux::app::post_change>::emotes_in(const std::string& room) const -> std::vector<mux::emote>;
template auto account<mux::app::post_change>::pinned_of(const loom::client::joined_room& kept) -> std::vector<std::string>;
template auto account<mux::app::post_change>::space(const loom::client::joined_room& kept) -> bool;
template auto account<mux::app::post_change>::children_of(const loom::client::joined_room& kept) -> std::vector<std::string>;
template void account<mux::app::post_change>::members(const conversation_id& in, const loom::client::joined_room& kept);
}  // namespace mux::matrix
