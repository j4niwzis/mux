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
import mux.proto.matrix.client;
// What the plugins hear of its events: found where they are read.
import mux.plugins.matrix;

namespace mux::proto::matrix::client {
template void account<mux::app::post_change>::event(const conversation_id& in, const loom::ev::timeline_event& one, placement_t where,
                                                     bool sealed);
template void account<mux::app::post_change>::encrypted(const conversation_id& in, const loom::ev::timeline_event& one, std::chrono::sys_time<std::chrono::milliseconds> at, placement_t where);
template void account<mux::app::post_change>::service(const conversation_id& in, const loom::ev::timeline_event& one, std::chrono::sys_time<std::chrono::milliseconds> at, placement_t where, std::string said, room_event_t kind, std::optional<std::string> html);
template auto account<mux::app::post_change>::name_in(const std::string& room, const std::string& user) const -> std::string;
template void account<mux::app::post_change>::done(const conversation_id& in, const loom::ev::timeline_event& one, event_type_t type, std::chrono::sys_time<std::chrono::milliseconds> at, placement_t where);
template void account<mux::app::post_change>::redaction(const conversation_id& in, const loom::ev::timeline_event& one,
                                                       std::chrono::sys_time<std::chrono::milliseconds> at, placement_t where);
template auto account<mux::app::post_change>::body_of(std::string plain, const std::optional<std::string>& format, const std::optional<std::string>& formatted_body) -> body;
// A message read, given to the model: defined in the class, and called from
// the events read here.
template void account<mux::app::post_change>::added(message made, placement_t where, bool sealed);
// What a plugin is told of an event it hears (heard_here), the flag that
// makes late news news (late_news_while), and this session's party: defined
// in the class, called from the events read here and from the plugins'
// units -- made here, as the extern template leaves them to be.
template std::string account<mux::app::post_change>::party() const;
template account<mux::app::post_change>::late_news_while::late_news_while(bool& flag, bool now);
template account<mux::app::post_change>::late_news_while::~late_news_while();
template bool account<mux::app::post_change>::heard_here::news() const;
template bool account<mux::app::post_change>::heard_here::history() const;
template bool account<mux::app::post_change>::heard_here::mine() const;
template std::string account<mux::app::post_change>::heard_here::party() const;
}  // namespace mux::proto::matrix::client
