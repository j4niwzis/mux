// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.encryption.ui:page -- A Matrix account's Encryption page, as
// Element's Security & Privacy has it: room keys to verified sessions only;
// this session, its ID and its key in fours, to compare with what another
// session shows; device verification -- cross-signing set up here, or
// brought back with the recovery key, or reset -- and the key backup made
// anew or deleted; room keys exported or imported; unverified sessions
// signed out. Listed for Matrix accounts only (page_shown).
module;
#include "skiff/named.h"
export module mux.plugin.encryption.ui:page;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.model;
import skiff.nodes.flow;
import skiff.nodes.text;
import skiff.widgets.button;
import skiff.widgets.model;
import mux.composition;
import mux.core;
import mux.config;
import mux.proto.kept;
import mux.proto.matrix;
import mux.proto.matrix.requests;
import mux.ui.base;
import mux.ui.proto.matrix;
import mux.plugin.encryption.types;

export namespace mux::plugins::encryption {

namespace page_detail {
using namespace ::mux::ui;
namespace said = ::mux::proto::matrix::settings_detail;
using facts_t = said::facts_t;

// This session, as its account's protocol state says it, once known.
[[nodiscard]] inline std::string session_line(const facts_t& now) {
  if (!now)
    return {};
  return spl::visit(spl::overloaded{[](const ::mux::proto::matrix::state& known) {
                                      if (known.device_id.empty())
                                        return std::string();
                                      const std::string grouped = known.ed25519 | std::views::enumerate | std::views::transform([](const auto& at) {
                                            const auto [index, letter] = at;
                                            return index > 0 && index % 4 == 0 ? std::string{' ', letter} : std::string(1, letter);
                                          }) | std::views::join | std::ranges::to<std::string>();
                                      return std::format("Session ID: {}\nSession key: {}", known.device_id, grouped);
                                    },
                                    [](const auto&) { return std::string(); }},
                    now->state);
}

inline auto security_view(const palette& colours, const account_page_facts&) {
  namespace c = skiff::compose;
  using flip = sends<::mux::ui::request::flip_only_verified>;
  using toggle_t = toggle_for<flip>;
  return SKIFF_NAMED(security_view, c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      section_title(colours, "ENCRYPTION"),
      c::row(c::hbox(16.0f, {.fillX = true, .height = kRowHeight, .padding = {0.0f, 20.0f, 0.0f, 20.0f}}),
             c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                       elided(nodes::Text("Never send encrypted messages to unverified sessions", 15.0f, colours.text))),
             c::applied<facts_t>([](const facts_t& now) { return now && config::only_verified_of(now->account); },
                                 [](toggle_t& toggle, bool on) { toggle.setOn(on); },
                                 c::styled({.alignSelf = scene::align::kMiddle}, toggle_t(colours.widgets, flip{})))),
      c::shown_for<facts_t>([](const facts_t& now) { return !session_line(now).empty(); },
                            c::text_for<facts_t>(session_line, nodes::Text("", 13.0f, colours.dim))),
      // Device verification, as Cinny puts it: cross-signing set up here,
      // or brought back with the recovery key. Without it, a session
      // verified by emoji is trusted only by the client that verified it,
      // and nothing is signed. Element's last resort under them: new
      // cross-signing keys, the dialog saying what it undoes; and its Secure
      // Backup, made anew or deleted.
      c::styled({.margin = {14.0f, 0.0f, 0.0f, 0.0f}}, section_title(colours, "DEVICE VERIFICATION")),
      c::text_for<facts_t>(said::security_said, said::note(colours, "")),
      said::row_if<sends<setup_cross_signing>>(colours, "Set up cross-signing…", std::not_fn(said::cross_signing)),
      said::row_if<sends<restore_cross_signing>>(colours, "Restore with the recovery key…", std::not_fn(said::cross_signing)),
      row_item(colours, "Reset your identity…", sends<reset_identity>{}),
      said::row_if<sends<reset_backup>>(colours, "Reset the key backup", said::cross_signing),
      said::row_if<sends<delete_backup>>(colours, "Delete the key backup", said::backed_up),
      c::styled({.margin = {14.0f, 0.0f, 0.0f, 0.0f}}, section_title(colours, "ROOM KEYS")),
      row_item(colours, "Export room keys…", sends<export_room_keys>{}),
      row_item(colours, "Import room keys…", sends<import_room_keys>{}),
      // Element's: every session of one's own not verified, signed out.
      c::styled({.margin = {14.0f, 0.0f, 0.0f, 0.0f}}, section_title(colours, "SESSIONS")),
      row_item(colours, "Sign out unverified sessions…", sends<sign_out_unverified>{})));
}
}  // namespace page_detail

constexpr std::string_view page_title(security_page) { return "Encryption"; }
inline ui::icon_t page_icon(security_page) { return ui::icon::check{}; }
// Listed for a Matrix account's pages, no other's.
constexpr bool page_shown(security_page, const ::mux::proto::matrix::state&) { return true; }
constexpr bool page_shown(security_page, const auto&) { return false; }
inline auto page_view(security_page, const ui::palette& colours, const ui::account_page_facts& first) {
  return page_detail::security_view(colours, first);
}
// As it opens: the sessions asked of the server, and with them whether this
// one has the cross-signing keys and the backup is on.
inline ::mux::proto::matrix::request::refresh_sessions asked_as_it_opens(security_page, const account_id&) { return {}; }

}  // namespace mux::plugins::encryption
