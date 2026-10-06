// SPDX-License-Identifier: AGPL-3.0-only
export module mux.platform.files;
import std;
import splice.bytes;

export namespace mux::platform::files {
inline std::optional<std::string> read(const std::string& path) { return spl::bytes::file_text(path); }
inline std::string name(const std::string& path) { return std::filesystem::path(path).filename().string(); }
inline bool write(const std::string& path, std::string_view bytes) {
  std::ofstream out(path, std::ios::binary);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  out.close();
  return !out.fail();
}
}
