// SPDX-License-Identifier: AGPL-3.0-only
// mux.config: the accounts mux keeps between runs, in
// $XDG_CONFIG_HOME/mux/accounts.json (~/.config/mux/accounts.json when that
// is not set). The passwords are in it, so the file is made readable by its
// owner and no one else before anything is written into it, and replaced
// whole, so a crash halfway through a save leaves the old file.
export module mux.config;

import std;
import splice;
import knot;
import mux.vault;
import mux.proto.kept;

export import :config_choices;
export import :config_settings;
export import :config_looks;
export import :config_limits;
export import :config_accounts;

export namespace mux::config {

// A name -- an address, a room's id, a media source -- as a file's: kept as
// it is where it is plain (letters, digits, '@', '-', '_', and '.' but not
// first), every other byte as %XX -- '%' too, and a leading '.', so no "."
// or ".." and nothing hidden. One name to one file: before, every other
// character became '_', and "!a:b" and "!a_b" shared one.
[[nodiscard]] inline std::string file_name_of(std::string_view name) {
  const auto as_file = [name](std::size_t at) {
    const auto c = static_cast<unsigned char>(name[at]);
    const bool plain = std::isalnum(c) != 0 || c == '@' || c == '-' || c == '_' || (c == '.' && at > 0);
    constexpr std::string_view digits = "0123456789ABCDEF";
    return plain ? std::string(1, name[at]) : std::string{'%', digits[c >> 4], digits[c & 15u]};
  };
  std::string out = std::ranges::to<std::string>(std::views::join(std::views::transform(std::views::iota(std::size_t{0}, name.size()), as_file)));
  return out.empty() ? std::string("%") : out;
}
// The name as files were named before: to find what was kept under it.
[[nodiscard]] inline std::string old_file_name_of(std::string_view name) {
  return std::ranges::to<std::string>(std::views::transform(name, [](char c) {
           return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '@' || c == '.' || c == '-' ? c : '_';
         }));
}
// Where something is kept: what was kept where it was before moved there,
// where it is still there and nothing is where it goes now.
inline std::filesystem::path moved_from(std::filesystem::path now, const std::filesystem::path& before) {
  if (now == before)
    return now;
  std::error_code failed;
  if (!std::filesystem::exists(now, failed) && std::filesystem::exists(before, failed)) {
    std::filesystem::create_directories(now.parent_path(), failed);
    std::filesystem::rename(before, now, failed);
  }
  return now;
}

// Where what can be fetched again is kept: $XDG_CACHE_HOME/mux, or
// ~/.cache/mux.
std::filesystem::path cache_path(std::string_view name) {
  if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg)
    return std::filesystem::path(xdg) / "mux" / name;
  if (const char* home = std::getenv("HOME"); home && *home)
    return std::filesystem::path(home) / ".cache" / "mux" / name;
  return std::filesystem::path(std::format("mux-cache-{}", name));
}

std::filesystem::path default_path() {
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
    return std::filesystem::path(xdg) / "mux" / "accounts.json";
  if (const char* home = std::getenv("HOME"); home && *home)
    return std::filesystem::path(home) / ".config" / "mux" / "accounts.json";
  return std::filesystem::path("mux-accounts.json");
}

// The accounts kept at `where`: none when there is no file yet, and what is
// wrong with it when there is one that cannot be read.
// A protocol with no old list of its own: a list no file has.
namespace legacy_defaults {
template <class Entry>
struct none {
  std::optional<std::vector<Entry>> no_legacy_accounts;
};
template <class Entry>
consteval auto json_schema(knot::type<none<Entry>>) {
  return knot::schema<none<Entry>>();
}
template <class Entry>
constexpr type_tag<none<Entry>> legacy_accounts_type(const auto&, type_tag<Entry>) {
  return {};
}
template <class Entry>
const std::optional<std::vector<Entry>>& legacy_entries(const none<Entry>& all) {
  return all.no_legacy_accounts;
}
}  // namespace legacy_defaults
// An old file's lists, each its protocol's (legacy_accounts_type): its
// entries read once as the protocol's own and once as every account's, the
// two zipped into accounts as files have them now.
template <class... Tags>
[[nodiscard]] std::vector<saved_account> legacy_accounts_in(protocol_list<Tags...>, std::string_view text) {
  std::vector<saved_account> out;
  (
      [&] {
        using kept = kept_of<Tags>;
        using legacy_defaults::legacy_accounts_type;
        using legacy_defaults::legacy_entries;
        using own_list = typename decltype(legacy_accounts_type(kept{}, type_tag<kept>{}))::type;
        using shared_list = typename decltype(legacy_accounts_type(kept{}, type_tag<account_shared>{}))::type;
        const auto own = knot::try_read<own_list>(text);
        const auto shared = knot::try_read<shared_list>(text);
        if (!own || !shared || !legacy_entries(*own) || !legacy_entries(*shared))
          return;
        std::ranges::for_each(std::views::zip(*legacy_entries(*own), *legacy_entries(*shared)), [&](const auto& both) {
          const auto& [mine, every] = both;
          out.push_back(saved_account{std::string(protocol_word(mine)), kept_saved_t{mine}, every});
        });
      }(),
      ...);
  return out;
}

std::expected<file, std::string> load(const std::filesystem::path& where, mux::vault::vault& vault) {
  std::error_code failed;
  if (!std::filesystem::exists(where, failed))
    return file{};
  // Through the vault: sealed where local data is encrypted, and not read
  // with it locked or with another key.
  const auto opened = vault.read_file(where);
  if (!opened)
    return std::unexpected(std::format("cannot open {}{}", where.string(),
                                       vault.locked() ? ": local data is encrypted and locked" : ""));
  const std::string& text = *opened;
  auto read = knot::try_read<file>(text);
  if (!read)
    return std::unexpected(
        std::format("{} is not an accounts file: {} at {}", where.string(), read.error().message, read.error().offset));
  // An old file's lists, each its protocol's, read into accounts as they are
  // now, before the file's own.
  std::vector<saved_account> old = legacy_accounts_in(protocols{}, text);
  if (!old.empty()) {
    std::ranges::move(read->accounts.value_or(std::vector<saved_account>{}), std::back_inserter(old));
    read->accounts = std::move(old);
  }
  return std::move(*read);
}

// The accounts written to `where`: the directory made (its owner's alone),
// the new file made its owner's alone before the passwords go into it, and
// put in place of the old one in one rename.
std::expected<void, std::string> save(const std::filesystem::path& where, const file& accounts, mux::vault::vault& vault) {
  namespace fs = std::filesystem;
  std::error_code failed;
  if (where.has_parent_path() && !fs::exists(where.parent_path(), failed)) {
    fs::create_directories(where.parent_path(), failed);
    if (failed)
      return std::unexpected(std::format("cannot make {}: {}", where.parent_path().string(), failed.message()));
    fs::permissions(where.parent_path(), fs::perms::owner_all, fs::perm_options::replace, failed);
  }
  // Through the vault: made the owner's alone before the passwords go in,
  // sealed where local data is encrypted, put in place in one rename.
  std::string text;
  knot::write(text, accounts);
  text += '\n';
  if (!vault.write_file(where, text, true))
    return std::unexpected(std::format("cannot write {}", where.string()));
  return {};
}

}  // namespace mux::config
