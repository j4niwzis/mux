// SPDX-License-Identifier: AGPL-3.0-only
// Matrix's connection contract on skiff-framework's browser transport.
module;
#include <boost/asio/error.hpp>
export module mux.http:connection;
import std;
import mux.net;
import skiff.platform.net;
import :types;

export namespace mux::http {
class connection {
public:
  connection(net::loop &owner, net::tls &, url where, std::optional<net::proxy> via = std::nullopt)
      : owner_(&owner), where_(std::move(where)), via_(std::move(via)) {}
  connection(const connection &) = delete;
  connection &operator=(const connection &) = delete;
  const url &where() const noexcept { return where_; }
  bool busy() const noexcept { return busy_; }
  template <class Progress = no_progress>
  response request(std::string_view method, std::string_view target, std::string_view body = {},
                   std::optional<std::string_view> bearer = std::nullopt,
                   std::chrono::seconds timeout = std::chrono::seconds(60), std::string_view type = {},
                   const Progress *progress = nullptr, std::string_view accept = "application/json",
                   std::span<const std::pair<std::string, std::string>> headers = {}) {
    const auto authorization = bearer.transform([](std::string_view value) { return std::string(value); });
    if (authorization)
      bearer = std::string_view(*authorization);
    namespace platform = skiff::platform::net;
    const turn mine(*this);
    if (via_) throw std::runtime_error("The browser manages its own proxy. Remove the per-account proxy in mux.");
    const auto verb = platform::method_of(method);
    if (!verb) throw std::runtime_error("Unsupported HTTP method");
    platform::request asked{.method = *verb,
      .url = "https://" + (where_.host.contains(':') ? "[" + where_.host + "]" : where_.host) +
             (where_.port == 443 ? "" : ":" + std::to_string(where_.port)) + where_.path + std::string(target),
      .body = std::string(body), .type = std::string(type.empty() ? "application/json" : type),
      .headers = headers | std::ranges::to<std::vector>(),
      .timeout = std::chrono::duration_cast<std::chrono::milliseconds>(timeout)};
    asked.headers.emplace_back("Accept", accept);
    if (bearer) asked.headers.emplace_back("Authorization", "Bearer " + std::string(*bearer));
    transport_.start(1, asked);
    struct release {
      platform::browser &transport;
      ~release() { transport.abort(1); (void)transport.take(1); }
    } held{transport_};
    // Looked at again sooner while an answer is fresh, then less often: a
    // long poll waits up to half a minute, and looked at every 5 ms the
    // page worked all the time, doing nothing (the user's).
    auto wait = std::chrono::milliseconds(5);
    while (true) {
      if (auto got = transport_.take(1)) {
        if (got->cancelled) throw net::failure("Browser request cancelled", asio::error::operation_aborted);
        if (got->status == 0) throw net::failure(got->body, asio::error::connection_refused);
        return {.status = got->status, .body = std::move(got->body),
                .retry_after_ms = got->after ? std::optional<std::int64_t>(got->after->count()) : std::nullopt,
                .location = std::move(got->location)};
      }
      if (progress) {
        const auto bytes = transport_.transferred(1);
        if (!(*progress)(bytes.read, bytes.total)) transport_.abort(1);
      }
      owner_->sleep(wait);
      wait = std::min(wait * 2, std::chrono::milliseconds(100));
    }
  }
  void abort() { transport_.abort(1); }
  void close() { abort(); }
private:
  struct turn {
    connection &of;
    explicit turn(connection &on) : of(on) {
      while (of.busy_) {
        if (!std::ranges::contains(of.waiting_, of.owner_->current())) of.waiting_.push_back(of.owner_->current());
        of.owner_->park();
      }
      of.busy_ = true;
    }
    ~turn() {
      of.busy_ = false;
      if (!of.waiting_.empty()) { const auto next = of.waiting_.front(); of.waiting_.pop_front(); of.owner_->wake(next); }
    }
  };
  net::loop *owner_;
  url where_;
  std::optional<net::proxy> via_;
  skiff::platform::net::browser transport_;
  bool busy_ = false;
  std::deque<net::loop::handle> waiting_;
};
}
