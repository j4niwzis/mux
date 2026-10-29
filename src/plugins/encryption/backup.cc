// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.encryption.matrix:backup -- Cross-signing set up and restored, the key backup made, read and reset, secret storage, unverified sessions signed out.
export module mux.plugin.encryption.matrix:backup;

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
import mux.logic.formatting;
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
import mux.plugin.encryption.changes;
import :account;

// The members defined here are declared in :account.
namespace mux::proto::matrix::client {
// The password, as interactive auth takes it, for uploading cross-signing
// keys (the client's own is its sign-out's).
struct cross_signing_password {
  struct identifier_t {
    std::string type = "m.id.user";
    std::string user;
    friend consteval auto json_schema(knot::type<identifier_t>) { return knot::schema<identifier_t>(); }
  };
  identifier_t identifier;
  std::string password;
  friend consteval auto json_schema(knot::type<cross_signing_password>) { return knot::schema<cross_signing_password>(); }
};

template <class Sink>
void encrypted_account<Sink>::setup_cross_signing(std::string password, bool reset) {
  this->spawn_guarded([this, password = std::move(password), reset] {
    if (!crypto_ || !api_) {
      sink_(change::refused{id_, "Not set up: encryption is not running for this account."});
      return;
    }
    // Already set up -- by another device, another client: never replaced
    // from here. A new identity would undo every verification of it, and
    // whoever had verified the account would see it change.
    {
      auto got = this->keys_of(id_.address);
      if (!got) {
        sink_(change::refused{id_, "Not set up: this account's keys could not be fetched: " + got.error().said()});
        return;
      }
      // Unless the identity is being reset, by the user's word.
      if (!reset && crypto::master_of(*got, id_.address)) {
        sink_(change::refused{id_, "This account has cross-signing already. Use Restore with the recovery key to give "
                                   "this device its keys, or verify this device from one that has them."});
        return;
      }
    }
    using upload = loom::cs::upload_cross_signing_keys;
    using master_t = upload::body_t::cross_signing_key_t;
    using self_t = upload::body_t::cross_signing_key_2_t;
    using user_t = upload::body_t::cross_signing_key_3_t;
    const auto secrets = crypto::new_cross_signing();
    const auto master_pub = crypto::detail::ed25519_public(secrets.master);
    const auto self_pub = crypto::detail::ed25519_public(secrets.self_signing);
    const auto user_pub = crypto::detail::ed25519_public(secrets.user_signing);
    if (!master_pub || !self_pub || !user_pub) {
      sink_(change::refused{id_, "Not set up: the keys could not be made."});
      return;
    }
    const auto signatures_raw = [&](const std::string& key_id, const std::string& signature) {
      return knot::raw{knot::to_json_string(crypto::signatures_t{{id_.address, {{key_id, signature}}}})};
    };
    // Each key signed: the master by this device, the other two by the master.
    master_t master{.user_id = id_.address, .usage = {master_t::usage_item_values::master{}}, .keys = {{"ed25519:" + *master_pub, *master_pub}}};
    self_t self{.user_id = id_.address, .usage = {self_t::usage_item_values::self_signing{}}, .keys = {{"ed25519:" + *self_pub, *self_pub}}};
    user_t users{.user_id = id_.address, .usage = {user_t::usage_item_values::user_signing{}}, .keys = {{"ed25519:" + *user_pub, *user_pub}}};
    const auto master_canonical = knot::to_canonical_json(crypto::key_signed_part<master_t>{master.user_id, master.usage, master.keys, {}});
    const auto self_canonical = knot::to_canonical_json(crypto::key_signed_part<self_t>{self.user_id, self.usage, self.keys, {}});
    const auto user_canonical = knot::to_canonical_json(crypto::key_signed_part<user_t>{users.user_id, users.usage, users.keys, {}});
    if (!master_canonical || !self_canonical || !user_canonical)
      return;
    master.signatures = signatures_raw("ed25519:" + crypto_->device_id(), crypto_->sign_as_device(*master_canonical));
    self.signatures = signatures_raw("ed25519:" + *master_pub, crypto::detail::ed25519_sign(secrets.master, *self_canonical).value_or(""));
    users.signatures = signatures_raw("ed25519:" + *master_pub, crypto::detail::ed25519_sign(secrets.master, *user_canonical).value_or(""));
    upload::body_t body{.master_key = master, .self_signing_key = self, .user_signing_key = users};
    auto first = perform(*api_, upload{.body = body});
    if (!first) {
      const auto& said = first.error().server;
      if (!said || said->status != 401 || !said->session) {
        sink_(change::refused{id_, "Cross-signing not set up: " + first.error().said()});
        return;
      }
      const std::string given = !password.empty() ? password : how_.password;
      const bool asked_elsewhere = spl::visit(
          spl::overloaded{[](const uia_password&) { return false; },
                          [&](const uia_browser& page) {
                            uia_ = pending_uia{*said->session, uia_whole{}};
                            uia_cross_ = uia_cross_signing{secrets, body, *master_pub, *self_pub};
                            sink_(proto::matrix::uia_in_browser{id_, page.url, "Set up cross-signing"});
                            return true;
                          },
                          [&](const uia_none&) {
                            sink_(change::refused{id_, "Cross-signing not set up: your password is needed."});
                            return true;
                          }},
          this->uia_answer(*said, given));
      if (asked_elsewhere)
        return;
      upload::body_t::authentication_data_t auth{.type = "m.login.password", .session = *said->session};
      auth.rest = as_body(cross_signing_password{.identifier = {.user = how_.user_id}, .password = given});
      body.auth = std::move(auth);
      if (auto done = perform(*api_, upload{.body = body}); !done) {
        sink_(change::refused{id_, "Cross-signing not set up: " + done.error().said()});
        return;
      }
    }
    this->finish_cross_signing(secrets, *master_pub, *self_pub);
  });
}

template <class Sink>
void encrypted_account<Sink>::continue_uia(const std::string& session) {
  if (!uia_cross_)
    return;
  auto one = std::move(*std::exchange(uia_cross_, std::nullopt));
  using upload = loom::cs::upload_cross_signing_keys;
  one.body.auth = upload::body_t::authentication_data_t{.session = session};
  if (auto done = perform(*api_, upload{.body = one.body}); !done) {
    sink_(change::refused{id_, "Cross-signing not set up: " + done.error().said()});
    return;
  }
  this->finish_cross_signing(one.secrets, one.master_pub, one.self_pub);
}

template <class Sink>
void encrypted_account<Sink>::finish_cross_signing(const crypto::cross_signing_secrets& secrets, const std::string& master_pub,
                                         const std::string& self_pub) {
  crypto_->keep_cross_signing(secrets);
  crypto_->verify_master(id_.address, master_pub);
  // This device, signed with the self-signing key.
  if (const auto own = crypto_->signed_device_keys()) {
    const auto canonical = knot::to_canonical_json(own->keys);
    const auto by_self = canonical ? crypto::detail::ed25519_sign(secrets.self_signing, *canonical) : std::nullopt;
    if (by_self) {
      const crypto::signed_device_part signed_one{.algorithms = own->keys.algorithms,
                                                  .device_id = own->keys.device_id,
                                                  .keys = own->keys.keys,
                                                  .user_id = own->keys.user_id,
                                                  .signatures = {{id_.address, {{"ed25519:" + self_pub, *by_self}}}}};
      std::map<std::string, std::map<std::string, knot::raw>> signed_body;
      signed_body[id_.address][own->keys.device_id] = knot::raw{knot::to_json_string(signed_one)};
      (void)perform(*api_, loom::cs::upload_cross_signing_signatures{.body = std::move(signed_body)});
    }
  }
  const auto backup_secret = this->make_backup(secrets);
  const auto recovery = this->store_secrets(secrets, backup_secret);
  sink_(change::notice{
      id_, "Cross-signing set up",
      recovery ? std::format("This account now has its own cross-signing keys. They are kept on this device, and on "
                             "your server sealed under this recovery key -- write it down and keep it safe: with it, "
                             "another device takes them back; without it, they are lost with this device.\n\n{}",
                             *recovery)
               : std::string("This account now has its own cross-signing keys, kept on this device only: they could "
                             "not be put in secret storage.")});
}

template <class Sink>
std::optional<std::string> encrypted_account<Sink>::make_backup(const crypto::cross_signing_secrets& secrets) {
  const auto [secret, public_key] = crypto::new_backup_key();
  const auto canonical = knot::to_canonical_json(crypto::backup_auth_signed_part{public_key});
  const auto master_pub = crypto::detail::ed25519_public(secrets.master);
  const auto by_master = canonical ? crypto::detail::ed25519_sign(secrets.master, *canonical) : std::nullopt;
  if (!canonical || !master_pub || !by_master)
    return std::nullopt;
  using version_t = loom::cs::post_room_keys_version;
  const version_t::body_t::auth_data_t auth{
      .public_key = public_key,
      .signatures = std::map<std::string, std::map<std::string, std::string>>{
          {id_.address,
           {{"ed25519:" + crypto_->device_id(), crypto_->sign_as_device(*canonical)}, {"ed25519:" + *master_pub, *by_master}}}}};
  auto made = perform(*api_, version_t{.body = {.algorithm = version_t::body_t::algorithm_values::m_megolm_backup_v1_curve25519_aes_sha2{},
                                                .auth_data = auth}});
  if (!made) {
    log(id_, "key backup not made: {}", made.error().said());
    return std::nullopt;
  }
  crypto_->keep_backup(made->version, secret);
  this->upload_backup();
  return secret;
}

template <class Sink>
void encrypted_account<Sink>::upload_backup() {
  if (!crypto_ || !api_)
    return;
  const auto backup = crypto_->backup();
  if (!backup)
    return;
  const auto public_key = crypto::backup_public_of(backup->second);
  if (!public_key)
    return;
  const auto entries = crypto_->not_backed_up(100);
  if (entries.empty())
    return;
  loom::cs::put_room_keys ask{.version = backup->first};
  std::vector<std::string> ids;
  for (const crypto::backup_entry& one : entries) {
    const auto sealed = crypto::seal_backup(*public_key, one.plain);
    if (!sealed)
      continue;
    ask.body.rooms[one.room].sessions.insert_or_assign(
        one.session_id, loom::cs::def::key_backup_data_t{.first_message_index = one.first_index,
                                                          .forwarded_count = 0,
                                                          .is_verified = one.verified,
                                                          .session_data = {.ephemeral = sealed->ephemeral,
                                                                           .ciphertext = sealed->ciphertext,
                                                                           .mac = sealed->mac}});
    ids.push_back(one.session_id);
  }
  if (auto done = perform(*api_, ask); !done) {
    log(id_, "room keys not backed up: {}", done.error().said());
    return;
  }
  crypto_->backed_up(ids);
}

template <class Sink>
std::size_t encrypted_account<Sink>::restore_backup(const std::string& secret) {
  auto current = perform(*api_, loom::cs::get_room_keys_version_current{});
  if (!current)
    return 0;
  // The backup's key is the one secret storage gave: else it is not this
  // account's backup to read, nor to write to.
  const auto& public_key = current->auth_data.public_key;
  if (!public_key || crypto::backup_public_of(secret) != *public_key)
    return 0;
  auto keys = perform(*api_, loom::cs::get_room_keys{.version = current->version});
  if (!keys)
    return 0;
  std::vector<crypto::exported_session> sessions;
  for (const auto& [room, backup] : keys->rooms)
    for (const auto& [id, data] : backup.sessions) {
      const auto& said = data.session_data;
      const auto plain = said.ephemeral && said.ciphertext && said.mac
                             ? crypto::open_backup(secret, crypto::backup_session_data{.ephemeral = *said.ephemeral,
                                                                                         .ciphertext = *said.ciphertext,
                                                                                         .mac = *said.mac})
                             : std::nullopt;
      if (!plain)
        continue;
      sessions.push_back(crypto::exported_session{.room_id = room,
                                                  .sender_key = plain->sender_key,
                                                  .sender_claimed_keys = plain->sender_claimed_keys,
                                                  .session_id = id,
                                                  .session_key = plain->session_key});
    }
  const std::size_t taken = crypto_->import_sessions(sessions);
  crypto_->keep_backup(current->version, secret);
  this->decrypt_all_waiting();
  return taken;
}

// The key read mentions are sealed under, in Secret Storage under its storage
// key: taken where it is there, put there (this device's, or a new one) where not.
template <class Sink>
template <class Key>
void encrypted_account<Sink>::mentions_key_in_storage(const Key& storage, const std::string& storage_id) {
  const std::string name("net.mux.mentions_key");
  if (const auto stored = perform(*api_, account_data_as<crypto::stored_secret>{{.user_id = id_.address, .type = name}})) {
    if (const auto sealed = stored->encrypted.find(storage_id); sealed != stored->encrypted.end())
      if (const auto opened = crypto::detail::open_secret(storage, name, sealed->second))
        if (auto bytes = crypto::from_base64(*opened); bytes && bytes->size() == 32) {
          this->keep_mentions_key(std::move(*bytes));
          return;
        }
  }
  // None there yet: this device's own, or a new one, put there.
  std::vector<std::uint8_t> key = this->mentions_key() ? *this->mentions_key() : crypto::random_bytes(32);
  const crypto::stored_secret sealed{
      .encrypted = {{storage_id, crypto::detail::seal_secret(storage, name, spl::bytes::base64_text(key))}}};
  if (perform(*api_, loom::cs::set_account_data{.user_id = id_.address, .type = name,
                                                 .body = knot::raw{knot::to_json_string(sealed)}}))
    this->keep_mentions_key(std::move(key));
}

template <class Sink>
std::optional<std::string> encrypted_account<Sink>::store_secrets(const crypto::cross_signing_secrets& secrets,
                                                        const std::optional<std::string>& backup_secret) {
  const auto made = crypto::make_storage_key();
  const auto put = [&](std::string type, const auto& value) {
    return perform(*api_, loom::cs::set_account_data{.user_id = id_.address, .type = std::move(type),
                                                     .body = knot::raw{knot::to_json_string(value)}})
        .has_value();
  };
  const auto sealed = [&](std::string_view name, const std::string& secret) {
    return crypto::stored_secret{.encrypted = {{made.id, crypto::detail::seal_secret(made.key, name, secret)}}};
  };
  const bool all = put("m.secret_storage.key." + made.id, made.info) &&
                   put("m.cross_signing.master", sealed("m.cross_signing.master", secrets.master)) &&
                   put("m.cross_signing.self_signing", sealed("m.cross_signing.self_signing", secrets.self_signing)) &&
                   put("m.cross_signing.user_signing", sealed("m.cross_signing.user_signing", secrets.user_signing)) &&
                   (!backup_secret || put("m.megolm_backup.v1", sealed("m.megolm_backup.v1", *backup_secret))) &&
                   put("m.secret_storage.default_key", crypto::default_storage_key{made.id});
  if (all)
    this->mentions_key_in_storage(made.key, made.id);
  return all ? std::optional<std::string>(made.recovery) : std::nullopt;
}

template <class Sink>
void encrypted_account<Sink>::restore_cross_signing(std::string recovery) {
  this->spawn_guarded([this, recovery = std::move(recovery)] {
    if (!crypto_ || !api_)
      return;
    const auto refused = [&](std::string why) { sink_(change::refused{id_, "Not restored: " + why}); };
    const auto key = crypto::key_of_recovery(recovery);
    if (!key)
      return refused("that is not a recovery key (a letter wrong, or one missing).");
    const auto get = [&]<class Content>(std::string type) {
      return perform(*api_, account_data_as<Content>{{.user_id = id_.address, .type = std::move(type)}});
    };
    const auto id = get.template operator()<crypto::default_storage_key>("m.secret_storage.default_key");
    if (!id)
      return refused("this account keeps no secrets on its server.");
    const auto info = get.template operator()<crypto::storage_key_info>("m.secret_storage.key." + id->key);
    if (!info || !crypto::is_storage_key(*key, *info))
      return refused("that is not this account's recovery key.");
    const auto secret = [&](std::string name) -> std::optional<std::string> {
      const auto stored = get.template operator()<crypto::stored_secret>(name);
      if (!stored)
        return std::nullopt;
      const auto sealed = stored->encrypted.find(id->key);
      return sealed == stored->encrypted.end() ? std::nullopt : crypto::detail::open_secret(*key, name, sealed->second);
    };
    const auto master = secret("m.cross_signing.master");
    const auto self = secret("m.cross_signing.self_signing");
    const auto users = secret("m.cross_signing.user_signing");
    if (!master || !self || !users)
      return refused("the keys kept there could not be opened.");
    const crypto::cross_signing_secrets secrets{.master = *master, .self_signing = *self, .user_signing = *users};
    // The key read mentions are sealed under, as Secret Storage keeps it.
    this->mentions_key_in_storage(*key, id->key);
    // Taken only where they are the keys the server lists for this user.
    auto got = this->keys_of(id_.address);
    const auto listed = got ? crypto::master_of(*got, id_.address) : std::nullopt;
    if (!listed || crypto::detail::ed25519_public(secrets.master) != listed)
      return refused("the keys kept there are not the ones your account has.");
    crypto_->keep_cross_signing(secrets);
    crypto_->verify_master(id_.address, *listed);
    if (got->device_keys)
      if (const auto own = got->device_keys->find(id_.address); own != got->device_keys->end())
        if (const auto device = own->second.find(crypto_->device_id()); device != own->second.end())
          this->cross_sign_device(device->second);
    // And the room keys in the key backup, where there is one.
    const auto backup_secret = secret("m.megolm_backup.v1");
    const std::size_t restored = backup_secret ? this->restore_backup(*backup_secret) : 0;
    sink_(change::notice{id_, "Cross-signing restored",
                         std::format("This device has your cross-signing keys back, and has signed itself with them. {} room "
                                     "keys were taken from the key backup.",
                                     restored)});
  });
}

template <class Sink>
void encrypted_account<Sink>::cross_sign_device(const loom::cs::query_keys::response_t::device_information_t& info) {
  const auto secrets = crypto_ ? crypto_->cross_signing_keys() : std::nullopt;
  if (!secrets || !api_ || info.user_id != id_.address)
    return;
  const auto self_pub = crypto::detail::ed25519_public(secrets->self_signing);
  const auto canonical =
      knot::to_canonical_json(crypto::device_signed_part{info.algorithms, info.device_id, info.keys, info.user_id, info.rest});
  const auto signature = canonical ? crypto::detail::ed25519_sign(secrets->self_signing, *canonical) : std::nullopt;
  if (!self_pub || !signature)
    return;
  const crypto::signed_device_part signed_one{.algorithms = info.algorithms,
                                              .device_id = info.device_id,
                                              .keys = info.keys,
                                              .user_id = info.user_id,
                                              .signatures = {{id_.address, {{"ed25519:" + *self_pub, *signature}}}},
                                              .rest = info.rest};
  std::map<std::string, std::map<std::string, knot::raw>> body;
  body[id_.address][info.device_id] = knot::raw{knot::to_json_string(signed_one)};
  (void)perform(*api_, loom::cs::upload_cross_signing_signatures{.body = std::move(body)});
}

template <class Sink>
void encrypted_account<Sink>::cross_sign_user(const std::string& user, const loom::cs::query_keys::response_t::cross_signing_key_t& master) {
  const auto secrets = crypto_ ? crypto_->cross_signing_keys() : std::nullopt;
  if (!secrets || !api_ || user == id_.address || master.keys.size() != 1)
    return;
  using key_t = loom::cs::query_keys::response_t::cross_signing_key_t;
  const auto user_pub = crypto::detail::ed25519_public(secrets->user_signing);
  const auto canonical = knot::to_canonical_json(crypto::key_signed_part<key_t>{master.user_id, master.usage, master.keys, master.rest});
  const auto signature = canonical ? crypto::detail::ed25519_sign(secrets->user_signing, *canonical) : std::nullopt;
  if (!user_pub || !signature)
    return;
  const crypto::signed_key_part<key_t> signed_one{.user_id = master.user_id,
                                                  .usage = master.usage,
                                                  .keys = master.keys,
                                                  .signatures = {{id_.address, {{"ed25519:" + *user_pub, *signature}}}},
                                                  .rest = master.rest};
  std::map<std::string, std::map<std::string, knot::raw>> body;
  body[user][master.keys.begin()->second] = knot::raw{knot::to_json_string(signed_one)};
  (void)perform(*api_, loom::cs::upload_cross_signing_signatures{.body = std::move(body)});
}

template <class Sink>
void encrypted_account<Sink>::reset_backup() {
  this->spawn_guarded([this] {
    if (!crypto_ || !api_)
      return;
    const auto keys = crypto_->cross_signing_keys();
    if (!keys) {
      sink_(change::refused{id_, "Not reset: this session has no cross-signing keys. Set up cross-signing, or restore "
                                 "it with your recovery key, first."});
      return;
    }
    if (const auto old = crypto_->backup())
      (void)perform(*api_, loom::cs::delete_room_keys_version{.version = old->first});
    crypto_->forget_backup();
    const auto backup_secret = this->make_backup(*keys);
    const auto recovery = this->store_secrets(*keys, backup_secret);
    sink_(change::notice{id_, "Key backup reset",
                         recovery ? std::format("A new key backup holds your room keys from now. Your cross-signing keys and "
                                                "its key are kept on your server under this new recovery key -- the old "
                                                "one no longer opens them. Write it down:\n\n{}",
                                                *recovery)
                                  : std::string("A new key backup holds your room keys from now.")});
  });
}

template <class Sink>
void encrypted_account<Sink>::delete_backup() {
  this->spawn_guarded([this] {
    if (!crypto_ || !api_)
      return;
    const auto old = crypto_->backup();
    if (!old) {
      sink_(change::refused{id_, "This session writes to no key backup."});
      return;
    }
    if (auto gone = perform(*api_, loom::cs::delete_room_keys_version{.version = old->first}); !gone) {
      sink_(change::refused{id_, "The key backup was not deleted: " + gone.error().said()});
      return;
    }
    crypto_->forget_backup();
    sink_(change::notice{id_, "Key backup deleted",
                         "Your room keys are no longer backed up on your server: a session you sign in to anew will not "
                         "read what was said before it."});
  });
}

template <class Sink>
void encrypted_account<Sink>::sign_out_unverified(std::string password) {
  this->spawn_guarded([this, password = std::move(password)] {
    if (!crypto_ || !api_)
      return;
    const auto all = this->own_sessions_now();
    if (!all)
      return;
    std::vector<std::string> unverified =
        (*all) | std::views::filter([&](const own_session& one) { return one.id != crypto_->device_id() && !one.trusted; }) | std::views::transform(&own_session::id) | std::ranges::to<std::vector>();
    if (unverified.empty()) {
      sink_(change::notice{id_, "Sign out unverified sessions", "Every other session of yours is verified."});
      return;
    }
    this->sign_out_sessions(std::move(unverified), password);
  });
}

}  // namespace mux::proto::matrix::client
