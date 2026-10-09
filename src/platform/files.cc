// SPDX-License-Identifier: AGPL-3.0-only
export module mux.platform.files;
import std;

export namespace mux::platform::files {
inline std::optional<std::string> read(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return std::nullopt;
  // Read blocks, including for streams without a known size. Per-character
  // stream iterators made cached media reads expensive in debug builds.
  std::string bytes;
  std::array<char, 8192> block;
  while (in.read(block.data(), static_cast<std::streamsize>(block.size())) || in.gcount() > 0)
    bytes.append(block.data(), static_cast<std::size_t>(in.gcount()));
  if (in.bad() || (!in.eof() && in.fail()))
    return std::nullopt;
  return bytes;
}
inline std::string name(const std::string& path) { return std::filesystem::path(path).filename().string(); }
inline bool write(const std::string& path, std::string_view bytes) {
  std::ofstream out(path, std::ios::binary);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  out.close();
  return !out.fail();
}
}
