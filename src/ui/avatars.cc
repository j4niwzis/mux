// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:avatars -- Avatars: their colours, initials, the pictures kept, and drawing them.
export module mux.ui:avatars;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.image;
import mux.core;
import mux.config;
import :base;
import :icons;

export namespace mux::ui {

// An avatar's colour, the same every time for the same id.
[[nodiscard]] inline skia::SkColor avatar_colour(std::string_view id) {
  static constexpr std::array<skia::SkColor, 7> palette{
      skia::colorSetARGB(255, 229, 115, 115), skia::colorSetARGB(255, 235, 150, 80),
      skia::colorSetARGB(255, 112, 185, 105), skia::colorSetARGB(255, 82, 160, 225),
      skia::colorSetARGB(255, 150, 120, 225), skia::colorSetARGB(255, 80, 185, 190),
      skia::colorSetARGB(255, 220, 110, 170)};
  return palette[std::hash<std::string_view>{}(id) % palette.size()];
}

// A proxy profile's colour, as Gajim gives each its own: the same every
// time for the same name, so an account's choice is known at a glance.
[[nodiscard]] inline skia::SkColor proxy_colour(std::string_view name) { return avatar_colour(name); }

// Up to two letters for an avatar: the first character of each of the first
// two words of a name, or of an address's local part -- in any script, whole
// (a Cyrillic name has Cyrillic initials, not a question mark).
[[nodiscard]] inline std::string initials_of(std::string_view whole) {
  // An address: its local part, as its protocol has it.
  const std::string local = proto::local_part_of(state_before(protocol_of(whole)), whole);
  std::string_view name = local;
  std::string out;
  int taken = 0;
  bool start = true;
  for (std::size_t at = 0; at < name.size() && taken < 2;) {
    const auto lead = static_cast<unsigned char>(name[at]);
    const std::size_t length = lead < 0x80 ? 1 : lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    const bool letter = lead >= 0x80 || std::isalnum(lead) != 0;
    if (letter && start) {
      out += lead < 0x80 ? std::string(1, static_cast<char>(std::toupper(lead))) : std::string(name.substr(at, length));
      ++taken;
    }
    start = !letter;
    at += length;
  }
  return out.empty() ? std::string("?") : out;
}

// Telegram's userpics for those with no picture: a gradient, top to bottom,
// of one of its seven pairs (tdesktop's historyPeerNUserpicBg and Bg2), the
// same for the same id, with the initials in white.
[[nodiscard]] inline std::pair<skia::SkColor, skia::SkColor> userpic_colours(std::string_view id) {
  static constexpr std::array<std::pair<skia::SkColor, skia::SkColor>, 7> pairs{{
      {skia::colorSetARGB(255, 0xff, 0x84, 0x5e), skia::colorSetARGB(255, 0xd4, 0x52, 0x46)},  // red
      {skia::colorSetARGB(255, 0xfe, 0xbb, 0x5b), skia::colorSetARGB(255, 0xf6, 0x81, 0x36)},  // orange
      {skia::colorSetARGB(255, 0xb6, 0x94, 0xf9), skia::colorSetARGB(255, 0x6c, 0x61, 0xdf)},  // violet
      {skia::colorSetARGB(255, 0x9a, 0xd1, 0x64), skia::colorSetARGB(255, 0x46, 0xba, 0x43)},  // green
      {skia::colorSetARGB(255, 0x5b, 0xcb, 0xe3), skia::colorSetARGB(255, 0x35, 0x9a, 0xd4)},  // cyan
      {skia::colorSetARGB(255, 0x5c, 0xaf, 0xfa), skia::colorSetARGB(255, 0x40, 0x8a, 0xcf)},  // blue
      {skia::colorSetARGB(255, 0xff, 0x8a, 0xac), skia::colorSetARGB(255, 0xd9, 0x55, 0x74)},  // pink
  }};
  return pairs[std::hash<std::string_view>{}(id) % pairs.size()];
}

// A round avatar: the colour of `id`, and the initials of `name` in it.
// Pictures fetched, decoded, by what they are of. Least recently drawn
// first out, past a number of bytes; what is out is read from the disk
// again when it is wanted. Each kind has its own store and budget:
// avatars, emoji, stickers, message thumbnails, and whole pictures.
class avatar_cache {
 public:
  // Held to this many bytes; set from Storage. What was drawn in this frame
  // or the one before is never let go, over the budget if need be: what is
  // on screen stays.
  std::size_t budget = 32u << 20;
  // The pictures on screen waiting for one of these: woken as one comes.
  skiff::scene::Waiters waiting;
  void clear() {
    images_.clear();
    order_.clear();
    bytes_ = 0;
  }

  // A picture, counted as used now: in this frame.
  [[nodiscard]] const skia::Sp<skia::SkImage>* find(std::string_view key) {
    const auto found = images_.find(key);
    if (found == images_.end())
      return nullptr;
    order_.splice(order_.begin(), order_, found->second.used);
    found->second.frame = frame();
    return &found->second.image;
  }
  [[nodiscard]] bool has(std::string_view key) const { return images_.contains(key); }
  void put(std::string key, skia::Sp<skia::SkImage> image) {
    if (!image)
      return;
    if (const auto found = images_.find(key); found != images_.end()) {
      bytes_ -= found->second.bytes;
      order_.erase(found->second.used);
      images_.erase(found);
    }
    const std::size_t size = static_cast<std::size_t>(image->width()) * static_cast<std::size_t>(image->height()) * 4u;
    order_.push_front(key);
    images_.emplace(std::move(key), entry{std::move(image), order_.begin(), size, frame()});
    bytes_ += size;
    // The least recently used out -- but none drawn lately: from there on,
    // everything is newer still.
    while (bytes_ > budget && order_.size() > 1) {
      const auto oldest = images_.find(order_.back());
      if (oldest->second.frame + 1 >= frame())
        break;
      bytes_ -= oldest->second.bytes;
      images_.erase(oldest);
      order_.pop_back();
    }
    waiting.wake();
  }

  // The frame being drawn, counted by the window: what was used in it and
  // the one before is on screen.
  static std::uint64_t& frame() {
    static std::uint64_t now = 1;
    return now;
  }

 private:
  struct entry {
    skia::Sp<skia::SkImage> image;
    std::list<std::string>::iterator used;
    std::size_t bytes = 0;
    std::uint64_t frame = 0;  // the last it was drawn in
  };
  std::list<std::string> order_;  // the most recently used first
  std::map<std::string, entry, std::less<>> images_;
  std::size_t bytes_ = 0;
};
using image_cache = avatar_cache;
inline image_cache& avatar_images() {
  static image_cache images;
  return images;
}
inline image_cache& emoji_images() {
  static image_cache images;
  return images;
}
inline image_cache& sticker_images() {
  static image_cache images;
  return images;
}
inline image_cache& emote_images(emote_kind kind = emote_kind::emoji) {
  return kind == emote_kind::emoji ? emoji_images() : sticker_images();
}
inline image_cache& thumbnails() {
  static image_cache images;
  return images;
}
// How far what is being fetched whole has come, by its source: what its
// loader shows.
inline std::map<std::string, float, std::less<>>& download_progress() {
  static std::map<std::string, float, std::less<>> kept;
  return kept;
}
// Downloads the user stopped, by source: shown stopped until asked again.
inline std::set<std::string, std::less<>>& stopped_downloads() {
  static std::set<std::string, std::less<>> kept;
  return kept;
}
[[nodiscard]] inline std::optional<float> progress_of(std::string_view source) {
  const auto found = download_progress().find(source);
  return found == download_progress().end() ? std::nullopt : std::optional(found->second);
}

// A picture's blurred preview, from its blurhash, by its source.
inline image_cache& previews() {
  static image_cache images;
  return images;
}
inline image_cache& whole_pictures() {
  static image_cache images;
  return images;
}

// Pictures that move -- a GIF, an animated WebP -- by their source: their
// frames, each with how long it stays, shown in place of the still picture,
// the frame for the time. Held to a number of bytes, the least recently
// drawn out first; none drawn lately.
class animation_cache {
 public:
  std::size_t budget = 128u << 20;
  void put(std::string key, std::vector<skia::Frame> frames) {
    if (frames.size() < 2)
      return;
    animation made;
    for (const skia::Frame& one : frames) {
      made.total_ms += one.durationMs;
      made.bytes += static_cast<std::size_t>(one.image->width()) * static_cast<std::size_t>(one.image->height()) * 4u;
    }
    made.frames = std::move(frames);
    made.drawn = image_cache::frame();
    if (const auto found = all_.find(key); found != all_.end())
      bytes_ -= found->second.bytes;
    bytes_ += made.bytes;
    all_.insert_or_assign(std::move(key), std::move(made));
    while (bytes_ > budget) {
      auto oldest = all_.end();
      for (auto it = all_.begin(); it != all_.end(); ++it)
        if (it->second.drawn + 1 < image_cache::frame() && (oldest == all_.end() || it->second.drawn < oldest->second.drawn))
          oldest = it;
      if (oldest == all_.end())
        break;
      bytes_ -= oldest->second.bytes;
      all_.erase(oldest);
    }
  }
  [[nodiscard]] bool has(std::string_view key) const { return all_.contains(key); }
  // The frame of `key`'s animation to show at `ms` of a steady clock.
  [[nodiscard]] const skia::Sp<skia::SkImage>* at(std::string_view key, double ms) {
    const auto found = all_.find(key);
    if (found == all_.end() || found->second.total_ms <= 0)
      return nullptr;
    animation& one = found->second;
    one.drawn = image_cache::frame();
    double into = std::fmod(ms, static_cast<double>(one.total_ms));
    for (const skia::Frame& each : one.frames) {
      if (into < each.durationMs)
        return &each.image;
      into -= each.durationMs;
    }
    return &one.frames.back().image;
  }
  void clear() {
    all_.clear();
    bytes_ = 0;
  }

 private:
  struct animation {
    std::vector<skia::Frame> frames;
    int total_ms = 0;
    std::size_t bytes = 0;
    std::uint64_t drawn = 0;
  };
  std::map<std::string, animation, std::less<>> all_;
  std::size_t bytes_ = 0;
};
inline animation_cache& animations() {
  static animation_cache all;
  return all;
}
inline animation_cache& emoji_animations() {
  static animation_cache all;
  return all;
}
inline animation_cache& sticker_animations() {
  static animation_cache all;
  return all;
}
inline animation_cache& emote_animations(emote_kind kind = emote_kind::emoji) {
  return kind == emote_kind::emoji ? emoji_animations() : sticker_animations();
}
// The clock animations are shown by.
[[nodiscard]] inline double animation_clock() {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Where a picture comes from, as an Image's source: a key, and where it is
// looked up -- asked for each frame, so one that comes later is shown.
// Values of types of their own, not functions kept.
using picture_ptr = const skia::Sp<skia::SkImage>*;
struct from_avatars {  // a chat's or a person's
  std::string key;
  picture_ptr operator()() const { return avatar_images().find(key); }
  skiff::scene::Waiters& waiters() const { return avatar_images().waiting; }
};
struct from_emotes {
  std::string key;
  emote_kind kind = emote_kind::emoji;
  picture_ptr operator()() const {
    if (picture_ptr still = emote_images(kind).find(key))
      return still;
    return emote_animations(kind).at(key, animation_clock());
  }
  skiff::scene::Waiters& waiters() const { return emote_images(kind).waiting; }
};
struct from_previews {  // blurred, from a blurhash, until the picture comes
  std::string key;
  picture_ptr operator()() const { return previews().find(key); }
};
struct from_thumbnails {
  std::string key;
  bool sticker = false;
  picture_ptr operator()() const { return (sticker ? sticker_images() : thumbnails()).find(key); }
  skiff::scene::Waiters& waiters() const { return (sticker ? sticker_images() : thumbnails()).waiting; }
};
// Where it moves, the frame for now; else its thumbnail.
struct from_moving_thumbnail {
  std::string key;
  bool emote = false;
  picture_ptr operator()() const {
    if (emote) {
      if (picture_ptr moving = sticker_animations().at(key, animation_clock()))
        return moving;
      return sticker_images().find(key);
    }
    if (picture_ptr moving = animations().at(key, animation_clock()))
      return moving;
    // Its thumbnail; else the whole of it, where that came first -- opened
    // in the viewer before its thumbnail was fetched.
    if (picture_ptr small = thumbnails().find(key); small && *small)
      return small;
    return whole_pictures().find(key);
  }
};
// Where it moves, the frame for now; else it whole.
struct from_moving_whole {
  std::string key;
  picture_ptr operator()() const {
    if (picture_ptr moving = animations().at(key, animation_clock()))
      return moving;
    return whole_pictures().find(key);
  }
};
// Where a chat's or a person's picture comes from: the avatars kept, by
// their id.
[[nodiscard]] inline from_avatars picture_of(std::string id) { return {std::move(id)}; }
// Their colours where they have no picture: Telegram's pair for their id.
[[nodiscard]] inline scene::Gradient gradient_of(std::string_view id) {
  const auto [top, bottom] = userpic_colours(id);
  return scene::Gradient{top, bottom};
}


}  // namespace mux::ui
