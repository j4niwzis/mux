// SPDX-License-Identifier: AGPL-3.0-only
// mux.calls.media -- A call's connection and its sound: WebRTC through
// libdatachannel -- ICE through NATs, DTLS, SRTP -- one Opus audio track;
// the microphone sent in frames of 20 ms, the other side played.
//
// What the connection says comes on libdatachannel's threads, through the
// callbacks it is given -- the one place a callable crosses into it. Each
// only puts what it says in a box the program drains (take()), and wakes
// the window: the program reads data, it is not called.
module;
#include <opus.h>
#include <rtc/rtc.hpp>
export module mux.calls.media;

import std;
import splice;
import mux.platform.audio;
export import mux.calls.types;

export namespace mux::calls {

// Calls are in this build.
inline constexpr bool kAvailable = true;

// One call's connection. `Wake`: what wakes the window, from any thread --
// called as what the connection said is put in the box. Not moved: the
// connection's callbacks hold it.
template <class Wake>
class media_session {
 public:
  media_session(const std::vector<ice_server>& servers, Wake wake) : wake_(std::move(wake)) {
    rtc::Configuration config;
    config.iceServers = servers | std::views::transform([](const ice_server& one) { return server_of(one); }) |
                        std::ranges::to<std::vector>();
    pc_ = std::make_unique<rtc::PeerConnection>(config);
    pc_->onLocalDescription([this](rtc::Description given) {
      this->tell(said::description{{kind_of(given.type()), std::string(given)}});
    });
    pc_->onLocalCandidate([this](rtc::Candidate given) {
      this->tell(said::candidate{{std::string(given), given.mid()}});
    });
    pc_->onStateChange([this](rtc::PeerConnection::State state) {
      switch (state) {
        case rtc::PeerConnection::State::Connected:
          this->tell(said::connected{});
          break;
        case rtc::PeerConnection::State::Failed:
          this->tell(said::failed{});
          break;
        case rtc::PeerConnection::State::Closed:
          this->tell(said::ended{});
          break;
        default:
          break;
      }
    });
    // The audio track: Opus at 48 kHz, one channel each way, packetized
    // and reassembled by libdatachannel.
    rtc::Description::Audio audio("audio", rtc::Description::Direction::SendRecv);
    audio.addOpusCodec(kPayload);
    audio.addSSRC(kSsrc, "mux");
    track_ = pc_->addTrack(audio);
    auto rtp = std::make_shared<rtc::RtpPacketizationConfig>(kSsrc, "mux", kPayload, rtc::OpusRtpPacketizer::DefaultClockRate);
    auto handler = std::make_shared<rtc::OpusRtpPacketizer>(rtp);
    handler->addToChain(std::make_shared<rtc::RtcpSrReporter>(rtp));
    handler->addToChain(std::make_shared<rtc::OpusRtpDepacketizer>());
    track_->setMediaHandler(handler);
    track_->onFrame([this](rtc::binary data, rtc::FrameInfo) { this->play(data); });
    int failed = 0;
    encoder_ = opus_encoder_create(platform::audio::call_audio::kRate, 1, OPUS_APPLICATION_VOIP, &failed);
    decoder_ = opus_decoder_create(platform::audio::call_audio::kRate, 1, &failed);
  }
  media_session(const media_session&) = delete;
  media_session& operator=(const media_session&) = delete;
  ~media_session() {
    sending_ = std::jthread();  // stopped and joined first: it reads the track
    if (pc_)
      pc_->close();
    track_.reset();
    pc_.reset();
    if (encoder_)
      opus_encoder_destroy(encoder_);
    if (decoder_)
      opus_decoder_destroy(decoder_);
  }

  // Calling: the offer, to come as said::description.
  void offer() {
    this->start_sound();
    pc_->setLocalDescription(rtc::Description::Type::Offer);
  }
  // Called and answering: their offer, ours to come as said::description.
  void answer(const session_description& offer) {
    this->start_sound();
    pc_->setRemoteDescription(rtc::Description(offer.sdp, rtc::Description::Type::Offer));
    pc_->setLocalDescription(rtc::Description::Type::Answer);
  }
  // Calling, and answered: their answer.
  void answered(const session_description& answer) {
    pc_->setRemoteDescription(rtc::Description(answer.sdp, rtc::Description::Type::Answer));
  }
  // An address of theirs.
  void add_candidate(const ice_candidate& one) { pc_->addRemoteCandidate(rtc::Candidate(one.line, one.mid)); }
  // The microphone off, or on again: silence sent while it is off.
  void mute(bool off) { muted_.store(off); }
  [[nodiscard]] bool muted() const { return muted_.load(); }

  // What the connection told since last asked.
  [[nodiscard]] std::vector<said_t> take() {
    std::lock_guard held(lock_);
    return std::exchange(said_, {});
  }

 private:
  static constexpr int kPayload = 111;
  static constexpr rtc::SSRC kSsrc = 0x6d757831;  // "mux1"
  static constexpr int kMostBytes = 1275;          // the largest Opus frame

  static rtc::IceServer server_of(const ice_server& one) {
    return splice::visit(
        splice::overloaded{
            [&](relay::stun) { return rtc::IceServer(one.host, one.port); },
            [&](relay::turn_udp) {
              return rtc::IceServer(one.host, one.port, one.username, one.password, rtc::IceServer::RelayType::TurnUdp);
            },
            [&](relay::turn_tcp) {
              return rtc::IceServer(one.host, one.port, one.username, one.password, rtc::IceServer::RelayType::TurnTcp);
            },
            [&](relay::turn_tls) {
              return rtc::IceServer(one.host, one.port, one.username, one.password, rtc::IceServer::RelayType::TurnTls);
            }},
        one.kind);
  }
  static sdp_kind_t kind_of(rtc::Description::Type type) {
    return type == rtc::Description::Type::Answer ? sdp_kind_t{sdp_kind::answer{}} : sdp_kind_t{sdp_kind::offer{}};
  }
  void tell(said_t one) {
    {
      std::lock_guard held(lock_);
      said_.push_back(std::move(one));
    }
    wake_();
  }
  // The sound opened, and the microphone read and sent, a frame at a time,
  // on a thread of its own until the session goes.
  void start_sound() {
    if (sending_.joinable())
      return;
    sending_ = std::jthread([this](std::stop_token stop) {
      // Opened as soon as it can be: on a phone, once the microphone is
      // allowed -- asked as the call began.
      while (!stop.stop_requested() && !audio_.open())
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
      std::uint32_t samples = 0;
      std::array<unsigned char, kMostBytes> packet{};
      while (!stop.stop_requested()) {
        auto frame = audio_.heard();
        if (!frame) {
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
          continue;
        }
        if (muted_.load())
          frame->fill(0.0f);
        const int bytes = opus_encode_float(encoder_, frame->data(), platform::audio::call_audio::kFrame, packet.data(),
                                            static_cast<opus_int32>(packet.size()));
        samples += platform::audio::call_audio::kFrame;
        if (bytes <= 0 || !track_ || !track_->isOpen())
          continue;
        const auto sent = packet | std::views::take(bytes) |
                          std::views::transform([](unsigned char b) { return static_cast<std::byte>(b); }) |
                          std::ranges::to<rtc::binary>();
        track_->sendFrame(sent, rtc::FrameInfo(samples));
      }
    });
  }
  // A frame of theirs, decoded and played. Opus reads the packet whole: its
  // bytes, as it takes them, in a buffer on the stack.
  void play(const rtc::binary& data) {
    if (!decoder_ || data.empty() || data.size() > kMostBytes)
      return;
    std::array<unsigned char, kMostBytes> packet{};
    std::ranges::transform(data, packet.begin(), [](std::byte b) { return std::to_integer<unsigned char>(b); });
    std::array<float, 5760> pcm{};  // 120 ms, the longest Opus frame
    const int got = opus_decode_float(decoder_, packet.data(), static_cast<opus_int32>(data.size()), pcm.data(),
                                      static_cast<int>(pcm.size()), 0);
    if (got > 0)
      audio_.play(pcm | std::views::take(got));
  }

  Wake wake_;
  std::mutex lock_;
  std::vector<said_t> said_;
  std::unique_ptr<rtc::PeerConnection> pc_;
  std::shared_ptr<rtc::Track> track_;
  OpusEncoder* encoder_ = nullptr;
  OpusDecoder* decoder_ = nullptr;
  platform::audio::call_audio audio_;
  std::atomic<bool> muted_{false};
  // Last: stopped and joined before what it uses goes.
  std::jthread sending_;
};

}  // namespace mux::calls
