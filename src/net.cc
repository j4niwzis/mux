// SPDX-License-Identifier: AGPL-3.0-only
// mux.net: all of mux's I/O on one io_context, run by fibers.
//
// A fiber is a stackful coroutine (Boost.Context): it waits for an Asio
// operation by parking, and the operation's completion wakes it. Code in a
// fiber reads as blocking code -- which is how tern's session is written --
// and nothing blocks the thread. The loop is tern's scheduler as well:
// current(), park() and wake(), so a tern request made from one fiber parks
// while another reads.
//
// On top: TLS streams as tern's transport (STARTTLS and direct TLS, the
// certificate checked against the name, channel binding for SCRAM-PLUS), and
// the DNS lookups XMPP needs (SRV, over UDP, through tern's own encoder and
// decoder).
module;

#include <unistd.h>

#include <boost/asio.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/context/fiber.hpp>
#include <boost/context/protected_fixedsize_stack.hpp>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

export module mux.net;

import std;
import tern;

export namespace mux::net {

namespace asio = boost::asio;
using error_code = boost::system::error_code;
using tcp = asio::ip::tcp;

// A failure of the network, as the operation that met it says.
struct failure : std::runtime_error {
  error_code code;
  failure(std::string_view what, error_code code)
      : std::runtime_error(std::string(what) + ": " + code.message()), code(code) {}
};

// Fibers on one io_context.
class loop {
 public:
  struct task {
    boost::context::fiber suspended;  // the fiber, while it is not running
    boost::context::fiber back;       // where it returns to, while it runs
    bool queued = false;
    bool finished = false;
    std::exception_ptr failure;
  };
  using handle = task*;

  loop() = default;
  loop(const loop&) = delete;
  loop& operator=(const loop&) = delete;

  asio::io_context& io() noexcept { return io_; }

  // A fiber started: it runs once the loop gets to it. An exception that
  // leaves it comes out of run(): a fiber is expected to deal with what it
  // can, and what it cannot is a bug to see.
  template <class Body>
  void spawn(Body body, std::size_t stack = 256 * 1024) {
    auto made = std::make_unique<task>();
    task* self = made.get();
    self->suspended = boost::context::fiber(
        std::allocator_arg, boost::context::protected_fixedsize_stack(stack),
        [self, body = std::move(body)](boost::context::fiber&& back) mutable {
          self->back = std::move(back);
          try {
            body();
          } catch (...) {
            self->failure = std::current_exception();
          }
          self->finished = true;
          return std::move(self->back);
        });
    tasks_.push_back(std::move(made));
    wake(self);
  }

  // Runs until there is nothing left to do, or stop().
  void run() { io_.run(); }
  void stop() { io_.stop(); }

  // The running fiber; nullptr outside one.
  handle current() const noexcept { return current_; }

  // The running fiber set aside until something wakes it. It may be woken
  // for nothing: whoever parks checks what it waited for, and parks again.
  void park() {
    task* self = current_;
    if (!self)
      throw std::logic_error("mux::net::loop::park outside a fiber");
    self->back = std::move(self->back).resume();
  }

  // A fiber to run again, when the loop gets to it: once, however often it
  // is asked before then.
  void wake(handle one) {
    if (!one || one->queued || one->finished)
      return;
    one->queued = true;
    asio::post(io_, [this, one] {
      one->queued = false;
      resume(one);
    });
  }

  // An Asio operation started by `start`, given the completion handler, and
  // waited for: its error and the values it completes with.
  template <class... Values, class Start>
  std::tuple<error_code, Values...> await(Start start) {
    task* self = current_;
    if (!self)
      throw std::logic_error("mux::net::loop::await outside a fiber");
    std::optional<std::tuple<error_code, Values...>> got;
    start([this, self, &got](error_code error, Values... values) {
      got.emplace(error, std::move(values)...);
      wake(self);
    });
    while (!got)
      park();
    return std::move(*got);
  }

  // The running fiber set aside for a while.
  void sleep(std::chrono::steady_clock::duration how_long) {
    asio::steady_timer timer(io_, how_long);
    (void)await<>([&](auto done) { timer.async_wait(std::move(done)); });
  }

 private:
  void resume(task* one) {
    if (one->finished)
      return;
    task* was = current_;
    current_ = one;
    one->suspended = std::move(one->suspended).resume();
    current_ = was;
    if (one->finished) {
      std::exception_ptr failed = one->failure;
      std::erase_if(tasks_, [one](const std::unique_ptr<task>& kept) { return kept.get() == one; });
      if (failed)
        std::rethrow_exception(failed);
    }
  }

  asio::io_context io_;
  std::vector<std::unique_ptr<task>> tasks_;
  task* current_ = nullptr;
};

// The loop as tern sees a scheduler.
struct scheduler {
  loop* owner = nullptr;
  using handle = loop::handle;
  handle current() const { return owner->current(); }
  void park() { owner->park(); }
  void wake(handle one) { owner->wake(one); }
};

// A client's TLS settings: the system's trusted certificates, TLS 1.2 and up.
inline asio::ssl::context client_tls() {
  asio::ssl::context made(asio::ssl::context::tls_client);
  made.set_default_verify_paths();
  made.set_options(asio::ssl::context::default_workarounds | asio::ssl::context::no_sslv2 |
                   asio::ssl::context::no_sslv3 | asio::ssl::context::no_tlsv1 | asio::ssl::context::no_tlsv1_1);
  made.set_verify_mode(asio::ssl::verify_peer);
  return made;
}

// A TCP connection, TLS once it is started, as tern's transport: input as
// the chunks each read brings -- read only when tern asks whether there is
// more -- and output kept until tern flushes a unit.
class stream {
 public:
  stream(loop& owner, asio::ssl::context& tls, tcp::socket socket)
      : owner_(&owner), stream_(std::move(socket), tls) {}
  stream(const stream&) = delete;
  stream& operator=(const stream&) = delete;

  struct chunks;
  struct iterator {
    using value_type = std::string_view;
    using difference_type = std::ptrdiff_t;
    stream* from = nullptr;
    std::string_view operator*() const { return from->current_; }
    iterator& operator++() {
      from->fetched_ = false;
      return *this;
    }
    void operator++(int) { ++*this; }
    bool operator==(std::default_sentinel_t) const { return from->at_end(); }
  };
  struct chunks {
    stream* from = nullptr;
    iterator begin() const { return {from}; }
    std::default_sentinel_t end() const { return {}; }
  };

  chunks& input() {
    view_.from = this;
    return view_;
  }

  void write(std::string_view bytes) { pending_ += bytes; }

  // What was written sent, and waited for. One write at a time is in
  // flight, whichever fiber asked: a fiber that flushes while another's
  // write is under way leaves its bytes to that one, which sends until
  // nothing is pending. A read may be in flight beside it -- a TLS stream
  // takes one of each at once.
  void flush() {
    if (writing_)
      return;
    writing_ = true;
    while (!pending_.empty() && !failed_) {
      sending_.clear();
      std::swap(sending_, pending_);
      const auto [error, sent] = tls_ ? owner_->await<std::size_t>([&](auto done) {
        asio::async_write(stream_, asio::buffer(sending_), std::move(done));
      })
                                      : owner_->await<std::size_t>([&](auto done) {
                                          asio::async_write(stream_.next_layer(), asio::buffer(sending_),
                                                            std::move(done));
                                        });
      if (error)
        failed_ = error;
    }
    writing_ = false;
  }

  // STARTTLS (RFC 6120, 5.4.3.3), or direct TLS (XEP-0368) before anything
  // is said: the name told to the server (SNI), and its certificate checked
  // against it and the system's trust.
  bool start_tls(std::string_view host) {
    const std::string name(host);
    if (!::SSL_set_tlsext_host_name(stream_.native_handle(), name.c_str()))
      return false;
    stream_.set_verify_mode(asio::ssl::verify_peer);
    stream_.set_verify_callback(asio::ssl::host_name_verification(name));
    const auto [error] = owner_->await<>([&](auto done) {
      stream_.async_handshake(asio::ssl::stream_base::client, std::move(done));
    });
    if (error) {
      failed_ = error;
      return false;
    }
    tls_ = true;
    fetched_ = false;
    ended_ = false;
    current_ = {};
    return true;
  }

  bool secured() const { return tls_; }

  // RFC 9266: tls-exporter on TLS 1.3; tls-server-end-point (RFC 5929) on
  // TLS 1.2, where the exporter is only safe with extended master secret,
  // which cannot be relied on.
  std::optional<tern::channel_binding> channel_binding() {
    if (!tls_)
      return std::nullopt;
    SSL* ssl = stream_.native_handle();
    if (::SSL_version(ssl) >= TLS1_3_VERSION) {
      unsigned char out[32];
      static constexpr char label[] = "EXPORTER-Channel-Binding";
      if (::SSL_export_keying_material(ssl, out, sizeof out, label, sizeof label - 1, nullptr, 0, 0) != 1)
        return std::nullopt;
      return tern::channel_binding{"tls-exporter", tern::crypto::bytes(out, out + sizeof out)};
    }
    X509* certificate = ::SSL_get1_peer_certificate(ssl);
    if (!certificate)
      return std::nullopt;
    int digest_nid = NID_undef;
    ::X509_get_signature_info(certificate, &digest_nid, nullptr, nullptr, nullptr);
    const EVP_MD* digest = ::EVP_get_digestbynid(digest_nid);
    // MD5 and SHA-1 are replaced by SHA-256, and so is a signature with no
    // digest of its own (RFC 5929, 4.1).
    if (!digest || digest_nid == NID_md5 || digest_nid == NID_sha1)
      digest = ::EVP_sha256();
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    const bool made = ::X509_digest(certificate, digest, out, &length) == 1;
    ::X509_free(certificate);
    if (!made)
      return std::nullopt;
    return tern::channel_binding{"tls-server-end-point", tern::crypto::bytes(out, out + length)};
  }

  // What ended the stream, where the network did.
  std::optional<error_code> failed() const { return failed_; }

  void close() {
    error_code ignored;
    if (tls_)
      (void)owner_->await<>([&](auto done) { stream_.async_shutdown(std::move(done)); });
    stream_.next_layer().shutdown(tcp::socket::shutdown_both, ignored);
    stream_.next_layer().close(ignored);
  }

 private:
  bool at_end() {
    if (!fetched_) {
      fetched_ = true;
      const auto [error, n] = tls_ ? owner_->await<std::size_t>([&](auto done) {
        stream_.async_read_some(asio::buffer(buffer_), std::move(done));
      })
                                   : owner_->await<std::size_t>([&](auto done) {
                                       stream_.next_layer().async_read_some(asio::buffer(buffer_), std::move(done));
                                     });
      if (error || n == 0) {
        ended_ = true;
        current_ = {};
        if (error && error != asio::error::eof && error != asio::ssl::error::stream_truncated)
          failed_ = error;
      } else {
        current_ = std::string_view(buffer_.data(), n);
      }
    }
    return ended_;
  }

  loop* owner_;
  asio::ssl::stream<tcp::socket> stream_;
  bool tls_ = false;
  std::string pending_;
  std::string sending_;
  bool writing_ = false;
  std::array<char, 16384> buffer_{};
  std::string_view current_;
  bool fetched_ = false;
  bool ended_ = false;
  chunks view_;
  std::optional<error_code> failed_;
};

// A file descriptor read by a fiber: the terminal, a pipe. A copy of the
// descriptor is taken, so the caller's stays as it was.
class descriptor {
 public:
  descriptor(loop& owner, int fd) : owner_(&owner), stream_(owner.io(), ::dup(fd)) {}

  // What one read brings: nothing at the end, or where reading failed.
  std::size_t read_some(std::span<char> into) {
    const auto [error, n] = owner_->await<std::size_t>([&](auto done) {
      stream_.async_read_some(asio::buffer(into.data(), into.size()), std::move(done));
    });
    return error ? 0 : n;
  }

 private:
  loop* owner_;
  asio::posix::stream_descriptor stream_;
};

// A host's addresses, each tried in turn: the connected socket.
inline tcp::socket connect(loop& owner, std::string_view host, std::uint16_t port) {
  tcp::resolver resolver(owner.io());
  const auto [resolved, found] = owner.await<tcp::resolver::results_type>([&](auto done) {
    resolver.async_resolve(std::string(host), std::to_string(port), std::move(done));
  });
  if (resolved)
    throw failure("resolving " + std::string(host), resolved);
  tcp::socket socket(owner.io());
  const auto [connected, to] = owner.await<tcp::endpoint>([&](auto done) {
    asio::async_connect(socket, found, std::move(done));
  });
  if (connected)
    throw failure("connecting to " + std::string(host), connected);
  return socket;
}

// Connections accepted on a port of this machine: 0 for any free one.
class listener {
 public:
  explicit listener(loop& owner, std::uint16_t port = 0, std::string_view address = "127.0.0.1")
      : owner_(&owner), acceptor_(owner.io(), tcp::endpoint(asio::ip::make_address(std::string(address)), port)) {}

  std::uint16_t port() const { return acceptor_.local_endpoint().port(); }

  tcp::socket accept() {
    tcp::socket socket(owner_->io());
    const auto [error] = owner_->await<>([&](auto done) { acceptor_.async_accept(socket, std::move(done)); });
    if (error)
      throw failure("accepting", error);
    return socket;
  }

 private:
  loop* owner_;
  tcp::acceptor acceptor_;
};

// The nameserver /etc/resolv.conf names first; the local stub where it
// names none.
inline asio::ip::address nameserver() {
  std::ifstream conf("/etc/resolv.conf");
  std::string word;
  while (conf >> word)
    if (word == "nameserver" && conf >> word) {
      error_code bad;
      const auto address = asio::ip::make_address(word, bad);
      if (!bad)
        return address;
    }
  return asio::ip::make_address("127.0.0.53");
}

// A domain's XMPP client service by its SRV records (RFC 6120, 3.2.1), in
// the order they are to be tried; the domain itself on 5222 where there are
// none, or the lookup fails. Asked over UDP, with tern's encoder and
// decoder; a second try after a second without an answer.
inline std::vector<tern::srv::target> xmpp_targets(loop& owner, std::string_view domain) {
  std::vector<tern::srv::target> found;
  error_code opened;
  asio::ip::udp::socket socket(owner.io());
  const asio::ip::udp::endpoint server(nameserver(), 53);
  socket.open(server.protocol(), opened);
  std::random_device entropy;
  if (!opened)
    for (int attempt = 0; attempt < 2 && found.empty(); ++attempt) {
      const auto id = static_cast<std::uint16_t>(entropy());
      const auto question = tern::srv::query(domain, id);
      const auto [sent_error, sent] = owner.await<std::size_t>([&](auto done) {
        socket.async_send_to(asio::buffer(question), server, std::move(done));
      });
      if (sent_error)
        break;
      std::array<std::uint8_t, 4096> answer{};
      asio::steady_timer deadline(owner.io(), std::chrono::seconds(1));
      deadline.async_wait([&](error_code expired) {
        if (!expired)
          socket.cancel();
      });
      const auto [received_error, received] = owner.await<std::size_t>([&](auto done) {
        socket.async_receive(asio::buffer(answer), std::move(done));
      });
      deadline.cancel();
      if (received_error)
        continue;
      if (auto targets = tern::srv::answers(std::span<const std::uint8_t>(answer.data(), received), id))
        found = std::move(*targets);
    }
  std::mt19937 random(entropy());
  auto ordered = tern::srv::ordered(found, random);
  if (ordered.empty())
    ordered.push_back(tern::srv::fallback(domain));
  return ordered;
}

}  // namespace mux::net
