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
import mux.proto.irc;
import mux.proto.irc.kept;
import mux.proto.irc.links;
import mux.proto.irc.changes;

export namespace mux::proto::irc::client {

struct settings {
  std::string address;  // what the account is known by: ircs://nick@host
  target where;
  std::string password;  // SASL's, where not empty
  std::optional<std::string> login, server_password, username, realname;
  std::vector<std::string> channels;
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

// What a status symbol in a channel stands for, as mux shows a member's role.
[[nodiscard]] inline std::optional<std::string> role_of(std::string_view prefixes) {
  if (prefixes.empty()) return std::nullopt;
  static constexpr std::array<std::pair<char, std::string_view>, 5> known{
      {{'~', "founder"}, {'&', "admin"}, {'@', "operator"}, {'%', "half-operator"}, {'+', "voice"}}};
  const auto found = std::ranges::find(known, prefixes.front(), &decltype(known)::value_type::first);
  return found == known.end() ? std::nullopt : std::optional<std::string>(found->second);
}

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
  }

  // A message said, in as many lines as it takes: each line of the text a
  // PRIVMSG, cut where a line is too long for the server (cairn's pieces).
  void send(std::string to, std::string text, std::optional<std::string> reply_to = std::nullopt, std::vector<mux::mention> = {},
            std::vector<mux::styled_run> = {}, std::optional<std::string> local = std::nullopt, plugin_message_parts = {}) {
    this->spawn_guarded([this, to = std::move(to), text = std::move(text), reply_to = std::move(reply_to), local = std::move(local)] {
      message out{.in = {id_, to},
                  .id = local ? *local : "mux-" + std::to_string(++sent_),
                  .sender = nick_,
                  .at = now(),
                  .body = {text, std::nullopt},
                  .outgoing = true,
                  .delivery = delivery::sending{}};
      out.replies_to = reply_to;
      sink_(change::message_added{out});
      const bool sent = this->say_lines(to, text, reply_to, [](std::string_view target, std::string_view piece) {
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
    if (!how_.password.empty()) asked.sasl = cairn::sasl::plain{how_.login.value_or(where.nick), how_.password};
    // Not echo-message: what is sent is shown as sent, and an echo would be
    // a second copy of it.
    std::erase(asked.caps, std::string("echo-message"));
    asked.caps.push_back("chathistory");
    asked.version = "mux";
    asked.source = "https://github.com/j4niwzis/mux";
    client_.emplace(std::move(asked));
    nick_ = where.nick;
    channels_ = cairn::channels(where.nick);
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
                     connected_ = true;
                     nick_ = std::string(one.nick);
                     channels_.set_nick(nick_);
                     log(id_, "registered, as {}", nick_);
                     say(connection::online{});
                     publish_state();
                     sink_(change::conversation_updated{.id = server_chat(), .kind = conversation_kind::direct{}, .name = server_name()});
                     for (const auto& room : joined_) this->write(cairn::msg::join{cairn::commas{room}, {}, {}, {}});
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

  void received(const cairn::message& m) {
    channels_.take(m);
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
                   [](const auto&) {}},
               m.what);
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
      case 403: case 405: case 471: case 473: case 474: case 475: case 477: case 482: case 404: case 401:
        sink_(change::refused{id_, std::format("{}: {}", param(0), shown_text(one.params.back()))});
        break;
      default: break;
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
    if ((done.type != "chathistory" && done.type != "draft/chathistory") || done.params.empty()) return;
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
  // Each line's server-time, by its id: what MARKREAD and a page back name.
  std::unordered_map<std::string, std::string> times_;
  std::uint64_t sent_ = 0, received_ = 0;
  bool stopping_ = false, started_ = false, retry_ = true, connected_ = false;
  std::size_t commands_ = 0;
};

// The account as made: named as the other protocols' are (account_of), for
// what deletes it.
template <class Sink>
using account_of = account<Sink>;

}  // namespace mux::proto::irc::client
