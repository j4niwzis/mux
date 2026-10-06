// SPDX-License-Identifier: AGPL-3.0-only
// mux.platform.audio -- Sound: a voice message or an audio file decoded -- Opus in
// Ogg, as Matrix's voice messages are, or Vorbis -- and played through the
// window's own SDL, one at a time, as Telegram plays them.
module;
#include <opus.h>
#include <vorbis/vorbisfile.h>
export module mux.platform.audio;

import std;
import sdl;
import splice.bytes;

export namespace mux::platform::audio {

// Sound decoded: its samples, interleaved, as floats; how many channels and
// how many samples a second.
struct pcm {
  std::vector<float> samples;
  int channels = 0;
  int rate = 0;
  [[nodiscard]] double seconds() const {
    return channels > 0 && rate > 0 ? static_cast<double>(samples.size()) / channels / rate : 0.0;
  }
};

// An Ogg stream's packets, in order: its pages read, their segments joined
// where a packet runs on from one to the next (a lacing value of 255).
[[nodiscard]] inline std::vector<std::string> ogg_packets(std::string_view data) {
  std::vector<std::string> out;
  std::string current;
  std::size_t at = 0;
  while (at + 27 <= data.size() && data.substr(at, 4) == "OggS") {
    const auto segments = static_cast<unsigned char>(data[at + 26]);
    if (at + 27 + segments > data.size())
      break;
    const std::string_view lacing = data.substr(at + 27, segments);
    std::size_t body = at + 27 + segments;
    for (const char each : lacing) {
      const auto length = static_cast<unsigned char>(each);
      if (body + length > data.size())
        return out;
      current.append(data.substr(body, length));
      body += length;
      if (length < 255)
        out.push_back(std::exchange(current, std::string()));
    }
    at = body;
  }
  return out;
}

// The most samples a sound is decoded to: 64 Mi floats, 256 MB -- some
// eleven minutes in stereo at 48 kHz. A few megabytes of packets that each
// decode to the longest frame would otherwise be gigabytes (review 5); what
// is past it is not played.
inline constexpr std::size_t kMostSamples = std::size_t{64} << 20;

// Opus in Ogg: its head says the channels and the samples to skip at the
// start; its tags are passed over; the rest are packets decoded at 48 kHz.
[[nodiscard]] inline std::optional<pcm> decode_opus(const std::vector<std::string>& packets) {
  if (packets.size() < 2 || packets[0].size() < 19 || !packets[0].starts_with("OpusHead"))
    return std::nullopt;
  const int channels = static_cast<unsigned char>(packets[0][9]);
  const int pre_skip = static_cast<unsigned char>(packets[0][10]) | (static_cast<unsigned char>(packets[0][11]) << 8);
  if (channels < 1 || channels > 2)
    return std::nullopt;
  int failed = 0;
  OpusDecoder* decoder = opus_decoder_create(48000, channels, &failed);
  if (!decoder || failed != OPUS_OK)
    return std::nullopt;
  pcm out{.channels = channels, .rate = 48000};
  std::vector<float> frame(static_cast<std::size_t>(5760 * channels));
  for (std::size_t i = 2; i < packets.size(); ++i) {
    const auto packet = spl::bytes::buffer_of(spl::bytes::of(packets[i]));  // opus reads the packet whole
    const int got = opus_decode_float(decoder, packet.data(), static_cast<opus_int32>(packet.size()), frame.data(), 5760, 0);
    if (got > 0)
      out.samples.insert(out.samples.end(), frame.begin(), frame.begin() + got * channels);
    if (out.samples.size() >= kMostSamples)
      break;
  }
  opus_decoder_destroy(decoder);
  const auto skipped = std::min(out.samples.size(), static_cast<std::size_t>(pre_skip * channels));
  out.samples.erase(out.samples.begin(), out.samples.begin() + static_cast<std::ptrdiff_t>(skipped));
  return out;
}

// Vorbis, through vorbisfile, read from memory.
struct memory_source {
  std::string_view data;
  std::size_t at = 0;
};
[[nodiscard]] inline std::optional<pcm> decode_vorbis(std::string_view data) {
  memory_source source{data};
  ov_callbacks callbacks{
      +[](void* into, std::size_t size, std::size_t count, void* from) -> std::size_t {
        auto& read = *static_cast<memory_source*>(from);
        const std::size_t wanted = size * count;
        const std::size_t given = std::min(wanted, read.data.size() - read.at);
        std::memcpy(into, read.data.data() + read.at, given);
        read.at += given;
        return size ? given / size : 0;
      },
      +[](void* from, ogg_int64_t offset, int whence) -> int {
        auto& read = *static_cast<memory_source*>(from);
        const auto base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? static_cast<ogg_int64_t>(read.at)
                                                                        : static_cast<ogg_int64_t>(read.data.size());
        const ogg_int64_t to = base + offset;
        if (to < 0 || to > static_cast<ogg_int64_t>(read.data.size()))
          return -1;
        read.at = static_cast<std::size_t>(to);
        return 0;
      },
      nullptr,
      +[](void* from) -> long { return static_cast<long>(static_cast<memory_source*>(from)->at); }};
  OggVorbis_File file;
  if (ov_open_callbacks(&source, &file, nullptr, 0, callbacks) != 0)
    return std::nullopt;
  const vorbis_info* info = ov_info(&file, -1);
  pcm out{.channels = info ? info->channels : 0, .rate = info ? static_cast<int>(info->rate) : 0};
  float** channels = nullptr;
  int section = 0;
  for (long got = 0; out.samples.size() < kMostSamples && (got = ov_read_float(&file, &channels, 4096, &section)) > 0;) {
    // A chained stream's link may have other channels than the first: read
    // past the arrays it gives, it was memory that is not theirs (review 5).
    // Where it changes, what came so far is the sound.
    const vorbis_info* now = ov_info(&file, section);
    if (!now || now->channels != out.channels)
      break;
    for (long i = 0; i < got; ++i)
      for (int c = 0; c < out.channels; ++c)
        out.samples.push_back(channels[c][i]);
  }
  ov_clear(&file);
  if (out.channels < 1 || out.rate < 1)
    return std::nullopt;
  return out;
}

// Whatever of these the bytes are.
[[nodiscard]] inline std::optional<pcm> decode(std::string_view bytes) {
  const auto packets = ogg_packets(bytes);
  if (auto opus = decode_opus(packets))
    return opus;
  return decode_vorbis(bytes);
}

// The speaker: one sound at a time, by the key it was asked for under --
// paused and played on, or put away for another.
class speaker {
 public:
  void play(std::string key, const pcm& sound) {
    this->stop();
    if (!sdl::SDL_WasInit(sdl::kInitAudio) && !sdl::SDL_InitSubSystem(sdl::kInitAudio))
      return;
    const sdl::SDL_AudioSpec spec{sdl::SDL_AUDIO_F32, sound.channels, sound.rate};
    stream_ = sdl::SDL_OpenAudioDeviceStream(sdl::kAudioDeviceDefaultPlayback, &spec, nullptr, nullptr);
    if (!stream_)
      return;
    const auto bytes = static_cast<int>(sound.samples.size() * sizeof(float));
    sdl::SDL_PutAudioStreamData(stream_, sound.samples.data(), bytes);
    sdl::SDL_FlushAudioStream(stream_);
    sdl::SDL_ResumeAudioStreamDevice(stream_);
    key_ = std::move(key);
    total_ = bytes;
    per_second_ = static_cast<double>(sound.rate) * sound.channels * sizeof(float);
    paused_ = false;
  }
  void toggle() {
    if (!stream_)
      return;
    if (paused_)
      sdl::SDL_ResumeAudioStreamDevice(stream_);
    else
      sdl::SDL_PauseAudioStreamDevice(stream_);
    paused_ = !paused_;
  }
  void stop() {
    if (stream_)
      sdl::SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
    key_.clear();
  }
  // Put away once all of it has been played.
  void tick() {
    if (stream_ && !paused_ && sdl::SDL_GetAudioStreamQueued(stream_) == 0)
      this->stop();
  }
  [[nodiscard]] bool holds(std::string_view key) const { return stream_ && key_ == key; }
  [[nodiscard]] bool playing(std::string_view key) const { return this->holds(key) && !paused_; }
  [[nodiscard]] double position() const {
    if (!stream_ || per_second_ <= 0.0)
      return 0.0;
    return (total_ - sdl::SDL_GetAudioStreamQueued(stream_)) / per_second_;
  }
  [[nodiscard]] double length() const { return per_second_ > 0.0 ? total_ / per_second_ : 0.0; }

 private:
  sdl::SDL_AudioStream* stream_ = nullptr;
  std::string key_;
  int total_ = 0;
  double per_second_ = 0.0;
  bool paused_ = false;
};

// A call's sound: what the microphone hears, and the other side played --
// mono, 48 kHz, in floats, as Opus has it -- each on a stream of SDL's own,
// which SDL fills and drains on its own thread. Read in frames of 20 ms,
// what a call sends at a time.
class call_audio {
 public:
  static constexpr int kRate = 48000;
  static constexpr int kFrame = kRate / 50;
  using frame = std::array<float, kFrame>;

  call_audio() = default;
  call_audio(const call_audio&) = delete;
  call_audio& operator=(const call_audio&) = delete;
  ~call_audio() {
    if (auto* heard = heard_.load())
      sdl::SDL_DestroyAudioStream(heard);
    if (auto* played = played_.load())
      sdl::SDL_DestroyAudioStream(played);
  }
  // Both opened -- each where it is not yet, so that it can be asked again:
  // false where there is no sound, or no microphone -- or, on a phone, where
  // it is not allowed yet.
  [[nodiscard]] bool open() {
    if (!sdl::SDL_WasInit(sdl::kInitAudio) && !sdl::SDL_InitSubSystem(sdl::kInitAudio))
      return false;
    const sdl::SDL_AudioSpec spec{sdl::SDL_AUDIO_F32, 1, kRate};
    if (!played_.load())
      if (auto* played = sdl::SDL_OpenAudioDeviceStream(sdl::kAudioDeviceDefaultPlayback, &spec, nullptr, nullptr)) {
        sdl::SDL_ResumeAudioStreamDevice(played);
        played_.store(played);
      }
    if (!heard_.load())
      if (auto* heard = sdl::SDL_OpenAudioDeviceStream(sdl::kAudioDeviceDefaultRecording, &spec, nullptr, nullptr)) {
        sdl::SDL_ResumeAudioStreamDevice(heard);
        heard_.store(heard);
      }
    return heard_.load() && played_.load();
  }
  // The next 20 ms the microphone heard, where there is that much yet.
  [[nodiscard]] std::optional<frame> heard() {
    constexpr int bytes = kFrame * static_cast<int>(sizeof(float));
    auto* heard = heard_.load();
    if (!heard || sdl::SDL_GetAudioStreamAvailable(heard) < bytes)
      return std::nullopt;
    frame out{};
    if (sdl::SDL_GetAudioStreamData(heard, out.data(), bytes) != bytes)
      return std::nullopt;
    return out;
  }
  // The other side's samples, played after what is queued. SDL copies them:
  // the floats are read where they lie.
  template <std::ranges::contiguous_range Samples>
    requires std::same_as<std::ranges::range_value_t<Samples>, float>
  void play(const Samples& samples) {
    if (auto* played = played_.load())
      sdl::SDL_PutAudioStreamData(played, std::ranges::data(samples),
                                  static_cast<int>(std::ranges::size(samples) * sizeof(float)));
  }

 private:
  // Opened on the call's sending thread, played into from the connection's:
  // each pointer read and set whole.
  std::atomic<sdl::SDL_AudioStream*> heard_{nullptr};
  std::atomic<sdl::SDL_AudioStream*> played_{nullptr};
};

// A time, as a player shows it: minutes and seconds.
[[nodiscard]] inline std::string clock(double seconds) {
  const auto whole = static_cast<long>(std::max(0.0, seconds));
  return std::format("{}:{:02}", whole / 60, whole % 60);
}

// The chime a message comes with: two soft notes, a fifth apart, fading --
// made once, not a file. Played on a stream of its own, so a voice message
// playing goes on.
[[nodiscard]] inline pcm chime_sound() {
  pcm out{.channels = 1, .rate = 48000};
  const auto note = [&](double hz, double from, double length) {
    const auto first = static_cast<std::size_t>(from * out.rate);
    const auto count = static_cast<std::size_t>(length * out.rate);
    if (out.samples.size() < first + count)
      out.samples.resize(first + count, 0.0f);
    for (std::size_t i = 0; i < count; ++i) {
      const double t = static_cast<double>(i) / out.rate;
      const double fade = std::exp(-t * 9.0) * std::min(1.0, t * 400.0);
      out.samples[first + i] += static_cast<float>(0.25 * fade * std::sin(2.0 * std::numbers::pi * hz * t));
    }
  };
  note(880.0, 0.0, 0.35);
  note(1318.5, 0.09, 0.4);
  return out;
}
// What plays it: on a stream of its own, so a voice message playing goes on.
class chime {
 public:
  chime() = default;
  chime(const chime&) = delete;
  chime& operator=(const chime&) = delete;
  ~chime() {
    if (stream_)
      sdl::SDL_DestroyAudioStream(stream_);
  }
  void play() {
    if (!sdl::SDL_WasInit(sdl::kInitAudio) && !sdl::SDL_InitSubSystem(sdl::kInitAudio))
      return;
    if (stream_)
      sdl::SDL_DestroyAudioStream(stream_);
    const sdl::SDL_AudioSpec spec{sdl::SDL_AUDIO_F32, sound_.channels, sound_.rate};
    stream_ = sdl::SDL_OpenAudioDeviceStream(sdl::kAudioDeviceDefaultPlayback, &spec, nullptr, nullptr);
    if (!stream_)
      return;
    sdl::SDL_PutAudioStreamData(stream_, sound_.samples.data(), static_cast<int>(sound_.samples.size() * sizeof(float)));
    sdl::SDL_FlushAudioStream(stream_);
    sdl::SDL_ResumeAudioStreamDevice(stream_);
  }

 private:
  pcm sound_ = chime_sound();
  sdl::SDL_AudioStream* stream_ = nullptr;
};

}  // namespace mux::platform::audio
