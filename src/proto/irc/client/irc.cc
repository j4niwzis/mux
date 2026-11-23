// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.irc.client: an IRC account, run by a fiber on mux.net's loop
// through cairn, saying what happens as mux.core's changes.
//
// Connecting: the server's address, TLS from the first byte for ircs://
// (the certificate checked against the host), then cairn's registration --
// the capabilities, SASL, the nick -- and the lines as they come. cairn's
// client is sans-IO: one fiber reads and feeds it, and what anyone writes
// goes through its sinks into the stream, flushed by the fiber that wrote.
export module mux.proto.irc.client;

import std;
import splice;
import cairn;
import mux.core;
import mux.net;
import mux.http;
import mux.proto.irc;
import mux.proto.irc.kept;
import mux.proto.irc.links;
import mux.proto.irc.changes;

export namespace mux::proto::irc::client {

// SASL's mechanism, read once from what the account keeps.
namespace mechanism {
struct plain {};
struct scram_sha256 {};
}  // namespace mechanism
using mechanism_t = spl::variant<mechanism::plain, mechanism::scram_sha256>;
[[nodiscard]] inline mechanism_t mechanism_of(const std::optional<std::string>& named) {
  if (named == std::optional<std::string>("SCRAM-SHA-256")) return mechanism::scram_sha256{};
  return mechanism::plain{};
}
// A SCRAM nonce: printable, not a comma, from the system's randomness.
[[nodiscard]] inline std::string fresh_nonce() {
  static constexpr std::string_view letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::random_device random;
  return std::views::iota(0, 32) | std::views::transform([&](int) { return letters[random() % letters.size()]; }) | std::ranges::to<std::string>();
}

struct settings {
  std::string address;  // what the account is known by: ircs://nick@host
  target where;
  std::string password;  // SASL's, where not empty
  mechanism_t mechanism = mechanism::plain{};
  std::optional<std::string> login, server_password, username, realname;
  std::vector<std::string> channels;
  std::string bouncer_network;
  std::optional<net::proxy> proxy;
};

// IRCv3 server-time: YYYY-MM-DDThh:mm:ss.sssZ, always UTC. Read by hand:
// libc++ has no std::chrono::parse yet.
[[nodiscard]] inline std::optional<std::chrono::sys_time<std::chrono::milliseconds>> time_of(std::string_view text) {
  std::size_t at = 0;
  const auto number = [&](std::size_t digits) -> std::optional<int> {
    if (at + digits > text.size()) return std::nullopt;
    int value = 0;
    for (const char c : text.substr(at, digits)) {
      if (c < '0' || c > '9') return std::nullopt;
      value = value * 10 + (c - '0');
    }
    at += digits;
    return value;
  };
  const auto expect = [&](char c) { return at < text.size() && text[at] == c ? (++at, true) : false; };
  const auto year = number(4);
  if (!year || !expect('-')) return std::nullopt;
  const auto month = number(2);
  if (!month || !expect('-')) return std::nullopt;
  const auto day = number(2);
  if (!day || !expect('T')) return std::nullopt;
  const auto hour = number(2);
  if (!hour || !expect(':')) return std::nullopt;
  const auto minute = number(2);
  if (!minute || !expect(':')) return std::nullopt;
  const auto second = number(2);
  if (!second) return std::nullopt;
  int millis = 0;
  if (expect('.')) {
    const auto ms = number(3);
    if (!ms) return std::nullopt;
    millis = *ms;
  }
  const std::chrono::year_month_day date{std::chrono::year(*year), std::chrono::month(static_cast<unsigned>(*month)),
                                         std::chrono::day(static_cast<unsigned>(*day))};
  if (!date.ok() || *hour > 23 || *minute > 59 || *second > 60) return std::nullopt;
  return std::chrono::sys_days(date) + std::chrono::hours(*hour) + std::chrono::minutes(*minute) + std::chrono::seconds(*second) +
         std::chrono::milliseconds(millis);
}

// A line's text as mux shows it: mIRC's formatting codes taken out, and as
// UTF-8 -- read as Latin-1 where it is not UTF-8 (cairn.text).
[[nodiscard]] inline std::string shown_text(std::string_view wire) {
  const std::string plain = cairn::format::plain(wire) | std::ranges::to<std::string>();
  std::string out;
  cairn::text::write_utf8(plain, out);
  return out;
}

// mIRC's sixteen colours, as modern.ircdocs.horse/formatting gives them;
// those past 15 are shown without a colour.
inline constexpr std::array<std::uint32_t, 16> mirc_colours{0xffffff, 0x000000, 0x00007f, 0x009300, 0xff0000, 0x7f0000,
                                                            0x9c009c, 0xfc7f00, 0xffff00, 0x00fc00, 0x009393, 0x00ffff,
                                                            0x0000fc, 0xff00ff, 0x7f7f7f, 0xd2d2d2};
[[nodiscard]] inline std::optional<std::uint32_t> rgb_of(const cairn::format::colour& one) {
  if (one.hex) return one.rgb;
  if (one.number >= 0 && one.number < 16) return mirc_colours[static_cast<std::size_t>(one.number)];
  return std::nullopt;
}
// Text as HTML's: its markup characters escaped.
[[nodiscard]] inline std::string escaped_html(std::string_view text) {
  std::string out;
  for (const char c : text) {
    switch (c) {
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '&': out += "&amp;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}
// A line's formatting as the HTML mux shows a message's in: each span in
// the tags its style says -- bold, italic, underline, strikethrough,
// monospace, a colour -- its text UTF-8 as the whole line is read. None
// where nothing in it is formatted.
[[nodiscard]] inline std::optional<std::string> html_of(std::string_view wire) {
  const auto how = cairn::text::encoding_of(wire);
  std::string out;
  bool styled = false;
  for (const auto& one : cairn::format::spans(wire)) {
    const auto& st = one.style;
    std::string text;
    for (const char32_t c : cairn::text::decoded(one.text, how)) cairn::text::put_utf8(c, text);
    std::string open, close;
    const auto wrap = [&](bool on, std::string_view tag) {
      if (!on) return;
      open += std::format("<{}>", tag);
      close.insert(0, std::format("</{}>", tag));
      styled = true;
    };
    wrap(st.bold, "b");
    wrap(st.italic, "i");
    wrap(st.underline, "u");
    wrap(st.strikethrough, "s");
    wrap(st.monospace, "code");
    const auto fg = st.reverse ? rgb_of(st.background) : rgb_of(st.foreground);
    if (fg) {
      open += std::format("<font color=\"#{:06x}\">", *fg);
      close.insert(0, "</font>");
      styled = true;
    }
    out += open + escaped_html(text) + close;
  }
  if (!styled) return std::nullopt;
  return out;
}

// What is sent, with mux's runs of formatting written as mIRC's codes:
// each run's code at its start and at its end -- bold, italic, underline,
// strikethrough and monospace toggle; a colour set by ^D (hex) and reset by
// ^C; a spoiler as black on black. Runs as typed (/raw) and links, custom
// emoji -- what IRC has no code for -- are their text alone.
[[nodiscard]] inline std::string with_codes(std::string_view text, const std::vector<mux::styled_run>& styles) {
  struct mark {
    std::size_t at;
    bool closing;
    std::string code;
  };
  std::vector<mark> marks;
  for (const auto& run : styles) {
    const auto codes = spl::visit(
        spl::overloaded{[](const run_style::bold&) { return std::pair<std::string, std::string>{"\x02", "\x02"}; },
                        [](const run_style::italic&) { return std::pair<std::string, std::string>{"\x1d", "\x1d"}; },
                        [](const run_style::underline&) { return std::pair<std::string, std::string>{"\x1f", "\x1f"}; },
                        [](const run_style::strike&) { return std::pair<std::string, std::string>{"\x1e", "\x1e"}; },
                        [](const run_style::code&) { return std::pair<std::string, std::string>{"\x11", "\x11"}; },
                        [](const run_style::spoiler&) { return std::pair<std::string, std::string>{"\x03" "01,01", "\x03"}; },
                        [](const run_style::colour& c) {
                          return std::pair<std::string, std::string>{std::format("\x04{:06X}", c.rgb & 0xffffff), "\x04"};
                        },
                        [](const auto&) { return std::pair<std::string, std::string>{}; }},
        run.style);
    if (codes.first.empty() || run.first >= run.last || run.last > text.size()) continue;
    marks.push_back({run.first, false, codes.first});
    marks.push_back({run.last, true, codes.second});
  }
  // Closings before openings at one place, so that a run ending where the
  // next begins is not reopened by its own close.
  std::ranges::stable_sort(marks, [](const mark& a, const mark& b) { return a.at != b.at ? a.at < b.at : a.closing > b.closing; });
  std::string out;
  std::size_t from = 0;
  for (const auto& one : marks) {
    out.append(text.substr(from, one.at - from));
    out += one.code;
    from = one.at;
  }
  out.append(text.substr(from));
  return out;
}

// A DCC offer, as a CTCP DCC says it: a file to receive (SEND), or a chat
// of its own (CHAT) -- each at an address to connect to, or, where its port
// is 0, a token to answer with an address of one's own (passive DCC).
namespace dcc {
struct send {
  std::string name, host;
  std::uint16_t port = 0;
  std::uint64_t size = 0;
  std::string token;
};
struct chat {
  std::string host;
  std::uint16_t port = 0;
};
}  // namespace dcc
using dcc_t = spl::variant<dcc::send, dcc::chat>;
// An address as DCC writes it: IPv4 as one decimal number, IPv6 as it is.
[[nodiscard]] inline std::string dcc_host(std::string_view written) {
  if (written.find(':') != std::string_view::npos) return std::string(written);
  std::uint32_t n = 0;
  if (std::from_chars(written.data(), written.data() + written.size(), n).ec != std::errc{}) return std::string(written);
  return std::format("{}.{}.{}.{}", n >> 24, (n >> 16) & 255, (n >> 8) & 255, n & 255);
}
// And back: an IPv4 address as DCC's decimal number; IPv6 as it is.
[[nodiscard]] inline std::string dcc_written(std::string_view address) {
  std::uint32_t n = 0;
  int parts = 0;
  for (auto part : address | std::views::split('.')) {
    unsigned byte = 0;
    const std::string_view one(part.begin(), part.end());
    if (std::from_chars(one.data(), one.data() + one.size(), byte).ec != std::errc{} || byte > 255) return std::string(address);
    n = (n << 8) | byte;
    ++parts;
  }
  return parts == 4 ? std::to_string(n) : std::string(address);
}
// SEND "file name" host port size [token], CHAT chat host port.
[[nodiscard]] inline std::optional<dcc_t> dcc_of(std::string_view body) {
  const auto word = [&]() -> std::string {
    body.remove_prefix(std::min(body.find_first_not_of(' '), body.size()));
    if (body.starts_with('"')) {
      const auto end = body.find('"', 1);
      std::string got(body.substr(1, end == std::string_view::npos ? std::string_view::npos : end - 1));
      body.remove_prefix(end == std::string_view::npos ? body.size() : end + 1);
      return got;
    }
    const auto end = std::min(body.find(' '), body.size());
    std::string got(body.substr(0, end));
    body.remove_prefix(end);
    return got;
  };
  const auto number = [](const std::string& s, auto& out) { return std::from_chars(s.data(), s.data() + s.size(), out).ec == std::errc{}; };
  const std::string kind = word();
  const auto upper = kind | std::views::transform([](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }) |
                     std::ranges::to<std::string>();
  if (upper == "SEND") {
    dcc::send out;
    out.name = word();
    out.host = dcc_host(word());
    unsigned port = 0;
    if (!number(word(), port) || port > 65535) return std::nullopt;
    out.port = static_cast<std::uint16_t>(port);
    number(word(), out.size);
    out.token = word();
    // Only the file's own name: no path to put it anywhere else.
    if (const auto slash = out.name.find_last_of("/\\"); slash != std::string::npos) out.name = out.name.substr(slash + 1);
    if (out.name.empty()) return std::nullopt;
    return out;
  }
  if (upper == "CHAT") {
    dcc::chat out;
    word();  // "chat"
    out.host = dcc_host(word());
    unsigned port = 0;
    if (!number(word(), port) || port == 0 || port > 65535) return std::nullopt;
    out.port = static_cast<std::uint16_t>(port);
    return out;
  }
  return std::nullopt;
}
// Where a DCC file is fetched from, as a message's attachment says it:
// dcc://host:port?size=N, or dccp://nick?token=T&size=N for a passive one
// -- read again by fetch_media (dcc_source_of).
struct dcc_source {
  bool passive = false;
  std::string host;  // or, passive, the nick to answer
  std::uint16_t port = 0;
  std::uint64_t size = 0;
  std::string token, name;
};
[[nodiscard]] inline std::string source_of(const dcc_source& one) {
  if (one.passive) return std::format("dccp://{}?token={}&size={}&name={}", one.host, one.token, one.size, one.name);
  return std::format("dcc://{}:{}?size={}&name={}", one.host.find(':') != std::string::npos ? "[" + one.host + "]" : one.host, one.port,
                     one.size, one.name);
}
[[nodiscard]] inline std::optional<dcc_source> dcc_source_of(std::string_view source) {
  dcc_source out;
  if (source.starts_with("dccp://")) {
    out.passive = true;
    source.remove_prefix(7);
  } else if (source.starts_with("dcc://")) {
    source.remove_prefix(6);
  } else {
    return std::nullopt;
  }
  const auto query = source.find('?');
  std::string_view where = source.substr(0, query);
  const std::string_view rest = query == std::string_view::npos ? std::string_view() : source.substr(query + 1);
  if (!out.passive) {
    const auto colon = where.rfind(':');
    if (colon == std::string_view::npos) return std::nullopt;
    unsigned port = 0;
    if (std::from_chars(where.data() + colon + 1, where.data() + where.size(), port).ec != std::errc{} || port == 0 || port > 65535)
      return std::nullopt;
    out.port = static_cast<std::uint16_t>(port);
    where = where.substr(0, colon);
    if (where.starts_with('[') && where.ends_with(']')) where = where.substr(1, where.size() - 2);
  }
  out.host = std::string(where);
  for (auto pair : rest | std::views::split('&')) {
    const std::string_view one(pair.begin(), pair.end());
    const auto eq = one.find('=');
    const auto key = one.substr(0, eq), value = eq == std::string_view::npos ? std::string_view() : one.substr(eq + 1);
    if (key == "size") std::from_chars(value.data(), value.data() + value.size(), out.size);
    else if (key == "token") out.token = std::string(value);
    else if (key == "name") out.name = std::string(value);
  }
  return out;
}

// What a status symbol in a channel stands for, as mux shows a member's role.
[[nodiscard]] inline std::optional<std::string> role_of(std::string_view prefixes) {
  if (prefixes.empty()) return std::nullopt;
  static constexpr std::array<std::pair<char, std::string_view>, 5> known{
      {{'~', "founder"}, {'&', "admin"}, {'@', "operator"}, {'%', "half-operator"}, {'+', "voice"}}};
  const auto found = std::ranges::find(known, prefixes.front(), &decltype(known)::value_type::first);
  return found == known.end() ? std::nullopt : std::optional<std::string>(found->second);
}

}  // namespace mux::proto::irc::client

export namespace mux::contract {
// What a plugin hears of an IRC account's lines, and keeps of it
// (mux.plugins.irc).
struct irc_events {};
}  // namespace mux::contract

export namespace mux::proto::irc::client {

// What a plugin hears of a line come -- irc_message(contract::irc_events,
// type_tag<its feature>, the message, the account), found by ADL where the
// account is made -- told to the program as its own changes (tell_program):
// who is typing, for one.
template <class Plugin, class Account>
concept hears_irc_messages = requires(const cairn::message& got, Account& account) {
  irc_message(contract::irc_events{}, type_tag<Plugin>{}, got, account);
};
// What a plugin keeps of an account while it is connected -- irc_kept_type(
// contract::irc_events, type_tag<its feature>) -> type_tag<what> -- and
// every plugin's, in one tuple.
template <class Plugin>
struct irc_kept_of {
  struct type {};
};
template <class Plugin>
  requires requires { irc_kept_type(contract::irc_events{}, type_tag<Plugin>{}); }
struct irc_kept_of<Plugin> {
  using type = typename decltype(irc_kept_type(contract::irc_events{}, type_tag<Plugin>{}))::type;
};
template <class Plugin>
struct irc_kept_slot {
  typename irc_kept_of<Plugin>::type kept{};
};
template <class List>
struct irc_kept_slots {
  using type = std::tuple<>;
};
template <template <class...> class List, class... Plugins>
struct irc_kept_slots<List<Plugins...>> {
  using type = std::tuple<irc_kept_slot<Plugins>...>;
};
// The plugins an account's sink serves (the program's); none for any other.
template <class Sink>
struct irc_plugins {
  using type = plugin_list<>;
};
template <class Sink>
  requires requires { typename Sink::plugins; }
struct irc_plugins<Sink> {
  using type = typename Sink::plugins;
};

// net::stream's chunks as the characters cairn reads: the chunk a read
// brought walked a character at a time, where it is in it kept between
// lines. The chunk is a view into the stream's buffer, good until the next
// read -- which is asked only once it is walked to its end.
class characters {
 public:
  explicit characters(net::stream& from) : from_(&from) {}
  struct iterator {
    using value_type = char;
    using difference_type = std::ptrdiff_t;
    characters* of = nullptr;
    char operator*() const { return of->rest_.front(); }
    iterator& operator++() {
      of->rest_.remove_prefix(1);
      return *this;
    }
    void operator++(int) { ++*this; }
    bool operator==(std::default_sentinel_t) const { return !of->fill(); }
  };
  struct range {
    characters* of = nullptr;
    iterator begin() const { return {of}; }
    std::default_sentinel_t end() const { return {}; }
  };
  range input() { return {this}; }

 private:
  bool fill() {
    while (rest_.empty()) {
      auto& chunks = from_->input();
      auto at = chunks.begin();
      if (at == chunks.end()) return false;
      rest_ = *at;
      ++at;
    }
    return true;
  }
  net::stream* from_;
  std::string_view rest_;
};

// The stream as what cairn writes into: lines kept until a flush.
struct written {
  net::stream* to;
  void append(std::string_view bytes) { to->write(bytes); }
  void push_back(char c) { to->write(std::string_view(&c, 1)); }
};

// An account, and the changes it makes: `Sink` is called with each change,
// on the loop's thread -- a mailbox's push, or a test's vector.
template <class Sink>
class account {
 public:
  account(net::loop& loop, net::tls& tls, settings how, Sink sink)
      : loop_(&loop), tls_(&tls), how_(std::move(how)), sink_(std::move(sink)), id_{protocol::irc{}, how_.address} {
    for (const auto& one : how_.channels) joined_.insert(one);
  }
  account(const account&) = delete;
  account& operator=(const account&) = delete;

  const account_id& id() const noexcept { return id_; }

  // Connected, and kept connected, by a fiber of its own.
  void start() {
    if (started_ || stopping_) return;
    started_ = true;
    loop_->spawn([this] { run(); });
  }
  // Called on the network loop: stop recovery before canceling its I/O.
  void stop() {
    stopping_ = true;
    if (wire_) wire_->abort();
    for (auto& [name, chat] : chats_) chat->abort();
  }

  // A message said, in as many lines as it takes: each line of the text a
  // PRIVMSG, cut where a line is too long for the server (cairn's pieces).
  void send(std::string to, std::string text, std::optional<std::string> reply_to = std::nullopt, std::vector<mux::mention> = {},
            std::vector<mux::styled_run> styles = {}, std::optional<std::string> local = std::nullopt, plugin_message_parts = {}) {
    this->spawn_guarded([this, to = std::move(to), text = std::move(text), reply_to = std::move(reply_to), local = std::move(local),
                         styles = std::move(styles)] {
      message out{.in = {id_, to},
                  .id = local ? *local : "mux-" + std::to_string(++sent_),
                  .sender = nick_,
                  .at = now(),
                  .body = {text, std::nullopt},
                  .outgoing = true,
                  .delivery = delivery::sending{}};
      out.replies_to = reply_to;
      sink_(change::message_added{out});
      const std::string coded = with_codes(text, styles);
      if (const auto chat = chats_.find(to); chat != chats_.end()) {
        chat->second->write(coded + "\n");
        chat->second->flush();
        sink_(change::delivery_changed{out.in, out.id, chat->second->failed() ? delivery_t{delivery::failed{}} : delivery_t{delivery::sent{}}});
        return;
      }
      const bool sent = this->say_lines(to, coded, reply_to, [](std::string_view target, std::string_view piece) {
        return cairn::message_t(cairn::msg::privmsg{target, piece});
      });
      sink_(change::delivery_changed{out.in, out.id, sent ? delivery_t{delivery::sent{}} : delivery_t{delivery::failed{}}});
    });
  }
  // IRC has no edits: what was said stays said.
  void edit(std::string, std::string, std::string, std::vector<mux::styled_run> = {}, std::vector<mux::mention> = {}) {
    sink_(change::refused{id_, "IRC messages cannot be edited."});
  }
  // One's own line taken back (draft/message-redaction): the server says it
  // back to everyone in the chat, this account too, and it goes then.
  void remove(std::string to, std::string id) {
    this->spawn_guarded([this, to = std::move(to), id = std::move(id)] {
      if (!client_ || !client_->has_cap("draft/message-redaction")) {
        sink_(change::refused{id_, "This server does not let messages be deleted."});
        return;
      }
      this->write(cairn::msg::redact{to, id, {}});
    });
  }
  // A channel left (PART); a private conversation closed.
  void leave(std::string room) {
    this->spawn_guarded([this, room = std::move(room)] {
      if (client_ && is_channel(room) && joined_.contains(room)) this->write(cairn::msg::part{cairn::commas{room}, {}});
      if (const auto chat = chats_.find(room); chat != chats_.end()) chat->second->abort();
      joined_.erase(room);
      sink_(change::conversation_removed{{id_, room}});
    });
  }
  void join(std::string room, const std::vector<std::string>&) {
    this->spawn_guarded([this, room = std::move(room)] {
      joined_.insert(room);
      if (client_ && client_->registered()) this->write(cairn::msg::join{cairn::commas{room}, {}, {}, {}});
    });
  }
  // A private conversation opened with someone, by their nick.
  void create_direct(std::string user) {
    sink_(change::conversation_updated{.id = {id_, user}, .kind = conversation_kind::direct{}, .name = user});
    sink_(change::history_position{{id_, user}, state_.history ? std::optional<std::string>(std::string()) : std::nullopt});
  }
  // Where one has read up to, kept on the server (draft/read-marker): the
  // time of the line, as the server stamped it.
  void mark_read(std::string to, std::string id) {
    this->spawn_guarded([this, to = std::move(to), id = std::move(id)] {
      const auto when = times_.find(id);
      if (!client_ || !state_.read_marker || when == times_.end()) return;
      const std::string stamp = "timestamp=" + when->second;
      this->write(cairn::msg::markread{to, stamp});
    });
  }
  // Older lines of a chat, asked of the server (IRCv3 chathistory): the
  // latest where `before` is empty, else those before the point it names
  // (msgid=... or timestamp=..., as history_position gave it). They come as
  // a batch, read where it ends (paged()).
  void load_older(std::string with, std::string before) {
    this->spawn_guarded([this, with = std::move(with), before = std::move(before)] {
      if (!client_ || !state_.history) {
        sink_(change::history_position{{id_, with}, std::nullopt});
        return;
      }
      namespace h = cairn::msg::history;
      const h::query asked = before.empty() ? h::query(h::latest{with, h::none{}, page_size})
                                            : h::query(h::before{with, h::point_of(before), page_size});
      this->write(cairn::msg::chathistory{asked});
    });
  }

  // Explore: the server's channels (LIST), those whose name or topic has
  // the words asked, as the replies come (322), told at their end (323).
  void search_directory(std::string server, std::string query, std::optional<std::string> = std::nullopt) {
    this->spawn_guarded([this, server = std::move(server), query = std::move(query)] {
      if (!client_) return;
      listing_ = listing{server.empty() ? how_.where.host : server, query, {}};
      this->write(cairn::msg::list{{}, {}});
    });
  }
  // Someone's card: WHOIS, its replies gathered (311...) and told at its end
  // (318).
  void fetch_profile(std::string user) {
    this->spawn_guarded([this, user = std::move(user)] {
      if (!client_) return;
      whois_[fold(user)] = whois_reply{user};
      this->write(cairn::msg::whois{cairn::params_rest{user}});
    });
  }
  // Someone's presence: watched with MONITOR where the server has it (730,
  // 731 as they come and go); AWAY as away-notify says, besides.
  void fetch_presence(std::string user) {
    this->spawn_guarded([this, user = std::move(user)] {
      if (!client_ || is_channel(user) || watched_.contains(fold(user))) return;
      watched_.insert(fold(user));
      if (client_->server().monitor) this->write(cairn::msg::monitor{"+", cairn::commas{user}});
    });
  }
  // One's own presence: away (with what it says, or "Away"), or back.
  void set_presence(presence now) {
    this->spawn_guarded([this, now = std::move(now)] {
      if (!client_) return;
      const bool away = spl::visit(spl::overloaded{[](availability::away) { return true; }, [](availability::extended_away) { return true; },
                                                   [](availability::do_not_disturb) { return true; }, [](const auto&) { return false; }},
                                   now.state);
      const std::string said = now.status.value_or("Away");
      this->write(cairn::msg::away{away ? std::optional<std::string_view>(said) : std::nullopt});
    });
  }
  // One's own name: the nick (NICK); the server says it back, and it is
  // taken then.
  void set_display_name(std::string name) {
    this->spawn_guarded([this, name = std::move(name)] {
      if (client_) this->write(cairn::msg::nick{name});
    });
  }
  // A channel's members, asked again (NAMES).
  void fetch_members(std::string room) {
    this->spawn_guarded([this, room = std::move(room)] {
      if (client_ && is_channel(room)) this->write(cairn::msg::names{cairn::commas{room}});
    });
  }
  // A channel managed: its topic, someone invited, sent out, banned or let
  // back. A channel has no name but its own: not renamed.
  void manage(std::string room, const room_action_t& action) {
    spl::visit(spl::overloaded{
                   [&](const room_action::rename&) { sink_(change::refused{id_, "An IRC channel's name is its own: it cannot be renamed."}); },
                   [&](const room_action::retopic& one) {
                     this->spawn_guarded([this, room, topic = one.topic] { this->write(cairn::msg::topic{room, std::optional<std::string_view>(topic)}); });
                   },
                   [&](const room_action::invite& one) {
                     this->spawn_guarded([this, room, user = one.user] { this->write(cairn::msg::invite{user, room}); });
                   },
                   [&](const room_action::kick& one) {
                     this->spawn_guarded([this, room, user = one.user, why = one.reason.value_or("")] {
                       this->write(cairn::msg::kick{room, user, why});
                     });
                   },
                   [&](const room_action::ban& one) {
                     this->spawn_guarded([this, room, user = one.user, why = one.reason] {
                       this->raw(std::format("MODE {} +b {}!*@*", room, user));
                       if (why) this->write(cairn::msg::kick{room, user, *why});
                     });
                   },
                   [&](const room_action::unban& one) {
                     this->spawn_guarded([this, room, user = one.user] { this->raw(std::format("MODE {} -b {}!*@*", room, user)); });
                   }},
               action);
  }
  // A reaction put on a message, or taken back (+draft/react, +draft/unreact
  // on a TAGMSG, client tags): where the server takes message tags.
  void react(std::string room, std::string target, std::string key, bool on, std::optional<std::string> = std::nullopt) {
    this->spawn_guarded([this, room = std::move(room), target = std::move(target), key = std::move(key), on] {
      if (!client_ || !client_->has_cap("message-tags")) {
        sink_(change::refused{id_, "This server does not carry reactions."});
        return;
      }
      std::string line = on ? "@+draft/react=" : "@+draft/unreact=";
      for (char c : cairn::escaped(key)) line.push_back(c);
      line += ";+draft/reply=";
      for (char c : cairn::escaped(target)) line.push_back(c);
      line += " TAGMSG " + room;
      this->raw(line);
      sink_(change::reaction_changed{.in = {id_, room}, .id = target, .key = key, .who = nick_, .added = on, .at = now(), .live = true});
    });
  }

  // A file sent: put on the server's file host where it has one
  // (draft/FILEHOST, soju's) and its link said; else offered by DCC SEND, from
  // a port of this machine, to whoever takes it first.
  void send_file(std::string to, std::string local, std::string bytes, std::string name, std::string mimetype, bool image, int width,
                 int height, std::string caption, std::optional<std::string> reply_to, plugin_message_parts, std::optional<video_look>) {
    this->spawn_guarded([this, to = std::move(to), local = std::move(local), bytes = std::move(bytes), name = std::move(name),
                         mimetype = std::move(mimetype), image, width, height, caption = std::move(caption), reply_to] {
      message out{.in = {id_, to}, .id = local, .sender = nick_, .at = now(), .body = {caption, {}}, .outgoing = true,
                  .delivery = delivery::sending{}};
      out.replies_to = reply_to;
      attachment file{.source = "local:" + local, .name = name, .mimetype = mimetype, .size = static_cast<std::int64_t>(bytes.size()),
                      .width = width, .height = height};
      if (image) file.kind = attachment_kind::image{};
      out.attachment = file;
      sink_(change::message_added{out});
      const auto done = [&](bool sent) { sink_(change::delivery_changed{out.in, out.id, sent ? delivery_t{delivery::sent{}} : delivery_t{delivery::failed{}}}); };
      if (!client_) {
        done(false);
        return;
      }
      const auto* host = client_->server().token("draft/FILEHOST");
      if (!host) host = client_->server().token("soju.im/FILEHOST");
      if (host) {
        try {
          const auto url = http::url::parse(*host);
          if (!url) {
            sink_(change::refused{id_, "The server's file host has no address mux can read."});
            done(false);
            return;
          }
          const auto path = host->find('/', host->find("://") == std::string::npos ? 0 : host->find("://") + 3);
          http::connection upload(*loop_, *tls_, http::url{.host = url->host, .port = url->port}, how_.proxy);
          const std::vector<std::pair<std::string, std::string>> headers{{"Content-Disposition", std::format("attachment; filename=\"{}\"", name)}};
          const auto got = upload.request("POST", path == std::string::npos ? "/" : host->substr(path), bytes, std::nullopt,
                                          std::chrono::seconds(120), mimetype, static_cast<const http::no_progress*>(nullptr), "*/*", headers);
          if (got.status < 200 || got.status >= 300 || !got.location) {
            sink_(change::refused{id_, std::format("The file host refused the file ({}).", got.status)});
            done(false);
            return;
          }
          // Where it is: the address the host gave, made whole.
          std::string link = *got.location;
          if (link.starts_with('/')) link = host->substr(0, path == std::string::npos ? host->size() : path) + link;
          this->say_lines(to, caption.empty() ? link : caption + " " + link, reply_to, [](std::string_view target, std::string_view piece) {
            return cairn::message_t(cairn::msg::privmsg{target, piece});
          });
          done(true);
        } catch (const net::failure&) {
          sink_(change::refused{id_, "The file could not be put on the server's file host."});
          done(false);
        }
        return;
      }
      // DCC SEND: from this machine, on a port of its own, to the first to connect.
      try {
        net::listener offered(*loop_, 0, "0.0.0.0");
        const std::string address = dcc_written(wire_ ? wire_->local_address() : std::string());
        this->write(cairn::msg::ctcp{to, "DCC", std::format("SEND \"{}\" {} {} {}", name, address, offered.port(), bytes.size()), false});
        done(true);
        net::stream peer(*loop_, *tls_, offered.accept());
        offered.close();
        peer.write(bytes);
        peer.flush();
        // Its acknowledgements, as long as it sends them: then let go.
        auto& acks = peer.input();
        for (auto at = acks.begin(); at != acks.end(); ++at) {}
      } catch (const net::failure& failed) {
        sink_(change::refused{id_, std::format("The file was not sent: {}", failed.what())});
      }
    });
  }
  // A DCC file fetched: connected to (or, passive, listened for), its bytes
  // read and acknowledged as DCC does, and handed over as any media is.
  void fetch_media(std::string source, media_use_t use, int, bool = false) {
    const auto from = dcc_source_of(source);
    if (!from) return;
    this->spawn_guarded([this, source = std::move(source), use = std::move(use), from = *from] {
      try {
        std::optional<net::stream> peer;
        if (from.passive) {
          net::listener waiting(*loop_, 0, "0.0.0.0");
          const std::string address = dcc_written(wire_ ? wire_->local_address() : std::string());
          this->write(cairn::msg::ctcp{from.host, "DCC", std::format("SEND \"{}\" {} {} {} {}", from.name, address, waiting.port(), from.size, from.token), false});
          peer.emplace(*loop_, *tls_, waiting.accept());
        } else {
          peer.emplace(*loop_, *tls_, net::connect(*loop_, from.host, from.port));
        }
        std::string got;
        auto& chunks = peer->input();
        for (auto at = chunks.begin(); got.size() < from.size && at != chunks.end(); ++at) {
          got.append(*at);
          const auto total = static_cast<std::uint32_t>(got.size());
          const char ack[4] = {static_cast<char>(total >> 24), static_cast<char>(total >> 16), static_cast<char>(total >> 8), static_cast<char>(total)};
          peer->write(std::string_view(ack, 4));
          peer->flush();
        }
        if (from.size != 0 && got.size() < from.size) {
          sink_(change::refused{id_, std::format("{} came cut short ({} of {} bytes).", from.name, got.size(), from.size)});
          return;
        }
        sink_(change::avatar_loaded{use, source, std::move(got)});
      } catch (const net::failure& failed) {
        sink_(change::refused{id_, std::format("{} could not be fetched: {}", from.name, failed.what())});
      }
    });
  }

  // What a plugin's IRC side (mux.plugins.irc) does through the account:
  // work on the account's loop; whether it is connected; a change told to
  // the program; the chat a line is in; client tags sent alone (a TAGMSG,
  // where the server takes message tags); and what it keeps of the account.
  template <class Work>
  void on_fiber(Work work) {
    this->spawn_guarded(std::move(work));
  }
  [[nodiscard]] bool connected() const { return client_.has_value() && client_->registered(); }
  template <class Change>
  void tell_program(Change change) {
    sink_(std::move(change));
  }
  [[nodiscard]] std::string chat_for(std::string_view target, std::string_view nick) const { return chat_of(target, nick); }
  [[nodiscard]] bool mine_nick(std::string_view nick) const { return mine(nick); }
  // Tags as the wire has them -- "+typing=active" -- escaped by the caller.
  void send_tags(std::string_view to, std::string_view tags) {
    if (!client_ || !client_->has_cap("message-tags")) return;
    this->raw(std::format("@{} TAGMSG {}", tags, to));
  }
  template <class Plugin>
  [[nodiscard]] auto& kept_for(type_tag<Plugin>) {
    return std::get<irc_kept_slot<Plugin>>(plugins_kept_).kept;
  }

  // A command typed in a chat (command_of), done.
  void command(std::string room, command_t what) {
    this->spawn_guarded([this, room = std::move(room), what = std::move(what)] {
      if (!client_) {
        sink_(change::refused{id_, "Not connected."});
        return;
      }
      spl::visit(spl::overloaded{
                     [&](const command::join& one) {
                       for (const auto& c : words_of(one.channels)) joined_.insert(c);
                       this->write(cairn::msg::join{cairn::commas{one.channels}, cairn::commas{one.keys}, {}, {}});
                     },
                     [&](const command::part& one) {
                       if (is_channel(room)) this->write(cairn::msg::part{cairn::commas{room}, one.reason});
                       joined_.erase(room);
                       sink_(change::conversation_removed{{id_, room}});
                     },
                     [&](const command::me& one) {
                       this->write(cairn::msg::action{room, one.text});
                       this->echo(room, one.text, true);
                     },
                     [&](const command::msg& one) {
                       if (!is_channel(one.to)) this->create_direct(one.to);
                       if (one.text.empty()) return;
                       this->say_lines(one.to, one.text, std::nullopt, [](std::string_view target, std::string_view piece) {
                         return cairn::message_t(cairn::msg::privmsg{target, piece});
                       });
                       this->echo(one.to, one.text, false);
                     },
                     [&](const command::notice& one) {
                       this->say_lines(one.to, one.text, std::nullopt, [](std::string_view target, std::string_view piece) {
                         return cairn::message_t(cairn::msg::notice{target, piece});
                       });
                     },
                     [&](const command::nick& one) { this->write(cairn::msg::nick{one.to}); },
                     [&](const command::topic& one) {
                       this->write(cairn::msg::topic{room, one.text.empty() ? std::nullopt : std::optional<std::string_view>(one.text)});
                     },
                     // A line as typed: no CR or LF in it, so that it stays one.
                     [&](const command::quote& one) {
                       const std::string line = one.line | std::views::filter([](char c) { return c != '\r' && c != '\n'; }) |
                                                std::ranges::to<std::string>();
                       wire_->write(line);
                       wire_->write("\r\n");
                       wire_->flush();
                     }},
                 what);
    });
  }

 private:
  static constexpr unsigned page_size = 50;
  static std::chrono::sys_time<std::chrono::milliseconds> now() {
    return std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
  }
  void say(connection_t state) { sink_(change::connection_changed{id_, std::move(state)}); }
  bool is_channel(std::string_view name) const {
    return client_ ? client_->server().is_channel(name) : channel_name(name);
  }
  bool mine(std::string_view nick) const {
    return client_ && cairn::equal(nick, client_->nick(), client_->server().casemapping);
  }

  // A fiber of this account; what it throws past its own handling is caught
  // here, logged and said, as XMPP's are.
  template <class Body>
  void spawn_guarded(Body body) {
    ++commands_;
    loop_->spawn([this, body = std::move(body)] mutable {
      struct finished {
        std::size_t& commands;
        ~finished() { --commands; }
      } done{commands_};
      try {
        body();
      } catch (const std::exception& failed) {
        log(id_, "stopped by an error: {}", failed.what());
        this->say(connection::failed{std::format("Stopped by an error: {}", failed.what())});
      }
    });
  }

  // A line written as it is (no CR or LF in it), and sent.
  void raw(std::string_view line) {
    if (!wire_) return;
    const std::string clean = line | std::views::filter([](char c) { return c != '\r' && c != '\n'; }) | std::ranges::to<std::string>();
    wire_->write(clean);
    wire_->write("\r\n");
    wire_->flush();
  }
  // A nick folded by the server's casemapping: what people are found by.
  std::string fold(std::string_view nick) const {
    if (!client_) return std::string(nick);
    return std::ranges::to<std::string>(cairn::folded(nick, client_->server().casemapping));
  }
  // A message written to the server, and sent.
  template <class Message>
  bool write(const Message& what) {
    if (!wire_) return false;
    written out{wire_};
    cairn::write(cairn::message_t(what), out);
    wire_->flush();
    return !wire_->failed();
  }
  // A text said to someone, each of its lines in as many pieces as the
  // server's line has room for; the first answers `reply_to` where the
  // server takes message tags.
  template <class Make>
  bool say_lines(std::string_view to, std::string_view text, const std::optional<std::string>& reply_to, Make make) {
    if (!wire_ || !client_) return false;
    // The prefix the server puts before it, as long as it may be: the nick,
    // the user, a host of the longest.
    const std::string prefix = client_->nick() + "!~" + how_.username.value_or("mux") + "@" + std::string(63, 'h');
    written out{wire_};
    // More than one line, and draft/multiline: one batch, the lines in it,
    // shown as one message by those who read the batch.
    const bool multiline = text.find('\n') != std::string_view::npos && client_->has_cap("draft/multiline") && client_->has_cap("batch");
    const std::string ref = "ml" + std::to_string(++batches_);
    if (multiline) {
      if (reply_to && client_->has_cap("message-tags")) {
        out.append("@+draft/reply=");
        for (char c : cairn::escaped(*reply_to)) out.push_back(c);
        out.append(" ");
      }
      out.append(std::format("BATCH +{} draft/multiline {}\r\n", ref, to));
      for (auto line : text | std::views::split('\n')) {
        out.append("@batch=" + ref + " ");
        cairn::write(make(to, std::string_view(line.begin(), line.end()).empty() ? std::string_view(" ") : std::string_view(line.begin(), line.end())), out);
      }
      out.append(std::format("BATCH -{}\r\n", ref));
      wire_->flush();
      return !wire_->failed();
    }
    bool first = true;
    for (auto line : text | std::views::split('\n')) {
      const std::string_view one(line.begin(), line.end());
      for (std::string_view piece : cairn::pieces(one, cairn::text_room(prefix, "PRIVMSG", to))) {
        if (first && reply_to && client_->has_cap("message-tags")) {
          out.append("@+draft/reply=");
          for (char c : cairn::escaped(*reply_to)) out.push_back(c);
          out.append(" ");
        }
        first = false;
        cairn::write(make(to, piece), out);
      }
    }
    wire_->flush();
    return !wire_->failed();
  }
  // What one said by a command, shown as said (no echo-message asked).
  void echo(std::string_view to, std::string_view text, bool emote) {
    message out{.in = {id_, std::string(to)},
                .id = "mux-" + std::to_string(++sent_),
                .sender = client_ ? client_->nick() : nick_,
                .at = now(),
                .body = body_of(text, emote),
                .outgoing = true};
    sink_(change::message_added{std::move(out)});
  }
  static mux::body body_of(std::string_view text, bool emote) {
    if (!emote) return {std::string(text), std::nullopt};
    const std::string escaped = text | std::views::transform([](char c) {
                                  switch (c) {
                                    case '<': return std::string("&lt;");
                                    case '>': return std::string("&gt;");
                                    case '&': return std::string("&amp;");
                                    default: return std::string(1, c);
                                  }
                                }) | std::views::join | std::ranges::to<std::string>();
    return {std::string(text), "<em>" + escaped + "</em>"};
  }

  void run() {
    std::chrono::seconds delay{1};
    while (!stopping_) {
      retry_ = true;
      connected_ = false;
      try {
        this->connect_once();
      } catch (const std::exception& failed) {
        log(id_, "connection stopped by an error: {}", failed.what());
        say(connection::failed{failed.what()});
      }
      state_ = {};
      sink_(change::protocol_state_changed{id_, protocol_state_t{state_}});
      if (stopping_ || !retry_) break;
      if (connected_) delay = std::chrono::seconds(1);
      log(id_, "reconnecting in {} seconds", delay.count());
      for (std::chrono::seconds waited{0}; waited < delay && !stopping_; waited += std::chrono::seconds(1))
        loop_->sleep(std::chrono::seconds(1));
      delay = std::min(delay * 2, std::chrono::seconds(60));
    }
    started_ = false;
  }

  void connect_once() {
    say(connection::connecting{});
    const auto& where = how_.where;
    std::optional<net::tcp::socket> socket;
    try {
      log(id_, "connecting to {}:{}", where.host, where.port);
      socket.emplace(net::connect(*loop_, how_.proxy, where.host, where.port));
    } catch (const net::failure& failed) {
      log(id_, "{}:{} did not answer: {}", where.host, where.port, failed.what());
      say(connection::failed{std::format("{} did not answer: {}", where.host, failed.what())});
      return;
    }
    net::stream wire(*loop_, *tls_, std::move(*socket));
    wire_ = &wire;
    struct forget_connection {
      account& own;
      ~forget_connection() {
        own.wire_ = nullptr;
        own.client_.reset();
      }
    } forget{*this};
    if (stopping_) {
      wire.abort();
      return;
    }
    if (where.tls && !wire.start_tls(where.host)) {
      say(connection::failed{"TLS with " + where.host + " failed: its certificate, perhaps"});
      return;
    }
    cairn::registration asked{.nick = where.nick,
                              .username = how_.username.value_or(where.nick),
                              .realname = how_.realname.value_or(where.nick),
                              .password = how_.server_password.value_or("")};
    if (!how_.password.empty())
      asked.sasl = spl::visit(spl::overloaded{[&](mechanism::plain) -> cairn::sasl_t {
                                                return cairn::sasl::plain{how_.login.value_or(where.nick), how_.password};
                                              },
                                              [&](mechanism::scram_sha256) -> cairn::sasl_t {
                                                return cairn::sasl::scram_sha256{how_.login.value_or(where.nick), how_.password, fresh_nonce()};
                                              }},
                              how_.mechanism);
    // Not echo-message: what is sent is shown as sent, and an echo would be
    // a second copy of it.
    std::erase(asked.caps, std::string("echo-message"));
    asked.caps.push_back("chathistory");
    asked.bouncer_network = how_.bouncer_network;
    asked.version = "mux";
    asked.source = "https://github.com/j4niwzis/mux";
    client_.emplace(std::move(asked));
    nick_ = where.nick;
    channels_ = cairn::channels(where.nick);
    plugins_kept_ = {};
    written out{&wire};
    client_->start(out);
    wire.flush();

    characters in(wire);
    // The room a line is read into: as long as a line with tags may be
    // (8191 bytes of tags, 512 of the rest), and more.
    std::vector<char> room(16384);
    for (;;) {
      auto chars = in.input();
      auto at = chars.begin();
      if (at == chars.end()) break;
      const auto got = cairn::try_read(at, chars.end(), std::span<char>(room));
      if (!got) continue;
      client_->feed(*got, out, [&](const cairn::event_t& e) { this->on(e); });
      wire.flush();
      if (stopping_) break;
    }
    if (!stopping_) {
      log(id_, "the connection was lost");
      say(connection::failed{"the connection was lost"});
    }
    // Requests waiting on this connection let go before it goes.
    while (commands_ != 0) loop_->sleep(std::chrono::milliseconds(1));
    if (stopping_ || !wire.failed()) say(connection::offline{});
  }

  // The state as the server's capabilities and ISUPPORT say, told.
  void publish_state() {
    state_ = state{.online = client_->registered(),
                   .history = client_->has_cap("chathistory") || client_->has_cap("draft/chathistory"),
                   .read_marker = client_->has_cap("draft/read-marker"),
                   .network = client_->server().network,
                   .chantypes = client_->server().chantypes};
    sink_(change::protocol_state_changed{id_, protocol_state_t{state_}});
  }

  void on(const cairn::event_t& e) {
    spl::visit(spl::overloaded{
                   [&](const cairn::events::registered& one) {
                     // STS (IRCv3): offered over a plain connection, the
                     // port to come back on with TLS -- and that done, now.
                     if (!how_.where.tls)
                       if (const auto policy = client_->offered_value("sts")) {
                         for (auto part : *policy | std::views::split(',')) {
                           const std::string_view one_part(part.begin(), part.end());
                           unsigned port = 0;
                           if (one_part.starts_with("port=") &&
                               std::from_chars(one_part.data() + 5, one_part.data() + one_part.size(), port).ec == std::errc{} && port > 0 &&
                               port < 65536) {
                             log(id_, "the server asks for TLS (STS): connecting again on port {}", port);
                             how_.where.tls = true;
                             how_.where.port = static_cast<std::uint16_t>(port);
                             if (wire_) wire_->abort();
                             return;
                           }
                         }
                       }
                     connected_ = true;
                     nick_ = std::string(one.nick);
                     channels_.set_nick(nick_);
                     log(id_, "registered, as {}", nick_);
                     say(connection::online{});
                     publish_state();
                     sink_(change::conversation_updated{.id = server_chat(), .kind = conversation_kind::direct{}, .name = server_name()});
                     for (const auto& room : joined_) this->write(cairn::msg::join{cairn::commas{room}, {}, {}, {}});
                     // The private chats of the last month, where the server
                     // keeps history: in the list again.
                     if (state_.history) {
                       const auto stamp = [](auto t) { return std::format("timestamp={:%FT%T}Z", std::chrono::floor<std::chrono::milliseconds>(t)); };
                       const auto until = std::chrono::system_clock::now();
                       namespace h = cairn::msg::history;
                       const auto from_point = h::point_of(stamp(until - std::chrono::days(30)));
                       const auto to_point = h::point_of(stamp(until));
                       this->write(cairn::msg::chathistory{h::targets{from_point, to_point, 100}});
                     }
                     if (how_.bouncer_network.empty() && client_->has_cap("soju.im/bouncer-networks")) this->raw("BOUNCER LISTNETWORKS");
                     if (client_->server().monitor && !watched_.empty())
                       for (const auto& nick : watched_) this->write(cairn::msg::monitor{"+", cairn::commas{nick}});
                   },
                   [&](const cairn::events::caps_enabled&) {
                     if (client_->registered()) publish_state();
                   },
                   [&](const cairn::events::caps_offered&) {}, [&](const cairn::events::caps_withdrawn&) {
                     if (client_->registered()) publish_state();
                   },
                   [&](const cairn::events::logged_in& one) { log(id_, "logged in as {}", one.account); },
                   [&](const cairn::events::sasl_failed& one) {
                     sink_(change::notice{id_, "Not logged in", std::format("The server refused SASL ({}): {}", one.number, one.text)});
                   },
                   [&](const cairn::events::nick_taken& one) { log(id_, "{} is taken: trying {}", one.tried, one.next); },
                   [&](const cairn::events::batch& one) { this->batched(one); },
                   [&](const cairn::events::received& one) { this->received(one.what); },
                   [&](const cairn::events::closed& one) {
                     log(id_, "closed by the server: {}", one.text);
                     say(connection::failed{std::string(one.text)});
                   }},
               e);
  }

  conversation_id server_chat() const { return {id_, how_.where.host}; }
  std::string server_name() const { return state_.network.empty() ? how_.where.host : state_.network; }

  // The chat a line to `target` from `nick` is in: the channel; else the
  // other person -- the target where it is one's own.
  std::string chat_of(std::string_view target, std::string_view nick) const {
    if (is_channel(target)) return std::string(target);
    if (nick.empty() || mine(nick)) return std::string(target);
    return std::string(nick);
  }

  // A line said: a message of the chat it is in, its id and time the
  // server's (msgid, server-time) where it gives them.
  message message_of(const cairn::line_view& l, std::string_view target, std::string_view text, bool emote) {
    const std::string_view nick = l.from ? std::string_view(l.from->nick) : std::string_view();
    const bool from_server = !l.from || (l.from->user.empty() && l.from->host.empty() && nick.find('.') != std::string_view::npos);
    const conversation_id in = from_server || target == "*" ? server_chat() : conversation_id{id_, chat_of(target, nick)};
    message out{.in = in,
                .id = "irc-" + std::to_string(++received_),
                .sender = std::string(nick),
                .at = now(),
                .body = body_of(shown_text(text), emote),
                .outgoing = !nick.empty() && mine(nick)};
    if (!emote)
      if (auto formatted = html_of(text)) out.body.html = std::move(formatted);
    if (const auto id = l.tag_value("msgid")) out.id = std::ranges::to<std::string>(*id);
    if (const auto time = l.tag_value("time")) {
      const std::string stamp = std::ranges::to<std::string>(*time);
      if (const auto at = time_of(stamp)) out.at = *at;
      times_.insert_or_assign(out.id, stamp);
      ids_by_time_.insert_or_assign(stamp, out.id);
    }
    if (const auto reply = l.tag_value("+draft/reply")) out.replies_to = std::ranges::to<std::string>(*reply);
    return out;
  }
  // A private conversation heard of for the first time: in the list.
  void heard_from(const conversation_id& in) {
    if (in == server_chat() || is_channel(in.id) || known_.contains(in.id)) return;
    known_.insert(in.id);
    sink_(change::conversation_updated{.id = in, .kind = conversation_kind::direct{}, .name = in.id});
  }

  // A line come, to every plugin that hears it (hears_irc_messages).
  using sink_plugins_tag = type_tag<typename irc_plugins<Sink>::type>;
  template <class Plugin>
    requires hears_irc_messages<Plugin, account>
  void message_for(const cairn::message& got) {
    irc_message(contract::irc_events{}, type_tag<Plugin>{}, got, *this);
  }
  template <class Plugin>
  void message_for(const cairn::message&) {}
  template <template <class...> class List, class... Plugins>
  void plugins_hear_message(type_tag<List<Plugins...>>, const cairn::message& got) {
    (this->template message_for<Plugins>(got), ...);
  }

  void received(const cairn::message& m) {
    channels_.take(m);
    this->plugins_hear_message(sink_plugins_tag{}, m);
    const auto& l = m.line;
    const std::string_view nick = l.from ? std::string_view(l.from->nick) : std::string_view();
    const auto said = [&](std::string_view target, std::string_view text, bool emote) {
      auto one = message_of(l, target, text, emote);
      heard_from(one.in);
      sink_(change::message_added{std::move(one)});
    };
    spl::visit(spl::overloaded{
                   [&](const cairn::msg::privmsg& one) { said(one.target, one.body, false); },
                   [&](const cairn::msg::notice& one) { said(one.target, one.body, false); },
                   [&](const cairn::msg::action& one) { said(one.target, one.body, true); },
                   [&](const cairn::msg::join& one) {
                     for (std::string_view room : one.channels) {
                       if (mine(nick)) {
                         joined_.insert(std::string(room));
                         sink_(change::conversation_updated{.id = {id_, std::string(room)},
                                                            .kind = conversation_kind::group{},
                                                            .name = std::string(room),
                                                            .theirs = room_part_t{channel_part{.joined = true}}});
                         sink_(change::history_position{{id_, std::string(room)},
                                                        state_.history ? std::optional<std::string>(std::string()) : std::nullopt});
                       }
                       this->members_of(room);
                     }
                   },
                   [&](const cairn::msg::part& one) {
                     for (std::string_view room : one.channels) this->members_of(room);
                   },
                   [&](const cairn::msg::kick& one) { this->members_of(one.channel); },
                   [&](const cairn::msg::quit&) { this->all_members(); },
                   [&](const cairn::msg::nick& one) {
                     if (mine(nick) || cairn::equal(one.to, client_->nick(), client_->server().casemapping)) nick_ = client_->nick();
                     this->all_members();
                   },
                   [&](const cairn::msg::topic& one) {
                     if (one.body) this->topic_of(one.channel, shown_text(*one.body), nick);
                   },
                   [&](const cairn::msg::redact& one) {
                     sink_(change::message_redacted{{id_, chat_of(one.target, nick)}, std::string(one.msgid), std::string(nick),
                                                    one.reason.empty() ? std::nullopt : std::optional<std::string>(one.reason)});
                   },
                   [&](const cairn::msg::reply& one) { this->replied(one); },
                   // A CTCP DCC: a file offered, shown as one to fetch; a
                   // chat offered, connected to as a chat of its own (=nick).
                   [&](const cairn::msg::ctcp& one) {
                     if (one.reply || nick.empty() || !std::ranges::equal(one.command, std::string_view("DCC"), [](char x, char y) { return std::toupper(static_cast<unsigned char>(x)) == y; })) return;
                     const auto offer = dcc_of(one.body);
                     if (!offer) return;
                     spl::visit(spl::overloaded{
                                    [&](const dcc::send& file) {
                                      const dcc_source from{.passive = file.port == 0, .host = file.port == 0 ? std::string(nick) : file.host,
                                                            .port = file.port, .size = file.size, .token = file.token, .name = file.name};
                                      auto in = message_of(l, one.target, "", false);
                                      in.attachment = attachment{.source = source_of(from), .name = file.name,
                                                                 .size = static_cast<std::int64_t>(file.size)};
                                      heard_from(in.in);
                                      sink_(change::message_added{std::move(in)});
                                    },
                                    [&](const dcc::chat& chat) { this->open_chat(std::string(nick), chat); }},
                                *offer);
                   },
                   // Invited: the channel in the list as an invite; joined
                   // where it is accepted, let go where it is declined.
                   [&](const cairn::msg::invite& one) {
                     if (!mine(one.nick)) return;
                     sink_(change::conversation_updated{.id = {id_, std::string(one.channel)},
                                                        .kind = conversation_kind::group{},
                                                        .name = std::string(one.channel),
                                                        .invite = invite_info{.from = std::string(nick), .from_name = std::string(nick)}});
                   },
                   // away-notify: someone away, with what they say, or back.
                   [&](const cairn::msg::away& one) {
                     if (nick.empty()) return;
                     sink_(change::presence_changed{id_, std::string(nick),
                                                    one.body ? mux::presence{availability::away{}, shown_text(*one.body)}
                                                             : mux::presence{availability::online{}, std::nullopt}});
                   },
                   // Client tags without a message: a reaction put on or
                   // taken back (+draft/react, +draft/unreact on
                   // +draft/reply's message).
                   [&](const cairn::msg::tagmsg& one) {
                     const auto tag = [&](std::string_view key) -> std::optional<std::string> {
                       if (const auto v = l.tag_value(key)) return std::ranges::to<std::string>(*v);
                       return std::nullopt;
                     };
                     const auto target = tag("+draft/reply");
                     const auto put = tag("+draft/react");
                     const auto taken = tag("+draft/unreact");
                     if (!target || (!put && !taken) || nick.empty() || mine(nick)) return;
                     sink_(change::reaction_changed{.in = {id_, chat_of(one.target, nick)},
                                                    .id = *target,
                                                    .key = put ? *put : *taken,
                                                    .who = std::string(nick),
                                                    .added = put.has_value(),
                                                    .event = tag("msgid").value_or(""),
                                                    .at = now(),
                                                    .live = true});
                   },
                   // Standard replies: what failed refused, a warning or a
                   // note said.
                   [&](const cairn::msg::fail& one) {
                     sink_(change::refused{id_, std::format("{}: {}", one.command, shown_text(one.description()))});
                   },
                   [&](const cairn::msg::warn& one) {
                     sink_(change::notice{id_, std::string(one.command), shown_text(one.description())});
                   },
                   [&](const cairn::msg::note& one) {
                     sink_(change::notice{id_, std::string(one.command), shown_text(one.description())});
                   },
                   // Read elsewhere up to a time (draft/read-marker): the
                   // line of that time, where it is held.
                   [&](const cairn::msg::markread& one) {
                     std::string_view stamp = one.timestamp;
                     if (!stamp.starts_with("timestamp=")) return;
                     stamp.remove_prefix(10);
                     if (const auto found = ids_by_time_.find(std::string(stamp)); found != ids_by_time_.end())
                       sink_(change::read_marker{{id_, chat_of(one.target, nick_)}, found->second});
                   },
                   // setname: someone's real name, as their card says it.
                   [&](const cairn::msg::setname& one) {
                     if (!nick.empty()) sink_(change::profile_found{id_, std::string(nick), shown_text(one.realname), std::nullopt, std::nullopt});
                   },
                   [](const auto&) {}},
               m.what);
  }

  // A DCC chat: connected to, its lines read as they come -- a chat of its
  // own, =nick, what is said in it written to that connection.
  void open_chat(std::string nick, dcc::chat where) {
    this->spawn_guarded([this, nick = std::move(nick), where = std::move(where)] {
      const std::string chat = "=" + nick;
      try {
        auto peer = std::make_unique<net::stream>(*loop_, *tls_, net::connect(*loop_, where.host, where.port));
        net::stream& own = *peer;
        chats_[chat] = std::move(peer);
        sink_(change::conversation_updated{.id = {id_, chat}, .kind = conversation_kind::direct{}, .name = nick + " (DCC)"});
        std::string pending;
        auto& chunks = own.input();
        for (auto at = chunks.begin(); at != chunks.end(); ++at) {
          pending.append(*at);
          for (auto end = pending.find('\n'); end != std::string::npos; end = pending.find('\n')) {
            std::string line = pending.substr(0, end);
            pending.erase(0, end + 1);
            if (line.ends_with('\r')) line.pop_back();
            sink_(change::message_added{message{.in = {id_, chat}, .id = "dcc-" + std::to_string(++received_), .sender = nick, .at = now(),
                                                .body = {shown_text(line), std::nullopt}}});
          }
        }
      } catch (const net::failure& failed) {
        sink_(change::refused{id_, std::format("The DCC chat with {} failed: {}", nick, failed.what())});
      }
      chats_.erase(chat);
    });
  }

  // The numerics the account shows: a channel's topic and members, the
  // message of the day, and what was refused.
  void replied(const cairn::msg::reply& one) {
    const auto param = [&](std::size_t i) { return std::string(one.params[i]); };
    switch (one.number) {
      case 332: this->topic_of(param(0), shown_text(one.params.back()), {}); break;  // RPL_TOPIC
      case 366: this->members_of(param(0)); break;                                  // RPL_ENDOFNAMES
      case 375: motd_.clear(); break;
      case 372: motd_ += shown_text(one.params.back()) + "\n"; break;
      case 376:
      case 422:
        sink_(motd_read{id_, motd_});
        break;
      // WHOIS: gathered by the nick, told at its end.
      case 311: {  // RPL_WHOISUSER nick user host * :realname
        auto& w = whois_[fold(param(0))];
        w.nick = param(0);
        w.realname = shown_text(one.params.back());
        w.lines.push_back(std::format("{}!{}@{}", param(0), param(1), param(2)));
        break;
      }
      case 312:  // RPL_WHOISSERVER nick server :info
        whois_[fold(param(0))].lines.push_back(std::format("Server: {} ({})", param(1), shown_text(one.params.back())));
        break;
      case 313: whois_[fold(param(0))].lines.push_back(shown_text(one.params.back())); break;  // an operator
      case 317: {  // RPL_WHOISIDLE nick seconds signon :...
        long long seconds = 0;
        const std::string idle = param(1);
        std::from_chars(idle.data(), idle.data() + idle.size(), seconds);
        whois_[fold(param(0))].lines.push_back(std::format("Idle {}m {}s", seconds / 60, seconds % 60));
        break;
      }
      case 319: whois_[fold(param(0))].lines.push_back("Channels: " + shown_text(one.params.back())); break;
      case 330: whois_[fold(param(0))].account = param(1); whois_[fold(param(0))].lines.push_back("Logged in as " + param(1)); break;
      case 671: whois_[fold(param(0))].lines.push_back("Connected securely (TLS)"); break;
      case 301: whois_[fold(param(0))].lines.push_back("Away: " + shown_text(one.params.back())); break;
      case 318: {  // RPL_ENDOFWHOIS
        const auto found = whois_.find(fold(param(0)));
        if (found == whois_.end()) break;
        const auto& w = found->second;
        profile_details details;
        details.biography.plain = w.lines | std::views::join_with('\n') | std::ranges::to<std::string>();
        sink_(change::profile_found{id_, w.nick.empty() ? param(0) : w.nick,
                                    w.realname.empty() ? std::nullopt : std::optional<std::string>(w.realname), std::nullopt, details});
        whois_.erase(found);
        break;
      }
      // LIST: the channels as they come, those with the words asked; told at
      // the end, the busiest first.
      case 322:  // RPL_LIST channel users :topic
        if (listing_) {
          const std::string name = param(0), topic = shown_text(one.params.back());
          const auto has = [&](std::string_view in) {
            const auto lower = [](std::string_view s) {
              return s | std::views::transform([](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }) |
                     std::ranges::to<std::string>();
            };
            return lower(in).find(lower(listing_->query)) != std::string::npos;
          };
          if (listing_->query.empty() || has(name) || has(topic)) {
            std::int64_t users = 0;
            const auto count = param(1);
            std::from_chars(count.data(), count.data() + count.size(), users);
            listing_->rooms.push_back(directory_room{.id = name, .name = name, .alias = name, .topic = topic, .members = users});
          }
        }
        break;
      case 323:  // RPL_LISTEND
        if (listing_) {
          std::ranges::sort(listing_->rooms, std::ranges::greater{}, &directory_room::members);
          if (listing_->rooms.size() > 500) listing_->rooms.resize(500);
          sink_(change::directory_listed{.by = id_, .server = listing_->server, .query = listing_->query, .rooms = std::move(listing_->rooms)});
          listing_.reset();
        }
        break;
      // MONITOR: the people watched, come and gone.
      case 730:  // RPL_MONONLINE :nick!user@host,...
      case 731:  // RPL_MONOFFLINE :nick,...
        for (auto who : one.params.back() | std::views::split(',')) {
          std::string_view one_nick(who.begin(), who.end());
          one_nick = one_nick.substr(0, one_nick.find('!'));
          if (one_nick.empty()) continue;
          sink_(change::presence_changed{id_, std::string(one_nick),
                                         one.number == 730 ? mux::presence{availability::online{}, std::nullopt}
                                                           : mux::presence{availability::offline{}, std::nullopt}});
        }
        break;
      case 433:  // ERR_NICKNAMEINUSE, once registered: a nick asked for taken
        if (client_->registered()) sink_(change::refused{id_, std::format("The nick {} is taken.", param(0))});
        break;
      default:
        // Any other error (400-599): refused, as the server says it.
        if (one.number >= 400 && one.number < 600 && one.number != 433)
          sink_(change::refused{id_, std::format("{}: {}", param(0), shown_text(one.params.back()))});
        break;
    }
  }
  void topic_of(std::string_view room, std::string text, std::string_view by) {
    sink_(change::conversation_updated{.id = {id_, std::string(room)},
                                       .kind = conversation_kind::group{},
                                       .name = std::string(room),
                                       .topic = std::move(text),
                                       .theirs = room_part_t{channel_part{.joined = joined_.contains(std::string(room)),
                                                                          .topic_by = std::string(by)}}});
  }
  // A channel's members, as cairn's channels keeps them: told whole.
  void members_of(std::string_view room) {
    const auto* found = channels_.find(room);
    if (!found) return;
    sink_(change::members_changed{{id_, found->name},
                                  found->members | std::views::transform([](const cairn::member& one) {
                                    return mux::member{.id = one.nick, .name = one.nick, .role = role_of(one.prefixes)};
                                  }) | std::ranges::to<std::vector<mux::member>>(),
                                  {}});
  }
  void all_members() {
    for (const auto& one : channels_.all()) this->members_of(one.name);
  }

  // A batch ended: a page of history (chathistory), put before what the
  // chat holds, newest first; and where to page back from next -- none
  // where it gave less than a page, the beginning.
  void batched(const cairn::events::batch& done) {
    // draft/multiline: its lines one message -- joined by line breaks, or
    // run on where a line says draft/multiline-concat.
    if (done.type == "draft/multiline" && !done.params.empty()) {
      std::string text;
      std::optional<cairn::line_view> first;
      for (const auto& wire : done.lines)
        if (const auto l = cairn::try_parse(wire); l && (l->is("PRIVMSG") || l->is("NOTICE"))) {
          if (!first) first = *l;
          else if (!l->tag_value("draft/multiline-concat")) text += '\n';
          text.append(l->param(1));
        }
      if (!first) return;
      auto one = message_of(*first, done.params.front(), text, false);
      heard_from(one.in);
      sink_(change::message_added{std::move(one)});
      return;
    }
    // A bouncer's networks (soju.im/bouncer-networks): said, each with the id
    // an account binds to it by.
    if (done.type == "soju.im/bouncer-networks") {
      std::vector<std::string> said;
      for (const auto& wire : done.lines)
        if (const auto l = cairn::try_parse(wire); l && l->is("BOUNCER") && l->param(0) == "NETWORK") {
          std::string name(l->param(1));
          for (auto attr : l->param(2) | std::views::split(';')) {
            const std::string_view one(attr.begin(), attr.end());
            if (one.starts_with("name=")) name = std::string(one.substr(5));
          }
          said.push_back(std::format("{} (ID {})", name, l->param(1)));
        }
      if (!said.empty())
        sink_(change::notice{id_, "Networks on this bouncer",
                             (said | std::views::join_with(std::string_view(", ")) | std::ranges::to<std::string>()) +
                                 ". For one of them, add an account with its ID as the bouncer network (Advanced)."});
      return;
    }
    // The chats of before (CHATHISTORY TARGETS): each a private chat, in
    // the list, paged back from its newest.
    if (done.type == "draft/chathistory-targets" || done.type == "chathistory-targets") {
      for (const auto& wire : done.lines)
        if (const auto l = cairn::try_parse(wire); l && l->is("CHATHISTORY") && l->param(0) == "TARGETS") {
          const std::string who(l->param(1));
          if (is_channel(who)) continue;
          known_.insert(who);
          create_direct(who);
        }
      return;
    }
    // Any other batch -- a netsplit's, a netjoin's: its lines as if they had
    // come one by one.
    if (done.type != "chathistory" && done.type != "draft/chathistory") {
      for (const auto& wire : done.lines)
        if (const auto l = cairn::try_parse(wire)) this->received(cairn::read(*l));
      return;
    }
    if (done.params.empty()) return;
    const conversation_id in{id_, done.params.front()};
    std::vector<message> page;
    for (const auto& wire : done.lines) {
      const auto l = cairn::try_parse(wire);
      if (!l) continue;
      const cairn::message m = cairn::read(*l);
      spl::visit(spl::overloaded{[&](const cairn::msg::privmsg& one) { page.push_back(message_of(*l, one.target, one.body, false)); },
                                 [&](const cairn::msg::notice& one) { page.push_back(message_of(*l, one.target, one.body, false)); },
                                 [&](const cairn::msg::action& one) { page.push_back(message_of(*l, one.target, one.body, true)); },
                                 [](const auto&) {}},
                 m.what);
    }
    std::optional<std::string> next;
    if (page.size() >= page_size) {
      const auto& oldest = page.front();
      if (!oldest.id.starts_with("irc-")) next = "msgid=" + oldest.id;
      else if (const auto stamp = times_.find(oldest.id); stamp != times_.end()) next = "timestamp=" + stamp->second;
    }
    for (auto& one : page | std::views::reverse) {
      one.in = in;
      sink_(change::message_added{std::move(one), placement::at_start{}});
    }
    sink_(change::history_position{in, next});
  }

  net::loop* loop_;
  net::tls* tls_;
  settings how_;
  Sink sink_;
  account_id id_;
  std::optional<cairn::client> client_;
  cairn::channels channels_;
  net::stream* wire_ = nullptr;
  state state_;
  std::string nick_, motd_;
  // The channels to be in: joined again on every connection.
  std::set<std::string> joined_;
  // The private conversations told of.
  std::set<std::string> known_;
  // Each line's server-time, by its id: what MARKREAD and a page back name;
  // and the id by the time, for a MARKREAD come.
  std::unordered_map<std::string, std::string> times_, ids_by_time_;
  // A LIST under way: where, what was asked, what came so far.
  struct listing {
    std::string server, query;
    std::vector<directory_room> rooms;
  };
  std::optional<listing> listing_;
  // WHOIS replies gathered, by the nick folded, until 318.
  struct whois_reply {
    std::string nick, realname, account, server;
    std::vector<std::string> lines;
  };
  std::map<std::string, whois_reply> whois_;
  // The people watched (MONITOR), folded: watched again on each connection.
  std::set<std::string> watched_;
  std::uint64_t batches_ = 0;
  // The DCC chats open, by their chat (=nick).
  std::map<std::string, std::unique_ptr<net::stream>> chats_;
  typename irc_kept_slots<typename irc_plugins<Sink>::type>::type plugins_kept_{};
  std::uint64_t sent_ = 0, received_ = 0;
  bool stopping_ = false, started_ = false, retry_ = true, connected_ = false;
  std::size_t commands_ = 0;
};

// The account as made: named as the other protocols' are (account_of), for
// what deletes it.
template <class Sink>
using account_of = account<Sink>;

}  // namespace mux::proto::irc::client
