// SPDX-License-Identifier: AGPL-3.0-only
// mux.dbus -- The desktop's notifications, as org.freedesktop.Notifications
// takes them: a message to the session bus, written here -- the bus's
// socket, its EXTERNAL sign-in, and the one method call marshalled by hand,
// as the D-Bus specification lays it out -- rather than a library for it.
// And UnifiedPush's connector (run_unified_push): a connection kept to the
// bus, a name owned on it, the distributor's calls read and answered.
module;
#include <stddef.h>
#if __has_include(<sys/un.h>)
#include <sys/socket.h>
#include <poll.h>      // a second's wait for what the bus says
#include <sys/time.h>  // timeval, for the reply wait
#include <sys/un.h>
#include <unistd.h>
#endif
export module mux.dbus;

import std;
import splice;

namespace mux::dbus::detail {

// A message's body and header as the wire has them: little-endian, each
// value aligned to its size from the message's start.
class writer {
 public:
  void align(std::size_t to) {
    while (out_.size() % to != 0)
      out_.push_back('\0');
  }
  void byte(std::uint8_t value) { out_.push_back(static_cast<char>(value)); }
  void u32(std::uint32_t value) {
    this->align(4);
    for (int i = 0; i < 4; ++i)
      out_.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
  }
  void i32(std::int32_t value) { this->u32(static_cast<std::uint32_t>(value)); }
  void string(std::string_view text) {  // s and o
    this->u32(static_cast<std::uint32_t>(text.size()));
    out_.append(text);
    out_.push_back('\0');
  }
  void signature(std::string_view text) {  // g
    this->byte(static_cast<std::uint8_t>(text.size()));
    out_.append(text);
    out_.push_back('\0');
  }
  // An array: its length, then its elements from their alignment on; the
  // length counted from after that padding, as the specification says.
  template <class Fill>
  void array(std::size_t element_alignment, Fill fill) {
    this->u32(0);
    const std::size_t length_at = out_.size() - 4;
    this->align(element_alignment);
    const std::size_t start = out_.size();
    fill();
    const auto length = static_cast<std::uint32_t>(out_.size() - start);
    for (int i = 0; i < 4; ++i)
      out_[length_at + i] = static_cast<char>((length >> (8 * i)) & 0xFF);
  }
  [[nodiscard]] std::string& bytes() { return out_; }

 private:
  std::string out_;
};

// A method call: its header fields -- path, interface, member, destination,
// the body's signature -- then its body.
inline std::string call(std::uint32_t serial, std::string_view path, std::string_view interface,
                        std::string_view member, std::string_view destination, std::string_view signature,
                        const std::string& body) {
  writer out;
  out.byte('l');  // little-endian
  out.byte(1);    // a method call
  out.byte(0);    // no flags
  out.byte(1);    // the protocol's version
  out.u32(static_cast<std::uint32_t>(body.size()));
  out.u32(serial);
  // A field: its code, its variant's type, its value -- a string or an
  // object path; the body's signature, a signature.
  const auto text_field = [&](std::uint8_t code, std::string_view type, std::string_view value) {
    out.align(8);
    out.byte(code);
    out.signature(type);
    out.string(value);
  };
  const auto signature_field = [&](std::uint8_t code, std::string_view value) {
    out.align(8);
    out.byte(code);
    out.signature("g");
    out.signature(value);
  };
  out.array(8, [&] {
    text_field(1, "o", path);
    text_field(2, "s", interface);
    text_field(3, "s", member);
    text_field(6, "s", destination);
    if (!signature.empty())
      signature_field(8, signature);
  });
  out.align(8);
  out.bytes() += body;
  return std::move(out.bytes());
}

}  // namespace mux::dbus::detail

export namespace mux::dbus {

// A notification on the desktop: the title over the text, with an "Open"
// action -- asked of the session bus's notification service. False where
// there is no bus, or it did not take it.
inline bool notify(std::string_view title, std::string_view text) {
#if __has_include(<sys/un.h>)
  const char* address = std::getenv("DBUS_SESSION_BUS_ADDRESS");
  if (address == nullptr)
    return false;
  // unix:path=... or unix:abstract=..., the first of several.
  const std::string_view said(address);
  const std::string_view first = said.substr(0, said.find(';'));
  sockaddr_un where{};
  where.sun_family = AF_UNIX;
  socklen_t length = 0;
  const auto value_of = [&](std::string_view key) -> std::optional<std::string_view> {
    const auto at = first.find(key);
    if (at == std::string_view::npos)
      return std::nullopt;
    const std::string_view rest = first.substr(at + key.size());
    return rest.substr(0, rest.find(','));
  };
  if (const auto path = value_of("path=")) {
    if (path->size() >= sizeof(where.sun_path))
      return false;
    std::ranges::copy(*path, where.sun_path);
    length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path->size() + 1);
  } else if (const auto name = value_of("abstract=")) {
    if (name->size() + 1 >= sizeof(where.sun_path))
      return false;
    where.sun_path[0] = '\0';
    std::ranges::copy(*name, where.sun_path + 1);
    length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + name->size() + 1);
  } else {
    return false;
  }
  const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
    return false;
  struct closer {
    int fd;
    ~closer() { ::close(fd); }
  } const closing{fd};
  timeval wait{.tv_sec = 2, .tv_usec = 0};
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof wait);
  // The one cast a C API asks for: connect() takes every
  // kind of address as a sockaddr.
  if (::connect(fd, reinterpret_cast<const sockaddr*>(&where), length) != 0)
    return false;
  const auto send_all = [&](std::string_view bytes) {
    while (!bytes.empty()) {
      const auto sent = ::send(fd, bytes.data(), bytes.size(), MSG_NOSIGNAL);
      if (sent <= 0)
        return false;
      bytes.remove_prefix(static_cast<std::size_t>(sent));
    }
    return true;
  };
  const auto read_line = [&]() -> std::string {
    std::string line;
    char c = 0;
    while (::recv(fd, &c, 1, 0) == 1) {
      line.push_back(c);
      if (line.ends_with("\r\n"))
        break;
    }
    return line;
  };
  // Signing in: EXTERNAL, as this user.
  std::string uid;
  for (const char digit : std::to_string(::getuid()))
    uid += std::format("{:02x}", static_cast<unsigned>(digit));
  if (!send_all(std::string_view("\0", 1)) || !send_all("AUTH EXTERNAL " + uid + "\r\n"))
    return false;
  if (!read_line().starts_with("OK"))
    return false;
  if (!send_all("BEGIN\r\n"))
    return false;
  // Hello, as every connection must first; then the notification.
  using detail::call;
  if (!send_all(call(1, "/org/freedesktop/DBus", "org.freedesktop.DBus", "Hello", "org.freedesktop.DBus", "", "")))
    return false;
  detail::writer body;
  body.string("mux");     // app_name
  body.u32(0);            // replaces_id
  body.string("");        // app_icon
  // The body may be read as markup (the spec's body-markup): what a message
  // says is escaped, so that none of it is a tag -- an <a>, an <img> of a
  // file (review 5).
  const std::string escaped = text | std::views::transform([](char c) -> std::string {
                                switch (c) {
                                  case '&': return "&amp;";
                                  case '<': return "&lt;";
                                  case '>': return "&gt;";
                                  default: return std::string(1, c);
                                }
                              }) |
                              std::views::join | std::ranges::to<std::string>();
  body.string(title);     // summary
  body.string(escaped);   // body
  body.array(4, [&] {     // actions: key, label
    body.string("default");
    body.string("Open");
  });
  body.array(8, [&] {});  // hints: none
  body.i32(-1);           // expire_timeout: the server's
  if (!send_all(call(2, "/org/freedesktop/Notifications", "org.freedesktop.Notifications", "Notify",
                     "org.freedesktop.Notifications", "susssasa{sv}i", body.bytes())))
    return false;
  // The replies read, so that the bus has them all before the socket goes.
  char drain[512];
  (void)::recv(fd, drain, sizeof drain, 0);
  return true;
#else
  (void)title;
  (void)text;
  return false;
#endif
}

}  // namespace mux::dbus

// ---- Reading the bus, and UnifiedPush over it ----
namespace mux::dbus::detail {

// A message as the wire has it, read: little-endian, each value aligned to
// its size from the message's start -- the writer's counterpart.
class reader {
 public:
  explicit reader(std::string_view bytes) : in_(bytes) {}
  [[nodiscard]] bool align(std::size_t to) {
    while (at_ % to != 0) {
      if (at_ >= in_.size())
        return false;
      ++at_;
    }
    return true;
  }
  [[nodiscard]] std::optional<std::uint8_t> byte() {
    if (at_ >= in_.size())
      return std::nullopt;
    return static_cast<std::uint8_t>(in_[at_++]);
  }
  [[nodiscard]] std::optional<std::uint32_t> u32() {
    if (!this->align(4) || at_ + 4 > in_.size())
      return std::nullopt;
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i)
      value |= static_cast<std::uint32_t>(static_cast<unsigned char>(in_[at_ + i])) << (8 * i);
    at_ += 4;
    return value;
  }
  [[nodiscard]] std::optional<std::string> string() {  // s and o
    const auto length = this->u32();
    if (!length || at_ + *length + 1 > in_.size())
      return std::nullopt;
    std::string out(in_.substr(at_, *length));
    at_ += *length + 1;
    return out;
  }
  [[nodiscard]] std::optional<std::string> signature() {  // g
    const auto length = this->byte();
    if (!length || at_ + *length + 1 > in_.size())
      return std::nullopt;
    std::string out(in_.substr(at_, *length));
    at_ += *length + 1;
    return out;
  }
  [[nodiscard]] std::optional<std::vector<std::uint8_t>> bytes() {  // ay
    const auto length = this->u32();
    if (!length || at_ + *length > in_.size())
      return std::nullopt;
    std::vector<std::uint8_t> out = in_.substr(at_, *length) |
                                    std::views::transform([](char c) { return static_cast<std::uint8_t>(c); }) |
                                    std::ranges::to<std::vector>();
    at_ += *length;
    return out;
  }
  [[nodiscard]] std::size_t at() const noexcept { return at_; }
  void seek(std::size_t to) noexcept { at_ = to; }

  // One whole type of a signature: a value of it passed over, and the type
  // taken off the front of the signature. What this does not take apart --
  // anything but the strings and bytes UnifiedPush sends -- is skipped by.
  [[nodiscard]] bool skip(std::string_view& types) {
    if (types.empty())
      return false;
    const std::string_view whole = one_type(types);
    if (whole.empty())
      return false;
    types.remove_prefix(whole.size());
    return this->skip_value(whole);
  }

  // The first complete type of a signature.
  [[nodiscard]] static std::string_view one_type(std::string_view types) {
    if (types.empty())
      return {};
    if (types.front() == 'a') {
      const std::string_view element = one_type(types.substr(1));
      return element.empty() ? std::string_view() : types.substr(0, 1 + element.size());
    }
    if (types.front() == '(' || types.front() == '{') {
      const char closing = types.front() == '(' ? ')' : '}';
      std::size_t at = 1;
      while (at < types.size() && types[at] != closing) {
        const std::string_view inner = one_type(types.substr(at));
        if (inner.empty())
          return {};
        at += inner.size();
      }
      return at < types.size() ? types.substr(0, at + 1) : std::string_view();
    }
    return types.substr(0, 1);
  }
  // How a type's values are aligned.
  [[nodiscard]] static std::size_t alignment_of(char type) {
    switch (type) {
      case 'y': case 'g': case 'v': return 1;
      case 'n': case 'q': return 2;
      case 'x': case 't': case 'd': case '(': case '{': return 8;
      default: return 4;
    }
  }

 private:
  [[nodiscard]] bool skip_value(std::string_view type) {
    switch (type.front()) {
      case 'y':
        return this->byte().has_value();
      case 'n': case 'q':
        if (!this->align(2) || at_ + 2 > in_.size())
          return false;
        at_ += 2;
        return true;
      case 'x': case 't': case 'd':
        if (!this->align(8) || at_ + 8 > in_.size())
          return false;
        at_ += 8;
        return true;
      case 's': case 'o':
        return this->string().has_value();
      case 'g':
        return this->signature().has_value();
      case 'v': {
        const auto inner = this->signature();
        if (!inner)
          return false;
        std::string_view rest = *inner;
        return this->skip(rest) && rest.empty();
      }
      case 'a': {
        const auto length = this->u32();
        if (!length || !this->align(alignment_of(type[1])) || at_ + *length > in_.size())
          return false;
        at_ += *length;
        return true;
      }
      case '(': case '{': {
        if (!this->align(8))
          return false;
        std::string_view inner = type.substr(1, type.size() - 2);
        while (!inner.empty())
          if (!this->skip(inner))
            return false;
        return true;
      }
      default:  // b, i, u, h
        if (!this->align(4) || at_ + 4 > in_.size())
          return false;
        at_ += 4;
        return true;
    }
  }

  std::string_view in_;
  std::size_t at_ = 0;
};

// A dictionary's value as UnifiedPush sends them: a string, bytes, or
// something else, passed over.
struct other_value {};
using dict_value = splice::variant<std::string, std::vector<std::uint8_t>, other_value>;

// a{sv}, read into its keys and values.
inline std::optional<std::map<std::string, dict_value>> read_dict(reader& in) {
  const auto length = in.u32();
  if (!length || !in.align(8))
    return std::nullopt;
  const std::size_t end = in.at() + *length;
  std::map<std::string, dict_value> out;
  while (in.at() < end) {
    if (!in.align(8))
      return std::nullopt;
    auto key = in.string();
    auto type = in.signature();
    if (!key || !type)
      return std::nullopt;
    if (*type == "s") {
      auto value = in.string();
      if (!value)
        return std::nullopt;
      out.insert_or_assign(std::move(*key), dict_value{std::move(*value)});
    } else if (*type == "ay") {
      auto value = in.bytes();
      if (!value)
        return std::nullopt;
      out.insert_or_assign(std::move(*key), dict_value{std::move(*value)});
    } else {
      std::string_view rest = *type;
      if (!in.skip(rest) || !rest.empty())
        return std::nullopt;
      out.insert_or_assign(std::move(*key), dict_value{other_value{}});
    }
  }
  return out;
}
// A string the dictionary has under `key`, where it has one.
inline std::optional<std::string> text_in(const std::map<std::string, dict_value>& dict, std::string_view key) {
  const auto found = dict.find(std::string(key));
  if (found == dict.end())
    return std::nullopt;
  return splice::visit(splice::overloaded{[](const std::string& text) -> std::optional<std::string> { return text; },
                                          [](const std::vector<std::uint8_t>&) -> std::optional<std::string> { return std::nullopt; },
                                          [](other_value) -> std::optional<std::string> { return std::nullopt; }},
                       found->second);
}
// a{sv} of strings, written.
inline void write_dict(writer& out, const std::vector<std::pair<std::string, std::string>>& entries) {
  out.array(8, [&] {
    for (const auto& [key, value] : entries) {
      out.align(8);
      out.string(key);
      out.signature("s");
      out.string(value);
    }
  });
}

// A message's header fields, those this sends.
struct fields {
  std::string_view path, interface, member, destination, error_name, signature;
  std::optional<std::uint32_t> reply_serial;
};
enum class kind : std::uint8_t { call = 1, method_return = 2, error = 3, signal = 4 };
inline std::string message(kind type, std::uint8_t flags, std::uint32_t serial, const fields& with, const std::string& body) {
  writer out;
  out.byte('l');
  out.byte(static_cast<std::uint8_t>(type));
  out.byte(flags);
  out.byte(1);
  out.u32(static_cast<std::uint32_t>(body.size()));
  out.u32(serial);
  const auto text_field = [&](std::uint8_t code, std::string_view type_of, std::string_view value) {
    if (value.empty())
      return;
    out.align(8);
    out.byte(code);
    out.signature(type_of);
    out.string(value);
  };
  out.array(8, [&] {
    text_field(1, "o", with.path);
    text_field(2, "s", with.interface);
    text_field(3, "s", with.member);
    text_field(4, "s", with.error_name);
    if (with.reply_serial) {
      out.align(8);
      out.byte(5);
      out.signature("u");
      out.u32(*with.reply_serial);
    }
    text_field(6, "s", with.destination);
    if (!with.signature.empty()) {
      out.align(8);
      out.byte(8);
      out.signature("g");
      out.signature(with.signature);
    }
  });
  out.align(8);
  out.bytes() += body;
  return std::move(out.bytes());
}

// A message come from the bus: its header's fields, and its body.
struct incoming {
  kind type = kind::signal;
  std::uint32_t serial = 0;
  std::optional<std::uint32_t> reply_serial;
  std::string path, interface, member, error_name, sender, signature;
  std::string body;
};

#if __has_include(<sys/un.h>)
// A connection to the session bus, signed in and said Hello on: messages
// sent, and read as they come.
class session {
 public:
  session(const session&) = delete;
  session& operator=(const session&) = delete;
  session(session&& other) noexcept : fd_(std::exchange(other.fd_, -1)), serial_(other.serial_) {}
  ~session() {
    if (fd_ >= 0)
      ::close(fd_);
  }

  static std::optional<session> open() {
    const char* address = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    if (address == nullptr)
      return std::nullopt;
    const std::string_view said(address);
    const std::string_view first = said.substr(0, said.find(';'));
    sockaddr_un where{};
    where.sun_family = AF_UNIX;
    socklen_t length = 0;
    const auto value_of = [&](std::string_view key) -> std::optional<std::string_view> {
      const auto at = first.find(key);
      if (at == std::string_view::npos)
        return std::nullopt;
      const std::string_view rest = first.substr(at + key.size());
      return rest.substr(0, rest.find(','));
    };
    if (const auto path = value_of("path=")) {
      if (path->size() >= sizeof(where.sun_path))
        return std::nullopt;
      std::ranges::copy(*path, where.sun_path);
      length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path->size() + 1);
    } else if (const auto name = value_of("abstract=")) {
      if (name->size() + 1 >= sizeof(where.sun_path))
        return std::nullopt;
      where.sun_path[0] = '\0';
      std::ranges::copy(*name, where.sun_path + 1);
      length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + name->size() + 1);
    } else {
      return std::nullopt;
    }
    session made(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (made.fd_ < 0)
      return std::nullopt;
    // The one cast a C API asks for: connect() takes every kind of address
    // as a sockaddr.
    if (::connect(made.fd_, reinterpret_cast<const sockaddr*>(&where), length) != 0)
      return std::nullopt;
    std::string uid;
    for (const char digit : std::to_string(::getuid()))
      uid += std::format("{:02x}", static_cast<unsigned>(digit));
    if (!made.send(std::string_view("\0", 1)) || !made.send("AUTH EXTERNAL " + uid + "\r\n"))
      return std::nullopt;
    std::string line;
    char c = 0;
    while (::recv(made.fd_, &c, 1, 0) == 1) {
      line.push_back(c);
      if (line.ends_with("\r\n"))
        break;
    }
    if (!line.starts_with("OK") || !made.send("BEGIN\r\n"))
      return std::nullopt;
    const std::uint32_t hello = made.next_serial();
    if (!made.send(message(kind::call, 0, hello,
                           {.path = "/org/freedesktop/DBus", .interface = "org.freedesktop.DBus", .member = "Hello",
                            .destination = "org.freedesktop.DBus"},
                           {})))
      return std::nullopt;
    return made;
  }

  [[nodiscard]] std::uint32_t next_serial() noexcept { return serial_++; }

  [[nodiscard]] bool send(std::string_view bytes) {
    while (!bytes.empty()) {
      const auto sent = ::send(fd_, bytes.data(), bytes.size(), MSG_NOSIGNAL);
      if (sent <= 0)
        return false;
      bytes.remove_prefix(static_cast<std::size_t>(sent));
    }
    return true;
  }

  // The next message, waited for a second at most: none where nothing came
  // in that time -- `closed` set where the bus has gone.
  [[nodiscard]] std::optional<incoming> read(bool& closed) {
    pollfd waiting{.fd = fd_, .events = POLLIN, .revents = 0};
    const int ready = ::poll(&waiting, 1, 1000);
    if (ready == 0)
      return std::nullopt;
    if (ready < 0 || (waiting.revents & (POLLERR | POLLHUP)) != 0) {
      closed = true;
      return std::nullopt;
    }
    std::string head(16, '\0');
    if (!this->read_exactly(head)) {
      closed = true;
      return std::nullopt;
    }
    reader fixed(head);
    const auto endian = fixed.byte();
    const auto type = fixed.byte();
    (void)fixed.byte();  // flags
    (void)fixed.byte();  // version
    const auto body_length = fixed.u32();
    const auto serial = fixed.u32();
    const auto fields_length = fixed.u32();
    if (endian != 'l' || !type || !body_length || !serial || !fields_length || *fields_length > (1u << 26) ||
        *body_length > (1u << 27)) {
      closed = true;  // big-endian peers and nonsense alike: nothing this can read further
      return std::nullopt;
    }
    const std::size_t header_end = (16 + static_cast<std::size_t>(*fields_length) + 7) / 8 * 8;
    std::string rest(header_end - 16 + *body_length, '\0');
    if (!this->read_exactly(rest)) {
      closed = true;
      return std::nullopt;
    }
    const std::string whole = head + rest;
    incoming got;
    got.type = static_cast<kind>(*type);
    got.serial = *serial;
    reader in(whole);
    in.seek(16);
    const std::size_t fields_end = 16 + *fields_length;
    while (in.at() < fields_end) {
      if (!in.align(8))
        break;
      const auto code = in.byte();
      const auto value_type = in.signature();
      if (!code || !value_type)
        break;
      const auto text = [&](std::string& into) {
        if (auto value = in.string())
          into = std::move(*value);
      };
      switch (*code) {
        case 1: text(got.path); break;
        case 2: text(got.interface); break;
        case 3: text(got.member); break;
        case 4: text(got.error_name); break;
        case 5: got.reply_serial = in.u32(); break;
        case 7: text(got.sender); break;
        case 8:
          if (auto value = in.signature())
            got.signature = std::move(*value);
          break;
        default: {
          std::string_view skipped = *value_type;
          if (!in.skip(skipped))
            in.seek(fields_end);
        }
      }
    }
    got.body = whole.substr(header_end);
    return got;
  }

 private:
  explicit session(int fd) : fd_(fd) {}
  [[nodiscard]] bool read_exactly(std::string& into) {
    std::size_t done = 0;
    while (done < into.size()) {
      const auto got = ::recv(fd_, into.data() + done, into.size() - done, 0);
      if (got <= 0)
        return false;
      done += static_cast<std::size_t>(got);
    }
    return true;
  }

  int fd_ = -1;
  std::uint32_t serial_ = 1;
};
#endif

// What UnifiedPush's distributor calls on a connector, by its member's name:
// read once, here, into a type.
namespace connector_call {
struct new_endpoint {};
struct message {};
struct unregistered {};
struct other {};
}  // namespace connector_call
using connector_call_t =
    splice::variant<connector_call::new_endpoint, connector_call::message, connector_call::unregistered, connector_call::other>;
inline connector_call_t connector_call_of(std::string_view member) {
  static const std::array<std::pair<std::string_view, connector_call_t>, 3> kNames{
      {{"NewEndpoint", connector_call_t{connector_call::new_endpoint{}}},
       {"Message", connector_call_t{connector_call::message{}}},
       {"Unregistered", connector_call_t{connector_call::unregistered{}}}}};
  const auto found = std::ranges::find(kNames, member, &std::pair<std::string_view, connector_call_t>::first);
  return found == kNames.end() ? connector_call_t{connector_call::other{}} : found->second;
}

}  // namespace mux::dbus::detail

export namespace mux::dbus {

// The name mux owns on the session bus, and is activated by.
inline constexpr std::string_view kAppId = "io.github.j4niwzis.mux";

// What UnifiedPush tells the program -- read off the bus by
// run_unified_push(), handed to its sink, drained by the program.
namespace push {
struct endpoint {  // where the push server takes this device's pushes
  std::string url;
};
struct message {};       // a push came: what it says is the server's to say, on the next sync
struct unregistered {};  // the distributor dropped the registration
struct registered {      // the distributor took it
  std::string distributor;
};
struct refused {  // the distributor would not
  std::string reason;
};
struct no_distributor {};  // none on the bus, running or to be started
struct no_bus {};          // no session bus at all
}  // namespace push
using push_event = splice::variant<push::endpoint, push::message, push::unregistered, push::registered, push::refused,
                                   push::no_distributor, push::no_bus>;

// A connection token for UnifiedPush: a UUIDv4 (RFC 9562), as its
// specification suggests -- not to be guessed.
inline std::string new_push_token() {
  std::random_device entropy;
  std::array<std::uint8_t, 16> bytes{};
  for (auto& one : bytes)
    one = static_cast<std::uint8_t>(entropy() & 0xFF);
  bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x40);
  bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80);
  std::string out;
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10)
      out.push_back('-');
    out += std::format("{:02x}", bytes[i]);
  }
  return out;
}

// UnifiedPush's D-Bus connector (its specification, DBUS 0.3.0), until
// `stop`: `service` owned on the session bus, org.unifiedpush.Connector2
// served at /org/unifiedpush/Connector, a distributor chosen -- the one
// UNIFIEDPUSH_DISTRIBUTOR names, else the first there is, running or to be
// started -- and registered with by `token`. What comes is handed to `sink`
// as a push_event, on this thread: the sink is the program's, and puts it
// where the program reads it. Run on a thread of its own.
//
// `forget`, set where it is stopped: the registration dropped with the
// distributor as it stops -- the user turned it off -- rather than kept for
// the next run, as where the program only closes.
template <class Sink>
void run_unified_push(std::stop_token stop, std::string service, std::string token, std::string description,
                      std::shared_ptr<const std::atomic<bool>> forget, Sink sink) {
#if __has_include(<sys/un.h>)
  using namespace detail;
  bool finishing = false;  // the last call, made though stopped
  auto opened = session::open();
  if (!opened) {
    sink(push_event{push::no_bus{}});
    return;
  }
  session& bus = *opened;
  bool closed = false;
  // A call made to the connector, answered: what it says handed on.
  const auto serve = [&](const incoming& call) {
    if (call.type != kind::call)
      return;
    const bool ours = call.path == "/org/unifiedpush/Connector" && call.interface == "org.unifiedpush.Connector2";
    if (!ours) {
      writer none;
      (void)bus.send(message(kind::error, 1, bus.next_serial(),
                             {.destination = call.sender, .error_name = "org.freedesktop.DBus.Error.UnknownMethod",
                              .reply_serial = call.serial},
                             none.bytes()));
      return;
    }
    std::map<std::string, dict_value> said;
    if (call.signature == "a{sv}") {
      reader in(call.body);
      said = read_dict(in).value_or(std::map<std::string, dict_value>{});
    }
    // Another registration's -- an old token: answered, not acted on.
    const bool mine = text_in(said, "token") == token;
    std::vector<std::pair<std::string, std::string>> answer;
    splice::visit(splice::overloaded{[&](connector_call::new_endpoint) {
                                       if (const auto url = text_in(said, "endpoint"); mine && url)
                                         sink(push_event{push::endpoint{*url}});
                                     },
                                     [&](connector_call::message) {
                                       if (mine)
                                         sink(push_event{push::message{}});
                                       if (const auto id = text_in(said, "id"))
                                         answer.emplace_back("id", *id);
                                     },
                                     [&](connector_call::unregistered) {
                                       if (mine)
                                         sink(push_event{push::unregistered{}});
                                     },
                                     [&](connector_call::other) {}},
                  connector_call_of(call.member));
    writer body;
    write_dict(body, answer);
    (void)bus.send(message(kind::method_return, 1, bus.next_serial(),
                           {.destination = call.sender, .signature = "a{sv}", .reply_serial = call.serial}, body.bytes()));
  };
  // A call made, and its answer waited for -- what is called of this
  // meanwhile served.
  const auto ask = [&](std::string_view destination, std::string_view path, std::string_view interface,
                       std::string_view member, std::string_view signature, const std::string& body) -> std::optional<incoming> {
    const std::uint32_t serial = bus.next_serial();
    if (!bus.send(message(kind::call, 0, serial,
                          {.path = path, .interface = interface, .member = member, .destination = destination,
                           .signature = signature},
                          body)))
      return std::nullopt;
    for (int waited = 0; (finishing || !stop.stop_requested()) && !closed && waited < 30;) {
      auto got = bus.read(closed);
      if (!got) {
        ++waited;
        continue;
      }
      if ((got->type == kind::method_return || got->type == kind::error) && got->reply_serial == serial)
        return got;
      serve(*got);
    }
    return std::nullopt;
  };
  const auto names_of = [&](std::string_view member) {
    std::vector<std::string> out;
    if (auto got = ask("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", member, "", {});
        got && got->type == kind::method_return) {
      reader in(got->body);
      if (const auto length = in.u32()) {
        const std::size_t end = in.at() + *length;
        while (in.at() < end)
          if (auto name = in.string())
            out.push_back(std::move(*name));
          else
            break;
      }
    }
    return out;
  };

  // The name, owned: what the distributor calls, and what D-Bus starts mux
  // by where it is not running.
  {
    writer body;
    body.string(service);
    body.u32(4);  // DBUS_NAME_FLAG_DO_NOT_QUEUE
    (void)ask("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName", "su", body.bytes());
  }
  // The distributor: the one asked for, else the first there is.
  constexpr std::string_view kPrefix = "org.unifiedpush.Distributor.";
  std::vector<std::string> distributors = names_of("ListNames");
  distributors.append_range(names_of("ListActivatableNames"));
  std::erase_if(distributors, [&](const std::string& name) { return !name.starts_with(kPrefix); });
  std::ranges::sort(distributors);
  distributors.erase(std::ranges::unique(distributors).begin(), distributors.end());
  std::string chosen;
  if (const char* asked = std::getenv("UNIFIEDPUSH_DISTRIBUTOR"); asked && std::ranges::contains(distributors, std::string(asked)))
    chosen = asked;
  else if (!distributors.empty())
    chosen = distributors.front();
  if (chosen.empty()) {
    sink(push_event{push::no_distributor{}});
    return;
  }
  {
    writer body;
    write_dict(body, {{"service", service}, {"token", token}, {"description", description}});
    const auto got = ask(chosen, "/org/unifiedpush/Distributor", "org.unifiedpush.Distributor2", "Register", "a{sv}", body.bytes());
    if (!got || got->type != kind::method_return) {
      sink(push_event{push::refused{got ? got->error_name : std::string("no answer")}});
      return;
    }
    reader in(got->body);
    const auto said = read_dict(in).value_or(std::map<std::string, dict_value>{});
    if (text_in(said, "success") != "REGISTRATION_SUCCEEDED") {
      sink(push_event{push::refused{text_in(said, "reason").value_or("REGISTRATION_FAILED")}});
      return;
    }
    sink(push_event{push::registered{chosen}});
  }
  // And from here on, what the distributor says.
  while (!stop.stop_requested() && !closed)
    if (auto got = bus.read(closed))
      serve(*got);
  if (!closed && forget && forget->load()) {
    finishing = true;
    writer body;
    write_dict(body, {{"token", token}});
    (void)ask(chosen, "/org/unifiedpush/Distributor", "org.unifiedpush.Distributor2", "Unregister", "a{sv}", body.bytes());
  }
#else
  (void)stop;
  (void)service;
  (void)token;
  (void)description;
  (void)forget;
  sink(push_event{push::no_bus{}});
#endif
}

}  // namespace mux::dbus
