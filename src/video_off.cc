// SPDX-License-Identifier: AGPL-3.0-only
// mux.video, built without FFmpeg (MUX_VIDEO off): the same player, which
// opens nothing -- a video is then played by the system's player, as a file
// is opened.
export module mux.video;

import std;
import skia;

export namespace mux::video {

// Whether videos play in the window, in this build.
inline constexpr bool kPlays = false;

class player {
 public:
  // What a video is, looked at before it is sent: its size, its length and
  // its first picture -- what a message of it says, the picture its
  // thumbnail. Nothing where it cannot be read.
  struct look_t {
    int width = 0, height = 0;
    double seconds = 0.0;
    skia::Sp<skia::SkImage> first;
  };
  [[nodiscard]] static std::optional<look_t> look(const std::filesystem::path&) { return std::nullopt; }
  [[nodiscard]] static std::unique_ptr<player> open(const std::filesystem::path&, bool = true) { return nullptr; }
  bool advance(double) { return false; }
  void toggle() {}
  void seek(double) {}
  [[nodiscard]] bool paused() const noexcept { return true; }
  [[nodiscard]] double position() const { return 0.0; }
  [[nodiscard]] double length() const { return 0.0; }
  [[nodiscard]] const skia::Sp<skia::SkImage>& picture() const noexcept { return picture_; }

 private:
  skia::Sp<skia::SkImage> picture_;
};

}  // namespace mux::video
