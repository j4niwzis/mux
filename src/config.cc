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

// A theme, and what draws the window: kept as words in the file, read into
// these once, and used as these everywhere else.
namespace theme {
// Telegram Desktop's four: its base palette, day-blue, night, night-green.
struct classic {
  friend bool operator==(classic, classic) = default;
};
struct day {
  friend bool operator==(day, day) = default;
};
struct tinted {
  friend bool operator==(tinted, tinted) = default;
};
struct night {
  friend bool operator==(night, night) = default;
};
}  // namespace theme
using theme_t = std::variant<theme::classic, theme::day, theme::tinted, theme::night>;
// The accent a theme is drawn with: its own, or one of Telegram's circles --
// each a shade of its own in each theme.
namespace accent {
struct theme_own {
  friend bool operator==(theme_own, theme_own) = default;
};
struct blue {
  friend bool operator==(blue, blue) = default;
};
struct green {
  friend bool operator==(green, green) = default;
};
struct pink {
  friend bool operator==(pink, pink) = default;
};
struct orange {
  friend bool operator==(orange, orange) = default;
};
struct purple {
  friend bool operator==(purple, purple) = default;
};
struct red {
  friend bool operator==(red, red) = default;
};
struct grey {
  friend bool operator==(grey, grey) = default;
};
struct gold {
  friend bool operator==(gold, gold) = default;
};
}  // namespace accent
using accent_t = std::variant<accent::theme_own, accent::blue, accent::green, accent::pink, accent::orange,
                              accent::purple, accent::red, accent::grey, accent::gold>;
namespace renderer {
struct opengl {
  friend bool operator==(opengl, opengl) = default;
};
struct software {
  friend bool operator==(software, software) = default;
};
}  // namespace renderer
using renderer_t = std::variant<renderer::opengl, renderer::software>;
namespace proxy_kind {
struct socks5 {
  friend bool operator==(socks5, socks5) = default;
};
struct http {
  friend bool operator==(http, http) = default;
};
}  // namespace proxy_kind
using proxy_kind_t = std::variant<proxy_kind::socks5, proxy_kind::http>;

// The words of the file, and what they mean: anything else is the default.
[[nodiscard]] inline theme_t theme_of(const std::optional<std::string>& word) {
  if (word == "classic")
    return theme::classic{};
  if (word == "day" || word == "light")  // "light": mux's own light, before
    return theme::day{};
  if (word == "night" || word == "dark")  // "dark": mux's own dark, before
    return theme::night{};
  return theme::tinted{};
}
[[nodiscard]] inline accent_t accent_of(const std::optional<std::string>& word) {
  if (word == "blue" || word == "cyan")
    return accent::blue{};
  if (word == "green")
    return accent::green{};
  if (word == "pink")
    return accent::pink{};
  if (word == "orange")
    return accent::orange{};
  if (word == "purple")
    return accent::purple{};
  if (word == "red")
    return accent::red{};
  if (word == "grey")
    return accent::grey{};
  if (word == "gold")
    return accent::gold{};
  return accent::theme_own{};
}
[[nodiscard]] inline renderer_t renderer_of(const std::optional<std::string>& word) {
  return word == "software" ? renderer_t{renderer::software{}} : renderer_t{renderer::opengl{}};
}
[[nodiscard]] inline proxy_kind_t proxy_kind_of(std::string_view word) {
  return word == "http" ? proxy_kind_t{proxy_kind::http{}} : proxy_kind_t{proxy_kind::socks5{}};
}
[[nodiscard]] constexpr std::string_view word_of(theme::classic) { return "classic"; }
[[nodiscard]] constexpr std::string_view word_of(theme::day) { return "day"; }
[[nodiscard]] constexpr std::string_view word_of(theme::tinted) { return "tinted"; }
[[nodiscard]] constexpr std::string_view word_of(theme::night) { return "night"; }
[[nodiscard]] constexpr std::string_view word_of(accent::theme_own) { return "theme"; }
[[nodiscard]] constexpr std::string_view word_of(accent::blue) { return "blue"; }
[[nodiscard]] constexpr std::string_view word_of(accent::green) { return "green"; }
[[nodiscard]] constexpr std::string_view word_of(accent::pink) { return "pink"; }
[[nodiscard]] constexpr std::string_view word_of(accent::orange) { return "orange"; }
[[nodiscard]] constexpr std::string_view word_of(accent::purple) { return "purple"; }
[[nodiscard]] constexpr std::string_view word_of(accent::red) { return "red"; }
[[nodiscard]] constexpr std::string_view word_of(accent::grey) { return "grey"; }
[[nodiscard]] constexpr std::string_view word_of(accent::gold) { return "gold"; }
[[nodiscard]] constexpr std::string_view word_of(renderer::opengl) { return "opengl"; }
[[nodiscard]] constexpr std::string_view word_of(renderer::software) { return "software"; }
[[nodiscard]] constexpr std::string_view word_of(proxy_kind::socks5) { return "socks5"; }
[[nodiscard]] constexpr std::string_view word_of(proxy_kind::http) { return "http"; }
template <class... Ts>
[[nodiscard]] std::string word_of(const std::variant<Ts...>& one) {
  return std::string(std::visit([](auto each) { return word_of(each); }, one));
}
// What a user reads for a proxy's kind.
[[nodiscard]] constexpr std::string_view label_of(proxy_kind::socks5) { return "SOCKS5"; }
[[nodiscard]] constexpr std::string_view label_of(proxy_kind::http) { return "HTTP"; }
[[nodiscard]] inline std::string_view label_of(const proxy_kind_t& one) {
  return std::visit([](auto each) { return label_of(each); }, one);
}

// A proxy, as a named profile of the program's list -- as Gajim keeps them
// -- that accounts choose by its name: SOCKS5 or HTTP CONNECT.
struct proxy_settings {
  std::string name;
  std::string kind = "socks5";  // "socks5" or "http"
  std::string host;
  std::int64_t port = 1080;
  std::optional<std::string> username;
  std::optional<std::string> password;
  friend bool operator==(const proxy_settings&, const proxy_settings&) = default;
};

// An XMPP account: a JID and how to reach its server.
struct xmpp_account {
  std::string address;  // user@domain
  std::string password;
  bool enabled = true;
  std::string resource = "mux";
  // Where to connect, instead of what the domain's SRV records say.
  std::optional<std::string> host;
  std::optional<std::int64_t> port;
  // PLAIN over a stream TLS has not secured: only for a server on this
  // machine, under test. Never over a network.
  bool plain_without_tls = false;
  // Whether the people one talks to are told a message was read. Nothing
  // said is yes.
  std::optional<bool> read_receipts;
  // The name of the proxy profile it connects through, where it has one.
  std::optional<std::string> proxy;
  friend bool operator==(const xmpp_account&, const xmpp_account&) = default;
};

// A Matrix account: a user ID and its homeserver.
struct matrix_account {
  std::string user_id;  // @user:server
  std::string password;
  bool enabled = true;
  // The client-server API's base URL, instead of what .well-known says.
  std::optional<std::string> homeserver;
  // What the server shows for this login among the account's devices.
  std::string device_name = "mux";
  std::optional<bool> read_receipts;
  std::optional<std::string> proxy;
  // The session the server gave, kept so the next start goes on with it.
  std::optional<std::string> access_token;
  std::optional<std::string> device_id;
  friend bool operator==(const matrix_account&, const matrix_account&) = default;
};

// One saved account, of either protocol.
using account_t = std::variant<xmpp_account, matrix_account>;

// A chat muted: no notifications from it, its unread count in grey.
struct muted_chat {
  std::string account;       // the account's address
  std::string conversation;  // the chat's id in it
  friend bool operator==(const muted_chat&, const muted_chat&) = default;
};

// The file: a list for each protocol, so each entry says what it is by where
// it is, and has only its own protocol's keys.
struct file {
  std::vector<xmpp_account> xmpp;
  std::vector<matrix_account> matrix;
  // How much the window moves: "none", "reduced" (sections unfold, panels
  // just appear) or "full". Nothing said is full.
  std::optional<std::string> motion;
  std::optional<std::vector<muted_chat>> muted;
  // The proxy profiles accounts choose from.
  std::optional<std::vector<proxy_settings>> proxies;
  // The theme, "dark" or "light", and what draws the window, "opengl" or
  // "software". Nothing said is dark and OpenGL.
  std::optional<std::string> theme;
  std::optional<std::string> accent;
  std::optional<std::string> renderer;
  friend bool operator==(const file&, const file&) = default;
};

consteval auto json_schema(knot::type<proxy_settings>) { return knot::schema<proxy_settings>(); }
consteval auto json_schema(knot::type<xmpp_account>) { return knot::schema<xmpp_account>(); }
consteval auto json_schema(knot::type<matrix_account>) { return knot::schema<matrix_account>(); }
consteval auto json_schema(knot::type<muted_chat>) { return knot::schema<muted_chat>(); }
consteval auto json_schema(knot::type<file>) { return knot::schema<file>(); }

// What an account is known by: its JID or its user ID. The two never meet:
// a user ID starts with '@', and a JID cannot.
[[nodiscard]] inline const std::string& address_of(const xmpp_account& one) noexcept { return one.address; }
[[nodiscard]] inline const std::string& address_of(const matrix_account& one) noexcept { return one.user_id; }
[[nodiscard]] inline const std::string& address_of(const account_t& one) noexcept {
  return std::visit([](const auto& each) -> const std::string& { return address_of(each); }, one);
}

[[nodiscard]] inline bool& enabled_of(account_t& one) noexcept {
  return std::visit([](auto& each) -> bool& { return each.enabled; }, one);
}
[[nodiscard]] inline bool enabled_of(const account_t& one) noexcept {
  return std::visit([](const auto& each) { return each.enabled; }, one);
}

// Whether an account sends read receipts, and the proxy it goes through.
[[nodiscard]] inline bool read_receipts_of(const account_t& one) {
  return std::visit([](const auto& each) { return each.read_receipts.value_or(true); }, one);
}
[[nodiscard]] inline std::optional<bool>& read_receipts_in(account_t& one) {
  return std::visit([](auto& each) -> std::optional<bool>& { return each.read_receipts; }, one);
}
[[nodiscard]] inline std::optional<std::string>& proxy_in(account_t& one) {
  return std::visit([](auto& each) -> std::optional<std::string>& { return each.proxy; }, one);
}
[[nodiscard]] inline const std::optional<std::string>& proxy_of(const account_t& one) {
  return std::visit([](const auto& each) -> const std::optional<std::string>& { return each.proxy; }, one);
}
// The profile of that name, where there is one.
[[nodiscard]] inline const proxy_settings* find_proxy(const std::vector<proxy_settings>& all,
                                                      const std::optional<std::string>& name) {
  if (!name)
    return nullptr;
  const auto found = std::ranges::find(all, *name, &proxy_settings::name);
  return found == all.end() ? nullptr : &*found;
}

[[nodiscard]] constexpr std::string_view protocol_name(const xmpp_account&) noexcept { return "XMPP"; }
[[nodiscard]] constexpr std::string_view protocol_name(const matrix_account&) noexcept { return "Matrix"; }
[[nodiscard]] inline std::string_view protocol_name(const account_t& one) noexcept {
  return std::visit([](const auto& each) { return protocol_name(each); }, one);
}

constexpr bool is_matrix(std::string_view address) noexcept { return address.starts_with('@'); }

// An account from an address alone, as on the command line: the address says
// the protocol.
[[nodiscard]] inline account_t account_from(std::string address, std::string password) {
  if (is_matrix(address))
    return matrix_account{.user_id = std::move(address), .password = std::move(password)};
  return xmpp_account{.address = std::move(address), .password = std::move(password)};
}

// Each account into its protocol's list.
struct into_its_list {
  file& into;
  void operator()(const xmpp_account& one) const { into.xmpp.push_back(one); }
  void operator()(const matrix_account& one) const { into.matrix.push_back(one); }
};

// All the accounts of a file, XMPP first; and a file of accounts.
[[nodiscard]] inline std::vector<account_t> accounts_of(const file& from) {
  std::vector<account_t> out;
  out.reserve(from.xmpp.size() + from.matrix.size());
  out.append_range(from.xmpp);
  out.append_range(from.matrix);
  return out;
}
[[nodiscard]] inline file file_of(std::span<const account_t> accounts) {
  file out;
  for (const account_t& one : accounts)
    std::visit(into_its_list{out}, one);
  return out;
}

std::optional<std::string> check(const xmpp_account& one) {
  const std::string_view address = one.address;
  if (address.empty())
    return "Type the address: user@example.com";
  if (address.find_first_of(" \t\r\n/") != std::string_view::npos)
    return "An XMPP address is user@domain, with no spaces";
  const auto at = address.find('@');
  if (at == std::string_view::npos || at == 0 || at + 1 == address.size() ||
      address.find('@', at + 1) != std::string_view::npos)
    return "An XMPP address is user@domain";
  if (one.resource.empty() || one.resource.find_first_of(" \t\r\n") != std::string::npos)
    return "The resource is a word with no spaces, such as mux";
  if (one.host && one.host->empty())
    return "Leave the host empty, or type one";
  if (one.port && (*one.port < 1 || *one.port > 65535))
    return "A port is a number from 1 to 65535";
  if (one.password.empty())
    return "Type the password";
  return std::nullopt;
}

std::optional<std::string> check(const matrix_account& one) {
  const std::string_view user = one.user_id;
  if (user.empty())
    return "Type the user ID: @user:example.org";
  if (user.find_first_of(" \t\r\n") != std::string_view::npos)
    return "A user ID has no spaces in it";
  const auto colon = user.find(':');
  if (!is_matrix(user) || colon == std::string_view::npos || colon == 1 || colon + 1 == user.size())
    return "A Matrix user ID is @user:server";
  if (one.homeserver && !one.homeserver->starts_with("https://") && !one.homeserver->starts_with("http://"))
    return "The homeserver is a URL: https://matrix.example.org";
  if (one.device_name.empty())
    return "Name this device, such as mux";
  if (one.password.empty())
    return "Type the password";
  return std::nullopt;
}

std::optional<std::string> check(const account_t& one) {
  return std::visit([](const auto& each) { return check(each); }, one);
}

// Where what the program keeps between runs, and could make again, is put:
// $XDG_STATE_HOME/mux, or ~/.local/state/mux.
std::filesystem::path state_path(std::string_view name) {
  if (const char* xdg = std::getenv("XDG_STATE_HOME"); xdg && *xdg)
    return std::filesystem::path(xdg) / "mux" / name;
  if (const char* home = std::getenv("HOME"); home && *home)
    return std::filesystem::path(home) / ".local" / "state" / "mux" / name;
  return std::filesystem::path(std::format("mux-{}", name));
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
