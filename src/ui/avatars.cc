// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:avatars -- Avatars: their colours, initials, the pictures kept, and drawing them.
export module mux.ui:avatars;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.widgets;
import mux.core;
import mux.config;
export import :icons;

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
[[nodiscard]] inline std::string initials_of(std::string_view name) {
  if (name.starts_with('@'))
    name.remove_prefix(1);
  name = name.substr(0, name.find_first_of("@:"));
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
// again when it is wanted. Three of them, so that one kind never pushes
// another out: avatars (by a chat's id, a person's), the thumbnails of the
// pictures in messages, and whole pictures (both by their source).
class avatar_cache {
 public:
  // Held to this many bytes; set from Storage.
  std::size_t budget = 32u << 20;
  void clear() {
    images_.clear();
    order_.clear();
    bytes_ = 0;
  }

  // A picture, counted as used now.
  [[nodiscard]] const skia::Sp<skia::SkImage>* find(std::string_view key) {
    const auto found = images_.find(key);
    if (found == images_.end())
      return nullptr;
    order_.splice(order_.begin(), order_, found->second.used);
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
    images_.emplace(std::move(key), entry{std::move(image), order_.begin(), size});
    bytes_ += size;
    while (bytes_ > budget && order_.size() > 1) {
      const auto oldest = images_.find(order_.back());
      bytes_ -= oldest->second.bytes;
      images_.erase(oldest);
      order_.pop_back();
    }
  }

 private:
  struct entry {
    skia::Sp<skia::SkImage> image;
    std::list<std::string>::iterator used;
    std::size_t bytes = 0;
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
inline image_cache& thumbnails() {
  static image_cache images;
  return images;
}
inline image_cache& whole_pictures() {
  static image_cache images;
  return images;
}

inline void draw_avatar(skia::SkCanvas* canvas, const skia::SkRect& disc, std::string_view id, std::string_view name,
                        float alpha) {
  if (const skia::Sp<skia::SkImage>* found = avatar_images().find(id); found && *found) {
    const int saved = canvas->save();
    canvas->clipRRect(skia::SkRRect::MakeOval(disc), true);
    skia::SkPaint paint;
    paint.setAlphaf(alpha);
    canvas->drawImageRect(*found, disc, skia::SkSamplingOptions(skia::SkFilterMode::kLinear), &paint);
    canvas->restoreToCount(saved);
    return;
  }
  skia::SkFont* font = skiff::paint::defaultFont();
  if (font == nullptr)
    return;
  const skiff::paint::Painter p(canvas, *font);
  const auto [top, bottom] = userpic_colours(id);
  const int saved = canvas->save();
  canvas->clipRRect(skia::SkRRect::MakeOval(disc), true);
  skiff::paint::verticalGradient(canvas, disc, top, bottom, alpha);
  canvas->restoreToCount(saved);
  const std::string letters = initials_of(name);
  const float size = disc.width() * 0.4f;
  const float width = p.measure(letters, size, true);
  p.textIn(disc, letters, size, skia::colorSetARGB(255, 255, 255, 255), alpha, true, (disc.width() - width) * 0.5f);
}

}  // namespace mux::ui
