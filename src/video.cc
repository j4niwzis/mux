// SPDX-License-Identifier: AGPL-3.0-only
// mux.video -- A video played, as Telegram Desktop plays one, through
// FFmpeg: its file read by libavformat, its pictures decoded by libavcodec
// and made RGBA by libswscale for Skia to draw, its sound decoded and made
// interleaved float stereo by libswresample for SDL to play.
//
// The pictures are decoded as they are due, on the window's thread: a clip
// in a chat is short, and one frame decoded a frame is what a viewer waits
// on anyway. The sound read on the way is queued to SDL as it comes, a frame
// ahead of the picture at most.
module;
#include <errno.h>
#include <SDL3/SDL.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
export module mux.video;

import std;
import skia;

namespace mux::video {

// FFmpeg's things, let go as its API says each is.
struct format_closer {
  void operator()(AVFormatContext* f) const { avformat_close_input(&f); }
};
struct codec_closer {
  void operator()(AVCodecContext* c) const { avcodec_free_context(&c); }
};
struct packet_closer {
  void operator()(AVPacket* p) const { av_packet_free(&p); }
};
struct frame_closer {
  void operator()(AVFrame* f) const { av_frame_free(&f); }
};
struct scaler_closer {
  void operator()(SwsContext* s) const { sws_freeContext(s); }
};
struct resampler_closer {
  void operator()(SwrContext* s) const { swr_free(&s); }
};
struct stream_closer {
  void operator()(SDL_AudioStream* s) const { SDL_DestroyAudioStream(s); }
};

// A stream's decoder, opened; none where the file has no such stream or
// nothing here decodes it.
[[nodiscard]] std::pair<int, std::unique_ptr<AVCodecContext, codec_closer>> decoder_of(AVFormatContext* file,
                                                                                         AVMediaType type) {
  const AVCodec* codec = nullptr;
  const int index = av_find_best_stream(file, type, -1, -1, &codec, 0);
  if (index < 0 || codec == nullptr)
    return {-1, nullptr};
  std::unique_ptr<AVCodecContext, codec_closer> context(avcodec_alloc_context3(codec));
  if (!context || avcodec_parameters_to_context(context.get(), file->streams[index]->codecpar) < 0)
    return {-1, nullptr};
  context->thread_count = 0;  // as many as it sees fit
  if (avcodec_open2(context.get(), codec, nullptr) < 0)
    return {-1, nullptr};
  return {index, std::move(context)};
}

}  // namespace mux::video

export namespace mux::video {

// Whether videos play in the window, in this build.
inline constexpr bool kPlays = true;

// The sound it plays: interleaved float stereo, at this rate.
inline constexpr int kRate = 48000;

class player {
 public:
  // Opened from a file; nothing where it cannot be read or has no picture.
  [[nodiscard]] static std::unique_ptr<player> open(const std::filesystem::path& where) {
    AVFormatContext* raw = nullptr;
    if (avformat_open_input(&raw, where.string().c_str(), nullptr, nullptr) < 0)
      return nullptr;
    std::unique_ptr<player> made(new player);
    made->file_.reset(raw);
    if (avformat_find_stream_info(raw, nullptr) < 0)
      return nullptr;
    std::tie(made->video_index_, made->video_) = decoder_of(raw, AVMEDIA_TYPE_VIDEO);
    if (!made->video_)
      return nullptr;
    std::tie(made->audio_index_, made->audio_) = decoder_of(raw, AVMEDIA_TYPE_AUDIO);
    made->packet_.reset(av_packet_alloc());
    made->frame_.reset(av_frame_alloc());
    if (made->audio_)
      made->open_sound();
    return made;
  }

  // Moved on to `now_ms` (the window's clock): the picture due then made
  // the one shown. Whether it changed.
  bool advance(double now_ms) {
    now_ms_ = now_ms;
    if (!started_) {
      started_ = true;
      started_ms_ = now_ms;
      this->resume_sound();
    }
    const double at = this->position();
    bool changed = false;
    while (!ended_ && (!ahead_ || ahead_->second <= at)) {
      if (ahead_) {
        picture_ = std::move(ahead_->first);
        changed = true;
      }
      ahead_ = this->decode_next();
      if (!ahead_)
        ended_ = true;
    }
    // Its end: left on its last picture, paused, to be played again.
    if (ended_ && !ahead_ && !paused_ && at >= this->length()) {
      base_ = this->length();
      paused_ = true;
      this->pause_sound();
    }
    return changed;
  }
  void toggle() {
    if (paused_ && ended_ && !ahead_) {
      this->seek(0.0);
      return;
    }
    if (paused_) {
      started_ms_ = now_ms_;
      paused_ = false;
      this->resume_sound();
    } else {
      base_ = this->position();
      paused_ = true;
      this->pause_sound();
    }
  }
  // To `seconds` from its start: the picture there next, the sound from there.
  void seek(double seconds) {
    seconds = std::clamp(seconds, 0.0, this->length());
    const auto target = static_cast<std::int64_t>(seconds * AV_TIME_BASE);
    av_seek_frame(file_.get(), -1, target, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(video_.get());
    if (audio_)
      avcodec_flush_buffers(audio_.get());
    if (sound_)
      SDL_ClearAudioStream(sound_.get());
    draining_ = false;
    ended_ = false;
    ahead_.reset();
    base_ = seconds;
    started_ms_ = now_ms_;
    if (paused_) {
      paused_ = false;
      this->resume_sound();
    }
  }
  [[nodiscard]] bool paused() const noexcept { return paused_; }
  [[nodiscard]] double position() const {
    return paused_ || !started_ ? base_ : std::min(this->length(), base_ + (now_ms_ - started_ms_) / 1000.0);
  }
  [[nodiscard]] double length() const {
    return file_ && file_->duration > 0 ? static_cast<double>(file_->duration) / AV_TIME_BASE : 0.0;
  }
  // The picture shown now; none before the first is decoded.
  [[nodiscard]] const skia::Sp<skia::SkImage>& picture() const noexcept { return picture_; }

 private:
  player() = default;

  // The next picture and when it is due, the sound on the way queued.
  std::optional<std::pair<skia::Sp<skia::SkImage>, double>> decode_next() {
    for (;;) {
      if (avcodec_receive_frame(video_.get(), frame_.get()) == 0) {
        auto made = this->picture_of(*frame_);
        av_frame_unref(frame_.get());
        if (made.first)
          return made;
        continue;
      }
      if (draining_)
        return std::nullopt;
      if (av_read_frame(file_.get(), packet_.get()) < 0) {
        // The file read to its end: what the decoders hold, given out.
        draining_ = true;
        avcodec_send_packet(video_.get(), nullptr);
        continue;
      }
      if (packet_->stream_index == video_index_)
        avcodec_send_packet(video_.get(), packet_.get());
      else if (audio_ && packet_->stream_index == audio_index_)
        this->hear(*packet_);
      av_packet_unref(packet_.get());
    }
  }
  std::pair<skia::Sp<skia::SkImage>, double> picture_of(const AVFrame& got) {
    const AVStream* stream = file_->streams[video_index_];
    const std::int64_t stamp = got.best_effort_timestamp != AV_NOPTS_VALUE ? got.best_effort_timestamp : got.pts;
    const std::int64_t start = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
    const double at = stamp == AV_NOPTS_VALUE ? base_ : static_cast<double>(stamp - start) * av_q2d(stream->time_base);
    scaler_.reset(sws_getCachedContext(scaler_.release(), got.width, got.height, static_cast<AVPixelFormat>(got.format),
                                       got.width, got.height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr));
    if (!scaler_)
      return {nullptr, at};
    rgba_.resize(static_cast<std::size_t>(got.width) * static_cast<std::size_t>(got.height) * 4u);
    std::uint8_t* planes[4] = {rgba_.data(), nullptr, nullptr, nullptr};
    int strides[4] = {got.width * 4, 0, 0, 0};
    sws_scale(scaler_.get(), got.data, got.linesize, 0, got.height, planes, strides);
    return {skia::imageFromRGBA(got.width, got.height, rgba_.data()), at};
  }
  // A packet of sound: decoded, made float stereo, queued to be played.
  void hear(const AVPacket& packet) {
    if (avcodec_send_packet(audio_.get(), &packet) < 0)
      return;
    while (avcodec_receive_frame(audio_.get(), frame_.get()) == 0) {
      if (resampler_ && sound_) {
        const int most = swr_get_out_samples(resampler_.get(), frame_->nb_samples);
        // Bytes, as the resampler writes them: two floats a sample.
        samples_.resize(static_cast<std::size_t>(std::max(0, most)) * 2u * sizeof(float));
        std::uint8_t* out[1] = {samples_.data()};
        const int made = swr_convert(resampler_.get(), out, most,
                                     const_cast<const std::uint8_t**>(frame_->extended_data), frame_->nb_samples);
        if (made > 0)
          SDL_PutAudioStreamData(sound_.get(), samples_.data(), made * 2 * static_cast<int>(sizeof(float)));
      }
      av_frame_unref(frame_.get());
    }
  }
  void open_sound() {
    SwrContext* raw = nullptr;
    const AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    if (swr_alloc_set_opts2(&raw, &stereo, AV_SAMPLE_FMT_FLT, kRate, &audio_->ch_layout, audio_->sample_fmt,
                            audio_->sample_rate, 0, nullptr) < 0 ||
        swr_init(raw) < 0) {
      swr_free(&raw);
      return;
    }
    resampler_.reset(raw);
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO))
      return;
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, kRate};
    sound_.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr));
  }
  void resume_sound() {
    if (sound_)
      SDL_ResumeAudioStreamDevice(sound_.get());
  }
  void pause_sound() {
    if (sound_)
      SDL_PauseAudioStreamDevice(sound_.get());
  }

  std::unique_ptr<AVFormatContext, format_closer> file_;
  std::unique_ptr<AVCodecContext, codec_closer> video_, audio_;
  int video_index_ = -1, audio_index_ = -1;
  std::unique_ptr<AVPacket, packet_closer> packet_;
  std::unique_ptr<AVFrame, frame_closer> frame_;
  std::unique_ptr<SwsContext, scaler_closer> scaler_;
  std::unique_ptr<SwrContext, resampler_closer> resampler_;
  std::unique_ptr<SDL_AudioStream, stream_closer> sound_;
  std::vector<std::uint8_t> rgba_;
  std::vector<std::uint8_t> samples_;
  skia::Sp<skia::SkImage> picture_;
  std::optional<std::pair<skia::Sp<skia::SkImage>, double>> ahead_;
  bool started_ = false, paused_ = false, ended_ = false, draining_ = false;
  double base_ = 0.0, started_ms_ = 0.0, now_ms_ = 0.0;
};

}  // namespace mux::video
