// SPDX-License-Identifier: AGPL-3.0-only
// mux.dbus -- The desktop's notifications, as org.freedesktop.Notifications
// takes them: a message to the session bus, written here -- the bus's
// socket, its EXTERNAL sign-in, and the one method call marshalled by hand,
// as the D-Bus specification lays it out -- rather than a library for it.
module;
#include <stddef.h>
#if __has_include(<sys/un.h>)
#include <sys/socket.h>
#include <sys/time.h>  // timeval, for the reply wait
#include <sys/un.h>
#include <unistd.h>
#endif
export module mux.dbus;

import std;

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
