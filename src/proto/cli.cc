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
import splice;
import mux.core;
import mux.net;
import mux.proto.xmpp.client;
import mux.proto.matrix.client;
import mux.config;
import mux.proto.clients;
import mux.vault;

namespace {

// Each change in a line, by its own overload.
std::string name_of(const mux::connection_t& state) {
  return splice::visit(splice::overloaded{
                        [](const mux::connection::offline&) { return std::string("offline"); },
                        [](const mux::connection::connecting& now) {
                          return "connecting" + (now.reason ? ": " + *now.reason : std::string());
                        },
                        [](const mux::connection::online&) { return std::string("online"); },
                        [](const mux::connection::failed& now) { return "failed: " + now.error; },
                    },
                    state);
}
std::string name_of(const mux::availability_t& state) {
  return splice::visit(splice::overloaded{
                        [](const mux::availability::offline&) { return "offline"; },
                        [](const mux::availability::online&) { return "online"; },
                        [](const mux::availability::away&) { return "away"; },
                        [](const mux::availability::extended_away&) { return "away for long"; },
                        [](const mux::availability::do_not_disturb&) { return "busy"; },
                        [](const mux::availability::chat&) { return "chatty"; },
                    },
                    state);
}
std::string name_of(const mux::delivery_t& state) {
  return splice::visit(splice::overloaded{
                        [](const mux::delivery::sending&) { return "sending"; },
                        [](const mux::delivery::sent&) { return "sent"; },
                        [](const mux::delivery::delivered&) { return "delivered"; },
                        [](const mux::delivery::read&) { return "read"; },
                        [](const mux::delivery::failed&) { return "failed"; },
                    },
                    state);
}
std::string name_of(const mux::conversation_kind_t& kind) {
  return splice::visit(splice::overloaded{[](const mux::conversation_kind::direct&) { return "contact"; },
                                    [](const mux::conversation_kind::group&) { return "room"; }},
                    kind);
}

namespace said {
using namespace mux;
std::string of(const change::connection_changed& one) {
  return std::format("{} is {}", one.account.address, name_of(one.state));
}
std::string of(const change::account_removed& one) { return std::format("{} removed", one.account.address); }
std::string of(const change::conversation_updated& one) {
  return std::format("{} {} ({}{}{})", name_of(one.kind), one.id.id, one.name, one.encrypted ? ", encrypted" : "",
                     one.unread ? ", " + std::to_string(one.unread) + " unread" : std::string());
}
std::string of(const change::conversation_removed& one) { return std::format("left {}", one.id.id); }
std::string of(const change::presence_changed& one) {
  return std::format("{} is {}{}", one.contact, name_of(one.now.state),
                     one.now.status ? " (" + *one.now.status + ")" : std::string());
}
std::string of(const change::message_added& one) {
  return std::format("{} {} {}: {}", one.message.in.id, one.message.outgoing ? "<-" : "->", one.message.sender,
                     one.message.body.plain);
}
std::string of(const change::message_edited& one) { return std::format("{} edited {}: {}", one.in.id, one.id, one.now.plain); }
std::string of(const change::message_redacted& one) { return std::format("{} removed {}", one.in.id, one.id); }
std::string of(const change::message_acknowledged& one) { return std::format("{} is {}", one.local_id, one.id); }
std::string of(const change::delivery_changed& one) { return std::format("{} {}", one.id, name_of(one.now)); }
std::string of(const change::message_discarded& one) { return std::format("{} let go {}", one.in.id, one.id); }
std::string of(const change::reaction_changed& one) {
  return std::format("{} {} {} on {}", one.who, one.added ? "reacted" : "took back", one.key, one.id);
}
std::string of(const change::typing_changed& one) {
  return one.who.empty() ? std::string() : std::format("{} typing in {}", one.who.size(), one.in.id);
}
std::string of(const change::history_position&) { return std::string(); }
std::string of(const change::media_progress& one) {
  return std::format("{}: {}%", one.source, static_cast<int>(one.done * 100.0f));
}
std::string of(const change::preview_loaded& one) { return std::format("a preview of {}", one.url); }
std::string of(const change::mentioned& one) { return std::format("mentioned in {}", one.in.id); }
std::string of(const change::marks_shown&) { return std::string(); }
std::string of(const change::threads_listed& one) { return std::format("{} threads in {}", one.roots.size(), one.in.id); }
std::string of(const change::refused& one) { return one.what; }
std::string of(const change::devices_listed& one) { return std::format("{} has {} sessions", one.user, one.devices.size()); }
std::string of(const change::trust_changed& one) {
  return one.user + splice::visit(splice::overloaded{[](trust::verified) { return std::string(" is verified"); },
                                                     [](trust::unverified) { return std::string(" is not verified"); },
                                                     [](trust::changed) { return std::string("'s identity changed"); }},
                                  one.now);
}
std::string of(const change::notice& one) { return one.heading + ": " + one.what; }
std::string of(const change::message_encrypted& one) {
  return std::format("{} {} came encrypted{}", one.in.id, one.id, one.verified ? ", from a verified device" : "");
}
std::string of(const change::event_missing& one) { return std::format("{} has no {}", one.in.id, one.id); }
std::string of(const change::profile_found& one) { return std::format("profile of {}", one.user); }
std::string of(const change::people_found& one) {
  return std::format("{} found for {}", one.people.size(), one.query);
}
std::string of(const change::directory_listed& one) {
  return std::format("{} rooms in the directory of {}", one.rooms.size(), one.server.empty() ? "the home server" : one.server);
}
std::string of(const change::reacted_to_mine& one) { return std::format("a reaction to yours in {}", one.in.id); }
std::string of(const change::mark_taken&) { return std::string(); }
std::string of(const change::room_previewed& one) {
  return std::format("{}: {}", one.asked, one.preview.name.empty() ? one.preview.note : one.preview.name);
}
std::string of(const change::room_created& one) { return std::format("{} was made", one.id.id); }
std::string of(const change::window_opened& one) { return std::format("a window of {}'s history opened", one.in.id); }
std::string of(const change::window_extended& one) {
  return std::format("{}'s window paged forward{}", one.in.id, one.future_from ? "" : ", to the newest");
}
std::string of(const change::receipts_changed& one) { return std::format("{}: {} receipts", one.in.id, one.read_by.size()); }
std::string of(const change::avatar_loaded& one) { return std::format("a picture from {}, {} bytes", one.source, one.bytes.size()); }
std::string of(const change::marks_seen&) { return "marks read back as seen"; }
std::string of(const change::protocol_state_changed& one) { return one.account.address + ": its protocol's state told"; }
std::string of(const change::members_changed& one) {
  return std::format("{} has {} member{}", one.in.id, one.members.size(), one.members.size() == 1 ? "" : "s");
}
// A protocol's own change, as it says itself.
template <mux::protocol_change Change>
std::string of(const Change& one) {
  return describe(one);
}
}  // namespace said

std::string describe(const mux::change_t& what) {
  return splice::visit([](const auto& one) { return said::of(one); }, what);
}

// What the account says: told, and kept in the model. A type of its own,
// not a lambda: the accounts are made for it at namespace scope, below.
struct print_change {
  mux::model* model;
  void operator()(mux::change_t one) const {
    if (const std::string said = describe(one); !said.empty())
      std::println("{}", said);
    model->apply(one);
  }
};

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

// Each protocol's account, made at namespace scope: clang 23 crashed on
// format strings first made deep inside one's instantiation (see
// app/network.cc).
template class mux::proto::xmpp::client::account<print_change>;
template class mux::proto::matrix::client::account<print_change>;

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
  const print_change sink{&model};

  try {
    // Matrix keeps its keys in mux's data directory. Locate its vault header
    // before starting an account; this terminal client cannot unlock it yet.
    mux::vault::vault vault;
    vault.place(mux::config::default_path().parent_path() / "vault.json");
    if (vault.locked()) {
      std::println(std::cerr, "Local data is encrypted; this CLI needs vault unlock support.");
      return 1;
    }
    // The account its address names, made by its protocol: what it keeps
    // (config::account_from, by the protocol that owns the address), then
    // its client (make_account, by ADL on what it keeps).
    mux::config::account_t saved = mux::config::account_from(address, password);
    splice::visit(
        [&](auto& kept) {
          if (argc >= 3)
            server_given(kept, argv[2], argc == 4 ? std::optional<std::int64_t>(std::stoi(argv[3])) : std::nullopt);
          auto account = make_account(kept, loop, tls, vault, std::nullopt, sink);
          account->start();
          keyboard(loop, *account);
          loop.run();
        },
        saved.own);
  } catch (const std::exception& failed) {
    std::println(std::cerr, "stopped: {}", failed.what());
    return 1;
  }
  return 0;
}
