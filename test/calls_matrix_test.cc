// SPDX-License-Identifier: AGPL-3.0-only
import std;
import splice;
import loom.ev;
import mux.core;
import mux.composition;
import mux.proto.matrix.client;
import mux.plugin.calls.types;
import mux.plugin.calls.matrix;
import gtest;
#include "gtest/gtest-macros.h"

namespace {
namespace calls = mux::plugins::calls;
struct heard {
  mux::conversation_id in;
  struct { std::string sender = "@bob:example.org"; } event;
  std::chrono::sys_time<std::chrono::milliseconds> at{};
  bool live = true;
  std::vector<calls::call_signalled> signals;
  bool news() const { return live; }
  bool history() const { return true; }
  bool mine() const { return false; }
  std::string party() const { return "mux-device"; }
  void tell(calls::call_signalled signal) { signals.push_back(std::move(signal)); }
};

template <class Content>
std::string text_of(const Content& content) {
  heard context;
  const auto result = calls::matrix_heard(mux::contract::matrix_events{}, mux::type_tag<calls::feature>{}, content, context);
  return spl::get<calls::heard_as::line>(result).text;
}

TEST(CallsMatrix, InitialSyncRetainsCallTerminationAndCandidates) {
  heard context;
  calls::signalled(context, "call", "device", calls::call_said::invite{});
  calls::signalled(context, "call", "device", calls::call_said::candidates{});
  calls::signalled(context, "call", "device", calls::call_said::answer{});
  calls::signalled(context, "call", "device", calls::call_said::hangup{});
  EXPECT_EQ(context.signals.size(), 4u);
  context.live = false; // Backfilled history must not resurrect calls.
  calls::signalled(context, "old", "device", calls::call_said::invite{});
  EXPECT_EQ(context.signals.size(), 4u);
}

TEST(CallsMatrix, TimelineUsesReadableCallActions) {
  EXPECT_EQ(text_of(loom::ev::m_call_invite_content_t{}), " started a call");
  EXPECT_EQ(text_of(loom::ev::m_call_answer_content_t{}), " answered the call");
  EXPECT_EQ(text_of(loom::ev::m_call_reject_content_t{}), " declined the call");
  EXPECT_EQ(text_of(loom::ev::m_call_hangup_content_t{}), " ended the call");
  loom::ev::m_call_hangup_content_t timeout{};
  timeout.reason = loom::ev::m_call_hangup_content_t::reason_values::invite_timeout{};
  EXPECT_EQ(text_of(timeout), "’s call was not answered");
}
TEST(CallsMatrix, InternalCallEventsStayOutOfTheTimeline) {
  heard context;
  const auto result = calls::matrix_heard(mux::contract::matrix_events{}, mux::type_tag<calls::feature>{},
      loom::ev::m_call_negotiate_content_t{}, context);
  EXPECT_TRUE(spl::holds_alternative<calls::heard_as::quiet>(result));
  const auto metadata = calls::matrix_heard(mux::contract::matrix_events{}, mux::type_tag<calls::feature>{},
      loom::ev::m_call_sdp_stream_metadata_changed_content_t{}, context);
  EXPECT_TRUE(spl::holds_alternative<calls::heard_as::quiet>(metadata));
}
} // namespace

namespace {
struct sent_events {
  using content_t = spl::variant<loom::ev::m_call_invite_content_t, loom::ev::m_call_answer_content_t,
      loom::ev::m_call_candidates_content_t, loom::ev::m_call_hangup_content_t,
      loom::ev::m_call_reject_content_t, loom::ev::m_call_select_answer_content_t,
      loom::ev::m_call_negotiate_content_t>;
  std::vector<content_t> events;
  std::string party() const { return "mux-device"; }
  template <class Content> void send_event(std::string, std::string, Content content) { events.push_back(std::move(content)); }
};

TEST(CallsMatrix, RenegotiationIsSignalledWithoutATimelineLine) {
  heard context;
  loom::ev::m_call_negotiate_content_t content{};
  content.call_id = "active";
  content.party_id = "element-device";
  content.description.sdp = "updated SDP";
  content.lifetime = 60000;
  const auto result = calls::matrix_heard(mux::contract::matrix_events{}, mux::type_tag<calls::feature>{}, content, context);
  EXPECT_TRUE(spl::holds_alternative<calls::heard_as::quiet>(result));
  ASSERT_EQ(context.signals.size(), 1u);
  const auto& update = spl::get<calls::call_said::negotiate>(context.signals.front().said);
  EXPECT_EQ(update.description.sdp, "updated SDP");
  EXPECT_EQ(update.lifetime, std::chrono::milliseconds(60000));

  sent_events account;
  calls::send_signal{"!room", "active", calls::call_version::current{},
      calls::call_said::negotiate{{mux::calls::sdp_kind::answer{}, "answer SDP"}}}(account);
  ASSERT_EQ(account.events.size(), 1u);
  const auto& sent = spl::get<loom::ev::m_call_negotiate_content_t>(account.events.front());
  EXPECT_EQ(sent.call_id, "active");
  EXPECT_EQ(sent.party_id, "mux-device");
  EXPECT_EQ(sent.description.sdp, "answer SDP");
  EXPECT_TRUE(spl::holds_alternative<loom::ev::m_call_negotiate_content_t::description_t::type_values::answer>(sent.description.type));
}

TEST(CallsMatrix, AudioFailureKeepsItsReasonAndReadableText) {
  sent_events account;
  calls::send_signal{"!room", "active", calls::call_version::current{},
      calls::call_said::hangup{calls::call_end::media_failed{}}}(account);
  const auto& sent = spl::get<loom::ev::m_call_hangup_content_t>(account.events.front());
  EXPECT_TRUE(spl::holds_alternative<loom::ev::m_call_hangup_content_t::reason_values::user_media_failed>(*sent.reason));
  EXPECT_EQ(text_of(sent), " ended the call because audio could not be started");
}

TEST(CallsMatrix, CandidateKeepsItsActualMediaIndex) {
  sent_events account;
  calls::send_signal{"!room", "active", calls::call_version::current{},
      calls::call_said::candidates{{{"candidate:audio", "audio", 1}}}}(account);
  const auto& sent = spl::get<loom::ev::m_call_candidates_content_t>(account.events.front());
  ASSERT_EQ(sent.candidates.size(), 1u);
  EXPECT_EQ(sent.candidates.front().sdp_m_line_index, 1);
  EXPECT_EQ(sent.candidates.front().sdp_mid, "audio");
  EXPECT_FALSE(calls::candidate_index(0.5).has_value());
  EXPECT_FALSE(calls::candidate_index(-1).has_value());
}

TEST(CallsMatrix, RelayUrisKeepTheirTransportAndRejectInvalidPorts) {
  auto tls = calls::ice_server_of("turns:relay.example.org", "user", "pass");
  ASSERT_TRUE(tls.has_value());
  EXPECT_EQ(tls->port, 5349);
  EXPECT_TRUE(spl::holds_alternative<mux::calls::relay::turn_tls>(tls->kind));
  auto tcp = calls::ice_server_of("turn:[2001:db8::1]:443?transport=tcp", "user", "pass");
  ASSERT_TRUE(tcp.has_value());
  EXPECT_EQ(tcp->host, "2001:db8::1");
  EXPECT_EQ(tcp->port, 443);
  EXPECT_TRUE(spl::holds_alternative<mux::calls::relay::turn_tcp>(tcp->kind));
  for (const std::string uri : {"turn:relay:3478junk", "turn:relay:0", "turn:relay:",
       "turn:[::1]junk", "turn:relay?transport=sctp", "turns:relay?transport=udp", "stuns:relay"})
    EXPECT_FALSE(calls::ice_server_of(uri, "user", "pass").has_value());
}
} // namespace
