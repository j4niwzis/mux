// SPDX-License-Identifier: AGPL-3.0-only
// mux.matrix against a homeserver played by a fiber of the same loop, over
// real TLS on the loopback: a certificate made for the test, trusted by the
// client and nothing else. Login, a first sync with a named room, members,
// a formatted message, a reply, an edit, a reaction and typing; a message
// sent and acknowledged; a later sync; stopped.
import std;
import mux.core;
import mux.net;
import mux.matrix;
import mux.test.certificate;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

const std::string login = R"({"user_id":"@a:x.org","access_token":"tok","device_id":"DEV"})";

const std::string first_sync = R"({"next_batch":"s1","rooms":{"join":{"!r:x.org":{
 "state":{"events":[
  {"type":"m.room.create","state_key":"","event_id":"$c","sender":"@a:x.org","origin_server_ts":1,"content":{"room_version":"11"}},
  {"type":"m.room.name","state_key":"","event_id":"$n","sender":"@a:x.org","origin_server_ts":2,"content":{"name":"Garden"}},
  {"type":"m.room.member","state_key":"@b:x.org","event_id":"$mb","sender":"@b:x.org","origin_server_ts":3,"content":{"membership":"join","displayname":"B"}}]},
 "timeline":{"limited":false,"prev_batch":"p0","events":[
  {"type":"m.room.message","event_id":"$m1","sender":"@b:x.org","origin_server_ts":10,
   "content":{"msgtype":"m.text","body":"hello","format":"org.matrix.custom.html","formatted_body":"<b>hello</b>"}},
  {"type":"m.room.message","event_id":"$m2","sender":"@a:x.org","origin_server_ts":11,
   "content":{"msgtype":"m.text","body":"hi","m.relates_to":{"m.in_reply_to":{"event_id":"$m1"}}}},
  {"type":"m.room.message","event_id":"$m3","sender":"@b:x.org","origin_server_ts":12,
   "content":{"msgtype":"m.text","body":"* hello there","m.new_content":{"msgtype":"m.text","body":"hello there"},
              "m.relates_to":{"rel_type":"m.replace","event_id":"$m1"}}},
  {"type":"m.reaction","event_id":"$r1","sender":"@a:x.org","origin_server_ts":13,
   "content":{"m.relates_to":{"rel_type":"m.annotation","event_id":"$m1","key":"👍"}}}]},
 "ephemeral":{"events":[{"type":"m.typing","content":{"user_ids":["@b:x.org"]}}]},
 "unread_notifications":{"highlight_count":0,"notification_count":2}}}}})";

std::string answer(std::string_view body) {
  return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) +
         "\r\n\r\n" + std::string(body);
}

struct request {
  std::string method, target, headers, body;
};

// The requests on one connection, as they come: headers to the blank
// line, then as much body as Content-Length says.
std::optional<request> take(std::string& pending) {
  const auto end = pending.find("\r\n\r\n");
  if (end == std::string::npos)
    return std::nullopt;
  const std::string head = pending.substr(0, end);
  std::size_t length = 0;
  if (const auto at = head.find("Content-Length: "); at != std::string::npos)
    length = std::stoul(head.substr(at + 16));
  if (pending.size() < end + 4 + length)
    return std::nullopt;
  request made;
  const auto first_space = head.find(' ');
  made.method = head.substr(0, first_space);
  made.target = head.substr(first_space + 1, head.find(' ', first_space + 1) - first_space - 1);
  made.headers = head;
  made.body = pending.substr(end + 4, length);
  pending.erase(0, end + 4 + length);
  return made;
}

// What the test hears from the account, and what it does back: a sink
// that knows the account it belongs to, without anything type-erased.
struct recorder;
struct sink {
  recorder* to = nullptr;
  void operator()(mux::change_t one) const;
};

struct recorder {
  mux::net::loop* running = nullptr;
  mux::model model;
  std::vector<mux::connection> states;
  bool sent = false;
  std::optional<mux::matrix::account<sink>> account;
};

void sink::operator()(mux::change_t one) const {
  if (const auto* changed = std::get_if<mux::change::connection_changed>(&one)) {
    to->states.push_back(changed->state);
    if (changed->state == mux::connection::offline || changed->state == mux::connection::failed)
      to->running->stop();
  }
  // A message sent once the room is there.
  if (std::holds_alternative<mux::change::conversation_updated>(one) && !to->sent) {
    to->sent = true;
    to->account->send("!r:x.org", "from mux");
  }
  to->model.apply(one);
}

TEST(Matrix, AgainstAHomeserverOverTls) {
  const auto certificate = mux::test::self_signed();
  mux::net::loop running;
  auto server_tls = mux::net::server_tls(certificate.certificate_pem, certificate.key_pem);
  auto client_tls = mux::net::client_tls();
  mux::net::trust(client_tls, certificate.certificate_pem);
  mux::net::listener listening(running);

  std::vector<request> heard;
  recorder seen{.running = &running};
  int syncs = 0;
  running.spawn([&] {
    for (;;) {
      auto socket = listening.accept();
      auto wire = std::make_shared<mux::net::stream>(running, server_tls, std::move(socket));
      running.spawn([&, wire] {
        if (!wire->accept_tls())
          return;
        std::string pending;
        for (auto it = wire->input().begin(); it != std::default_sentinel; ++it) {
          pending += *it;
          while (auto one = take(pending)) {
            heard.push_back(*one);
            std::string body = "{}";
            if (one->target.starts_with("/_matrix/client/v3/login")) {
              body = login;
            } else if (one->target.starts_with("/_matrix/client/v3/sync")) {
              ++syncs;
              if (one->target.find("since=") == std::string::npos)
                body = first_sync;
              else {
                body = R"({"next_batch":"s)" + std::to_string(syncs) + R"("})";
                if (syncs >= 3)
                  seen.account->stop();
              }
            } else if (one->target.find("/send/m.room.message/") != std::string::npos) {
              body = R"({"event_id":"$sent"})";
            }
            wire->write(answer(body));
            wire->flush();
          }
        }
      });
    }
  });

  mux::matrix::settings how{.user_id = "@a:x.org",
                            .password = "pw",
                            .homeserver = "https://127.0.0.1:" + std::to_string(listening.port()),
                            .sync_timeout = std::chrono::milliseconds(0)};
  seen.account.emplace(running, client_tls, how, sink{&seen});
  seen.account->start();
  running.run();
  const auto& states = seen.states;
  const auto& model = seen.model;

  EXPECT_EQ(states.front(), mux::connection::connecting);
  EXPECT_NE(std::find(states.begin(), states.end(), mux::connection::online), states.end());
  EXPECT_EQ(states.back(), mux::connection::offline);

  const mux::account_id me{mux::protocol::matrix, "@a:x.org"};
  const mux::conversation* room = model.find({me, "!r:x.org"});
  ASSERT_NE(room, nullptr);
  EXPECT_EQ(room->name, "Garden");
  EXPECT_EQ(room->unread, 2);
  EXPECT_EQ(room->typing, (std::vector<std::string>{"@b:x.org"}));
  ASSERT_GE(room->timeline.size(), 3u);
  // The message, edited in place; its HTML; the reaction on it.
  EXPECT_EQ(room->timeline[0].id, "$m1");
  EXPECT_EQ(room->timeline[0].body.plain, "hello there");
  EXPECT_TRUE(room->timeline[0].edited);
  EXPECT_EQ(room->timeline[0].reactions.at("👍"), (std::set<std::string>{"@a:x.org"}));
  // The reply, and what it replies to.
  EXPECT_EQ(room->timeline[1].id, "$m2");
  EXPECT_EQ(room->timeline[1].replies_to, "$m1");
  EXPECT_TRUE(room->timeline[1].outgoing);
  // Sent from here: known by its event id once the server answered.
  const auto sent_one = std::find_if(room->timeline.begin(), room->timeline.end(),
                                     [](const mux::message& one) { return one.body.plain == "from mux"; });
  ASSERT_NE(sent_one, room->timeline.end());
  EXPECT_EQ(sent_one->id, "$sent");
  EXPECT_EQ(sent_one->delivery, mux::delivery::sent);

  // What the client asked: login without a token, sync with one, the
  // message PUT with a transaction id.
  ASSERT_FALSE(heard.empty());
  EXPECT_TRUE(heard.front().target.starts_with("/_matrix/client/v3/login"));
  EXPECT_NE(heard.front().body.find("m.login.password"), std::string::npos);
  EXPECT_NE(heard.front().body.find("\"user\":\"a\""), std::string::npos) << heard.front().body;
  bool bearer = false, put = false;
  for (const auto& one : heard) {
    if (one.target.starts_with("/_matrix/client/v3/sync"))
      bearer = bearer || one.headers.find("Authorization: Bearer tok") != std::string::npos;
    if (one.method == "PUT" && one.target.find("/send/m.room.message/mux1") != std::string::npos)
      put = true;
  }
  EXPECT_TRUE(bearer);
  EXPECT_TRUE(put);
}

}  // namespace
