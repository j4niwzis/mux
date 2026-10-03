// SPDX-License-Identifier: AGPL-3.0-only
// mux.platform.push -- Pushes from a push service, where the platform has
// one: UnifiedPush over D-Bus on a freedesktop system (push_backend*.cc).
export module mux.platform.push;

export import mux.platform.push.types;
import mux.platform.push.backend;
import std;

export namespace mux::platform::push {

// A connection token for UnifiedPush: a UUIDv4 (RFC 9562), as its
// specification suggests -- not to be guessed.
inline std::string new_token() {
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

// UnifiedPush's connector, as the platform has it (push_backend*.cc), until
// `stop`: `service` owned on the session bus, org.unifiedpush.Connector2
// served at /org/unifiedpush/Connector, a distributor chosen -- the one
// UNIFIEDPUSH_DISTRIBUTOR names, else the first there is, running or to be
// started -- and registered with by `token`. What comes is handed to `sink`
// as a push::event, on this thread: the sink is the program's, and puts it
// where the program reads it. Run on a thread of its own.
//
// `forget`, set where it is stopped: the registration dropped with the
// distributor as it stops -- the user turned it off -- rather than kept for
// the next run, as where the program only closes.
template <class Sink>
void run(std::stop_token stop, std::string service, std::string token, std::string description,
         std::shared_ptr<const std::atomic<bool>> forget, Sink sink) {
  backend::run(std::move(stop), std::move(service), std::move(token), std::move(description), std::move(forget),
               std::move(sink));
}

}  // namespace mux::platform::push
