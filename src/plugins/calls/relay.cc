// SPDX-License-Identifier: AGPL-3.0-only
// mux.calls.relay -- libnice's TURN_TLS mode is not RFC TURN over TLS on
// all supported versions. Give it a loopback TCP tunnel whose upstream is
// genuine TLS, verified through the same trust rules as the Matrix client.
module;
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <openssl/ssl.h>
export module mux.calls.relay;

import std;
import mux.net;

export namespace mux::calls {
class turn_tls_tunnel {
 public:
  turn_tls_tunnel(std::string host, std::uint16_t port)
      : host_(std::move(host)), port_(port), tls_(net::client_tls()),
        listener_(io_, {boost::asio::ip::address_v4::loopback(), 0}) {
    local_port_ = listener_.local_endpoint().port();
    boost::asio::co_spawn(io_, accept(), boost::asio::detached);
    worker_ = std::jthread([this] { io_.run(); });
  }
  turn_tls_tunnel(const turn_tls_tunnel&) = delete;
  turn_tls_tunnel& operator=(const turn_tls_tunnel&) = delete;
  ~turn_tls_tunnel() {
    io_.stop();
    worker_ = std::jthread();
  }
  [[nodiscard]] std::uint16_t port() const { return local_port_; }

 private:
  using tcp = boost::asio::ip::tcp;
  struct connection {
    tcp::socket local;
    boost::asio::ssl::stream<tcp::socket> remote;
    tcp::resolver resolver;
    boost::asio::steady_timer deadline;
    connection(tcp::socket socket, boost::asio::ssl::context& tls)
        : local(std::move(socket)), remote(local.get_executor(), tls),
          resolver(local.get_executor()), deadline(local.get_executor()) {}
    void close() {
      boost::system::error_code ignored;
      resolver.cancel();
      deadline.cancel();
      local.close(ignored);
      remote.lowest_layer().close(ignored);
    }
  };
  static boost::asio::awaitable<void> upload(std::shared_ptr<connection> peer) {
    try {
      std::array<unsigned char, 16 * 1024> bytes{};
      for (;;) {
        const auto size = co_await peer->local.async_read_some(boost::asio::buffer(bytes), boost::asio::use_awaitable);
        co_await boost::asio::async_write(peer->remote, boost::asio::buffer(bytes.data(), size), boost::asio::use_awaitable);
      }
    } catch (const std::exception&) { peer->close(); }
  }
  static boost::asio::awaitable<void> download(std::shared_ptr<connection> peer) {
    try {
      std::array<unsigned char, 16 * 1024> bytes{};
      for (;;) {
        const auto size = co_await peer->remote.async_read_some(boost::asio::buffer(bytes), boost::asio::use_awaitable);
        co_await boost::asio::async_write(peer->local, boost::asio::buffer(bytes.data(), size), boost::asio::use_awaitable);
      }
    } catch (const std::exception&) { peer->close(); }
  }
  boost::asio::awaitable<void> connect(std::shared_ptr<connection> peer) {
    try {
      peer->deadline.expires_after(std::chrono::seconds(15));
      peer->deadline.async_wait([peer](boost::system::error_code error) { if (!error) peer->close(); });
      const auto addresses = co_await peer->resolver.async_resolve(host_, std::to_string(port_), boost::asio::use_awaitable);
      co_await boost::asio::async_connect(peer->remote.next_layer(), addresses, boost::asio::use_awaitable);
      if (!SSL_set_tlsext_host_name(peer->remote.native_handle(), host_.c_str()))
        throw std::runtime_error("Could not set TURN server TLS name");
      peer->remote.set_verify_mode(boost::asio::ssl::verify_peer);
      peer->remote.set_verify_callback(net::peer_verification{host_});
      co_await peer->remote.async_handshake(boost::asio::ssl::stream_base::client, boost::asio::use_awaitable);
      peer->deadline.cancel();
      boost::asio::co_spawn(io_, upload(peer), boost::asio::detached);
      co_await download(peer);
    } catch (const std::exception&) { peer->close(); }
  }
  boost::asio::awaitable<void> accept() {
    try {
      for (;;) {
        auto socket = co_await listener_.async_accept(boost::asio::use_awaitable);
        auto peer = std::make_shared<connection>(std::move(socket), tls_.context());
        boost::asio::co_spawn(io_, connect(std::move(peer)), boost::asio::detached);
      }
    } catch (const std::exception&) { /* The listener is stopped with its call. */ }
  }
  std::string host_;
  std::uint16_t port_;
  boost::asio::io_context io_;
  net::tls tls_;
  tcp::acceptor listener_;
  std::uint16_t local_port_ = 0;
  std::jthread worker_;
};
} // namespace mux::calls
