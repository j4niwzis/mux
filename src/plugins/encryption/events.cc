// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.encryption.matrix:events -- Encrypted events read, and those waiting for their keys: from the backup, from this user's other sessions.
export module mux.plugin.encryption.matrix:events;

import mux.vault;
import std;
import loom.crypto;
import splice;
import knot;
import loom.cs.sliding_sync;
import loom.cs.versions;
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
import splice.bytes;
import loom.cs.account_data;
import loom.media;
import chevron.escape;
import mux.logic.forwarding;
import loom.cs.key_backup;
import loom.cs.keys;
import loom.cs.event_context;
import loom.cs.users;
import loom.cs.relations;
import loom.cs.threads_list;
import loom.cs.room_summary;
import loom.cs.list_public_rooms;
import loom.cs.space_hierarchy;
import loom.cs.cross_signing;
import loom.cs.room_state;
import loom.cs.content_repo;
import loom.cs.authed_content_repo;
import loom.cs.create_room;
import loom.cs.kicking;
import loom.cs.banning;
import loom.cs.inviting;
import loom.cs.profile;
import loom.cs.presence;
import loom.cs.device_management;
import loom.cs.pusher;
import mux.logic.markdown;
import loom.names;
import loom.cs.voip;
import mux.logic.reactions;
import loom.cs.to_device;
import loom.cs.room_upgrades;
import loom.cs.knocking;
import mux.composition;
import mux.proto.matrix.requests;
import mux.proto.matrix.client;
import mux.plugin.encryption.types;
import :account;

// The members defined here are declared in :account.
namespace mux::proto::matrix::client {

template <class Sink>
void encrypted_account<Sink>::encrypted(const conversation_id& in, const loom::ev::timeline_event& one,
                 std::chrono::sys_time<std::chrono::milliseconds> at, placement_t where) {
  // An encrypted event seen: the room is one, whatever its state says --
  // and was, at the latest, when this one was sent.
  this->remember_encrypted(in.id);
  // Only noted: when it began to be is not wanted here.
  std::ignore = this->encrypted_by(in.id, std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::milliseconds(one.origin_server_ts)));
  // Read with the room's Megolm session, where this device has it: the event
  // it was, its type and content, as any event is read -- the rest of it,
  // who sent it and when, the encrypted one's.
  if (crypto_) {
    std::optional<crypto::decrypted> clear;
    try {
      spl::visit(spl::overloaded{[&](const loom::ev::m_room_encrypted_content_t& content) {
                                         clear = crypto_->room_event(in.id, one.event_id, one.sender, content);
                                       },
                                       [](const auto&) {}},
                    one.content.data());
    } catch (const std::exception& failed) {
      log(id_, "encryption stopped: {}", failed.what());
      crypto_.reset();
    }
    // Read with an imported session: shown only where the sender has a
    // device with the key the session came from -- else anyone's name.
    if (clear && clear->imported_sender_key && !this->owns_key(one.sender, *clear->imported_sender_key))
      clear.reset();
    if (clear) {
      loom::ev::timeline_event made = one;
      made.type = std::move(clear->event.type);
      made.content = std::move(clear->event.content);
      spl::visit(spl::overloaded{[&](const loom::ev::m_room_encrypted_content_t& content) {
                                         if (content.m_relates_to)
                                           outer_reference_ = content.m_relates_to->event_id;
                                       },
                                       [](const auto&) {}},
                    one.content.data());
      // The relation an encrypted event carries in the clear -- an edit's,
      // as Element sends one in an encrypted room: m.relates_to outside what
      // is sealed -- put on the message it decrypted to, where that says
      // none of its own. Else the edit was a message of its own, shown
      // beside the one it edited, the same words twice (a tester's).
      spl::visit(spl::overloaded{[&](const loom::ev::m_room_encrypted_content_t& outer) {
                                   if (!outer.m_relates_to)
                                     return;
                                   spl::visit(spl::overloaded{[&](loom::ev::m_room_message_content_t& inner) {
                                                                if (inner.m_relates_to)
                                                                  return;
                                                                using relation = std::remove_cvref_t<decltype(*inner.m_relates_to)>;
                                                                if (auto taken = knot::convert<relation>(*outer.m_relates_to))
                                                                  inner.m_relates_to = std::move(*taken);
                                                              },
                                                              [](auto&) {}},
                                              made.content.data());
                                 },
                                 [](const auto&) {}},
                  one.content.data());
      // Gone however the event's reading ends -- thrown out of too: else the
      // next one read would take this one's reference.
      struct forget_reference {
        std::optional<std::string>& kept;
        ~forget_reference() { kept.reset(); }
      } const forgetting{outer_reference_};
      this->event(in, made, where, true);
      sink_(change::message_encrypted{in, one.event_id, clear->verified, clear->imported_sender_key.has_value()});
      return;
    }
  }
  // Not readable here (yet): said so -- in Element's words, why, where the
  // sender said why its key is withheld; else waiting for it, which may come
  // (a room key, the backup). Kept by its session, to be said again if the
  // reason comes after it.
  std::optional<std::string> session;
  spl::visit(spl::overloaded{[&](const loom::ev::m_room_encrypted_content_t& content) { session = content.session_id; },
                                   [](const auto&) {}},
                one.content.data());
  message waiting{.in = in,
                  .id = one.event_id,
                  .sender = one.sender,
                  .at = at,
                  .body = {"🔒 Waiting for this message, this may take a while.", std::nullopt},
                  // Encrypted, not yet read: never "not encrypted", which it
                  // was marked as every plain message in the room is.
                  .encrypted = true,
                  .outgoing = one.sender == id_.address};
  if (session) {
    if (const auto why = withheld_.find(*session); why != withheld_.end())
      waiting.body = {why->second, std::nullopt};
    else
      this->key_from_backup(in.id, *session);
    auto& kept = undecrypted_[*session];
    if (kept.size() < 200 && std::ranges::none_of(kept, [&](const undecrypted_event& each) { return each.event.event_id == one.event_id; }))
      kept.push_back({in, one, where});
  }
  sink_(change::message_added{std::move(waiting), where});
}

template <class Sink>
void encrypted_account<Sink>::key_from_backup(std::string room, std::string session) {
  if (!crypto_ || !backup_asked_.insert(session).second)
    return;
  this->spawn_guarded([this, room = std::move(room), session = std::move(session)] {
    if (!api_ || !crypto_)
      return;
    const auto kept = crypto_->backup();
    if (!kept) {
      this->ask_own_sessions(room, session);
      return;
    }
    auto got = perform(*api_, loom::cs::get_room_key_by_session_id{.version = kept->first, .room_id = room, .session_id = session},
                       std::chrono::seconds(30), http::lane::background);
    if (!got) {
      this->ask_own_sessions(room, session);
      return;
    }
    const auto& said = got->session_data;
    const auto plain = said.ephemeral && said.ciphertext && said.mac
                           ? crypto::open_backup(kept->second, crypto::backup_session_data{.ephemeral = *said.ephemeral,
                                                                                            .ciphertext = *said.ciphertext,
                                                                                            .mac = *said.mac})
                           : std::nullopt;
    if (!plain)
      return;
    const std::vector<crypto::exported_session> sessions{crypto::exported_session{.room_id = room,
                                                                                  .sender_key = plain->sender_key,
                                                                                  .sender_claimed_keys = plain->sender_claimed_keys,
                                                                                  .session_id = session,
                                                                                  .session_key = plain->session_key}};
    if (crypto_->import_sessions(sessions) > 0) {
      log(id_, "a room key from the backup, for messages waiting on it");
      this->decrypt_waiting(session);
    }
  });
}

template <class Sink>
void encrypted_account<Sink>::ask_own_sessions(const std::string& room, const std::string& session) {
  if (!crypto_ || !keys_requested_.insert(session).second)
    return;
  loom::ev::m_room_key_request_content_t asked;
  asked.body = loom::ev::m_room_key_request_content_t::requested_key_info_t{
      .algorithm = "m.megolm.v1.aes-sha2", .room_id = room, .sender_key = std::nullopt, .session_id = session};
  asked.action = loom::ev::m_room_key_request_content_t::action_values::request_{};
  asked.requesting_device_id = crypto_->device_id();
  asked.request_id = this->transaction();
  // To every session of this user's: in the clear, as key requests go.
  if (this->send_plain("m.room_key_request", id_.address, "*", knot::raw{knot::to_json_string(asked)}))
    log(id_, "a room key asked of your other sessions, for messages waiting on it");
}

template <class Sink>
void encrypted_account<Sink>::forwarded_in(const crypto::forwarded_key_got& got) {
  // Asked for here, and from this user: else a key anyone sends is not taken.
  if (got.sender != id_.address || !keys_requested_.contains(got.key.session_id) || !api_)
    return;
  // From a session of theirs this one trusts: cross-signed, or verified
  // here by comparing emoji -- the to-device sender is the server's word.
  auto listed = perform(*api_, loom::cs::query_keys{.body = {.device_keys = {{id_.address, {}}}}});
  if (!listed)
    return;
  const auto device = crypto::device_of(*listed, id_.address, got.sender_key, crypto_->pinned_master(id_.address));
  if (!device || device->ed25519 != got.ed25519 ||
      !(device->cross_signed || std::ranges::contains(crypto_->verified_keys(id_.address), device->ed25519))) {
    log(id_, "a forwarded room key refused: not from a session of yours trusted here");
    return;
  }
  if (!crypto_->accept_forwarded(got))
    return;
  keys_requested_.erase(got.key.session_id);
  log(id_, "a room key from your session {}, for messages waiting on it", device->device_id);
  this->decrypt_waiting(got.key.session_id);
}

template <class Sink>
void encrypted_account<Sink>::decrypt_waiting(const std::string& session) {
  const auto found = undecrypted_.find(session);
  if (found == undecrypted_.end())
    return;
  const auto waiting = std::move(found->second);
  undecrypted_.erase(found);
  for (const undecrypted_event& one : waiting) {
    // News when it came: news still, to a plugin that hears it -- a call's
    // invite says, by its lifetime, whether it is too late.
    const late_news_while news{late_news_, placed_as_news(one.where)};
    this->event(one.in, one.event,
                spl::visit(spl::overloaded{[](placement::aside aside) -> placement_t { return aside; },
                                                 [](const placement::in_window& window) -> placement_t { return window; },
                                                 [](const auto&) -> placement_t { return placement::in_window{}; }},
                              one.where));
  }
}

template <class Sink>
void encrypted_account<Sink>::decrypt_all_waiting() {
  const auto sessions = undecrypted_ | std::views::keys | std::ranges::to<std::vector<std::string>>();
  for (const std::string& session : sessions)
    this->decrypt_waiting(session);
}

}  // namespace mux::proto::matrix::client
