// SPDX-License-Identifier: AGPL-3.0-only
// mux.media: what is sent is sent as it is, less what it says about where
// and how it was made. A picture's file keeps its pixels' bytes untouched --
// nothing is decoded or written anew -- and loses the blocks that carry its
// metadata: a JPEG's APPn segments (EXIF, XMP, IPTC, a maker's) and its
// comments, a PNG's text, eXIf and tIME chunks, a WebP's EXIF and XMP
// chunks, a GIF's comments. What a picture is drawn by stays: a JPEG's JFIF
// header and its colour profile (APP2 ICC_PROFILE), a PNG's colour chunks.
export module mux.media;

import std;

export namespace mux::media {

// A picture's type, by its file's first bytes.
namespace picture {
struct png {};
struct jpeg {};
struct gif {};
struct webp {};
}  // namespace picture
using picture_t = std::variant<picture::png, picture::jpeg, picture::gif, picture::webp>;

[[nodiscard]] inline std::optional<picture_t> picture_of(std::string_view bytes) {
  if (bytes.starts_with("\x89PNG\r\n\x1A\n"))
    return picture::png{};
  if (bytes.starts_with("\xFF\xD8\xFF"))
    return picture::jpeg{};
  if (bytes.starts_with("GIF87a") || bytes.starts_with("GIF89a"))
    return picture::gif{};
  if (bytes.size() >= 12 && bytes.starts_with("RIFF") && bytes.substr(8, 4) == "WEBP")
    return picture::webp{};
  return std::nullopt;
}
[[nodiscard]] inline std::string_view mimetype_of(const picture_t& type) {
  return std::visit([](auto one) -> std::string_view {
    using T = decltype(one);
    if constexpr (std::same_as<T, picture::png>) return "image/png";
    else if constexpr (std::same_as<T, picture::jpeg>) return "image/jpeg";
    else if constexpr (std::same_as<T, picture::gif>) return "image/gif";
    else return "image/webp";
  }, type);
}
[[nodiscard]] inline std::string_view extension_of(const picture_t& type) {
  return std::visit([](auto one) -> std::string_view {
    using T = decltype(one);
    if constexpr (std::same_as<T, picture::png>) return "png";
    else if constexpr (std::same_as<T, picture::jpeg>) return "jpg";
    else if constexpr (std::same_as<T, picture::gif>) return "gif";
    else return "webp";
  }, type);
}

namespace detail {
[[nodiscard]] inline std::uint32_t big32(std::string_view b, std::size_t at) {
  return (std::uint32_t(std::uint8_t(b[at])) << 24) | (std::uint32_t(std::uint8_t(b[at + 1])) << 16) |
         (std::uint32_t(std::uint8_t(b[at + 2])) << 8) | std::uint32_t(std::uint8_t(b[at + 3]));
}
[[nodiscard]] inline std::uint32_t little32(std::string_view b, std::size_t at) {
  return std::uint32_t(std::uint8_t(b[at])) | (std::uint32_t(std::uint8_t(b[at + 1])) << 8) |
         (std::uint32_t(std::uint8_t(b[at + 2])) << 16) | (std::uint32_t(std::uint8_t(b[at + 3])) << 24);
}
inline void put_little32(std::string& b, std::size_t at, std::uint32_t v) {
  for (int i = 0; i < 4; ++i)
    b[at + static_cast<std::size_t>(i)] = static_cast<char>((v >> (8 * i)) & 0xFF);
}

// A JPEG less its APPn segments but JFIF (APP0) and the ICC profile (APP2),
// and less its comments; from the start of the scan on, as it is.
[[nodiscard]] inline std::string strip_jpeg(std::string_view in) {
  std::string out(in.substr(0, 2));  // SOI
  std::size_t at = 2;
  while (at + 4 <= in.size()) {
    if (std::uint8_t(in[at]) != 0xFF)
      return std::string(in);  // not what a JPEG is: left alone
    const std::uint8_t marker = std::uint8_t(in[at + 1]);
    if (marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7) || marker == 0x01) {
      out.append(in.substr(at, 2));
      at += 2;
      continue;
    }
    const std::size_t length = (std::size_t(std::uint8_t(in[at + 2])) << 8) | std::uint8_t(in[at + 3]);
    if (length < 2 || at + 2 + length > in.size())
      return std::string(in);
    const std::string_view segment = in.substr(at, 2 + length);
    const std::string_view payload = segment.substr(4);
    const bool app = marker >= 0xE0 && marker <= 0xEF;
    const bool keep = (!app && marker != 0xFE) || (marker == 0xE0 && payload.starts_with("JFIF")) ||
                      (marker == 0xE2 && payload.starts_with("ICC_PROFILE"));
    if (keep)
      out.append(segment);
    at += 2 + length;
    if (marker == 0xDA) {  // the scan: all the rest is the picture
      out.append(in.substr(at));
      return out;
    }
  }
  return std::string(in);
}

// A PNG less its text, eXIf and tIME chunks.
[[nodiscard]] inline std::string strip_png(std::string_view in) {
  std::string out(in.substr(0, 8));
  std::size_t at = 8;
  while (at + 12 <= in.size()) {
    const std::size_t length = big32(in, at);
    if (at + 12 + length > in.size())
      return std::string(in);
    const std::string_view type = in.substr(at + 4, 4);
    const bool metadata = type == "tEXt" || type == "zTXt" || type == "iTXt" || type == "eXIf" || type == "tIME";
    if (!metadata)
      out.append(in.substr(at, 12 + length));
    at += 12 + length;
    if (type == "IEND")
      return out;
  }
  return std::string(in);
}

// A WebP less its EXIF and XMP chunks, its header's flags and size set to
// what is left.
[[nodiscard]] inline std::string strip_webp(std::string_view in) {
  std::string out(in.substr(0, 12));
  std::size_t at = 12;
  std::optional<std::size_t> vp8x;
  while (at + 8 <= in.size()) {
    const std::size_t length = little32(in, at + 4);
    const std::size_t padded = length + (length & 1);
    if (at + 8 + length > in.size())
      return std::string(in);
    const std::string_view type = in.substr(at, 4);
    if (type != "EXIF" && type != "XMP ") {
      if (type == "VP8X")
        vp8x = out.size();
      out.append(in.substr(at, std::min(8 + padded, in.size() - at)));
    }
    at += 8 + padded;
  }
  if (vp8x && *vp8x + 8 < out.size())
    out[*vp8x + 8] = static_cast<char>(std::uint8_t(out[*vp8x + 8]) & ~0x0C);  // no EXIF, no XMP
  put_little32(out, 4, static_cast<std::uint32_t>(out.size() - 8));
  return out;
}

// A GIF less its comment extensions.
[[nodiscard]] inline std::string strip_gif(std::string_view in) {
  if (in.size() < 13)
    return std::string(in);
  const std::uint8_t packed = std::uint8_t(in[10]);
  std::size_t at = 13 + ((packed & 0x80) ? 3u * (1u << ((packed & 7) + 1)) : 0u);
  if (at > in.size())
    return std::string(in);
  std::string out(in.substr(0, at));
  const auto skip_blocks = [&](std::size_t from) -> std::optional<std::size_t> {
    while (from < in.size()) {
      const std::size_t size = std::uint8_t(in[from]);
      if (size == 0)
        return from + 1;
      from += 1 + size;
    }
    return std::nullopt;
  };
  while (at < in.size()) {
    const std::uint8_t kind = std::uint8_t(in[at]);
    if (kind == 0x3B) {  // the end
      out.push_back(in[at]);
      return out;
    }
    if (kind == 0x21 && at + 2 <= in.size()) {  // an extension
      const auto end = skip_blocks(at + 2);
      if (!end)
        return std::string(in);
      if (std::uint8_t(in[at + 1]) != 0xFE)  // not a comment: kept
        out.append(in.substr(at, *end - at));
      at = *end;
      continue;
    }
    if (kind == 0x2C && at + 10 <= in.size()) {  // an image: as it is
      const std::uint8_t local = std::uint8_t(in[at + 9]);
      std::size_t next = at + 10 + ((local & 0x80) ? 3u * (1u << ((local & 7) + 1)) : 0u) + 1;  // + LZW min size
      const auto end = skip_blocks(next);
      if (!end)
        return std::string(in);
      out.append(in.substr(at, *end - at));
      at = *end;
      continue;
    }
    return std::string(in);
  }
  return std::string(in);
}
}  // namespace detail

// A picture's file less its metadata, its pixels' bytes as they were; any
// other file, as it is.
[[nodiscard]] inline std::string without_metadata(std::string_view bytes) {
  const auto type = picture_of(bytes);
  if (!type)
    return std::string(bytes);
  return std::visit([&](auto one) {
    using T = decltype(one);
    if constexpr (std::same_as<T, picture::png>) return detail::strip_png(bytes);
    else if constexpr (std::same_as<T, picture::jpeg>) return detail::strip_jpeg(bytes);
    else if constexpr (std::same_as<T, picture::gif>) return detail::strip_gif(bytes);
    else return detail::strip_webp(bytes);
  }, *type);
}

}  // namespace mux::media
