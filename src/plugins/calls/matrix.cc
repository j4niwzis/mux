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
  };
  static const std::unordered_map<std::string_view, scheme_read> schemes{
      {"stun", {relay::stun{}, relay::stun{}, 3478}},
      {"stuns", {relay::stun{}, relay::stun{}, 3478}},
      {"turns", {relay::turn_tls{}, relay::turn_tls{}, 5349}},
      {"turn", {relay::turn_tcp{}, relay::turn_udp{}, 3478}}};
  const auto scheme = schemes.find(uri.substr(0, colon));
  if (scheme == schemes.end())
    return std::nullopt;
  std::string_view rest = uri.substr(colon + 1);
  std::string_view transport = "udp";
  if (const auto query = rest.find('?'); query != std::string_view::npos) {
    if (const auto said = rest.substr(query + 1); said.starts_with("transport="))
      transport = said.substr(std::string_view("transport=").size());
    rest = rest.substr(0, query);
  }
  static const std::unordered_map<std::string_view, bool> by_stream{{"tcp", true}, {"udp", false}};
  const auto stream = by_stream.find(transport);
  mux::calls::ice_server out{.kind = stream != by_stream.end() && stream->second ? *scheme->second.stream : *scheme->second.datagram,
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
    if (rest.starts_with(':'))
      rest = rest.substr(1);
    else
      rest = {};
  } else if (const auto port_at = rest.rfind(':'); port_at != std::string_view::npos) {
    out.host = std::string(rest.substr(0, port_at));
    rest = rest.substr(port_at + 1);
  } else {
    out.host = std::string(rest);
    rest = {};
  }
  if (!rest.empty()) {
    std::uint16_t port = 0;
    if (std::from_chars(rest.data(), rest.data() + rest.size(), port).ec != std::errc{})
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
  const bool invite = spl::visit(spl::overloaded{[](const call_said::invite&) { return true; }, [](const auto&) { return false; }}, said);
  if (!heard.news() || (heard.history() && !invite))
    return;
  // This session's own, echoed by the sync: nothing to tell.
  if (heard.mine() && party == heard.party())
    return;
  heard.tell(call_signalled{heard.in, std::move(call), std::move(party), heard.event.sender, heard.mine(), heard.at,
                            std::move(said)});
}

// Each m.call.* event: its signal told, and whether it is a line in the
// timeline -- the invite, the answer, the end, as before; the candidates and
// the caller's choice, not.
template <class Heard>
heard_t matrix_heard(contract::matrix_events, type_tag<feature>, const loom::ev::m_call_invite_content_t& content, Heard& heard) {
  calls::signalled(heard, content.call_id, content.party_id.value_or(""),
                   call_said::invite{{mux::calls::sdp_kind::offer{}, content.offer.sdp}, std::chrono::milliseconds(content.lifetime),
                                     call_version_of(content.version)});
  return heard_as::line{};
}
template <class Heard>
heard_t matrix_heard(contract::matrix_events, type_tag<feature>, const loom::ev::m_call_answer_content_t& content, Heard& heard) {
  calls::signalled(heard, content.call_id, content.party_id.value_or(""),
                   call_said::answer{{mux::calls::sdp_kind::answer{}, content.answer.sdp}});
  return heard_as::line{};
}
template <class Heard>
heard_t matrix_heard(contract::matrix_events, type_tag<feature>, const loom::ev::m_call_candidates_content_t& content,
                     Heard& heard) {
  calls::signalled(heard, content.call_id, content.party_id.value_or(""),
                   call_said::candidates{std::ranges::to<std::vector>(std::views::transform(content.candidates, [](const auto& each) {
                     return mux::calls::ice_candidate{each.candidate, each.sdp_mid.value_or("")};
                   }))});
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
                      [](reasons::user_media_failed) -> call_end_t { return call_end::failed{}; },
                      [](reasons::unknown_error) -> call_end_t { return call_end::failed{}; },
                      [](const std::string& said) -> call_end_t { return call_end::other{said}; }},
      content.reason.value_or(reasons::user_hangup{}));
  calls::signalled(heard, content.call_id, content.party_id.value_or(""), call_said::hangup{why});
  return heard_as::line{};
}
template <class Heard>
heard_t matrix_heard(contract::matrix_events, type_tag<feature>, const loom::ev::m_call_reject_content_t& content, Heard& heard) {
  calls::signalled(heard, content.call_id, content.party_id.value_or(""), call_said::reject{});
  return heard_as::line{};
}
template <class Heard>
heard_t matrix_heard(contract::matrix_events, type_tag<feature>, const loom::ev::m_call_select_answer_content_t& content,
                     Heard& heard) {
  calls::signalled(heard, content.call_id, content.party_id.value_or(""), call_said::select_answer{content.selected_party_id});
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
            [&](const call_said::candidates& one) {
              loom::ev::m_call_candidates_content_t content{};
              content.candidates = std::ranges::to<std::vector>(std::views::transform(one.them, [](const mux::calls::ice_candidate& each) {
                loom::ev::m_call_candidates_content_t::candidate_t made{};
                made.candidate = each.line;
                made.sdp_mid = each.mid;
                made.sdp_m_line_index = 0;
                return made;
              }));
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
  template <matrix_account Account>
  void operator()(Account& account) const {
    account.ask_server(loom::cs::get_turn_server{}, [&account](const auto& got) {
      std::vector<mux::calls::ice_server> servers;
      if (got)
        servers = std::ranges::to<std::vector>(
            std::views::transform(std::views::filter(std::views::transform(got->uris, [&](const std::string& uri) {
                                                       return ice_server_of(uri, got->username, got->password);
                                                     }),
                                                     [](const auto& one) { return one.has_value(); }),
                                  [](const auto& one) { return *one; }));
      else
        log(account.id(), "no TURN servers: {}", got.error().said());
      account.tell_program(call_servers{account.id(), std::move(servers), account.party()});
    });
  }
};

}  // namespace mux::plugins::calls
