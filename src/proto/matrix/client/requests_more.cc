// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix.client:requests_more -- A room's gap caught up on, rooms looked up and made, forwards, profiles, sessions, cross-signing and the key backup.
export module mux.proto.matrix.client:requests_more;

import std;
import chevron.escape;
import splice;
import knot;
import loom.api;
import loom.ev;
import loom.state;
import loom.cs.joining;
import loom.cs.leaving;
import loom.cs.login;
import loom.cs.message_pagination;
import loom.cs.event_context;
import loom.cs.receipts;
import loom.cs.redaction;
import loom.cs.users;
import loom.cs.relations;
import loom.cs.threads_list;
import loom.cs.room_summary;
import loom.cs.list_public_rooms;
import loom.cs.space_hierarchy;
import loom.cs.room_send;
import loom.cs.rooms;
import mux.proto.matrix.crypto;
import loom.cs.keys;
import loom.cs.cross_signing;
import loom.cs.room_state;
import loom.cs.content_repo;
import loom.cs.authed_content_repo;
import loom.cs.create_room;
import loom.cs.account_data;
import loom.cs.key_backup;
import loom.cs.kicking;
import loom.cs.banning;
import loom.cs.inviting;
import loom.cs.sync;
import loom.cs.typing;
import loom.cs.wellknown;
import loom.cs.profile;
import loom.cs.device_management;
import mux.config;
import mux.core;
import mux.http;
import mux.net;
import loom.cs.pusher;
import mux.logic.markdown;
import :account;
import :requests;

// The members defined here are declared in :account, and exported there.
namespace mux::proto::matrix::client {

template <class Sink>
void account<Sink>::catch_up(std::string room, std::string from, std::string until) {
  this->spawn_guarded([this, room = std::move(room), from = std::move(from), until = std::move(until)] {
    const conversation_id in{id_, room};
    std::map<std::string, std::string> sender_of;  // of every event the pages held
    struct reaction_found {
      std::string event, target;
      std::chrono::sys_time<std::chrono::milliseconds> at;
    };
    std::vector<reaction_found> reactions;
    std::optional<std::string> token = from;
    std::size_t read = 0, mentions = 0;
    // The gap's events, newest first as the pages bring them.
    std::vector<loom::ev::timeline_event> gap;
    // At most ten pages of a hundred: a gap longer than that is the
    // history's, paged back to when read.
    for (int page = 0; page < 10 && api_ && token; ++page) {
      auto got = perform(*api_, loom::cs::get_room_events{.room_id = room,
                                                          .from = token,
                                                          .dir = loom::cs::get_room_events::dir_values::b{},
                                                          .limit = 100});
      if (!got || got->chunk.empty())
        break;
      bool reached = false;
      for (const auto& one : got->chunk) {
        if (one.event_id == until) {
          reached = true;
          break;
        }
        ++read;
        sender_of.emplace(one.event_id, one.sender);
        gap.push_back(one);
        if (one.sender == id_.address)
          continue;
        const auto at = std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::milliseconds(one.origin_server_ts));
        splice::visit(
            splice::overloaded{
                [&](const loom::ev::m_room_message_content_t& content) {
                  // Not an edit: it is never shown under its own id -- its
                  // mark said "Loading..." for ever, the server finding it.
                  using values = loom::ev::m_room_message_content_t::m_relates_to_t::rel_type_values;
                  const bool edit = content.m_relates_to && content.m_relates_to->rel_type &&
                                    splice::visit(splice::overloaded{[](values::m_replace) { return true; },
                                                                     [](const auto&) { return false; }},
                                                  *content.m_relates_to->rel_type);
                  // An edit that mentions the user: the message it edits, as
                  // a live one is (tdesktop's mention added by an edit).
                  if (edit && content.m_relates_to->event_id && loom::client::mentions(content, id_.address)) {
                    ++mentions;
                    sink_(change::mentioned{in, *content.m_relates_to->event_id, at});
                  }
                  if (!edit && loom::client::mentions(content, id_.address)) {
                    ++mentions;
                    sink_(change::mentioned{in, one.event_id, at});
                  }
                },
                [&](const loom::ev::m_reaction_content_t& content) {
                  if (content.m_relates_to && content.m_relates_to->event_id)
                    reactions.push_back({one.event_id, *content.m_relates_to->event_id, at});
                },
                [](const auto&) {}},
            one.content.data());
      }
      if (reached)
        break;
      token = got->end;
    }
    // Into the timeline, oldest first, by their time: between what was there
    // and what the sync brought.
    for (const auto& one : gap | std::views::reverse)
      this->event(in, one, placement::in_window{});
    // The reactions to what the user sent: known by who sent it, where the
    // pages or the room's last events held it.
    const auto kept = state_.joined.find(room);
    const auto mine = [&](const std::string& target) {
      if (const auto found = sender_of.find(target); found != sender_of.end())
        return found->second == id_.address;
      if (kept != state_.joined.end())
        for (const auto& one : kept->second.timeline)
          if (one.event_id == target)
            return one.sender == id_.address;
      return false;
    };
    std::size_t to_mine = 0;
    for (const auto& one : reactions)
      if (mine(one.target)) {
        ++to_mine;
        sink_(change::reacted_to_mine{in, one.event, one.target, one.at});
      }
    log(id_, "caught up on {}: {} event{} put in, {} mention{}, {} reaction{} to yours", room, read, read == 1 ? "" : "s",
        mentions, mentions == 1 ? "" : "s", to_mine, to_mine == 1 ? "" : "s");
  });
}

template <class Sink>
void account<Sink>::preview_room(std::string room, std::vector<std::string> via) {
  this->spawn_guarded([this, room = std::move(room), via = std::move(via)] {
    if (!api_)
      return;
    auto got = perform(*api_, loom::cs::get_room_summary{
                                  .room_id_or_alias = room,
                                  .via = via.empty() ? std::nullopt : std::optional<std::vector<std::string>>(via)});
    if (!got) {
      sink_(change::room_previewed{id_, room, {.note = "Its server tells nothing of it: " + got.error().said()}});
      return;
    }
    sink_(change::room_previewed{id_, room,
                                 {.id = got->room_id,
                                  .name = got->name.value_or(""),
                                  .alias = got->canonical_alias.value_or(""),
                                  .topic = got->topic.value_or(""),
                                  .avatar = got->avatar_url,
                                  .members = got->num_joined_members,
                                  .knock = splice::visit(splice::overloaded{[](mux::proto::matrix::join_rule::knock) { return true; },
                                                                            [](const auto&) { return false; }},
                                                         join_rule_of(got->join_rule))}});
  });
}

// Asked to be let in, where the room lets people knock.
template <class Sink>
void account<Sink>::knock(std::string room, std::vector<std::string> via, std::string reason) {
  this->spawn_guarded([this, room = std::move(room), via = std::move(via), reason = std::move(reason)] {
    if (!api_)
      return;
    auto got = perform(*api_, loom::cs::knock_room{.room_id_or_alias = room,
                                                   .via = via.empty() ? std::nullopt : std::optional<std::vector<std::string>>(via),
                                                   .body = {.reason = reason.empty() ? std::nullopt : std::optional<std::string>(reason)}});
    if (!got)
      log(id_, "could not knock on {}: {}", room, got.error().said());
  });
}

template <class Sink>
void account<Sink>::create_group(std::string name) {
  this->spawn_guarded([this, name = std::move(name)] {
    if (!api_)
      return;
    auto made = perform(*api_, loom::cs::create_room{.body = {.name = name,
                                                              .initial_state = encrypted_from_the_start(),
                                                              .preset = loom::cs::create_room::body_t::preset_values::private_chat{}}});
    if (!made) {
      log(id_, "could not make the room {}: {}", name, made.error().said());
      return;
    }
    this->remember_encrypted(made->room_id);
    sink_(change::room_created{{id_, made->room_id}});
  });
}

// Where a forwarded message is from, beside its content (MSC2723, under
// its unstable name): the original's event, room, sender and time.
struct forwarded_mark {
  struct where_t {
    std::string event_id;
    std::string room_id;
    std::string sender;
    std::int64_t origin_server_ts = 0;
    friend consteval auto json_schema(knot::type<where_t>) { return knot::schema<where_t>(); }
  };
  where_t forwarded;
  friend consteval auto json_schema(knot::type<forwarded_mark>) {
    return knot::schema<forwarded_mark>().member<"forwarded">(knot::key("com.famedly.app.forwarded"));
  }
};

template <class Sink>
void account<Sink>::forward(std::string from, std::string event, std::string to) {
  this->spawn_sending([this, from = std::move(from), event = std::move(event), to = std::move(to)] {
    if (!api_)
      return;
    auto got = perform(*api_, loom::cs::get_one_room_event{.room_id = from, .event_id = event});
    if (!got) {
      log(id_, "could not fetch {} to forward: {}", event, got.error().said());
      return;
    }
    // A message's content as it is typed, without what it answered or
    // replaced: sent on as a message of its own. Anything else is not
    // forwarded.
    std::optional<loom::ev::m_room_message_content_t> content;
    splice::visit(splice::overloaded{[&](const loom::ev::m_room_message_content_t& one) { content = one; },
                                     [](const auto&) {}},
                  got->content.data());
    if (!content) {
      log(id_, "{} is not a message: not forwarded", event);
      return;
    }
    content->m_relates_to.reset();
    content->m_new_content.reset();
    // As MSC2723 forwards: the content as it was, and where it is from
    // beside it -- no words added to it; a client shows the forward its way.
    content->rest = as_body(forwarded_mark{.forwarded = {.event_id = event,
                                                         .room_id = from,
                                                         .sender = got->sender,
                                                         .origin_server_ts = static_cast<std::int64_t>(got->origin_server_ts)}});
    auto done = this->send_room_event(loom::cs::send_message{.room_id = to,
                                                      .event_type = "m.room.message",
                                                      .txn_id = this->transaction(),
                                                      .body = as_body(*content)});
    if (!done)
      log(id_, "could not forward {} to {}: {}", event, to, done.error().said());
  });
}

template <class Sink>
void account<Sink>::fetch_profile(std::string user) {
  this->spawn_guarded([this, user = std::move(user)] {
    if (!api_)
      return;
    auto got = perform(*api_, loom::cs::get_user_profile{.user_id = user});
    if (!got) {
      log(id_, "no profile of {}: {}", user, got.error().said());
      return;
    }
    sink_(change::profile_found{id_, user, got->displayname, got->avatar_url});
  });
}

// The password answered, as the spec's interactive auth takes it: who, by
// their user ID, and the password -- beside its type and session.
struct password_auth {
  struct identifier_t {
    std::string type = "m.id.user";
    std::string user;
    friend consteval auto json_schema(knot::type<identifier_t>) { return knot::schema<identifier_t>(); }
  };
  identifier_t identifier;
  std::string password;
  friend consteval auto json_schema(knot::type<password_auth>) { return knot::schema<password_auth>(); }
};

template <class Sink>
void account<Sink>::list_sessions() {
  this->spawn_guarded([this] {
    if (!api_)
      return;
    auto got = perform(*api_, loom::cs::get_devices{});
    if (!got) {
      sink_(proto::matrix::sessions_refused{id_, "Could not list the sessions: " + got.error().said()});
      return;
    }
    std::vector<mux::proto::matrix::session_info> out =
        got->devices.value_or(std::vector<loom::cs::def::device_t>{}) | std::views::transform([](const loom::cs::def::device_t& one) {
          return mux::proto::matrix::session_info{
              .id = one.device_id,
              .name = one.display_name.value_or(""),
              .ip = one.last_seen_ip,
              .last_seen = one.last_seen_ts ? std::optional(std::chrono::sys_time<std::chrono::milliseconds>(
                                                  std::chrono::milliseconds(*one.last_seen_ts)))
                                            : std::nullopt};
        }) |
        std::ranges::to<std::vector>();
    sink_(proto::matrix::sessions_listed{id_, how_.device_id.value_or(""), std::move(out)});
    sink_(proto::matrix::security_state{id_, crypto_ && crypto_->cross_signing_keys().has_value(), crypto_ && crypto_->backup().has_value()});
  });
}

template <class Sink>
void account<Sink>::rename_session(std::string device, std::string name) {
  this->spawn_guarded([this, device = std::move(device), name = std::move(name)] {
    if (!api_)
      return;
    auto done = perform(*api_, loom::cs::update_device{.device_id = device, .body = {.display_name = name}});
    if (!done)
      sink_(proto::matrix::sessions_refused{id_, "Could not rename the session: " + done.error().said()});
    this->list_sessions();
  });
}

template <class Sink>
void account<Sink>::sign_out_sessions(std::vector<std::string> devices, std::string password) {
  this->spawn_guarded([this, devices = std::move(devices), password = std::move(password)] {
    if (!api_ || devices.empty())
      return;
    using body_t = loom::cs::delete_devices::body_t;
    // Asked first without: the server says what it wants, and its session.
    auto first = perform(*api_, loom::cs::delete_devices{.body = body_t{.devices = devices}});
    if (first) {
      this->list_sessions();
      return;
    }
    const auto& said = first.error().server;
    if (!said || said->status != 401 || !said->session) {
      sink_(proto::matrix::sessions_refused{id_, "Could not sign out: " + first.error().said()});
      return;
    }
    const std::string given = !password.empty() ? password : how_.password;
    if (given.empty()) {
      sink_(proto::matrix::sessions_refused{id_, "Your password is needed to sign sessions out.", true});
      return;
    }
    body_t::authentication_data_t auth{.type = "m.login.password", .session = *said->session};
    auth.rest = as_body(password_auth{.identifier = {.user = how_.user_id}, .password = given});
    auto done = perform(*api_, loom::cs::delete_devices{.body = body_t{.devices = devices, .auth = std::move(auth)}});
    if (!done) {
      const bool wrong = done.error().server && done.error().server->status == 401;
      sink_(proto::matrix::sessions_refused{id_, wrong ? std::string("The password was not accepted.") : "Could not sign out: " + done.error().said(),
                                     wrong});
      return;
    }
    this->list_sessions();
  });
}

template <class Sink>
void account<Sink>::setup_cross_signing(std::string password, bool reset) {
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
      upload::body_t::authentication_data_t auth{.type = "m.login.password", .session = *said->session};
      auth.rest = as_body(password_auth{.identifier = {.user = how_.user_id}, .password = given});
      body.auth = std::move(auth);
      if (auto done = perform(*api_, upload{.body = body}); !done) {
        sink_(change::refused{id_, "Cross-signing not set up: " + done.error().said()});
        return;
      }
    }
    crypto_->keep_cross_signing(secrets);
    crypto_->verify_master(id_.address, *master_pub);
    // This device, signed with the self-signing key.
    if (const auto own = crypto_->signed_device_keys()) {
      const auto canonical = knot::to_canonical_json(own->keys);
      const auto by_self = canonical ? crypto::detail::ed25519_sign(secrets.self_signing, *canonical) : std::nullopt;
      if (by_self) {
        const crypto::signed_device_part signed_one{.algorithms = own->keys.algorithms,
                                                    .device_id = own->keys.device_id,
                                                    .keys = own->keys.keys,
                                                    .user_id = own->keys.user_id,
                                                    .signatures = {{id_.address, {{"ed25519:" + *self_pub, *by_self}}}}};
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
  });
}

template <class Sink>
std::optional<std::string> account<Sink>::make_backup(const crypto::cross_signing_secrets& secrets) {
  const auto [secret, public_key] = crypto::new_backup_key();
  const auto canonical = knot::to_canonical_json(crypto::backup_auth_signed_part{public_key});
  const auto master_pub = crypto::detail::ed25519_public(secrets.master);
  const auto by_master = canonical ? crypto::detail::ed25519_sign(secrets.master, *canonical) : std::nullopt;
  if (!canonical || !master_pub || !by_master)
    return std::nullopt;
  const crypto::backup_auth_data auth{
      .public_key = public_key,
      .signatures = {{id_.address,
                      {{"ed25519:" + crypto_->device_id(), crypto_->sign_as_device(*canonical)}, {"ed25519:" + *master_pub, *by_master}}}}};
  using version_t = loom::cs::post_room_keys_version;
  auto made = perform(*api_, version_t{.body = {.algorithm = version_t::body_t::algorithm_values::m_megolm_backup_v1_curve25519_aes_sha2{},
                                                .auth_data = knot::raw{knot::to_json_string(auth)}}});
  if (!made) {
    log(id_, "key backup not made: {}", made.error().said());
    return std::nullopt;
  }
  crypto_->keep_backup(made->version, secret);
  this->upload_backup();
  return secret;
}

template <class Sink>
void account<Sink>::upload_backup() {
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
                                                          .session_data = knot::raw{knot::to_json_string(*sealed)}});
    ids.push_back(one.session_id);
  }
  if (auto done = perform(*api_, ask); !done) {
    log(id_, "room keys not backed up: {}", done.error().said());
    return;
  }
  crypto_->backed_up(ids);
}

template <class Sink>
std::size_t account<Sink>::restore_backup(const std::string& secret) {
  auto current = perform(*api_, loom::cs::get_room_keys_version_current{});
  if (!current)
    return 0;
  const auto auth = knot::try_read<crypto::backup_auth_data>(current->auth_data.text);
  // The backup's key is the one secret storage gave: else it is not this
  // account's backup to read, nor to write to.
  if (!auth || crypto::backup_public_of(secret) != auth->public_key)
    return 0;
  auto keys = perform(*api_, loom::cs::get_room_keys{.version = current->version});
  if (!keys)
    return 0;
  std::vector<crypto::exported_session> sessions;
  for (const auto& [room, backup] : keys->rooms)
    for (const auto& [id, data] : backup.sessions) {
      const auto sealed = knot::try_read<crypto::backup_session_data>(data.session_data.text);
      const auto plain = sealed ? crypto::open_backup(secret, *sealed) : std::nullopt;
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

template <class Sink>
std::optional<std::string> account<Sink>::store_secrets(const crypto::cross_signing_secrets& secrets,
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
  return all ? std::optional<std::string>(made.recovery) : std::nullopt;
}

template <class Sink>
void account<Sink>::restore_cross_signing(std::string recovery) {
  this->spawn_guarded([this, recovery = std::move(recovery)] {
    if (!crypto_ || !api_)
      return;
    const auto refused = [&](std::string why) { sink_(change::refused{id_, "Not restored: " + why}); };
    const auto key = crypto::key_of_recovery(recovery);
    if (!key)
      return refused("that is not a recovery key (a letter wrong, or one missing).");
    const auto get = [&](std::string type) { return perform(*api_, loom::cs::get_account_data{.user_id = id_.address, .type = std::move(type)}); };
    const auto chosen = get("m.secret_storage.default_key");
    const auto id = read_answer<crypto::default_storage_key>(chosen);
    if (!id)
      return refused("this account keeps no secrets on its server.");
    const auto info_raw = get("m.secret_storage.key." + id->key);
    const auto info = read_answer<crypto::storage_key_info>(info_raw);
    if (!info || !crypto::is_storage_key(*key, *info))
      return refused("that is not this account's recovery key.");
    const auto secret = [&](std::string name) -> std::optional<std::string> {
      const auto raw = get(name);
      const auto stored = read_answer<crypto::stored_secret>(raw);
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
void account<Sink>::cross_sign_device(const loom::cs::query_keys::response_t::device_information_t& info) {
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
void account<Sink>::cross_sign_user(const std::string& user, const loom::cs::query_keys::response_t::cross_signing_key_t& master) {
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
void account<Sink>::fetch_quoted(std::string room, std::string target) {
  this->spawn_guarded([this, room = std::move(room), target = std::move(target)] {
    if (!api_)
      return;
    auto got = perform(*api_, loom::cs::get_one_room_event{.room_id = room, .event_id = target});
    if (!got) {
      log(id_, "could not fetch {} in {}: {}", target, room, got.error().said());
      // Not there, or not to be seen by this user: said so, and whatever
      // waits on it (a mark) let go. A network's failure is not that; the
      // server's status is: not found, not to be seen, or an id it cannot
      // read (400).
      if (const auto& server = got.error().server;
          server && (server->status == 404 || server->status == 403 || server->status == 400))
        sink_(change::event_missing{conversation_id{id_, room}, target});
      return;
    }
    event(conversation_id{id_, room}, *got, placement::aside{});
  });
}

template <class Sink>
void account<Sink>::load_context(std::string room, std::string target) {
  this->spawn_guarded([this, room = std::move(room), target = std::move(target)] {
    if (!api_)
      return;
    auto got = perform(*api_, loom::cs::get_event_context{.room_id = room, .event_id = target, .limit = 60});
    if (!got) {
      log(id_, "context of {} in {}: {}", target, room, got.error().said());
      // Not there, or not to be seen: said so -- a jump to it stops, rather
      // than paging the whole history back for it.
      if (const auto& server = got.error().server; server && (server->status == 404 || server->status == 403))
        sink_(change::event_missing{conversation_id{id_, room}, target});
      return;
    }
    const conversation_id in{id_, room};
    sink_(change::window_opened{in, got->start, got->end});
    // Before it, newest first: given oldest first.
    if (got->events_before)
      for (auto it = got->events_before->rbegin(); it != got->events_before->rend(); ++it)
        event(in, *it, placement::in_window{});
    // It, read as a timeline event from what the server gave.
    if (got->event)
      event(in, *got->event, placement::in_window{});
    if (got->events_after)
      for (const auto& one : *got->events_after)
        event(in, one, placement::in_window{});
    log(id_, "context of {} in {}: {} before, {} after", target, room,
        got->events_before ? got->events_before->size() : 0, got->events_after ? got->events_after->size() : 0);
  });
}

template <class Sink>
void account<Sink>::load_newer(std::string room, std::string from) {
  this->spawn_guarded([this, room = std::move(room), from = std::move(from)] {
    if (!api_)
      return;
    auto got = perform(*api_, loom::cs::get_room_events{.room_id = room,
                                                        .from = from,
                                                        .dir = loom::cs::get_room_events::dir_values::f{},
                                                        .limit = 40});
    if (!got) {
      log(id_, "newer in {}: {}", room, got.error().said());
      return;
    }
    const conversation_id in{id_, room};
    for (const auto& one : got->chunk)  // oldest first: each after the rest
      event(in, one, placement::in_window{});
    // Nothing more, or no token on: the newest is met, and it is live.
    sink_(change::window_extended{in, got->chunk.empty() ? std::nullopt : got->end});
  });
}

template <class Sink>
void account<Sink>::fetch_avatar(std::string source, std::string of) {
  this->fetch_media(std::move(source), media_use::avatar{std::move(of)}, 96, true);
}

}  // namespace mux::proto::matrix::client
