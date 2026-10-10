// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix.client against a homeserver played by a fiber of the same loop, over
// real TLS on the loopback: a certificate made for the test, trusted by the
// client and nothing else. Login, a first sync with a named room, members,
// a formatted message, a reply, an edit, a reaction and typing; a message
// sent and acknowledged; a later sync; stopped.
import std;
import mux.vault;
import splice;
import mux.core;
import mux.net;
import mux.proto.matrix.client;
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
   "content":{"m.relates_to":{"rel_type":"m.annotation","event_id":"$m1","key":"👍"}}},
  {"type":"m.room.member","state_key":"@renamed:x.org","event_id":"$renamed","sender":"@renamed:x.org","origin_server_ts":14,
   "content":{"membership":"join","displayname":"New"},
   "unsigned":{"prev_content":{"membership":"join","displayname":"Old <&>"}}}]},
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
  std::vector<mux::connection_t> states;
  bool sent = false;
  int room_updates = 0, member_updates = 0, typing_updates = 0;
  std::optional<mux::proto::matrix::client::account<sink>> account;
};

void sink::operator()(mux::change_t one) const {
  spl::visit(spl::overloaded{[&](const mux::change::connection_changed& changed) {
                               to->states.push_back(changed.state);
                               if (spl::visit(spl::overloaded{[](const mux::connection::offline&) { return true; },
                                                              [](const mux::connection::failed&) { return true; },
                                                              [](const auto&) { return false; }},
                                              changed.state))
                                 to->running->stop();
                             },
                             [](const auto&) {}},
             one);
  spl::visit(spl::overloaded{
      [&](const mux::change::conversation_updated&) { ++to->room_updates; },
      [&](const mux::change::members_changed&) { ++to->member_updates; },
      [&](const mux::change::typing_changed&) { ++to->typing_updates; },
      [](const auto&) {}}, one);
  // A message sent once the room is there.
  const bool room_there = spl::visit(spl::overloaded{[](const mux::change::conversation_updated&) { return true; },
                                                     [](const auto&) { return false; }},
                                     one);
  if (room_there && !to->sent) {
    to->sent = true;
    to->account->send("!r:x.org", "from mux");
    to->account->react("!r:x.org", "$m1", "mxc://remote/neocat", true, ":neocat:");
    to->account->react("!r:x.org", "$m1", "hi mxc://remote/neocat", true, "hi :neocat:");
    to->account->react("!r:x.org", "$m1", "👍", true);
    to->account->react("!r:x.org", "$m1", "mxc://remote/long", true, std::string(101, 'a'));
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
  int syncs = 0, reactions_sent = 0;
  std::array<int, 3> initial_updates{};
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
                if (syncs == 2) {
                  initial_updates = {seen.room_updates, seen.member_updates, seen.typing_updates};
                  body = R"({"next_batch":"s2","rooms":{"join":{"!r:x.org":{
                    "ephemeral":{"events":[{"type":"m.receipt","content":{
                      "$m1":{"m.read":{"@b:x.org":{"ts":15}}}}}]}}}}})";
                }
                if (syncs >= 3 && reactions_sent == 4)
                  seen.account->stop();
              }
            } else if (one->target.find("/send/m.reaction/") != std::string::npos) {
              ++reactions_sent;
              body = R"({"event_id":"$reaction)" + std::to_string(reactions_sent) + R"("})";
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

  mux::vault::vault vault;
  mux::proto::matrix::client::settings how{.user_id = "@a:x.org",
                            .password = "pw",
                            .homeserver = "https://127.0.0.1:" + std::to_string(listening.port()),
                            .sync_timeout = std::chrono::milliseconds(0),
                            .vault = &vault};
  seen.account.emplace(running, client_tls, how, sink{&seen});
  seen.account->start();
  running.run();
  EXPECT_EQ(seen.room_updates, initial_updates[0]);
  EXPECT_EQ(seen.member_updates, initial_updates[1]);
  EXPECT_EQ(seen.typing_updates, initial_updates[2]);
  const auto& states = seen.states;
  const auto& model = seen.model;

  EXPECT_EQ(states.front(), mux::connection_t{mux::connection::connecting{}});
  EXPECT_NE(std::find(states.begin(), states.end(), mux::connection_t{mux::connection::online{}}), states.end());
  EXPECT_EQ(states.back(), mux::connection_t{mux::connection::offline{}});

  const mux::account_id me{mux::protocol::matrix{}, "@a:x.org"};
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
  // Historical names remain literal text even when the person pill resolves
  // to the current member name; HTML must escape the previous content.
  const auto renamed = std::ranges::find(room->timeline, "$renamed", &mux::message::id);
  ASSERT_NE(renamed, room->timeline.end());
  EXPECT_EQ(renamed->body.plain, "New changed their display name from Old <&> to New");
  ASSERT_TRUE(renamed->body.html.has_value());
  EXPECT_NE(renamed->body.html->find(" changed their display name from Old &lt;&amp;&gt; to New"), std::string::npos);
  // Sent from here: known by its event id once the server answered.
  const auto sent_one = std::find_if(room->timeline.begin(), room->timeline.end(),
                                     [](const mux::message& one) { return one.body.plain == "from mux"; });
  ASSERT_NE(sent_one, room->timeline.end());
  EXPECT_EQ(sent_one->id, "$sent");
  EXPECT_EQ(sent_one->delivery, mux::delivery_t{mux::delivery::sent{}});

  // The wire keeps media identities separate from both MSC4027 label names.
  const auto reactions = std::ranges::to<std::vector>(std::views::filter(heard, [](const auto& one) {
    return one.target.find("/send/m.reaction/") != std::string::npos;
  }));
  ASSERT_EQ(reactions.size(), 4u);
  for (const auto& one : reactions) {
    if (one.body.find("mxc://remote/neocat") != std::string::npos) {
      EXPECT_NE(one.body.find("\"shortcode\":"), std::string::npos);
      EXPECT_NE(one.body.find("\"com.beeper.reaction.shortcode\":"), std::string::npos);
      EXPECT_NE(one.body.find(":neocat:"), std::string::npos);
    } else {
      EXPECT_EQ(one.body.find("shortcode"), std::string::npos);
    }
  }

  // What the client asked: login without a token, sync with one, the
  // message PUT with a transaction id -- one of this run's own, "mux-<when
  // the run began>-<n>", never the "mux1" every run used to start from: a
  // server answers a repeated id with the event it made the first time.
  ASSERT_FALSE(heard.empty());
  EXPECT_TRUE(heard.front().target.starts_with("/_matrix/client/v3/login"));
  EXPECT_NE(heard.front().body.find("m.login.password"), std::string::npos);
  EXPECT_NE(heard.front().body.find("\"user\":\"a\""), std::string::npos) << heard.front().body;
  bool bearer = false, put = false;
  for (const auto& one : heard) {
    if (one.target.starts_with("/_matrix/client/v3/sync"))
      bearer = bearer || one.headers.find("Authorization: Bearer tok") != std::string::npos;
    if (const auto at = one.target.find("/send/m.room.message/mux-");
        one.method == "PUT" && at != std::string::npos &&
        one.target.find("/send/m.room.message/mux1") == std::string::npos)
      put = true;
  }
  EXPECT_TRUE(bearer);
  EXPECT_TRUE(put);
}

struct media_sink {
  bool* online;
  std::vector<mux::change::avatar_loaded>* pictures;
  int* acknowledged;
  void operator()(mux::change_t one) const {
    spl::visit(spl::overloaded{
        [&](const mux::change::connection_changed& change) {
          *online = change.state == mux::connection_t{mux::connection::online{}};
        },
        [&](const mux::change::avatar_loaded& picture) { pictures->push_back(picture); },
        [&](const mux::change::message_acknowledged&) { ++*acknowledged; },
        [](const auto&) {}}, one);
  }
};

TEST(Matrix, MediaFallsBackToOriginalsAndReusesUploads) {
  const auto certificate = mux::test::self_signed();
  mux::net::loop running;
  auto server_tls = mux::net::server_tls(certificate.certificate_pem, certificate.key_pem);
  auto client_tls = mux::net::client_tls();
  mux::net::trust(client_tls, certificate.certificate_pem);
  mux::net::listener listening(running);
  bool online = false, finished = false;
  int acknowledged = 0;
  std::vector<request> heard;
  std::vector<mux::change::avatar_loaded> pictures;
  mux::vault::vault vault;
  mux::proto::matrix::client::settings how{.user_id = "@a:x.org", .password = "pw",
      .homeserver = "https://127.0.0.1:" + std::to_string(listening.port()),
      .sync_timeout = std::chrono::milliseconds(0), .vault = &vault};
  mux::proto::matrix::client::account account(running, client_tls, how, media_sink{&online, &pictures, &acknowledged});
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
            if (one->target.starts_with("/_matrix/client/v3/login"))
              body = login;
            else if (one->target.starts_with("/_matrix/client/v3/sync")) {
              running.sleep(std::chrono::milliseconds(10));
              body = R"({"next_batch":"s1"})";
            } else if (one->target.contains("/thumbnail/x.org/missing?")) {
              wire->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
              wire->flush();
              continue;
            } else if (one->target.contains("/thumbnail/x.org/ready?"))
              body = "server thumbnail";
            else if (one->target.contains("/download/x.org/missing"))
              body = "original image";
            else if (one->target.contains("/upload?filename=")) {
              const auto name = one->target.substr(one->target.find("filename=") + 9);
              body = "{\"content_uri\":\"mxc://x.org/" + name + "\"}";
            } else if (one->target.contains("/send/m.room.message/"))
              body = "{\"event_id\":\"$" + one->target.substr(one->target.rfind('/') + 1) + "\"}";
            wire->write(answer(body));
            wire->flush();
          }
        }
      });
    }
  });
  // Bound failures too: the old implementation never delivers the missing thumbnail.
  running.spawn([&] { running.sleep(std::chrono::seconds(5)); running.stop(); });
  account.start();
  running.spawn([&] {
    while (!online)
      running.sleep(std::chrono::milliseconds(1));
    account.fetch_media("mxc://x.org/missing", mux::media_use::thumbnail{}, 860);
    account.fetch_media("mxc://x.org/ready", mux::media_use::thumbnail{}, 860);
    account.send_file("!r:x.org", "image", "uploaded image", "image.png", "image/png", true, 2, 2, "");
    account.send_file("!r:x.org", "video", "uploaded video", "video.mp4", "video/mp4", false, 2, 2, "",
                      std::nullopt, std::nullopt, mux::video_look{1000, "video thumbnail", 2, 2});
    while (pictures.size() < 4 || acknowledged < 2)
      running.sleep(std::chrono::milliseconds(1));
    finished = true;
    account.stop();
    running.stop();
  });
  running.run();
  ASSERT_TRUE(finished);
  const auto bytes = [&](std::string_view source) {
    const auto found = std::ranges::find(pictures, source, &mux::change::avatar_loaded::source);
    return found == pictures.end() ? std::string() : found->bytes;
  };
  EXPECT_EQ(bytes("mxc://x.org/missing"), "original image");
  EXPECT_EQ(bytes("mxc://x.org/ready"), "server thumbnail");
  EXPECT_EQ(bytes("mxc://x.org/image.png"), "uploaded image");
  EXPECT_EQ(bytes("mxc://x.org/thumbnail.png"), "video thumbnail");
  EXPECT_FALSE(std::ranges::any_of(heard, [](const request& one) {
    return one.target.contains("/download/x.org/ready") ||
           (one.method == "GET" && (one.target.contains("image.png") || one.target.contains("thumbnail.png")));
  }));
}

}  // namespace
