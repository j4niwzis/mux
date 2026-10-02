// SPDX-License-Identifier: AGPL-3.0-only
// mux.logic.sending: a file as it is sent -- a picture known by its bytes,
// and one dropped on the window without its metadata and under a plain
// name, as Settings says.
export module mux.logic.sending;

import std;
import mux.config;
import mux.media;

export namespace mux::logic {

struct prepared {
  std::string bytes;
  std::string name;
  std::string mimetype = "application/octet-stream";
  std::optional<mux::media::picture_t> picture;  // what picture it is, where it is one
};

// A video, known by its first bytes as a picture is: an ISO media file's
// ftyp box (MP4; QuickTime's brand "qt  "; but not M4A, which is sound) or
// Matroska's EBML header (WebM, MKV). Its type is what lets it go as
// m.video: sent as application/octet-stream, an MP4 was a file.
[[nodiscard]] inline std::optional<std::string_view> video_type_of(std::string_view bytes) {
  if (bytes.size() >= 12 && bytes.substr(4, 4) == "ftyp") {
    const std::string_view brand = bytes.substr(8, 4);
    if (brand.starts_with("M4A") || brand.starts_with("M4B"))
      return std::nullopt;
    return brand == "qt  " ? std::string_view("video/quicktime") : std::string_view("video/mp4");
  }
  if (bytes.starts_with("\x1A\x45\xDF\xA3"))
    return std::string_view(bytes.find("webm") < 64 ? "video/webm" : "video/x-matroska");
  return std::nullopt;
}

// A file's bytes and name, as they go: a picture's type read from its
// bytes; a dropped picture's metadata cut out (its pixels' bytes as they
// were) and its name image.<type>, where the settings say so.
[[nodiscard]] inline prepared prepared_of(std::string bytes, std::string name, bool dropped,
                                          const mux::config::sending_settings& settings) {
  prepared out{std::move(bytes), std::move(name)};
  out.picture = mux::media::picture_of(out.bytes);
  if (!out.picture) {
    if (const auto video = video_type_of(out.bytes))
      out.mimetype = std::string(*video);
    return out;
  }
  out.mimetype = std::string(mux::media::mimetype_of(*out.picture));
  if (dropped && settings.strip_metadata)
    out.bytes = mux::media::without_metadata(out.bytes);
  if (dropped && settings.rename)
    out.name = std::format("image.{}", mux::media::extension_of(*out.picture));
  return out;
}

// A text worth sending: anything but blanks.
[[nodiscard]] inline bool sendable(std::string_view text) {
  return !std::ranges::all_of(text, [](unsigned char c) { return std::isspace(c) != 0; });
}

}  // namespace mux::logic
