// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix.client:base -- Settings, and reading values out of events.
export module mux.proto.matrix.client:base;

import std;
import mux.vault;
import knot;
import loom.api;
import loom.ev;
import loom.state;
import loom.cs.joining;
import loom.cs.leaving;
import loom.cs.login;
import loom.cs.message_pagination;
import loom.cs.receipts;
import loom.cs.redaction;
import loom.cs.room_send;
import loom.cs.rooms;
import loom.cs.sync;
import loom.cs.typing;
import loom.cs.wellknown;
import mux.config;
import mux.core;
import mux.http;
import mux.net;

export namespace mux::proto::matrix::client {

// Matrix's room rules and levels, as its protocol's state module has them
// (mux.proto.matrix.state): named here as before.
using proto::matrix::history_rule_t;
using proto::matrix::join_rule_t;
using proto::matrix::kCreatorPower;
using proto::matrix::power_need_t;
using proto::matrix::power_needs;
using proto::matrix::sends_state;
namespace join_rule = proto::matrix::join_rule;
namespace history_rule = proto::matrix::history_rule;
namespace power_need = proto::matrix::power_need;

struct settings {
  std::string user_id;  // @user:example.org
  std::string password;
  // The client-server API's base URL, instead of what the server name's
  // .well-known says.
  std::optional<std::string> homeserver;
  std::string device_name = "mux";
  // A proxy to connect through, where there is one.
  std::optional<net::proxy> proxy;
  // The session kept from before: logged in with, rather than logging in
  // again -- a new device each time -- where it is still good.
  std::optional<std::string> access_token;
  std::optional<std::string> device_id;
  // How long a sync waits on the server for something to happen.
  std::chrono::milliseconds sync_timeout = std::chrono::seconds(30);
  // Where its end-to-end encryption is kept: this device's Olm account and
  // sessions, and the rooms' Megolm sessions. None: no encryption.
  std::filesystem::path crypto_store;
  // Room keys only to sessions verified -- cross-signed by their owner, or
  // by emoji here: Element's "Never send encrypted messages to unverified
  // sessions".
  bool only_verified = false;
  // What is kept on disk is read and written through: the program's.
  mux::vault::vault* vault = nullptr;
};

// A typed content as a request's body: its JSON text.
inline knot::raw as_body(const auto& content) { return knot::raw{knot::to_json_string(content)}; }


}  // namespace mux::proto::matrix::client
