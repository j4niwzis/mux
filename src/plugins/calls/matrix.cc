// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.calls.matrix -- Calls over Matrix, the calls plugin's own: the
// m.call.* events an account reads, told to the program as the plugin's
// changes (call_signalled) -- as they happen only -- and its signals sent
// as them; the servers a call goes through asked of the account's server
// (/voip/turnServer: call_servers). Found where the account is made
// (mux.plugins.matrix): the Matrix client knows nothing of calls.
export module mux.plugin.calls.matrix;

import std;
import splice;
import mux.composition;
import loom.api;
import loom.ev;
import loom.cs.room_send;
import loom.cs.voip;
import mux.core;
import mux.proto.matrix.client;
import mux.plugin.calls.types;
export import mux.plugin.calls.changes;

export namespace mux::plugins::calls {

namespace heard_as = mux::proto::matrix::client::heard;
using heard_t = mux::proto::matrix::client::heard_t;

// A call's version as the event gives it, 0 or "1", read once.
inline call_version_t call_version_of(const spl::variant<std::string, std::int64_t>& wire) {
  return spl::visit(spl::overloaded{
      [](std::int64_t version) -> call_version_t {
        return version == 0 ? call_version_t{call_version::legacy{}} : call_version_t{call_version::current{}};
      },
      [](const std::string& version) -> call_version_t {
        static const std::unordered_map<std::string_view, call_version_t> known{
            {"0", call_version::legacy{}}, {"1", call_version::current{}}};
        const auto found = known.find(version);
        return found == known.end() ? call_version_t{call_version::current{}} : found->second;
      }}, wire);
}

// A TURN or STUN server as Matrix gives it, a URI (RFC 7064, 7065) --
// turn:host:port?transport=udp, turns:..., stun:... -- read into what a
// call's connection takes. Nothing, for one it cannot read.
[[nodiscard]] inline std::optional<mux::calls::ice_server> ice_server_of(std::string_view uri, const std::string& username,
                                                                        const std::string& password) {
  namespace relay = mux::calls::relay;
  const auto colon = uri.find(':');
  if (colon == std::string_view::npos)
    return std::nullopt;
  // The scheme, read once into what it is, and the port it means by default.
  struct scheme_read {
    std::optional<mux::calls::relay_t> stream, datagram;  // by its transport: TCP, UDP
    std::uint16_t port = 3478;
    std::string_view transport = "udp";
  };
  static const std::unordered_map<std::string_view, scheme_read> schemes{
      {"stun", {std::nullopt, relay::stun{}, 3478}},
      {"turns", {relay::turn_tls{}, std::nullopt, 5349, "tcp"}},
      {"turn", {relay::turn_tcp{}, relay::turn_udp{}, 3478}}};
  const auto scheme = schemes.find(uri.substr(0, colon));
  if (scheme == schemes.end())
    return std::nullopt;
  std::string_view rest = uri.substr(colon + 1);
  std::string_view transport = scheme->second.transport;
  if (const auto query = rest.find('?'); query != std::string_view::npos) {
    const auto said = rest.substr(query + 1);
    if (!said.starts_with("transport=")) return std::nullopt;
    transport = said.substr(std::string_view("transport=").size());
    rest = rest.substr(0, query);
  }
  static const std::unordered_map<std::string_view, bool> by_stream{{"tcp", true}, {"udp", false}};
  const auto stream = by_stream.find(transport);
  if (stream == by_stream.end()) return std::nullopt;
  const auto& kind = stream->second ? scheme->second.stream : scheme->second.datagram;
  if (!kind) return std::nullopt;
  mux::calls::ice_server out{.kind = *kind,
                             .username = username,
                             .password = password};
  out.port = scheme->second.port;
  // host, host:port, or [v6]:port.
  if (rest.starts_with('[')) {
    const auto close = rest.find(']');
    if (close == std::string_view::npos)
      return std::nullopt;
    out.host = std::string(rest.substr(1, close - 1));
    rest = rest.substr(close + 1);
    if (rest.starts_with(':')) {
      rest = rest.substr(1);
      if (rest.empty()) return std::nullopt;
    } else if (!rest.empty()) {
      return std::nullopt;
    }
  } else if (const auto port_at = rest.rfind(':'); port_at != std::string_view::npos) {
    out.host = std::string(rest.substr(0, port_at));
    if (out.host.contains(':')) return std::nullopt; // IPv6 must be bracketed.
    rest = rest.substr(port_at + 1);
    if (rest.empty()) return std::nullopt;
  } else {
    out.host = std::string(rest);
    rest = {};
  }
  if (!rest.empty()) {
    std::uint16_t port = 0;
    const auto parsed = std::from_chars(rest.data(), rest.data() + rest.size(), port);
    if (parsed.ec != std::errc{} || parsed.ptr != rest.data() + rest.size() || port == 0)
      return std::nullopt;
    out.port = port;
  }
  if (out.host.empty())
    return std::nullopt;
  return out;
}

// A call's signal come, told to the program -- as it happens only: what
// history pages back is the timeline's, a call long over. A still-ringing
// invite in the first sync is one to answer; older ones are ended by their
// lifetime in the calls part.
template <class Heard>
void signalled(Heard& heard, std::string call, std::string party, call_said_t said) {
  // Initial sync must replay the whole sequence: its hangup/answer can
  // cancel a still-valid invite, and its candidates are needed to answer.
  if (!heard.news())
    return;
  // This session's own, echoed by the sync: nothing to tell.
  if (heard.mine() && party == heard.party())
    return;
  heard.tell(call_signalled{heard.in, std::move(call), std::move(party), heard.event.sender, heard.mine(), heard.at,
                            std::move(said), heard.party()});
}

[[nodiscard]] inline std::optional<std::int64_t> candidate_index(const std::optional<double>& index) {
  if (!index || !std::isfinite(*index) || *index < 0 || *index > std::numeric_limits<int>::max() || std::floor(*index) != *index)
    return std::nullopt;
  return static_cast<std::int64_t>(*index);
}

// Each m.call.* event: its signal told, and whether it is a line in the
// timeline -- the invite, the answer, the end, as before; the candidates and
// the caller's choice, not.
template <class Heard>
heard_t matrix_heard(contract::matrix_events, type_tag<feature>, const loom::ev::m_call_invite_content_t& content, Heard& heard) {
  calls::signalled(heard, content.call_id, content.party_id.value_or(""),
                   call_said::invite{{mux::calls::sdp_kind::offer{}, content.offer.sdp}, std::chrono::milliseconds(content.lifetime),
                                     call_version_of(content.version)});
  return heard_as::line{" started a call"};
}
template <class Heard>
heard_t matrix_heard(contract::matrix_events, type_tag<feature>, const loom::ev::m_call_answer_content_t& content, Heard& heard) {
  calls::signalled(heard, content.call_id, content.party_id.value_or(""),
                   call_said::answer{{mux::calls::sdp_kind::answer{}, content.answer.sdp}});
  return heard_as::line{" answered the call"};
}
template <class Heard>
heard_t matrix_heard(contract::matrix_events, type_tag<feature>, const loom::ev::m_call_candidates_content_t& content,
                     Heard& heard) {
  calls::signalled(heard, content.call_id, content.party_id.value_or(""),
                   call_said::candidates{content.candidates | std::views::transform([](const auto& each) {
                     return mux::calls::ice_candidate{each.candidate, each.sdp_mid.value_or(""), candidate_index(each.sdp_m_line_index)};
                   }) | std::ranges::to<std::vector>()});
  return heard_as::quiet{};
}
template <class Heard>
heard_t matrix_heard(contract::matrix_events, type_tag<feature>, const loom::ev::m_call_hangup_content_t& content, Heard& heard) {
  using reasons = loom::ev::m_call_hangup_content_t::reason_values;
  const call_end_t why = spl::visit(
      spl::overloaded{[](reasons::user_hangup) -> call_end_t { return call_end::hung_up{}; },
                      [](reasons::user_busy) -> call_end_t { return call_end::busy{}; },
                      [](reasons::invite_timeout) -> call_end_t { return call_end::timed_out{}; },
                      [](reasons::ice_failed) -> call_end_t { return call_end::failed{}; },
                      [](reasons::ice_timeout) -> call_end_t { return call_end::failed{}; },
                      [](reasons::user_media_failed) -> call_end_t { return call_end::media_failed{}; },
                      [](reasons::unknown_error) -> call_end_t { return call_end::failed{}; },
                      [](const std::string& said) -> call_end_t { return call_end::other{said}; }},
      content.reason.value_or(reasons::user_hangup{}));
  calls::signalled(heard, content.call_id, content.party_id.value_or(""), call_said::hangup{why});
  return heard_as::line{spl::visit(spl::overloaded{
      [](call_end::hung_up) { return std::string(" ended the call"); },
      [](call_end::busy) { return std::string(" could not take the call (busy)"); },
      [](call_end::timed_out) { return std::string("’s call was not answered"); },
      [](call_end::failed) { return std::string(" ended the call because the connection failed"); },
      [](call_end::media_failed) { return std::string(" ended the call because audio could not be started"); },
      [](const call_end::other&) { return std::string(" ended the call"); }}, why)};
}
template <class Heard>
heard_t matrix_heard(contract::matrix_events, type_tag<feature>, const loom::ev::m_call_reject_content_t& content, Heard& heard) {
  calls::signalled(heard, content.call_id, content.party_id.value_or(""), call_said::reject{});
  return heard_as::line{" declined the call"};
}
template <class Heard>
heard_t matrix_heard(contract::matrix_events, type_tag<feature>, const loom::ev::m_call_select_answer_content_t& content,
                     Heard& heard) {
  calls::signalled(heard, content.call_id, content.party_id.value_or(""), call_said::select_answer{content.selected_party_id});
  return heard_as::quiet{};
}

// Transport renegotiation and stream metadata are internal signaling, not
// user actions to print as raw event types in the conversation.
template <class Heard>
heard_t matrix_heard(contract::matrix_events, type_tag<feature>, const loom::ev::m_call_negotiate_content_t& content, Heard& heard) {
  using kinds = loom::ev::m_call_negotiate_content_t::description_t::type_values;
  const auto kind = spl::visit(spl::overloaded{
      [](kinds::offer) -> std::optional<mux::calls::sdp_kind_t> { return mux::calls::sdp_kind::offer{}; },
      [](kinds::answer) -> std::optional<mux::calls::sdp_kind_t> { return mux::calls::sdp_kind::answer{}; },
      [](const std::string&) -> std::optional<mux::calls::sdp_kind_t> { return std::nullopt; }}, content.description.type);
  if (kind)
    calls::signalled(heard, content.call_id, content.party_id.value_or(""),
                    call_said::negotiate{{*kind, content.description.sdp}, std::chrono::milliseconds(content.lifetime)});
  return heard_as::quiet{};
}
template <class Heard>
heard_t matrix_heard(contract::matrix_events, type_tag<feature>,
                     const loom::ev::m_call_sdp_stream_metadata_changed_content_t&, Heard&) {
  return heard_as::quiet{};
}

// A Matrix account, as the calls plugin uses one: its party, events sent,
// its server asked.
template <class Account>
concept matrix_account = requires(Account& account) {
  { account.party() } -> std::convertible_to<std::string>;
  account.send_event(std::string(), std::string(), loom::ev::m_call_reject_content_t{});
};

// A signal of a call, sent to its room as its m.call.* event: in the
// version the call is in, from this session's party.
struct send_signal {
  std::string room;
  std::string call;
  call_version_t version;
  call_said_t said;

  template <matrix_account Account>
  void operator()(Account& account) const {
    const std::string party = account.party();
    const bool legacy = spl::visit(spl::overloaded{[](call_version::legacy) { return true; },
                                                   [](call_version::current) { return false; }}, version);
    const spl::variant<std::string, std::int64_t> wire =
        legacy ? spl::variant<std::string, std::int64_t>{std::int64_t{0}} : spl::variant<std::string, std::int64_t>{std::string("1")};
    spl::visit(
        spl::overloaded{
            [&](const call_said::invite& one) {
              loom::ev::m_call_invite_content_t content{};
              content.offer.type = loom::ev::m_call_invite_content_t::offer_t::type_values::offer{};
              content.offer.sdp = one.offer.sdp;
              content.lifetime = one.lifetime.count();
              content.call_id = call;
              content.version = wire;
              content.party_id = party;
              account.send_event(room, "m.call.invite", std::move(content));
            },
            [&](const call_said::answer& one) {
              loom::ev::m_call_answer_content_t content{};
              content.answer.type = loom::ev::m_call_answer_content_t::answer_t::type_values::answer{};
              content.answer.sdp = one.it.sdp;
              content.call_id = call;
              content.version = wire;
              content.party_id = party;
              account.send_event(room, "m.call.answer", std::move(content));
            },
            [&](const call_said::negotiate& one) {
              if (legacy)
                return;
              loom::ev::m_call_negotiate_content_t content{};
              using kinds = loom::ev::m_call_negotiate_content_t::description_t;
              content.description.type = spl::visit(spl::overloaded{
                  [](mux::calls::sdp_kind::offer) -> kinds::type_t { return kinds::type_values::offer{}; },
                  [](mux::calls::sdp_kind::answer) -> kinds::type_t { return kinds::type_values::answer{}; }}, one.description.kind);
              content.description.sdp = one.description.sdp;
              content.lifetime = one.lifetime.count();
              content.call_id = call;
              content.version = wire;
              content.party_id = party;
              account.send_event(room, "m.call.negotiate", std::move(content));
            },
            [&](const call_said::candidates& one) {
              loom::ev::m_call_candidates_content_t content{};
              content.candidates = one.them | std::views::transform([](const mux::calls::ice_candidate& each) {
                loom::ev::m_call_candidates_content_t::candidate_t made{};
                made.candidate = each.line;
                made.sdp_mid = each.mid;
                made.sdp_m_line_index = each.index;
                return made;
              }) | std::ranges::to<std::vector>();
              content.call_id = call;
              content.version = wire;
              content.party_id = party;
              account.send_event(room, "m.call.candidates", std::move(content));
            },
            [&](const call_said::hangup& one) {
              using reasons = loom::ev::m_call_hangup_content_t::reason_values;
              using reason_t = loom::ev::m_call_hangup_content_t::reason_t;
              loom::ev::m_call_hangup_content_t content{};
              content.reason = spl::visit(
                  spl::overloaded{[](call_end::hung_up) -> reason_t { return reasons::user_hangup{}; },
                                  [](call_end::busy) -> reason_t { return reasons::user_busy{}; },
                                  [](call_end::timed_out) -> reason_t { return reasons::invite_timeout{}; },
                                  [](call_end::failed) -> reason_t { return reasons::ice_failed{}; },
                                  [](call_end::media_failed) -> reason_t { return reasons::user_media_failed{}; },
                                  [](const call_end::other& said) -> reason_t { return said.said; }},
                  one.why);
              content.call_id = call;
              content.version = wire;
              content.party_id = party;
              account.send_event(room, "m.call.hangup", std::move(content));
            },
            // A legacy call has no reject: it is hung up.
            [&](const call_said::reject&) {
              spl::visit(spl::overloaded{[&](call_version::legacy) {
                                           loom::ev::m_call_hangup_content_t content{};
                                           content.call_id = call;
                                           content.version = wire;
                                           content.reason = loom::ev::m_call_hangup_content_t::reason_values::user_hangup{};
                                           account.send_event(room, "m.call.hangup", std::move(content));
                                         },
                                         [&](call_version::current) {
                                           loom::ev::m_call_reject_content_t content{};
                                           content.call_id = call;
                                           content.version = wire;
                                           content.party_id = party;
                                           account.send_event(room, "m.call.reject", std::move(content));
                                         }},
                         version);
            },
            // Nor a choice of answer.
            [&](const call_said::select_answer& one) {
              spl::visit(spl::overloaded{[](call_version::legacy) {},
                                         [&](call_version::current) {
                                           loom::ev::m_call_select_answer_content_t content{};
                                           content.selected_party_id = one.party;
                                           content.call_id = call;
                                           content.version = wire;
                                           content.party_id = party;
                                           account.send_event(room, "m.call.select_answer", std::move(content));
                                         }},
                         version);
            }},
        said);
  }
};

// The servers a call of the account goes through, asked of its server:
// told to the program as call_servers -- none, where it gives none.
struct ask_servers {
  std::string call;
  template <matrix_account Account>
  void operator()(Account& account) const {
    account.ask_server(loom::cs::get_turn_server{}, [&account, call = call](const auto& got) {
      std::vector<mux::calls::ice_server> servers;
      if (got)
        servers = got->uris | std::views::transform([&](const std::string& uri) {
                                                       return ice_server_of(uri, got->username, got->password);
                                                     }) | std::views::filter([](const auto& one) { return one.has_value(); }) | std::views::transform([](const auto& one) { return *one; }) | std::ranges::to<std::vector>();
      else
        log(account.id(), "no TURN servers: {}", got.error().said());
      account.tell_program(call_servers{account.id(), std::move(servers), account.party(), call});
    });
  }
};

}  // namespace mux::plugins::calls
