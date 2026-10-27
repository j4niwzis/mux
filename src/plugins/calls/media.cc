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
import mux.calls.relay;
export import mux.plugin.calls.changes;

export namespace mux::calls {

// Calls are in this build.
inline constexpr bool kAvailable = true;

// What an incoming offer actually negotiated, rather than our defaults.
struct offered_audio {
  std::string mid;
  int payload;
  rtc::Description::Direction direction = rtc::Description::Direction::SendRecv;
};
[[nodiscard]] inline offered_audio audio_in(const rtc::Description& remote) {
  for (int i = 0; i < remote.mediaCount(); ++i) {
    const auto choice = std::visit(spl::overloaded{
        [](const rtc::Description::Media* media) -> std::optional<offered_audio> {
          static const std::unordered_set<std::string_view> audio_types{"audio"};
          static const std::unordered_set<std::string_view> codecs{"opus"};
          if (!audio_types.contains(media->type()) || media->isRemoved())
            return std::nullopt;
          for (const int payload : media->payloadTypes()) {
            const auto* codec = media->rtpMap(payload);
            if (!codec) continue;
            std::string format = codec->format;
            std::ranges::transform(format, format.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (codecs.contains(format) && codec->clockRate == 48000)
              return offered_audio{media->mid(), payload, media->reciprocate().direction()};
          }
          return std::nullopt;
        },
        [](const rtc::Description::Application*) -> std::optional<offered_audio> { return std::nullopt; }}, remote.media(i));
    if (choice) return *choice;
  }
  throw std::runtime_error("Call offer has no supported Opus audio track");
}

struct audio_limits {
  std::chrono::steady_clock::duration open_timeout = std::chrono::seconds(30);
  std::chrono::steady_clock::duration frame_timeout = std::chrono::seconds(10);
};

// One call's connection. `Wake`: what wakes the window, from any thread --
// called as what the connection said is put in the box. Not moved: the
// connection's callbacks hold it.
template <class Wake, class Audio = platform::audio::call_audio>
class media_session {
 public:
  media_session(const std::vector<ice_server>& servers, Wake wake, audio_limits limits = {})
      : wake_(std::move(wake)), limits_(limits) {
    rtc::Configuration config;
    // Signalling owns negotiation: adding the track must not send an offer
    // before the call controller requests one.
    config.disableAutoNegotiation = true;
    config.iceServers = servers | std::views::transform([this](const ice_server& one) { return server_of(one); }) | std::ranges::to<std::vector>();
    pc_ = std::make_unique<rtc::PeerConnection>(config);
    callbacks_->owner = this;
    pc_->onLocalDescription([state = callbacks_](rtc::Description given) {
      std::lock_guard held(state->lock);
      if (auto* self = state->owner) {
        // During ICE restart, gathering may already be complete. Include
        // the retained candidates in the new SDP rather than waiting for
        // onLocalCandidate to emit them a second time.
        auto complete = self->pc_->localDescription().value_or(std::move(given));
        self->tell(said::description{{kind_of(complete.type()), std::string(complete)}});
      }
    });
    pc_->onLocalCandidate([state = callbacks_](rtc::Candidate given) {
      std::lock_guard held(state->lock);
      if (auto* self = state->owner)
        self->tell(said::candidate{{given.candidate(), given.mid(), self->media_index(given.mid())}});
    });
    pc_->onGatheringStateChange([state = callbacks_](rtc::PeerConnection::GatheringState status) {
      if (status != rtc::PeerConnection::GatheringState::Complete) return;
      std::lock_guard held(state->lock);
      if (auto* self = state->owner)
        if (const auto description = self->pc_->localDescription()) {
          const auto mid = description->bundleMid();
          self->tell(said::candidate{{"", mid, self->media_index(mid)}});
        }
    });
    pc_->onStateChange([state = callbacks_](rtc::PeerConnection::State status) {
      std::lock_guard held(state->lock);
      if (auto* self = state->owner) {
        switch (status) {
          case rtc::PeerConnection::State::Connected: self->tell(said::connected{}); break;
          case rtc::PeerConnection::State::Disconnected: self->tell(said::disconnected{}); break;
          case rtc::PeerConnection::State::Failed: self->tell(said::failed{}); break;
          case rtc::PeerConnection::State::Closed: self->tell(said::ended{}); break;
          default: break;
        }
      }
    });
    int failed = 0;
    encoder_ = opus_encoder_create(Audio::kRate, 1, OPUS_APPLICATION_VOIP, &failed);
    decoder_ = opus_decoder_create(Audio::kRate, 1, &failed);
    if (!encoder_ || !decoder_)
      tell(said::failed{"Audio codec could not be initialized", true});
  }
  media_session(const media_session&) = delete;
  media_session& operator=(const media_session&) = delete;
  ~media_session() {
    sending_ = std::jthread();  // stopped and joined first: it reads the track
    // Library callbacks can outlive close(). Detach the owner while holding
    // the same lock callbacks use, before destroying any audio resources.
    {
      std::lock_guard held(callbacks_->lock);
      callbacks_->owner = nullptr;
    }
    if (pc_) pc_->resetCallbacks();
    if (track_) track_->close();
    if (pc_) pc_->close();
    track_.reset();
    pc_.reset();
    if (encoder_)
      opus_encoder_destroy(encoder_);
    if (decoder_)
      opus_decoder_destroy(decoder_);
  }

  // Calling: the offer, to come as said::description.
  void offer() {
    if (!encoder_ || !decoder_) return;
    negotiate([&] {
      make_audio("audio", kPayload);
      this->start_sound();
      pc_->setLocalDescription(rtc::Description::Type::Offer);
    });
  }
  // Called and answering: their offer, ours to come as said::description.
  void answer(const session_description& offer) {
    if (!encoder_ || !decoder_) return;
    negotiate([&] {
      rtc::Description remote(offer.sdp, rtc::Description::Type::Offer);
      // Match the offered mid and Opus payload before applying the offer.
      // Otherwise libdatachannel creates a different remote track, leaving
      // our microphone and playback handlers on an unnegotiated track.
      const auto audio = audio_in(remote);
      make_audio(audio.mid, audio.payload, audio.direction);
      this->start_sound();
      pc_->setRemoteDescription(std::move(remote));
      pc_->setLocalDescription(rtc::Description::Type::Answer);
    });
  }
  // Calling, and answered: their answer.
  void answered(const session_description& answer) {
    negotiate([&] { pc_->setRemoteDescription(rtc::Description(answer.sdp, rtc::Description::Type::Answer)); });
  }
  // A v1 call remains the same call when SDP changes (hold, codec changes,
  // or a remote ICE restart). Its replies use m.call.negotiate as well.
  void renegotiate(const session_description& description) {
    negotiate([&] {
      const auto type = spl::visit(spl::overloaded{
          [](sdp_kind::offer) { return rtc::Description::Type::Offer; },
          [](sdp_kind::answer) { return rtc::Description::Type::Answer; }}, description.kind);
      // Ignore a duplicate/unsolicited answer instead of failing a stable call.
      if (type == rtc::Description::Type::Answer && pc_->signalingState() != rtc::PeerConnection::SignalingState::HaveLocalOffer)
        return;
      rtc::Description remote(description.sdp, type);
      if (type == rtc::Description::Type::Offer) {
        const auto audio = audio_in(remote);
        make_audio(audio.mid, audio.payload, audio.direction);
      }
      pc_->setRemoteDescription(std::move(remote));
      if (type == rtc::Description::Type::Offer)
        pc_->setLocalDescription(rtc::Description::Type::Answer);
    });
  }
  // An address of theirs.
  void add_candidate(const ice_candidate& one) {
    // Matrix sends an empty candidate to mark the end of gathering.
    if (one.line.empty()) return;
    try {
      std::string mid = one.mid;
      if (mid.empty() && one.index)
        if (const auto remote = pc_->remoteDescription(); remote && *one.index >= 0 && *one.index < remote->mediaCount())
          mid = std::visit([](const auto* media) { return media->mid(); }, remote->media(static_cast<int>(*one.index)));
      pc_->addRemoteCandidate(rtc::Candidate(one.line, std::move(mid)));
    }
    catch (const std::exception& failed) { tell(said::failed{failed.what()}); }
  }
  // The microphone off, or on again: silence sent while it is off.
  void mute(bool off) { muted_.store(off); }
  [[nodiscard]] bool muted() const { return muted_.load(); }

  // What the connection told since last asked.
  [[nodiscard]] std::vector<said_t> take() {
    std::lock_guard held(lock_);
    return std::exchange(said_, {});
  }

 private:
  std::optional<std::int64_t> media_index(const std::string& mid) const {
    if (const auto local = pc_->localDescription())
      for (int i = 0; i < local->mediaCount(); ++i)
        if (std::visit([&](const auto* media) { return media->mid() == mid; }, local->media(i)))
          return i;
    return std::nullopt;
  }
  struct callback_state {
    std::mutex lock;
    media_session* owner = nullptr;
  };
  void make_audio(std::string mid, int payload,
                  rtc::Description::Direction direction = rtc::Description::Direction::SendRecv) {
    // The audio track: Opus at 48 kHz, one channel each way, packetized
    // and reassembled by libdatachannel.
    std::lock_guard held(media_lock_);
    rtc::Description::Audio audio(std::move(mid), direction);
    audio.addOpusCodec(payload);
    audio.addSSRC(kSsrc, "mux", "mux-audio", "mux-microphone");
    if (track_) {
      if (track_->mid() != audio.mid())
        throw std::runtime_error("Renegotiation changed the audio track identity");
      track_->setDescription(audio);
    } else {
      track_ = pc_->addTrack(audio);
    }
    payload_.store(payload);
    // RTP sequence numbers must continue across SDP changes. Replacing the
    // packetizer on hold/resume makes SRTP reject our packets as replays.
    if (rtp_) {
      rtp_->payloadType = static_cast<std::uint8_t>(payload);
      return;
    }
    rtp_ = std::make_shared<rtc::RtpPacketizationConfig>(kSsrc, "mux", payload, rtc::OpusRtpPacketizer::DefaultClockRate);
    auto handler = std::make_shared<rtc::OpusRtpPacketizer>(rtp_);
    handler->addToChain(std::make_shared<rtc::RtcpSrReporter>(rtp_));
    handler->addToChain(std::make_shared<rtc::OpusRtpDepacketizer>());
    handler->addToChain(std::make_shared<rtc::RtcpReceivingSession>());
    track_->setMediaHandler(handler);
    track_->onFrame([state = callbacks_](rtc::binary data, rtc::FrameInfo info) {
      std::lock_guard held(state->lock);
      if (auto* self = state->owner; self && info.payloadType == self->payload_.load())
        self->play(data);
    });
  }
  template <class Action> void negotiate(Action action) {
    try { action(); }
    catch (const std::exception& failed) { tell(said::failed{failed.what()}); }
  }
  static constexpr int kPayload = 111;
  static constexpr rtc::SSRC kSsrc = 0x6d757831;  // "mux1"
  static constexpr int kMostBytes = 1275;          // the largest Opus frame
  static constexpr int kMostReceivedBytes = 6 * kMostBytes; // up to 120 ms in one packet

  rtc::IceServer server_of(const ice_server& one) {
    return spl::visit(
        spl::overloaded{
            [&](relay::stun) { return rtc::IceServer(one.host, one.port); },
            [&](relay::turn_udp) {
              return rtc::IceServer(one.host, one.port, one.username, one.password, rtc::IceServer::RelayType::TurnUdp);
            },
            [&](relay::turn_tcp) {
              return rtc::IceServer(one.host, one.port, one.username, one.password, rtc::IceServer::RelayType::TurnTcp);
            },
            [&](relay::turn_tls) {
              auto tunnel = std::make_unique<turn_tls_tunnel>(one.host, one.port);
              const auto port = tunnel->port();
              tunnels_.push_back(std::move(tunnel));
              return rtc::IceServer("127.0.0.1", port, one.username, one.password, rtc::IceServer::RelayType::TurnTcp);
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
      try {
        const auto deadline = std::chrono::steady_clock::now() + limits_.open_timeout;
        while (!stop.stop_requested() && !audio_.open()) {
          if (std::chrono::steady_clock::now() >= deadline) {
            tell(said::failed{"Microphone or audio output could not be opened. Check microphone permission and audio devices.", true});
            return;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::uint32_t samples = 0;
        std::array<unsigned char, kMostBytes> packet{};
        auto last_frame = std::chrono::steady_clock::now();
        while (!stop.stop_requested()) {
          auto frame = audio_.heard();
          if (!frame) {
            if (std::chrono::steady_clock::now() - last_frame > limits_.frame_timeout) {
              tell(said::failed{"The microphone stopped providing audio", true});
              return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
          }
          last_frame = std::chrono::steady_clock::now();
          std::lock_guard held(media_lock_);
          if (!track_ || !track_->isOpen()) continue;
          switch (track_->direction()) {
            case rtc::Description::Direction::SendOnly:
            case rtc::Description::Direction::SendRecv: break;
            default: continue; // The remote peer has put our microphone on hold.
          }
          if (muted_.load()) frame->fill(0.0f);
          const int bytes = opus_encode_float(encoder_, frame->data(), Audio::kFrame, packet.data(),
                                              static_cast<opus_int32>(packet.size()));
          if (bytes < 0) {
            tell(said::failed{"Microphone audio could not be encoded", true});
            return;
          }
          samples += Audio::kFrame;
          const auto sent = packet | std::views::take(bytes) | std::views::transform([](unsigned char b) { return static_cast<std::byte>(b); }) | std::ranges::to<rtc::binary>();
          track_->sendFrame(sent, rtc::FrameInfo(samples));
        }
      } catch (const std::exception& failed) {
        tell(said::failed{failed.what()});
      }
    });
  }

  // A frame of theirs, decoded and played. Opus reads the packet whole: its
  // bytes, as it takes them, in a buffer on the stack.
  void play(const rtc::binary& data) {
    if (!decoder_ || data.empty() || data.size() > kMostReceivedBytes)
      return;
    std::array<unsigned char, kMostReceivedBytes> packet{};
    std::ranges::transform(data, packet.begin(), [](std::byte b) { return std::to_integer<unsigned char>(b); });
    std::array<float, 5760> pcm{};  // 120 ms, the longest Opus frame
    const int got = opus_decode_float(decoder_, packet.data(), static_cast<opus_int32>(data.size()), pcm.data(),
                                      static_cast<int>(pcm.size()), 0);
    if (got > 0)
      audio_.play(pcm | std::views::take(got));
  }

  Wake wake_;
  audio_limits limits_;
  std::shared_ptr<callback_state> callbacks_ = std::make_shared<callback_state>();
  std::atomic<int> payload_{kPayload};
  std::mutex lock_;
  std::vector<said_t> said_;
  std::vector<std::unique_ptr<turn_tls_tunnel>> tunnels_;
  std::unique_ptr<rtc::PeerConnection> pc_;
  std::shared_ptr<rtc::Track> track_;
  std::mutex media_lock_;
  std::shared_ptr<rtc::RtpPacketizationConfig> rtp_;
  OpusEncoder* encoder_ = nullptr;
  OpusDecoder* decoder_ = nullptr;
  Audio audio_;
  std::atomic<bool> muted_{false};
  // Last: stopped and joined before what it uses goes.
  std::jthread sending_;
};

}  // namespace mux::calls
