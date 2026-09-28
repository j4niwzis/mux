// SPDX-License-Identifier: AGPL-3.0-only
// mux-cli: mux without its window -- one account on the terminal, for
// trying the network half before the UI is there.
//
//   mux-cli <user@domain> [host [port]]          an XMPP account
//   mux-cli <@user:server> [homeserver URL]      a Matrix account
//
// The password is read from MUX_PASSWORD. Every change the account makes is
// printed as it comes. A line typed as
//   <address or room id> <the message>
// is sent there; an empty line, or the end of input, stops the account.
import std;
import mux.core;
import mux.net;
import mux.xmpp;
import mux.matrix;

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
        } else if constexpr (std::same_as<type, change::account_removed>) {
          return std::format("{} removed", one.account.address);
        } else if constexpr (std::same_as<type, change::conversation_updated>) {
          return std::format("{} {} ({}{}{})", one.kind == conversation_kind::direct ? "contact" : "room", one.id.id,
                             one.name, one.encrypted ? ", encrypted" : "",
                             one.unread ? ", " + std::to_string(one.unread) + " unread" : std::string());
        } else if constexpr (std::same_as<type, change::conversation_removed>) {
          return std::format("left {}", one.id.id);
        } else if constexpr (std::same_as<type, change::presence_changed>) {
          static constexpr std::array names{"offline", "online", "away", "away for long", "busy", "chatty"};
          return std::format("{} is {}{}", one.contact, names[static_cast<int>(one.now.state)],
                             one.now.status ? " (" + *one.now.status + ")" : std::string());
        } else if constexpr (std::same_as<type, change::message_added>) {
          return std::format("{} {} {}: {}", one.message.in.id, one.message.outgoing ? "<-" : "->",
                             one.message.sender, one.message.body.plain);
        } else if constexpr (std::same_as<type, change::message_edited>) {
          return std::format("{} edited {}: {}", one.in.id, one.id, one.now.plain);
        } else if constexpr (std::same_as<type, change::message_redacted>) {
          return std::format("{} removed {}", one.in.id, one.id);
        } else if constexpr (std::same_as<type, change::message_acknowledged>) {
          return std::format("{} is {}", one.local_id, one.id);
        } else if constexpr (std::same_as<type, change::delivery_changed>) {
          static constexpr std::array names{"sending", "sent", "delivered", "read", "failed"};
          return std::format("{} {}", one.id, names[static_cast<int>(one.now)]);
        } else if constexpr (std::same_as<type, change::reaction_changed>) {
          return std::format("{} {} {} on {}", one.who, one.added ? "reacted" : "took back", one.key, one.id);
        } else if constexpr (std::same_as<type, change::typing_changed>) {
          return one.who.empty() ? std::string() : std::format("{} typing in {}", one.who.size(), one.in.id);
        } else {
          return std::string();
        }
      },
      what);
}

// What is typed, read by a fiber of its own and handed to the account.
template <class Account>
void keyboard(mux::net::loop& loop, Account& account) {
  loop.spawn([&loop, &account] {
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
        const auto space = line.find(' ');
        if (space == std::string::npos) {
          std::println(std::cerr, "to send: <address or room id> <the message>");
          continue;
        }
        account.send(line.substr(0, space), line.substr(space + 1));
      }
    }
    account.stop();
  });
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 4) {
    std::println(std::cerr, "usage: {} <user@domain> [host [port]] | <@user:server> [homeserver URL]", argv[0]);
    std::println(std::cerr, "the password is read from MUX_PASSWORD");
    return 2;
  }
  const char* password = std::getenv("MUX_PASSWORD");
  if (!password) {
    std::println(std::cerr, "MUX_PASSWORD is not set");
    return 2;
  }
  const std::string address = argv[1];

  mux::net::loop loop;
  auto tls = mux::net::client_tls();
  mux::model model;
  auto sink = [&model](mux::change_t one) {
    if (const std::string said = describe(one); !said.empty())
      std::println("{}", said);
    model.apply(one);
  };

  try {
    if (address.starts_with('@')) {
      mux::matrix::settings how{.user_id = address, .password = password};
      if (argc >= 3)
        how.homeserver = argv[2];
      mux::matrix::account account(loop, tls, std::move(how), sink);
      account.start();
      keyboard(loop, account);
      loop.run();
    } else {
      mux::xmpp::settings how{.address = address, .password = password};
      if (argc >= 3)
        how.host = argv[2];
      if (argc == 4)
        how.port = static_cast<std::uint16_t>(std::stoi(argv[3]));
      mux::xmpp::account account(loop, tls, std::move(how), sink);
      account.start();
      keyboard(loop, account);
      loop.run();
    }
  } catch (const std::exception& failed) {
    std::println(std::cerr, "stopped: {}", failed.what());
    return 1;
  }
  return 0;
}
