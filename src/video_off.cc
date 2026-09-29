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
  [[nodiscard]] static std::unique_ptr<player> open(const std::filesystem::path&) { return nullptr; }
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
