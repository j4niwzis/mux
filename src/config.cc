// SPDX-License-Identifier: AGPL-3.0-only
// mux.config: the accounts mux keeps between runs, in
// $XDG_CONFIG_HOME/mux/accounts.json (~/.config/mux/accounts.json when that
// is not set). The passwords are in it, so the file is made readable by its
// owner and no one else before anything is written into it, and replaced
// whole, so a crash halfway through a save leaves the old file.
export module mux.config;

import std;
import knot;

export namespace mux::config {

// One account as it is kept: what the login screen asks for.
struct saved_account {
  std::string address;  // user@domain for XMPP, @user:server for Matrix
  std::string password;
  bool enabled = true;
  // XMPP: where to connect, instead of what the domain's SRV records say.
  std::optional<std::string> host;
  std::optional<std::int64_t> port;
  // Matrix: the client-server API's base URL, instead of .well-known's.
  std::optional<std::string> homeserver;
  friend bool operator==(const saved_account&, const saved_account&) = default;
};

struct file {
  std::vector<saved_account> accounts;
  friend bool operator==(const file&, const file&) = default;
};

// An address says its protocol: a Matrix user id begins with '@'.
constexpr bool is_matrix(std::string_view address) noexcept { return address.starts_with('@'); }

// What is wrong with an account as typed, said so that it can be shown
// under the field; nothing when it can be used.
std::optional<std::string> check(const saved_account& one) {
  const std::string_view address = one.address;
  if (address.empty())
    return "Type an address: user@example.com or @user:example.org";
  if (address.find_first_of(" \t\r\n") != std::string_view::npos)
    return "An address has no spaces in it";
  if (is_matrix(address)) {
    const auto colon = address.find(':');
    if (colon == std::string_view::npos || colon == 1 || colon + 1 == address.size())
      return "A Matrix address is @user:server";
    if (one.host || one.port)
      return "Host and port are for XMPP; a Matrix account takes a homeserver URL";
    if (one.homeserver && !one.homeserver->starts_with("https://") && !one.homeserver->starts_with("http://"))
      return "The homeserver is a URL: https://matrix.example.org";
  } else {
    const auto at = address.find('@');
    if (at == std::string_view::npos || at == 0 || at + 1 == address.size() ||
        address.find('@', at + 1) != std::string_view::npos)
      return "An XMPP address is user@domain";
    if (one.homeserver)
      return "A homeserver is for Matrix; an XMPP account takes a host and a port";
    if (one.port && (*one.port < 1 || *one.port > 65535))
      return "A port is a number from 1 to 65535";
  }
  if (one.password.empty())
    return "Type the password";
  return std::nullopt;
}

// Where the file is: $XDG_CONFIG_HOME/mux, else $HOME/.config/mux.
std::filesystem::path default_path() {
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
    return std::filesystem::path(xdg) / "mux" / "accounts.json";
  if (const char* home = std::getenv("HOME"); home && *home)
    return std::filesystem::path(home) / ".config" / "mux" / "accounts.json";
  return std::filesystem::path("mux-accounts.json");
}

// The accounts kept at `where`: none when there is no file yet, and what is
// wrong with it when there is one that cannot be read.
std::expected<file, std::string> load(const std::filesystem::path& where) {
  std::error_code failed;
  if (!std::filesystem::exists(where, failed))
    return file{};
  std::ifstream in(where, std::ios::binary);
  if (!in)
    return std::unexpected(std::format("cannot open {}", where.string()));
  const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  auto read = knot::try_read<file>(text);
  if (!read)
    return std::unexpected(
        std::format("{} is not an accounts file: {} at {}", where.string(), read.error().message, read.error().offset));
  return std::move(*read);
}

// The accounts written to `where`: the directory made (its owner's alone),
// the new file made its owner's alone before the passwords go into it, and
// put in place of the old one in one rename.
std::expected<void, std::string> save(const std::filesystem::path& where, const file& accounts) {
  namespace fs = std::filesystem;
  std::error_code failed;
  if (where.has_parent_path() && !fs::exists(where.parent_path(), failed)) {
    fs::create_directories(where.parent_path(), failed);
    if (failed)
      return std::unexpected(std::format("cannot make {}: {}", where.parent_path().string(), failed.message()));
    fs::permissions(where.parent_path(), fs::perms::owner_all, fs::perm_options::replace, failed);
  }
  fs::path temporary = where;
  temporary += ".new";
  {
    std::ofstream made(temporary, std::ios::binary | std::ios::trunc);
    if (!made)
      return std::unexpected(std::format("cannot write {}", temporary.string()));
  }
  fs::permissions(temporary, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, failed);
  if (failed)
    return std::unexpected(std::format("cannot make {} private: {}", temporary.string(), failed.message()));
  std::string text;
  knot::write(text, accounts);
  {
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    out << text << '\n';
    out.flush();
    if (!out)
      return std::unexpected(std::format("cannot write {}", temporary.string()));
  }
  fs::rename(temporary, where, failed);
  if (failed)
    return std::unexpected(std::format("cannot replace {}: {}", where.string(), failed.message()));
  return {};
}

}  // namespace mux::config
