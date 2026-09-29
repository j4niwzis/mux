// SPDX-License-Identifier: AGPL-3.0-only
// mux.net's fibers and streams, on this machine's loopback.
import std;
import mux.net;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using mux::net::loop;

TEST(Loop, ParkAndWake) {
  loop running;
  std::vector<std::string> said;
  loop::handle first = nullptr;
  running.spawn([&] {
    first = running.current();
    said.push_back("a1");
    running.park();
    said.push_back("a2");
  });
  running.spawn([&] {
    said.push_back("b1");
    running.wake(first);
    said.push_back("b2");
  });
  running.run();
  EXPECT_EQ(said, (std::vector<std::string>{"a1", "b1", "b2", "a2"}));
}

TEST(Loop, WokenTwiceRunsOnce) {
  loop running;
  int resumed = 0;
  loop::handle waiting = nullptr;
  running.spawn([&] {
    waiting = running.current();
    running.park();
    ++resumed;
  });
  running.spawn([&] {
    running.wake(waiting);
    running.wake(waiting);
  });
  running.run();
  EXPECT_EQ(resumed, 1);
}

TEST(Loop, Sleep) {
  loop running;
  std::chrono::steady_clock::duration took{};
  running.spawn([&] {
    const auto from = std::chrono::steady_clock::now();
    running.sleep(std::chrono::milliseconds(20));
    took = std::chrono::steady_clock::now() - from;
  });
  running.run();
  EXPECT_GE(took, std::chrono::milliseconds(20));
}

TEST(Loop, AFailureComesOutOfRun) {
  loop running;
  running.spawn([] { throw std::runtime_error("inside"); });
  EXPECT_THROW(running.run(), std::runtime_error);
}

// Two fibers talking over a TCP connection on the loopback, each through a
// stream as tern would use it: chunks in as they come, bytes out on flush.
TEST(Stream, OverTheLoopback) {
  loop running;
  auto tls = mux::net::client_tls();
  mux::net::listener server(running);
  const std::uint16_t port = server.port();
  std::string heard_by_server, heard_by_client;
  running.spawn([&] {
    mux::net::stream wire(running, tls, server.accept());
    wire.write("hello, ");
    wire.write("client");
    wire.flush();
    for (auto it = wire.input().begin(); it != std::default_sentinel; ++it) {
      heard_by_server += *it;
      if (heard_by_server.size() >= 2)
        break;
    }
    wire.close();
  });
  running.spawn([&] {
    mux::net::stream wire(running, tls, mux::net::connect(running, "127.0.0.1", port));
    EXPECT_FALSE(wire.secured());
    EXPECT_FALSE(wire.channel_binding().has_value());
    auto& chunks = wire.input();
    for (auto it = chunks.begin(); it != std::default_sentinel; ++it) {
      heard_by_client += *it;
      if (heard_by_client.size() >= 13)
        break;
    }
    wire.write("hi");
    wire.flush();
  });
  running.run();
  EXPECT_EQ(heard_by_client, "hello, client");
  EXPECT_EQ(heard_by_server, "hi");
}

// Two fibers flushing through one stream at once: one write in flight, and
// every byte sent, in the order it was written.
TEST(Stream, FlushesFromTwoFibers) {
  loop running;
  auto tls = mux::net::client_tls();
  mux::net::listener server(running);
  const std::uint16_t port = server.port();
  std::string heard;
  running.spawn([&] {
    mux::net::stream wire(running, tls, server.accept());
    for (auto it = wire.input().begin(); it != std::default_sentinel; ++it)
      heard += *it;
  });
  running.spawn([&] {
    mux::net::stream wire(running, tls, mux::net::connect(running, "127.0.0.1", port));
    bool second_done = false;
    running.spawn([&] {
      wire.write(std::string(100000, 'b'));
      wire.flush();
      second_done = true;
    });
    wire.write(std::string(100000, 'a'));
    wire.flush();
    while (!second_done)
      running.sleep(std::chrono::milliseconds(1));
    wire.close();
  });
  running.run();
  ASSERT_EQ(heard.size(), 200000u);
  EXPECT_EQ(heard.find('b'), 100000u);
}

TEST(Connect, ARefusedPortFails) {
  loop running;
  std::optional<std::string> failed;
  running.spawn([&] {
    std::uint16_t closed = 0;
    {
      mux::net::listener gone(running);
      closed = gone.port();
    }
    try {
      (void)mux::net::connect(running, "127.0.0.1", closed);
    } catch (const mux::net::failure& why) {
      failed = why.what();
    }
  });
  running.run();
  EXPECT_TRUE(failed.has_value());
}

// Through a SOCKS5 proxy: the greeting, the request naming the host and
// port, and then the bytes of the connection itself.
TEST(Net, ThroughASocks5Proxy) {
  namespace net = mux::net;
  net::loop running;
  net::listener proxy(running);
  std::string asked;
  running.spawn([&] {
    auto socket = proxy.accept();
    asked = net::detail::read_exactly(running, socket, 3, "greeting");
    net::detail::write_all(running, socket, std::string("\x05\x00", 2), "choosing");
    const std::string head = net::detail::read_exactly(running, socket, 5, "request");
    asked += head;
    asked += net::detail::read_exactly(running, socket, static_cast<unsigned char>(head[4]) + 2u, "request");
    net::detail::write_all(running, socket, std::string("\x05\x00\x00\x01\x7f\x00\x00\x01\x14\x66", 10), "answering");
    net::detail::write_all(running, socket, "hello", "hello");
  });
  std::string heard;
  running.spawn([&] {
    auto socket = net::connect(running, net::proxy{.kind = net::proxy_kind::socks5{}, .host = "127.0.0.1", .port = proxy.port()},
                               "example.org", 5222);
    heard = net::detail::read_exactly(running, socket, 5, "reading");
  });
  running.run();
  EXPECT_EQ(heard, "hello");
  EXPECT_EQ(asked, std::string("\x05\x01\x00"
                               "\x05\x01\x00\x03\x0b"
                               "example.org"
                               "\x14\x66",
                               21));
}

}  // namespace
