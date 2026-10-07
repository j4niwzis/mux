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
#if defined(__ANDROID__)
#include "android_runtime.h"
#endif

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
import splice.bytes;
import splice;
import tern;

// Whether completion handlers are erased (outside a release build): CMake
// says which build this is; C++ cannot see it.
export namespace mux::net {
#ifdef MUX_ERASED_HANDLERS
inline constexpr bool kErasedHandlers = true;
#else
inline constexpr bool kErasedHandlers = false;
#endif
}  // namespace mux::net

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
          } catch (const boost::context::detail::forced_unwind&) {
            // The loop is going away and unwinds what is still parked:
            // that is how a fiber ends then, not a failure of it.
            throw;
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

  // Runs until stop(), whether or not there is anything to do: the loop of
  // a program whose other thread posts work to it.
  void run_forever() {
    auto kept = asio::make_work_guard(io_);
    io_.run();
  }

  // A fiber started from any thread: the loop's thread runs it.
  template <class Body>
  void post(Body body) {
    asio::post(io_, [this, body = std::move(body)]() mutable { spawn(std::move(body)); });
  }

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
    auto done = [this, self, &got](error_code error, Values... values) {
      got.emplace(error, std::move(values)...);
      wake(self);
    };
    // Outside a release build, the handler erased to its signature: every
    // Asio, Beast and TLS operation under it is then made once for each
    // signature, not once for each place it is awaited from -- most of a
    // build. A release build keeps the handler's own type, all of it inlined.
    if constexpr (kErasedHandlers)
      start(asio::any_completion_handler<void(error_code, Values...)>(std::move(done)));
    else
      start(std::move(done));
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

// TLS settings, as mux's own type: what imports mux.net does not see Asio,
// which is declared in this module's global fragment, and names this.
class tls {
 public:
  explicit tls(asio::ssl::context made) : context_(std::move(made)) {}
  asio::ssl::context& context() noexcept { return context_; }

 private:
  asio::ssl::context context_;
};

// A client's TLS settings: the system's trusted certificates, TLS 1.2 and up.
inline tls client_tls() {
  asio::ssl::context made(asio::ssl::context::tls_client);
  made.set_default_verify_paths();
  made.set_options(asio::ssl::context::default_workarounds | asio::ssl::context::no_sslv2 |
                   asio::ssl::context::no_sslv3 | asio::ssl::context::no_tlsv1 | asio::ssl::context::no_tlsv1_1);
  made.set_verify_mode(asio::ssl::verify_peer);
  return tls(std::move(made));
}

// How a server's certificate is checked: against its name, always (as
// asio's host_name_verification); and for trust, by OpenSSL against the
// system's CAs -- or, on Android, by Android itself, as its own TLS would
// (mux_android_trusts): the whole chain the server sent, decided once, at
// the leaf. Earlier depths are let through there: OpenSSL has no CAs of
// Android's to build the chain with, and Android builds it.
struct peer_verification {
  std::string host;
  // What Android said of this connection's chain, once asked, and why not.
  std::shared_ptr<std::optional<bool>> android_said = std::make_shared<std::optional<bool>>();
  std::shared_ptr<std::string> refused = std::make_shared<std::string>();
  bool operator()(bool preverified, asio::ssl::verify_context& context) const {
#if defined(__ANDROID__)
    (void)preverified;  // OpenSSL's own verdict: it holds none of Android's CAs
    ::X509_STORE_CTX* store = context.native_handle();
    if (::X509_STORE_CTX_get_error_depth(store) > 0)
      return true;
    if (!android_said->has_value()) {
      // The chain as the server sent it, leaf first, each DER-encoded.
      const STACK_OF(X509)* sent = ::X509_STORE_CTX_get0_untrusted(store);
      std::vector<std::vector<unsigned char>> ders;
      const int count = sent ? sk_X509_num(sent) : 0;
      for (int at = 0; at < count; ++at) {
        ::X509* one = sk_X509_value(sent, at);
        const int size = ::i2d_X509(one, nullptr);
        if (size <= 0)
          continue;
        std::vector<unsigned char> der(static_cast<std::size_t>(size));
        unsigned char* into = der.data();
        ::i2d_X509(one, &into);
        ders.push_back(std::move(der));
      }
      if (ders.empty())
        if (::X509* leaf = ::X509_STORE_CTX_get0_cert(store)) {
          const int size = ::i2d_X509(leaf, nullptr);
          std::vector<unsigned char> der(static_cast<std::size_t>(size > 0 ? size : 0));
          unsigned char* into = der.data();
          if (size > 0 && ::i2d_X509(leaf, &into) > 0)
            ders.push_back(std::move(der));
        }
      const std::vector<const unsigned char*> pointers =
          std::ranges::to<std::vector<const unsigned char*>>(std::views::transform(ders, [](const auto& one) { return one.data(); }));
      const std::vector<std::size_t> sizes =
          std::ranges::to<std::vector<std::size_t>>(std::views::transform(ders, [](const auto& one) { return one.size(); }));
      char why[256] = {};
      *android_said = mux_android_trusts(pointers.data(), sizes.data(), pointers.size(), host.c_str(), why, sizeof why);
      if (!**android_said)
        *refused = why;
    }
    if (!**android_said) {
      ::X509_STORE_CTX_set_error(store, X509_V_ERR_CERT_UNTRUSTED);
      return false;
    }
    // Trusted by Android: the name checked as anywhere else.
    return asio::ssl::host_name_verification(host)(true, context);
#else
    return asio::ssl::host_name_verification(host)(preverified, context);
#endif
  }
};

// Why a peer's certificate was not trusted, in OpenSSL's words ("unable to
// get local issuer certificate", "certificate has expired"), where that was
// what failed: what "certificate verify failed" alone did not say.
inline std::string verify_reason(const ::SSL* connection) {
  const long result = ::SSL_get_verify_result(connection);
  return result == X509_V_OK ? std::string() : std::string(::X509_verify_cert_error_string(result));
}

// A server's TLS settings, from its certificate chain and key in PEM:
// what a test's server, or a local one, speaks with.
inline tls server_tls(std::string_view certificate_pem, std::string_view key_pem) {
  asio::ssl::context made(asio::ssl::context::tls_server);
  made.set_options(asio::ssl::context::default_workarounds | asio::ssl::context::no_sslv2 |
                   asio::ssl::context::no_sslv3 | asio::ssl::context::no_tlsv1 | asio::ssl::context::no_tlsv1_1);
  made.use_certificate_chain(asio::buffer(certificate_pem.data(), certificate_pem.size()));
  made.use_private_key(asio::buffer(key_pem.data(), key_pem.size()), asio::ssl::context::pem);
  return tls(std::move(made));
}

// A certificate trusted besides the system's: a server's own, where the
// user has said to trust it, or a test's.
inline void trust(tls& settings, std::string_view certificate_pem) {
  settings.context().add_certificate_authority(asio::buffer(certificate_pem.data(), certificate_pem.size()));
}

// A TCP connection, TLS once it is started, as tern's transport: input as
// the chunks each read brings -- read only when tern asks whether there is
// more -- and output kept until tern flushes a unit.
class stream {
 public:
  stream(loop& owner, tls& settings, tcp::socket socket)
      : owner_(&owner), stream_(std::move(socket), settings.context()) {}
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
    stream_.set_verify_callback(peer_verification{name});
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

  // TLS on the server's side of the connection, with the settings the
  // stream was made with.
  bool accept_tls() {
    const auto [error] = owner_->await<>([&](auto done) {
      stream_.async_handshake(asio::ssl::stream_base::server, std::move(done));
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

// How long a host's name is looked up for, and its addresses tried, before
// it is given up on: each waited for as long as the system let it -- a
// connect whose first packet is dropped, two minutes and more -- and an
// account stood offline with nothing said.
inline constexpr std::chrono::seconds kResolveFor{15};
inline constexpr std::chrono::seconds kConnectFor{15};

// A host's addresses, each tried in turn: the connected socket.
inline tcp::socket connect(loop& owner, std::string_view host, std::uint16_t port) {
  tcp::resolver resolver(owner.io());
  bool resolve_expired = false;
  asio::steady_timer resolving(owner.io(), kResolveFor);
  resolving.async_wait([&](error_code ended) {
    if (!ended) {
      resolve_expired = true;
      resolver.cancel();
    }
  });
  const auto [resolved, found] = owner.await<tcp::resolver::results_type>([&](auto done) {
    resolver.async_resolve(std::string(host), std::to_string(port), std::move(done));
  });
  resolving.cancel();
  if (resolved)
    throw failure("resolving " + std::string(host) + (resolve_expired ? " (timed out)" : ""),
                  resolve_expired ? error_code(asio::error::timed_out) : resolved);
  tcp::socket socket(owner.io());
  bool connect_expired = false;
  asio::steady_timer connecting(owner.io(), kConnectFor);
  connecting.async_wait([&](error_code ended) {
    if (!ended) {
      connect_expired = true;
      error_code ignored;
      socket.cancel(ignored);
    }
  });
  const auto [connected, to] = owner.await<tcp::endpoint>([&](auto done) {
    asio::async_connect(socket, found, std::move(done));
  });
  connecting.cancel();
  if (connected)
    throw failure("connecting to " + std::string(host) + (connect_expired ? " (timed out)" : ""),
                  connect_expired ? error_code(asio::error::timed_out) : connected);
  return socket;
}

// A proxy to connect through: SOCKS5 (RFC 1928, with RFC 1929's user name
// and password where one is given) or HTTP CONNECT (RFC 9110, 9.3.6, with
// Basic authorization where one is given).
namespace proxy_kind {
struct socks5 {};
struct http {};
}  // namespace proxy_kind
using proxy_kind_t = spl::variant<proxy_kind::socks5, proxy_kind::http>;

// Who a domain's SRV records are asked of, through a proxy: the system's
// nameserver (where the proxy can reach it), none at all, or one chosen --
// whoever is asked learns which domain is looked up.
namespace srv_lookup {
struct system {};
struct none {};
struct server {
  asio::ip::address address;
};
}  // namespace srv_lookup
using srv_lookup_t = spl::variant<srv_lookup::system, srv_lookup::none, srv_lookup::server>;
// As a profile says it, read once: unset the system's; "off" none; else a
// nameserver's address -- and none where it is not one, rather than the
// system's, which the user had chosen not to ask.
[[nodiscard]] inline srv_lookup_t srv_lookup_of(const std::optional<std::string>& said) {
  if (!said)
    return srv_lookup::system{};
  if (*said == "off")
    return srv_lookup::none{};
  error_code bad;
  const auto address = asio::ip::make_address(*said, bad);
  if (bad)
    return srv_lookup::none{};
  return srv_lookup::server{address};
}

struct proxy {
  proxy_kind_t kind = proxy_kind::socks5{};
  std::string host;
  std::uint16_t port = 1080;
  std::optional<std::string> username;
  std::optional<std::string> password;
  srv_lookup_t srv = srv_lookup::system{};
};

namespace detail {

inline void write_all(loop& owner, tcp::socket& socket, std::string_view bytes, std::string_view what) {
  const auto [error, n] = owner.await<std::size_t>([&](auto done) {
    asio::async_write(socket, asio::buffer(bytes.data(), bytes.size()), std::move(done));
  });
  if (error)
    throw failure(what, error);
}
inline std::string read_exactly(loop& owner, tcp::socket& socket, std::size_t count, std::string_view what) {
  std::string out(count, '\0');
  const auto [error, n] = owner.await<std::size_t>([&](auto done) {
    asio::async_read(socket, asio::buffer(out.data(), out.size()), std::move(done));
  });
  if (error)
    throw failure(what, error);
  return out;
}

inline std::string base64(std::string_view in) {
  static constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  std::size_t i = 0;
  for (; i + 2 < in.size(); i += 3) {
    const unsigned n = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8) |
                       static_cast<unsigned char>(in[i + 2]);
    out += alphabet[(n >> 18) & 63];
    out += alphabet[(n >> 12) & 63];
    out += alphabet[(n >> 6) & 63];
    out += alphabet[n & 63];
  }
  if (i + 1 == in.size()) {
    const unsigned n = static_cast<unsigned char>(in[i]) << 16;
    out += alphabet[(n >> 18) & 63];
    out += alphabet[(n >> 12) & 63];
    out += "==";
  } else if (i + 2 == in.size()) {
    const unsigned n = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8);
    out += alphabet[(n >> 18) & 63];
    out += alphabet[(n >> 12) & 63];
    out += alphabet[(n >> 6) & 63];
    out += '=';
  }
  return out;
}

// A refusal of the proxy's, as a failure of connecting.
[[noreturn]] inline void refused(std::string_view why) {
  throw failure(std::string("the proxy ") + std::string(why), asio::error::connection_refused);
}

inline void socks5(loop& owner, tcp::socket& socket, const proxy& via, std::string_view host, std::uint16_t port) {
  const bool login = via.username.has_value();
  write_all(owner, socket, login ? std::string("\x05\x02\x00\x02", 4) : std::string("\x05\x01\x00", 3),
            "greeting the proxy");
  const std::string chosen = read_exactly(owner, socket, 2, "reading the proxy's greeting");
  if (chosen[0] != '\x05')
    refused("does not speak SOCKS5");
  if (chosen[1] == '\x02') {
    if (!login)
      refused("asks for a user name");
    const std::string& user = *via.username;
    const std::string password = via.password.value_or("");
    if (user.size() > 255 || password.size() > 255)
      refused("takes user names and passwords of at most 255 bytes");
    std::string asked("\x01", 1);
    asked += static_cast<char>(user.size());
    asked += user;
    asked += static_cast<char>(password.size());
    asked += password;
    write_all(owner, socket, asked, "logging in to the proxy");
    const std::string answer = read_exactly(owner, socket, 2, "reading the proxy's login");
    if (answer[1] != '\x00')
      refused("did not take the user name and password");
  } else if (chosen[1] != '\x00') {
    refused("accepts none of the ways offered to log in");
  }
  if (host.size() > 255)
    refused("takes host names of at most 255 bytes");
  std::string request("\x05\x01\x00\x03", 4);
  request += static_cast<char>(host.size());
  request += host;
  request += static_cast<char>(port >> 8);
  request += static_cast<char>(port & 0xff);
  write_all(owner, socket, request, "asking the proxy to connect");
  const std::string head = read_exactly(owner, socket, 4, "reading the proxy's answer");
  if (head[1] != '\x00')
    refused("could not connect (SOCKS5 reply " + std::to_string(static_cast<unsigned char>(head[1])) + ")");
  std::size_t rest = 2;  // the port
  if (head[3] == '\x01')
    rest += 4;
  else if (head[3] == '\x04')
    rest += 16;
  else if (head[3] == '\x03')
    rest += static_cast<unsigned char>(read_exactly(owner, socket, 1, "reading the proxy's answer")[0]);
  else
    refused("answered with an unknown kind of address");
  (void)read_exactly(owner, socket, rest, "reading the proxy's answer");
}

inline void http_connect(loop& owner, tcp::socket& socket, const proxy& via, std::string_view host,
                         std::uint16_t port) {
  const std::string where = std::string(host) + ":" + std::to_string(port);
  std::string request = "CONNECT " + where + " HTTP/1.1\r\nHost: " + where + "\r\n";
  if (via.username)
    request += "Proxy-Authorization: Basic " + base64(*via.username + ":" + via.password.value_or("")) + "\r\n";
  request += "\r\n";
  write_all(owner, socket, request, "asking the proxy to connect");
  std::string answer;
  while (!answer.ends_with("\r\n\r\n")) {
    if (answer.size() > 16384)
      refused("answered with more than a head");
    answer += read_exactly(owner, socket, 1, "reading the proxy's answer");
  }
  // HTTP/1.x 200 ...
  const auto space = answer.find(' ');
  if (space == std::string::npos || answer.compare(space + 1, 3, "200") != 0)
    refused("would not connect: " + answer.substr(0, answer.find('\r')));
}

}  // namespace detail

// Whether a host may be asked for what a message names (a link's preview)
// from this machine: not this machine itself, nor its own network. A link
// is anyone's to write, and fetching it here would GET whatever it names
// there -- a router's page, a printer's -- from the user's machine. A name
// that resolves to such an address is not caught here.
[[nodiscard]] inline bool public_host(std::string_view host) {
  const std::string lower = spl::bytes::lower_text(host);
  if (lower == "localhost" || lower.ends_with(".localhost") || lower.ends_with(".local") ||
      lower.ends_with(".internal") || lower.ends_with(".lan") || (!lower.contains('.') && !lower.contains(':')))
    return false;
  error_code bad;
  const auto address = asio::ip::make_address(lower, bad);
  if (bad)
    return true;  // a name
  if (address.is_loopback() || address.is_unspecified() || address.is_multicast())
    return false;
  if (address.is_v6()) {
    const auto v6 = address.to_v6();
    return !(v6.is_link_local() || v6.is_site_local() || (v6.to_bytes()[0] & 0xfe) == 0xfc || v6.is_v4_mapped());
  }
  const auto b = address.to_v4().to_bytes();
  return !(b[0] == 10 || b[0] == 127 || b[0] == 0 || (b[0] == 172 && (b[1] & 0xf0) == 16) ||
           (b[0] == 192 && b[1] == 168) || (b[0] == 169 && b[1] == 254) || (b[0] == 100 && (b[1] & 0xc0) == 64));
}

// A connection to host:port, through a proxy where one is given.
inline tcp::socket connect(loop& owner, const std::optional<proxy>& via, std::string_view host, std::uint16_t port) {
  if (!via)
    return connect(owner, host, port);
  tcp::socket socket = connect(owner, via->host, via->port);
  spl::visit(spl::overloaded{[&](proxy_kind::socks5) { detail::socks5(owner, socket, *via, host, port); },
                                   [&](proxy_kind::http) { detail::http_connect(owner, socket, *via, host, port); }},
                via->kind);
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
  // No more connections: an accept waiting ends with a failure.
  void close() {
    error_code ignored;
    acceptor_.close(ignored);
  }

 private:
  loop* owner_;
  tcp::acceptor acceptor_;
};

// A request's head, off a connection accepted: up to its blank line -- a
// browser sent back to this machine, as an OAuth 2.0 sign-in ends.
inline std::string read_request_head(loop& owner, tcp::socket& socket) {
  std::string head;
  const auto [error, n] = owner.await<std::size_t>([&](auto done) {
    asio::async_read_until(socket, asio::dynamic_buffer(head, 16 * 1024), "\r\n\r\n", std::move(done));
  });
  if (error)
    throw failure("reading a request", error);
  return head;
}
// An answer written back on a connection accepted.
inline void answer(loop& owner, tcp::socket& socket, std::string_view bytes) {
  detail::write_all(owner, socket, bytes, "answering a request");
}

// The nameserver /etc/resolv.conf names first; the local stub where it
// names none.
inline std::optional<asio::ip::address> nameserver() {
#if defined(__ANDROID__)
  char name[128];
  if (!mux_android_nameserver(name, sizeof(name))) return std::nullopt;
  error_code bad;
  const auto address = asio::ip::make_address(name, bad);
  return bad ? std::nullopt : std::optional(address);
#else
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
#endif
}

// A domain's XMPP client service by its SRV records (RFC 6120, 3.2.1), in
// the order they are to be tried; the domain itself on 5222 where there are
// none, or the lookup fails. Asked over UDP, with tern's encoder and
// decoder; a second try after a second without an answer.
inline std::vector<tern::srv::target> xmpp_targets(loop& owner, std::string_view domain) {
  std::vector<tern::srv::target> found;
  error_code opened;
  asio::ip::udp::socket socket(owner.io());
  const auto dns = nameserver();
  if (!dns) return found;
  const asio::ip::udp::endpoint server(*dns, 53);
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

// Whether a nameserver is one only this machine or its network reaches: a
// proxy elsewhere cannot ask it.
inline bool local_only(const asio::ip::address& address) {
  if (address.is_loopback() || address.is_unspecified())
    return true;
  if (address.is_v6())
    return address.to_v6().is_link_local() || (address.to_v6().to_bytes()[0] & 0xfe) == 0xfc;
  const auto b = address.to_v4().to_bytes();
  return b[0] == 10 || (b[0] == 172 && (b[1] & 0xf0) == 16) || (b[0] == 192 && b[1] == 168) ||
         (b[0] == 169 && b[1] == 254);
}

// The same, through a proxy: the SRV records asked over TCP (RFC 7766)
// through the proxy, of the nameserver /etc/resolv.conf names, so that no
// query leaves this machine outside the proxy. Where that nameserver is one
// the proxy cannot reach, or the query fails, no SRV records are asked at
// all, and the domain on 5222 is connected to through the proxy, which
// resolves it.
inline std::vector<tern::srv::target> xmpp_targets(loop& owner, const std::optional<proxy>& via,
                                                   std::string_view domain) {
  if (!via)
    return xmpp_targets(owner, domain);
  std::vector<tern::srv::target> found;
  // Whom to ask: the system's nameserver, where one the proxy can reach;
  // the one chosen; or none.
  const std::optional<asio::ip::address> asked = spl::visit(
      spl::overloaded{[](srv_lookup::system) -> std::optional<asio::ip::address> {
                           const auto server = nameserver();
                           return !server || local_only(*server) ? std::nullopt : server;
                         },
                         [](srv_lookup::none) -> std::optional<asio::ip::address> { return std::nullopt; },
                         [](const srv_lookup::server& chosen) -> std::optional<asio::ip::address> { return chosen.address; }},
      via->srv);
  if (asked) {
    const auto& server = *asked;
    try {
      tcp::socket socket = connect(owner, via, server.to_string(), 53);
      std::random_device entropy;
      const auto id = static_cast<std::uint16_t>(entropy());
      const auto question = tern::srv::query(domain, id);
      std::string framed;
      framed += static_cast<char>((question.size() >> 8) & 0xff);
      framed += static_cast<char>(question.size() & 0xff);
      for (const auto byte : question)
        framed += static_cast<char>(byte);
      detail::write_all(owner, socket, framed, "asking for SRV records");
      const std::string length = detail::read_exactly(owner, socket, 2, "reading SRV records");
      const std::size_t size = (static_cast<std::size_t>(static_cast<unsigned char>(length[0])) << 8) |
                               static_cast<unsigned char>(length[1]);
      const std::string answer = detail::read_exactly(owner, socket, size, "reading SRV records");
      std::vector<std::uint8_t> bytes(answer.begin(), answer.end());
      if (auto targets = tern::srv::answers(std::span<const std::uint8_t>(bytes.data(), bytes.size()), id))
        found = std::move(*targets);
    } catch (const failure&) {
      found.clear();
    }
  }
  std::mt19937 random(std::random_device{}());
  auto ordered = tern::srv::ordered(found, random);
  if (ordered.empty())
    ordered.push_back(tern::srv::fallback(domain));
  return ordered;
}

}  // namespace mux::net
