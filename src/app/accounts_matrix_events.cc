// SPDX-License-Identifier: AGPL-3.0-only
// The Matrix account as the program runs it: the members defined in its
// events partition, instantiated here alone -- one of four units the account
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
template void account<mux::app::post_change>::event(const conversation_id& in, const loom::ev::timeline_event& one, placement_t where,
                                                     bool sealed);
template void account<mux::app::post_change>::encrypted(const conversation_id& in, const loom::ev::timeline_event& one, std::chrono::sys_time<std::chrono::milliseconds> at, placement_t where);
template void account<mux::app::post_change>::service(const conversation_id& in, const loom::ev::timeline_event& one, std::chrono::sys_time<std::chrono::milliseconds> at, placement_t where, std::string said, room_event_t kind, std::optional<std::string> html);
template auto account<mux::app::post_change>::name_in(const std::string& room, const std::string& user) const -> std::string;
template void account<mux::app::post_change>::done(const conversation_id& in, const loom::ev::timeline_event& one, event_type_t type, std::chrono::sys_time<std::chrono::milliseconds> at, placement_t where);
template void account<mux::app::post_change>::redaction(const conversation_id& in, const loom::ev::timeline_event& one,
                                                       std::chrono::sys_time<std::chrono::milliseconds> at, placement_t where);
template auto account<mux::app::post_change>::body_of(std::string plain, const std::optional<std::string>& format, const std::optional<std::string>& formatted_body) -> body;
}  // namespace mux::matrix
