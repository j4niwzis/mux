// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.room_admin.app:xmpp -- What the program does with an XMPP group
// chat's admin pages: their requests sent for the room being managed, a
// configuration's save kept with the room until the server answers, and
// what it answers taken in. Built where XMPP is.
export module mux.plugin.room_admin.app:xmpp;

import std;
import splice;
import mux.core;
import mux.config;
import mux.proto.xmpp;
import mux.proto.xmpp.changes;
import mux.ui;
import mux.app.network;
import mux.app.services;
import mux.plugin.room_settings.types;
import mux.plugin.room_settings.ui;
import mux.plugin.room_admin.types;

export namespace mux::plugins::room_admin {

struct xmpp_glue {
  app::services* s_;

  void take(const proto::xmpp::muc_settings_loaded& loaded) {
    const auto chosen = s_->managed();
    if (!chosen || chosen->account != loaded.account || chosen->id != loaded.room) return;
    if (const auto& open = *s_->showing->look<std::optional<room_settings::protocol_settings<::mux::protocol::xmpp>>>()) {
      auto now = *open;
      now.theirs = loaded.settings;
      ui::show(*s_->showing, std::optional(std::move(now)));
    }
  }
  void take(const proto::xmpp::muc_configuration_failed& failed) {
    const auto chosen = s_->managed();
    if (!chosen || chosen->account != failed.account || chosen->id != failed.room) return;
    // The save done with: what the server said, beside the fields as they
    // were sent.
    this->change_room(*chosen, [&](proto::xmpp::muc_room& room) {
      room.saving = false;
      room.failure = failed.reason;
    });
  }
  void apply(const proto::xmpp::request::refresh_muc& one) { this->muc_request(one); }
  // A configuration saved: on its way, the fields as sent kept with the room
  // until the server answers -- loaded again, or what was wrong.
  void apply(const proto::xmpp::request::configure_muc& one) {
    this->muc_request(one);
    if (const auto chosen = s_->managed())
      this->change_room(*chosen, [&](proto::xmpp::muc_room& room) {
        room.saving = true;
        room.failure.reset();
        room.fields = one.fields;
      });
  }
  void apply(const proto::xmpp::request::muc_identity& one) { this->muc_request(one); }
  void apply(const proto::xmpp::request::muc_privilege& one) { this->muc_request(one); }
  void apply(const proto::xmpp::request::destroy_muc& one) { this->muc_request(one); }
  void apply(const proto::xmpp::request::rejoin_muc& one) { this->muc_request(one); }

 private:
  template <class Request>
  void muc_request(const Request& one) {
    const auto chosen = s_->managed();
    if (!chosen || s_->demo()) return;
    s_->net->on_account_of(*chosen, [room = chosen->id, one](auto& account)
        -> decltype(void(account.change_muc(room, one))) { account.change_muc(room, one); });
  }
  // The room the settings show, its configuration changed as `change` says,
  // and the page shown again from it.
  template <class Change>
  void change_room(const conversation_id& in, Change change) {
    const auto& open = *s_->showing->look<std::optional<room_settings::protocol_settings<::mux::protocol::xmpp>>>();
    if (!open || open->chat != in)
      return;
    auto now = *open;
    spl::visit(spl::overloaded{[&](proto::xmpp::muc_room& room) { change(room); }, [](auto&) {}}, now.theirs);
    ui::show(*s_->showing, std::optional(std::move(now)));
  }
};

}  // namespace mux::plugins::room_admin
