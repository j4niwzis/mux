// SPDX-License-Identifier: AGPL-3.0-only
// mux.xmpp: an XMPP account, run by a fiber on mux.net's loop through tern,
// saying what happens as mux.core's changes.
//
// Connecting: the domain's SRV records tried in order, STARTTLS with the
// certificate checked against the domain, SCRAM (-PLUS with channel
// binding) through tern; then the roster -- from the version kept, where
// the server versions rosters -- presence, and the stanzas as they come,
// through a tern inbox of the account's own.
export module mux.xmpp;

import std;
import tern;
import mux.core;
import mux.net;

export namespace mux::xmpp {

struct settings {
  std::string address;  // user@domain
  std::string password;
  std::string resource = "mux";
  // Where to connect, instead of what the domain's SRV records say.
  std::optional<std::string> host;
  std::optional<std::uint16_t> port;
  // PLAIN over a stream TLS has not secured: only for a server on this
  // machine, under test. Never over a network.
  bool plain_without_tls = false;
};

// A JID's bare part: what a conversation is kept under.
inline std::string bare(std::string_view jid) { return std::string(jid.substr(0, jid.find('/'))); }

inline availability_t availability_of(const std::optional<std::string>& show) {
  if (!show)
    return availability::online{};
  if (*show == "away")
    return availability::away{};
  if (*show == "xa")
    return availability::extended_away{};
  if (*show == "dnd")
    return availability::do_not_disturb{};
  if (*show == "chat")
    return availability::chat{};
  return availability::online{};
}

// XEP-0203's stamp, XEP-0082's DateTime: CCYY-MM-DDThh:mm:ss[.sss](Z|+hh:mm|-hh:mm)
// -- when a stanza was first sent. Read by hand: libc++ has no
// std::chrono::parse yet.
inline std::optional<std::chrono::sys_time<std::chrono::milliseconds>> stamp_of(std::string_view text) {
  std::size_t at = 0;
  const auto number = [&](std::size_t digits) -> std::optional<int> {
    if (at + digits > text.size())
      return std::nullopt;
    int value = 0;
    for (std::size_t i = 0; i < digits; ++i) {
      const char c = text[at + i];
      if (c < '0' || c > '9')
        return std::nullopt;
      value = value * 10 + (c - '0');
    }
    at += digits;
    return value;
  };
  const auto expect = [&](char c) {
    if (at < text.size() && text[at] == c) {
      ++at;
      return true;
    }
    return false;
  };
  const auto year = number(4);
  if (!year || !expect('-'))
    return std::nullopt;
  const auto month = number(2);
  if (!month || !expect('-'))
    return std::nullopt;
  const auto day = number(2);
  if (!day || !expect('T'))
    return std::nullopt;
  const auto hour = number(2);
  if (!hour || !expect(':'))
    return std::nullopt;
  const auto minute = number(2);
  if (!minute || !expect(':'))
    return std::nullopt;
  const auto second = number(2);
  if (!second)
    return std::nullopt;
  int millis = 0;
  if (expect('.')) {
    int digits = 0;
    while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
      if (digits < 3)
        millis = millis * 10 + (text[at] - '0');
      ++digits;
      ++at;
    }
    if (digits == 0)
      return std::nullopt;
    for (; digits < 3; ++digits)
      millis *= 10;
  }
  std::chrono::minutes offset{0};
  if (!expect('Z')) {
    const bool behind = at < text.size() && text[at] == '-';
    if (!expect('+') && !expect('-'))
      return std::nullopt;
    const auto hours = number(2);
    if (!hours || !expect(':'))
      return std::nullopt;
    const auto minutes = number(2);
    if (!minutes)
      return std::nullopt;
    offset = std::chrono::hours(*hours) + std::chrono::minutes(*minutes);
    if (behind)
      offset = -offset;
  }
  if (at != text.size())
    return std::nullopt;
  const std::chrono::year_month_day date{std::chrono::year(*year), std::chrono::month(static_cast<unsigned>(*month)),
                                         std::chrono::day(static_cast<unsigned>(*day))};
  if (!date.ok() || *hour > 23 || *minute > 59 || *second > 60)
    return std::nullopt;
  return std::chrono::sys_days(date) + std::chrono::hours(*hour) + std::chrono::minutes(*minute) +
         std::chrono::seconds(*second) + std::chrono::milliseconds(millis) - offset;
}

// An account, and the changes it makes: `Sink` is called with each change,
// on the loop's thread -- a mailbox's push, or a test's vector.
template <class Sink>
class account {
 public:
  using session_type = decltype(tern::connect(std::declval<net::stream&>(), std::declval<const tern::options&>(),
                                              tern::answering<>{}, net::scheduler{}));

  account(net::loop& loop, net::tls& tls, settings how, Sink sink)
      : loop_(&loop), tls_(&tls), how_(std::move(how)), sink_(std::move(sink)) {
    const auto at = how_.address.find('@');
    user_ = how_.address.substr(0, at);
    domain_ = at == std::string::npos ? how_.address : how_.address.substr(at + 1);
    id_ = account_id{protocol::xmpp{}, bare(how_.address)};
  }
  account(const account&) = delete;
  account& operator=(const account&) = delete;

  const account_id& id() const noexcept { return id_; }

  // Connected, and kept connected, by a fiber of its own.
  void start() {
    loop_->spawn([this] { run(); });
  }

  // A chat message sent: from any fiber, or posted to the loop from another
  // thread. What was sent is said as a change at once, and marked sent once
  // it has gone out.
  void send(std::string to, std::string text) {
    loop_->spawn([this, to = bare(to), text = std::move(text)] {
      message out{.in = {id_, to},
                  .id = "mux-" + std::to_string(++sent_),
                  .sender = id_.address,
                  .at = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()),
                  .body = {text, std::nullopt},
                  .outgoing = true,
                  .delivery = delivery::sending{}};
      sink_(change::message_added{out});
      if (!session_) {
        sink_(change::delivery_changed{out.in, out.id, delivery::failed{}});
        return;
      }
      session_->send(tern::message::chat{.to = to, .id = out.id, .body = text});
      sink_(change::delivery_changed{out.in, out.id, wire_ && !wire_->failed() ? delivery_t{delivery::sent{}} : delivery_t{delivery::failed{}}});
    });
  }

  // Unavailable, and the stream closed.
  void stop() {
    loop_->spawn([this] {
      stopping_ = true;
      if (session_)
        session_->close();
    });
  }

 private:
  void say(connection_t state) { sink_(change::connection_changed{id_, std::move(state)}); }

  void run() {
    say(connection::connecting{});
    std::vector<tern::srv::target> targets;
    if (how_.host)
      targets.push_back({0, 0, how_.port.value_or(5222), *how_.host});
    else
      targets = net::xmpp_targets(*loop_, domain_);
    std::optional<net::tcp::socket> socket;
    std::string why;
    for (const auto& target : targets) {
      try {
        socket.emplace(net::connect(*loop_, target.host, target.port));
        break;
      } catch (const net::failure& failed) {
        why = failed.what();
      }
    }
    if (!socket) {
      say(connection::failed{"no server of " + domain_ + " answered: " + why});
      return;
    }
    net::stream wire(*loop_, *tls_, std::move(*socket));
    wire_ = &wire;
    tern::options options;
    options.username = user_;
    options.domain = domain_;
    options.password = how_.password;
    options.resource = how_.resource;
    options.plain_without_tls = how_.plain_without_tls;
    options.self = {.identities = {{.category = "client", .type = "pc", .name = "mux"}}};
    options.caps_node = "https://github.com/j4niwzis/mux";
    auto made = tern::try_connect(wire, options, tern::answering<>{}, net::scheduler{loop_});
    if (!made) {
      wire_ = nullptr;
      say(connection::failed{made.error().detail.empty() ? "the connection failed" : made.error().detail});
      return;
    }
    session_type& session = *made;
    session_ = &session;
    say(connection::online{});

    // Opened before anything is asked, so that nothing that arrives while
    // the roster is fetched is missed.
    auto inbox = session.open_inbox();
    if (session.try_sync(roster_))
      for (const auto& [jid, item] : roster_.items)
        contact(item);
    session.available();

    for (;;) {
      auto one = inbox.try_next();
      if (!one) {
        if (!stopping_)
          say(connection::failed{one.error().detail});
        break;
      }
      if (!*one)
        break;
      on(**one);
    }
    session_ = nullptr;
    wire_ = nullptr;
    if (stopping_ || !wire.failed())
      say(connection::offline{});
  }

  void contact(const tern::roster_item& item) {
    sink_(change::conversation_updated{.id = {id_, item.jid},
                                       .kind = conversation_kind::direct{},
                                       .name = item.name.value_or(item.jid)});
  }

  void on(const tern::stanza_t& one) {
    if (const auto* message = std::get_if<tern::message_t>(&one)) {
      std::visit([this](const auto& got) { on_message(got); }, *message);
    } else if (const auto* presence = std::get_if<tern::presence_t>(&one)) {
      std::visit([this](const auto& got) { on_presence(got); }, *presence);
    } else if (const auto* iq = std::get_if<tern::iq_t>(&one)) {
      // A roster push, handed out once tern has answered it.
      if (const auto* set = std::get_if<tern::iq::set>(iq))
        if (roster_.apply(*set))
          for (const auto& [jid, item] : roster_.items)
            contact(item);
    }
  }

  // A chat or a normal message with a body is a message; anything else is
  // not the conversation's.
  void on_message(const tern::message::chat& got) { this->text_message(got); }
  void on_message(const tern::message::normal& got) { this->text_message(got); }
  template <class Other>
  void on_message(const Other&) {}

  template <class Message>
  void text_message(const Message& got) {
    if (!got.body || !got.from)
      return;
    const std::string from = bare(*got.from);
    message in{.in = {id_, from},
               .id = got.id.value_or(""),
               .sender = from,
               .at = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()),
               .body = {*got.body, std::nullopt}};
    for (const auto& carried : got.payload)
      if (const auto* delayed = carried.template get_if<tern::delay>())
        if (const auto at = stamp_of(delayed->stamp))
          in.at = *at;
    sink_(change::message_added{std::move(in)});
  }

  void on_presence(const tern::presence::available& got) {
    if (got.from)
      sink_(change::presence_changed{id_, bare(*got.from), {availability_of(got.show), got.status}});
  }
  void on_presence(const tern::presence::unavailable& got) {
    if (got.from)
      sink_(change::presence_changed{id_, bare(*got.from), {availability::offline{}, got.status}});
  }
  template <class Other>
  void on_presence(const Other&) {}

  net::loop* loop_;
  net::tls* tls_;
  settings how_;
  Sink sink_;
  std::string user_, domain_;
  account_id id_;
  tern::roster_cache roster_;
  session_type* session_ = nullptr;
  net::stream* wire_ = nullptr;
  std::uint64_t sent_ = 0;
  bool stopping_ = false;
};

}  // namespace mux::xmpp
