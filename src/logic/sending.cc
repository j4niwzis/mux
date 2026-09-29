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

// A file's bytes and name, as they go: a picture's type read from its
// bytes; a dropped picture's metadata cut out (its pixels' bytes as they
// were) and its name image.<type>, where the settings say so.
[[nodiscard]] inline prepared prepared_of(std::string bytes, std::string name, bool dropped,
                                          const mux::config::sending_settings& settings) {
  prepared out{std::move(bytes), std::move(name)};
  out.picture = mux::media::picture_of(out.bytes);
  if (!out.picture)
    return out;
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
