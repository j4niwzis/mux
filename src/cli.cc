// SPDX-License-Identifier: AGPL-3.0-only
// mux-cli: mux without its window -- an XMPP account on the terminal, for
// trying the network half before the UI is there.
//
//   mux-cli <user@domain> [host [port]]      the password is read from MUX_PASSWORD
//
// Every change the account makes is printed as it comes. A line typed as
//   someone@example.com: the message
// is sent to that address; an empty line or end of input closes the stream.
import std;
import mux.core;
import mux.net;
import mux.xmpp;

namespace {

std::string describe(const mux::change_t& what) {
  return std::visit(
      [](const auto& one) -> std::string {
        using type = std::remove_cvref_t<decltype(one)>;
        using namespace mux;
        if constexpr (std::same_as<type, change::connection_changed>) {
          static constexpr std::array names{"offline", "connecting", "online", "failed"};
          return std::format("{} is {}{}", one.account.address, names[static_cast<int>(one.state)],
                             one.error ? ": " + *one.error : std::string());
        } else if constexpr (std::same_as<type, change::conversation_updated>) {
          return std::format("contact {} ({})", one.id.id, one.name);
        } else if constexpr (std::same_as<type, change::presence_changed>) {
          static constexpr std::array names{"offline", "online", "away", "away for long", "busy", "chatty"};
          return std::format("{} is {}{}", one.contact, names[static_cast<int>(one.now.state)],
                             one.now.status ? " (" + *one.now.status + ")" : std::string());
        } else if constexpr (std::same_as<type, change::message_added>) {
          return std::format("{} {} {}: {}", one.message.in.id, one.message.outgoing ? "<-" : "->",
                             one.message.sender, one.message.body.plain);
        } else if constexpr (std::same_as<type, change::delivery_changed>) {
          static constexpr std::array names{"sending", "sent", "delivered", "read", "failed"};
          return std::format("{} {}", one.id, names[static_cast<int>(one.now)]);
        } else {
          return "a change";
        }
      },
      what);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 4) {
    std::println(stderr, "usage: {} <user@domain> [host [port]]; the password in MUX_PASSWORD", argv[0]);
    return 2;
  }
  const char* password = std::getenv("MUX_PASSWORD");
  if (!password) {
    std::println(stderr, "MUX_PASSWORD is not set");
    return 2;
  }
  mux::xmpp::settings how{.address = argv[1], .password = password};
  if (argc >= 3)
    how.host = argv[2];
  if (argc == 4)
    how.port = static_cast<std::uint16_t>(std::stoi(argv[3]));

  mux::net::loop loop;
  auto tls = mux::net::client_tls();
  mux::model model;
  auto sink = [&model](mux::change_t one) {
    std::println("{}", describe(one));
    model.apply(one);
  };
  mux::xmpp::account account(loop, tls, std::move(how), sink);
  account.start();

  // What is typed, read by a fiber of its own.
  loop.spawn([&] {
    mux::net::descriptor input(loop, 0);
    std::string pending;
    std::array<char, 1024> buffer{};
    for (;;) {
      const std::size_t n = input.read_some(buffer);
      if (n == 0)
        break;
      pending.append(buffer.data(), n);
      for (std::size_t end; (end = pending.find('\n')) != std::string::npos;) {
        const std::string line = pending.substr(0, end);
        pending.erase(0, end + 1);
        if (line.empty()) {
          account.stop();
          return;
        }
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
          std::println(stderr, "to send: someone@example.com: the message");
          continue;
        }
        std::string text = line.substr(colon + 1);
        if (text.starts_with(' '))
          text.erase(0, 1);
        account.send(line.substr(0, colon), std::move(text));
      }
    }
    account.stop();
  });

  try {
    loop.run();
  } catch (const std::exception& failed) {
    std::println(stderr, "stopped: {}", failed.what());
    return 1;
  }
  return 0;
}
