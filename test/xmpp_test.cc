// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.xmpp.client against a scripted server on the loopback: connected, the roster
// read into conversations, a chat message with its delay, presence, a
// roster push, and the end of the stream -- all as mux.core's changes.
import std;
import splice;
import mux.core;
import mux.net;
import mux.proto.xmpp.client;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

std::string server_header(std::string_view id) {
  return "<?xml version='1.0'?><stream:stream xmlns='jabber:client' xmlns:stream='http://etherx.jabber.org/streams' "
         "id='" + std::string(id) + "' from='example.com' version='1.0'>";
}

const std::string script =
    server_header("s1") +
    "<stream:features><mechanisms xmlns='urn:ietf:params:xml:ns:xmpp-sasl'>"
    "<mechanism>PLAIN</mechanism></mechanisms></stream:features>"
    "<success xmlns='urn:ietf:params:xml:ns:xmpp-sasl'/>" + server_header("s2") +
    "<stream:features><bind xmlns='urn:ietf:params:xml:ns:xmpp-bind'/></stream:features>"
    "<iq type='result' id='bind_1'><bind xmlns='urn:ietf:params:xml:ns:xmpp-bind'>"
    "<jid>user@example.com/mux</jid></bind></iq>"
    // The server's disco#info, asked first: no archive (XEP-0313).
    "<iq type='result' id='tern-1' from='example.com'><query xmlns='http://jabber.org/protocol/disco#info'>"
    "<identity category='server' type='im'/></query></iq>"
    "<iq type='result' id='tern-2'><query xmlns='jabber:iq:roster'>"
    "<item jid='nurse@example.com' subscription='both' name='Nurse'/>"
    "<item jid='romeo@example.net' subscription='both'/></query></iq>"
    "<iq type='result' id='tern-3'><pubsub xmlns='http://jabber.org/protocol/pubsub'>"
    "<items node='urn:xmpp:bookmarks:1'><item id='balcony@rooms.example.com'>"
    "<conference xmlns='urn:xmpp:bookmarks:1' name='The Balcony' autojoin='true'><nick>user</nick></conference>"
    "</item></items></pubsub></iq>"
    "<presence from='balcony@rooms.example.com/juliet'><x xmlns='http://jabber.org/protocol/muc#user'>"
    "<item affiliation='owner' role='moderator'/></x></presence>"
    "<presence from='balcony@rooms.example.com/user'><x xmlns='http://jabber.org/protocol/muc#user'>"
    "<item affiliation='member' role='participant'/><status code='110'/></x></presence>"
    "<message from='balcony@rooms.example.com/juliet' type='groupchat' id='g1'>"
    "<body>Wherefore art thou?</body></message>"
    "<message from='romeo@example.net/orchard' to='user@example.com/mux' type='chat' id='m1'>"
    "<body>Shall I hear more?</body>"
    "<delay xmlns='urn:xmpp:delay' stamp='2026-09-26T17:00:00Z'/></message>"
    "<presence from='romeo@example.net/orchard'><show>away</show><status>below the balcony</status></presence>"
    "<iq type='set' id='push1'><query xmlns='jabber:iq:roster'>"
    "<item jid='juliet@example.com' subscription='to'/></query></iq>"
    "</stream:stream>";

TEST(Xmpp, AScriptedSession) {
  mux::net::loop running;
  auto tls = mux::net::client_tls();
  mux::net::listener server(running);
  std::string heard;
  running.spawn([&] {
    mux::net::stream wire(running, tls, server.accept());
    wire.write(script);
    wire.flush();
    for (auto it = wire.input().begin(); it != std::default_sentinel; ++it)
      heard += *it;
  });
  std::vector<mux::change_t> said;
  mux::model model;
  auto sink = [&](mux::change_t one) {
    model.apply(one);
    said.push_back(std::move(one));
  };
  mux::proto::xmpp::client::account account(running, tls,
                             {.address = "user@example.com",
                              .password = "pencil",
                              .resource = "mux",
                              .host = "127.0.0.1",
                              .port = server.port(),
                              .plain_without_tls = true},
                             sink);
  account.start();
  running.run();

  const mux::account_id me{mux::protocol::xmpp{}, "user@example.com"};
  // Connecting, online, and offline at the end of the stream.
  std::vector<mux::connection_t> states;
  for (const auto& one : said)
    spl::visit(spl::overloaded{[&](const mux::change::connection_changed& changed) { states.push_back(changed.state); },
                               [](const auto&) {}},
               one);
  EXPECT_EQ(states, (std::vector<mux::connection_t>{mux::connection::connecting{}, mux::connection::online{},
                                                    mux::connection::offline{}}));
  ASSERT_TRUE(model.accounts().contains(me));
  const mux::account& kept = model.accounts().at(me);
  // The roster, and the push that added Juliet.
  ASSERT_TRUE(kept.conversations.contains("nurse@example.com"));
  EXPECT_EQ(kept.conversations.at("nurse@example.com").name, "Nurse");
  EXPECT_TRUE(kept.conversations.contains("juliet@example.com"));
  // Romeo's message, in his conversation, at the time it was first sent.
  const mux::conversation* romeo = model.find({me, "romeo@example.net"});
  ASSERT_NE(romeo, nullptr);
  ASSERT_EQ(romeo->timeline.size(), 1u);
  EXPECT_EQ(romeo->timeline[0].body.plain, "Shall I hear more?");
  EXPECT_EQ(romeo->timeline[0].id, "m1");
  EXPECT_EQ(std::format("{:%FT%T}", romeo->timeline[0].at), "2026-09-26T17:00:00.000");
  // His presence.
  ASSERT_TRUE(kept.presences.contains("romeo@example.net"));
  EXPECT_EQ(kept.presences.at("romeo@example.net").state, mux::availability_t{mux::availability::away{}});
  EXPECT_EQ(kept.presences.at("romeo@example.net").status, "below the balcony");
  // The room bookmarked: in the list, joined, its occupants its members,
  // and Juliet's message in it, from her.
  const mux::conversation* balcony = model.find({me, "balcony@rooms.example.com"});
  ASSERT_NE(balcony, nullptr);
  EXPECT_EQ(balcony->name, "The Balcony");
  ASSERT_EQ(balcony->members.size(), 2u);
  EXPECT_EQ(balcony->members[0].name, "juliet");
  EXPECT_EQ(balcony->members[0].role, "owner");
  EXPECT_EQ(balcony->members[1].name, "user");
  ASSERT_EQ(balcony->timeline.size(), 1u);
  EXPECT_EQ(balcony->timeline[0].sender, "balcony@rooms.example.com/juliet");
  EXPECT_EQ(balcony->timeline[0].body.plain, "Wherefore art thou?");
  EXPECT_FALSE(balcony->timeline[0].outgoing);
  EXPECT_NE(heard.find("urn:xmpp:bookmarks:1"), std::string::npos) << heard;
  EXPECT_NE(heard.find("http://jabber.org/protocol/muc"), std::string::npos) << heard;
  EXPECT_NE(heard.find("balcony@rooms.example.com/user"), std::string::npos) << heard;
  // What the client said: PLAIN, the roster asked for, the push answered,
  // presence.
  EXPECT_NE(heard.find("mechanism='PLAIN'"), std::string::npos) << heard;
  EXPECT_NE(heard.find("jabber:iq:roster"), std::string::npos) << heard;
  EXPECT_NE(heard.find("id=\"push1\""), std::string::npos) << heard;
  EXPECT_NE(heard.find("<presence"), std::string::npos) << heard;
}

}  // namespace
