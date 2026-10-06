// SPDX-License-Identifier: AGPL-3.0-only
// UnifiedPush's D-Bus connector (its specification, DBUS 0.3.0): a
// connection kept to the session bus, a name owned on it, the distributor's
// calls read and answered.
export module mux.platform.freedesktop.push_backend;

import std;
import splice;
import mux.platform.push.types;
import mux.platform.freedesktop.bus;

namespace mux::platform::freedesktop::unified_push {

// What UnifiedPush's distributor calls on a connector, by its member's name:
// read once, here, into a type.
namespace connector_call {
struct new_endpoint {};
struct message {};
struct unregistered {};
struct other {};
}  // namespace connector_call
using connector_call_t =
    spl::variant<connector_call::new_endpoint, connector_call::message, connector_call::unregistered, connector_call::other>;
inline connector_call_t connector_call_of(std::string_view member) {
  static const std::array<std::pair<std::string_view, connector_call_t>, 3> kNames{
      {{"NewEndpoint", connector_call_t{connector_call::new_endpoint{}}},
       {"Message", connector_call_t{connector_call::message{}}},
       {"Unregistered", connector_call_t{connector_call::unregistered{}}}}};
  const auto found = std::ranges::find(kNames, member, &std::pair<std::string_view, connector_call_t>::first);
  return found == kNames.end() ? connector_call_t{connector_call::other{}} : found->second;
}

}  // namespace mux::platform::freedesktop::unified_push

export namespace mux::platform::push::backend {

template <class Sink>
void run(std::stop_token stop, std::string service, std::string token, std::string description,
         std::shared_ptr<const std::atomic<bool>> forget, Sink sink) {
  using namespace freedesktop::bus;
  using namespace freedesktop::unified_push;
  bool finishing = false;  // the last call, made though stopped
  auto opened = session::open();
  if (!opened) {
    sink(event{push::no_bus{}});
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
      (void)bus.send(freedesktop::bus::message(kind::error, 1, bus.next_serial(),
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
    spl::visit(spl::overloaded{[&](connector_call::new_endpoint) {
                                       if (const auto url = text_in(said, "endpoint"); mine && url)
                                         sink(event{push::endpoint{*url}});
                                     },
                                     [&](connector_call::message) {
                                       if (mine)
                                         sink(event{push::message{}});
                                       if (const auto id = text_in(said, "id"))
                                         answer.emplace_back("id", *id);
                                     },
                                     [&](connector_call::unregistered) {
                                       if (mine)
                                         sink(event{push::unregistered{}});
                                     },
                                     [&](connector_call::other) {}},
                  connector_call_of(call.member));
    writer body;
    write_dict(body, answer);
    (void)bus.send(freedesktop::bus::message(kind::method_return, 1, bus.next_serial(),
                           {.destination = call.sender, .signature = "a{sv}", .reply_serial = call.serial}, body.bytes()));
  };
  // A call made, and its answer waited for -- what is called of this
  // meanwhile served.
  const auto ask = [&](std::string_view destination, std::string_view path, std::string_view interface,
                       std::string_view member, std::string_view signature, const std::string& body) -> std::optional<incoming> {
    const std::uint32_t serial = bus.next_serial();
    if (!bus.send(freedesktop::bus::message(kind::call, 0, serial,
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
    sink(event{push::no_distributor{}});
    return;
  }
  {
    writer body;
    write_dict(body, {{"service", service}, {"token", token}, {"description", description}});
    const auto got = ask(chosen, "/org/unifiedpush/Distributor", "org.unifiedpush.Distributor2", "Register", "a{sv}", body.bytes());
    if (!got || got->type != kind::method_return) {
      sink(event{push::refused{got ? got->error_name : std::string("no answer")}});
      return;
    }
    reader in(got->body);
    const auto said = read_dict(in).value_or(std::map<std::string, dict_value>{});
    if (text_in(said, "success") != "REGISTRATION_SUCCEEDED") {
      sink(event{push::refused{text_in(said, "reason").value_or("REGISTRATION_FAILED")}});
      return;
    }
    sink(event{push::registered{chosen}});
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
}

}  // namespace mux::platform::push::backend
