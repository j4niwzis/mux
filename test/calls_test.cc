// SPDX-License-Identifier: AGPL-3.0-only
#include <rtc/rtc.hpp>
import std;
import splice;
import mux.calls.media;
import gtest;
#include "gtest/gtest-macros.h"

TEST(Calls, AnswerUsesBrowserMidAndNegotiatedOpusPayload) {
  // A browser offer with video first, PCMU, and Opus on a non-default PT.
  rtc::Description remote(
      "v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 96\r\na=mid:video\r\n"
      "a=rtpmap:96 VP8/90000\r\n"
      "m=audio 9 UDP/TLS/RTP/SAVPF 0 109\r\na=mid:0\r\na=sendrecv\r\n"
      "a=rtpmap:0 PCMU/8000\r\na=rtpmap:109 opus/48000/2\r\n",
      rtc::Description::Type::Offer);
  const auto audio = mux::calls::audio_in(remote);
  EXPECT_EQ(audio.mid, "0");
  EXPECT_EQ(audio.payload, 109);
}

TEST(Calls, AnswerRejectsOffersWithoutSupportedAudio) {
  rtc::Description remote(
      "v=0\r\nm=audio 9 UDP/TLS/RTP/SAVPF 0\r\na=mid:0\r\n"
      "a=rtpmap:0 PCMU/8000\r\n", rtc::Description::Type::Offer);
  EXPECT_THROW((void)mux::calls::audio_in(remote), std::runtime_error);
}

namespace {
struct silent_audio {
  static constexpr int kRate = 48000, kFrame = 960;
  inline static std::atomic<bool> available{true}, delivering{true};
  using frame = std::array<float, kFrame>;
  std::chrono::steady_clock::time_point next{};
  bool open() { return available.load(); }
  std::optional<frame> heard() {
    if (!delivering.load() || std::chrono::steady_clock::now() < next) return std::nullopt;
    next = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
    return frame{};
  }
  template <class Samples> void play(const Samples&) {}
};
struct wake_call { void operator()() const {} };
using session = mux::calls::media_session<wake_call, silent_audio>;

bool until(auto condition) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (std::chrono::steady_clock::now() < deadline) {
    if (condition()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return false;
}
}

TEST(Calls, AudioDeviceFailureIsReported) {
  silent_audio::available = false;
  session call({}, wake_call{}, {.open_timeout = std::chrono::milliseconds(50)});
  call.offer();
  bool failed = false;
  EXPECT_TRUE(until([&] {
    for (const auto& event : call.take())
      spl::visit(spl::overloaded{
          [&](const mux::calls::said::failed& failure) { failed = failure.audio && !failure.why.empty(); },
          [](const auto&) {}}, event);
    return failed;
  }));
  silent_audio::available = true;
}

TEST(Calls, StalledMicrophoneIsReported) {
  silent_audio::delivering = false;
  session call({}, wake_call{}, {.frame_timeout = std::chrono::milliseconds(50)});
  call.offer();
  bool failed = false;
  EXPECT_TRUE(until([&] {
    for (const auto& event : call.take())
      spl::visit(spl::overloaded{
          [&](const mux::calls::said::failed& failure) { failed = failure.audio; },
          [](const auto&) {}}, event);
    return failed;
  }));
  silent_audio::delivering = true;
}

TEST(Calls, TwoPeersConnectAndExchangeAudioDescriptions) {
  session caller({}, wake_call{}), callee({}, wake_call{});
  bool caller_up = false, callee_up = false, failed = false;
  auto relay = [&](session& from, session& to, bool& up, bool answering) {
    for (const auto& event : from.take())
      spl::visit(spl::overloaded{
          [&](const mux::calls::said::description& description) {
            if (answering) to.answer(description.it);
            else to.answered(description.it);
          },
          [&](const mux::calls::said::candidate& candidate) { to.add_candidate(candidate.it); },
          [&](mux::calls::said::connected) { up = true; },
          [&](const mux::calls::said::failed&) { failed = true; },
          [](const auto&) {}}, event);
  };
  caller.offer();
  EXPECT_TRUE(until([&] {
    relay(caller, callee, caller_up, true);
    relay(callee, caller, callee_up, false);
    return failed || (caller_up && callee_up);
  }));
  EXPECT_FALSE(failed);
  EXPECT_TRUE(caller_up && callee_up);
}

TEST(Calls, BrowserTrackSurvivesHoldResumeAndIceRestart) {
  struct remote_events {
    std::mutex lock;
    std::vector<mux::calls::said_t> events;
    std::atomic<int> frames{0};
  };
  auto events = std::make_shared<remote_events>();
  rtc::Configuration config;
  config.disableAutoNegotiation = true;
  rtc::PeerConnection browser(config);
  rtc::Description::Audio audio("0", rtc::Description::Direction::SendRecv);
  audio.addOpusCodec(109);
  auto track = browser.addTrack(audio);
  track->setMediaHandler(std::make_shared<rtc::OpusRtpDepacketizer>());
  track->onFrame([events](rtc::binary, rtc::FrameInfo) { ++events->frames; });
  browser.onLocalDescription([&browser, events](rtc::Description description) {
    description = browser.localDescription().value_or(std::move(description));
    std::lock_guard held(events->lock);
    events->events.push_back(mux::calls::said::description{{mux::calls::sdp_kind::offer{}, std::string(description)}});
  });
  browser.onLocalCandidate([events](rtc::Candidate candidate) {
    std::lock_guard held(events->lock);
    events->events.push_back(mux::calls::said::candidate{{candidate.candidate(), candidate.mid(), 0}});
  });
  session call({}, wake_call{});
  int answers = 0;
  bool failed = false;
  rtc::Description::Direction answered_direction = rtc::Description::Direction::Unknown;
  auto drive = [&] {
    std::vector<mux::calls::said_t> pending;
    { std::lock_guard held(events->lock); pending = std::exchange(events->events, {}); }
    for (const auto& event : pending)
      spl::visit(spl::overloaded{
          [&](const mux::calls::said::description& description) {
            if (answers == 0) call.answer(description.it);
            else call.renegotiate(description.it);
          },
          [&](const mux::calls::said::candidate& candidate) { call.add_candidate(candidate.it); },
          [](const auto&) {}}, event);
    for (const auto& event : call.take())
      spl::visit(spl::overloaded{
          [&](const mux::calls::said::description& description) {
            rtc::Description answer(description.it.sdp, rtc::Description::Type::Answer);
            answered_direction = mux::calls::audio_in(answer).direction;
            ++answers;
            browser.setRemoteDescription(std::move(answer));
          },
          [&](const mux::calls::said::candidate& candidate) {
            if (!candidate.it.line.empty()) browser.addRemoteCandidate(rtc::Candidate(candidate.it.line, candidate.it.mid));
          },
          [&](const mux::calls::said::failed&) { failed = true; },
          [](const auto&) {}}, event);
  };
  browser.setLocalDescription(rtc::Description::Type::Offer);
  EXPECT_TRUE(until([&] { drive(); return failed || events->frames > 0; }));
  EXPECT_FALSE(failed);
  EXPECT_GT(events->frames.load(), 0);

  audio.setDirection(rtc::Description::Direction::Inactive);
  track->setDescription(audio);
  browser.setLocalDescription(rtc::Description::Type::Offer);
  EXPECT_TRUE(until([&] { drive(); return failed || answers >= 2; }));
  EXPECT_EQ(answered_direction, rtc::Description::Direction::Inactive);

  audio.setDirection(rtc::Description::Direction::SendRecv);
  track->setDescription(audio);
  const int before_resume = events->frames;
  browser.setLocalDescription(rtc::Description::Type::Offer);
  EXPECT_TRUE(until([&] { drive(); return failed || (answers >= 3 && events->frames > before_resume); }));
  EXPECT_FALSE(failed);

  const int before_restart = events->frames;
  browser.setLocalDescription(rtc::Description::Type::Offer,
      {.iceUfrag = "mux-restart", .icePwd = "mux-restart-password-0123456789"});
  EXPECT_TRUE(until([&] { drive(); return failed || (answers >= 4 && events->frames > before_restart + 3); }));
  EXPECT_FALSE(failed);
  EXPECT_GE(answers, 4);
  browser.resetCallbacks();
}

TEST(Calls, TcpAndTlsTurnProduceRelayCandidates) {
  // Optional local coturn integration. Ordinary unit tests need no server.
  const char* host = std::getenv("MUX_TEST_TURN_HOST");
  if (!host) GTEST_SKIP();
  const auto setting = [](const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return std::string(value ? value : fallback);
  };
  const auto user = setting("MUX_TEST_TURN_USER", "mux-test");
  const auto password = setting("MUX_TEST_TURN_PASSWORD", "mux-test-pass");
  const auto tcp_port = std::stoi(setting("MUX_TEST_TURN_TCP_PORT", "3478"));
  const auto tls_port = std::stoi(setting("MUX_TEST_TURN_TLS_PORT", "5349"));
  for (const auto& server : std::array{
      mux::calls::ice_server{.kind = mux::calls::relay::turn_tcp{}, .host = host,
                            .port = static_cast<std::uint16_t>(tcp_port), .username = user, .password = password},
      mux::calls::ice_server{.kind = mux::calls::relay::turn_tls{}, .host = host,
                            .port = static_cast<std::uint16_t>(tls_port), .username = user, .password = password}}) {
    session call({server}, wake_call{});
    bool relayed = false;
    call.offer();
    EXPECT_TRUE(until([&] {
      for (const auto& event : call.take())
        spl::visit(spl::overloaded{
            [&](const mux::calls::said::candidate& candidate) {
              if (!candidate.it.line.empty())
                relayed = relayed || rtc::Candidate(candidate.it.line).type() == rtc::Candidate::Type::Relayed;
            },
            [](const auto&) {}}, event);
      return relayed;
    }));
  }
}

TEST(Calls, TlsTurnRejectsTheWrongCertificateName) {
  const char* host = std::getenv("MUX_TEST_TURN_BAD_HOST");
  const char* port = std::getenv("MUX_TEST_TURN_TLS_PORT");
  if (!host || !port) GTEST_SKIP();
  mux::calls::ice_server server{.kind = mux::calls::relay::turn_tls{}, .host = host,
      .port = static_cast<std::uint16_t>(std::stoi(port)), .username = "mux-test", .password = "mux-test-pass"};
  session call({server}, wake_call{});
  bool relayed = false;
  call.offer();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  until([&] {
    for (const auto& event : call.take())
      spl::visit(spl::overloaded{
          [&](const mux::calls::said::candidate& candidate) {
            if (!candidate.it.line.empty())
              relayed = relayed || rtc::Candidate(candidate.it.line).type() == rtc::Candidate::Type::Relayed;
          }, [](const auto&) {}}, event);
    return std::chrono::steady_clock::now() >= deadline;
  });
  EXPECT_FALSE(relayed);
}
