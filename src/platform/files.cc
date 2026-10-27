// SPDX-License-Identifier: AGPL-3.0-only
export module mux.platform.files;
import std;
import skiff.platform.storage;

export namespace mux::platform::files {
inline std::optional<std::string> read(const std::string &path) { return skiff::platform::storage::read_file(path); }
inline std::string name(const std::string &path) { return std::filesystem::path(path).filename().string(); }
inline bool write(const std::string &path, std::string_view bytes) { return skiff::platform::storage::write_file(path, bytes); }
}
