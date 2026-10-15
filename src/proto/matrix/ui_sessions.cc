// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui.proto.matrix:sessions -- The account's Sessions and Encryption pages.
export module mux.ui.proto.matrix:sessions;

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
import mux.core;
import mux.config;
import mux.proto.kept;
import mux.proto.matrix;
import mux.proto.matrix.requests;
import mux.ui.base;

// ---- what the pages read --------------------------------------------------------
// Each page is a function over the account's pages' facts (account_page_facts):
// the account as saved, its protocol's state, and what Matrix keeps of it
// while they are open (account_state) -- its sessions as the server listed
// them, its security, a session being renamed, a sign-out on its way, what
// the server refused. A press asks the program (request::change_account),
// which changes what it keeps and asks the server.
export namespace mux::proto::matrix::settings_detail {
using namespace ::mux::ui;

using facts_t = std::optional<account_page_facts>;
[[nodiscard]] inline const account_state& kept_of(const facts_t& now) {
  static const account_state none;
  return now ? spl::visit(spl::overloaded{[](const account_state& kept) -> const account_state& { return kept; },
                                          [](const auto&) -> const account_state& { return none; }},
                          now->kept)
             : none;
}
template <class Change>
struct asks {
  using Answer = request::change_account;
  Change change;
  Answer operator()() const { return {account_change_t{change}}; }
};
template <class Change>
asks<Change> ask(Change change) { return {std::move(change)}; }

// A row of the client's settings rows that asks something, shown or not as
// the facts say.
template <class Act, class Shown>
auto row_if(const palette& colours, std::string text, Shown shown) {
  return skiff::compose::shown_for<facts_t>(std::move(shown), row_item(colours, std::move(text), Act{}));
}
inline auto note(const palette& colours, std::string text) {
  return skiff::compose::styled({.fillX = true}, wrapped(nodes::Text(std::move(text), 13.0f, colours.dim)));
}
inline auto button_look(float width) {
  return scene::Spec{.width = width, .height = 30.0f, .alignSelf = scene::align::kMiddle};
}

// ---- the account's Sessions page ----------------------------------------------
// As Element's: this session first, then the others -- each its name, its
// ID, where and when it was last seen -- each to rename or sign out, and all
// the others at once. Where the server asks for the password to sign them
// out, a field for it.
// A session's ID, when it was last seen, from where: as Element says them.
// Its time told by mux.ui's seen_at: clang 23 crashed on that format
// string made first in this module (see app/network.cc).
[[nodiscard]] inline std::string facts_of(const session_info& one, bool current) {
  std::string out = one.id;
  if (current)
    out += " · this session";
  if (one.last_seen)
    out += " · last seen " + seen_at(*one.last_seen);
  if (one.ip)
    out += " · " + *one.ip;
  return out;
}

// One session as its row is made: made again where it is renamed or saved.
struct session_key {
  session_info one;
  bool current = false;
  bool renaming = false;
  friend bool operator==(const session_key&, const session_key&) = default;
};
struct name_draft {
  std::string name;
  friend bool operator==(const name_draft&, const name_draft&) = default;
};
// Its name over its ID, when and where it was last seen; Rename, and, where
// it is not this one, Verify and Sign out. Being renamed: its name in a
// field, and Save.
inline auto session_row(const palette& colours, const session_key& key) {
  namespace c = skiff::compose;
  const session_info& one = key.one;
  return c::local<name_draft>(
      c::row(c::hbox(8.0f, {.fillX = true, .height = 60.0f, .padding = {0.0f, 12.0f, 0.0f, 12.0f}, .cornerRadius = 8.0f,
                            .background = colours.tile}),
             c::visible(!key.renaming, c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                 two_lines(colours, one.name.empty() ? std::string("Unnamed session") : one.name, facts_of(one, key.current), 15.0f, 2.0f, 12.0f))),
             c::visible(key.renaming, c::bound<skiff::model::Field<&name_draft::name>>(
                 c::styled({.height = 32.0f, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                           widgets::TextField<std::string>(colours.widgets, "Session name")))),
             c::visible(key.renaming, c::asking_for<name_draft>(
                 [device = one.id](const name_draft& now) { return request::change_account{account_change::save_name{device, now.name}}; },
                 c::styled(button_look(70.0f), widgets::Button<scene::NoAction>(colours.widgets, "Save", {})))),
             c::visible(key.renaming, c::styled(button_look(76.0f),
                 widgets::Button<asks<account_change::rename>>(colours.widgets, "Cancel", ask(account_change::rename{one.id, false})))),
             c::visible(!key.renaming, c::styled(button_look(80.0f),
                 widgets::Button<asks<account_change::rename>>(colours.widgets, "Rename", ask(account_change::rename{one.id, true})))),
             c::visible(!key.current && !key.renaming, c::styled(button_look(70.0f),
                 button_for<sends<request::verify_session>>(colours.widgets, "Verify", {request::verify_session{one.id}}))),
             c::visible(!key.current && !key.renaming, c::styled(button_look(86.0f),
                 widgets::Button<asks<account_change::sign_out>>(colours.widgets, "Sign out", ask(account_change::sign_out{{one.id}}))))),
      name_draft{one.name});
}
using session_row_t = decltype(session_row(std::declval<const palette&>(), session_key{}));

// The sessions, as the server listed them: this one first, the others by
// when they were last seen, the latest first.
struct sessions_key {
  std::vector<session_key> current, others;
  friend bool operator==(const sessions_key&, const sessions_key&) = default;
};
[[nodiscard]] inline sessions_key sessions_of(const facts_t& now) {
  const account_state& kept = kept_of(now);
  auto all = kept.sessions.value_or(std::vector<session_info>{});
  std::ranges::stable_sort(all, std::ranges::greater{}, [](const session_info& one) {
    return one.last_seen.value_or(std::chrono::sys_time<std::chrono::milliseconds>{});
  });
  const auto key_of = [&](const session_info& one) {
    return session_key{one, one.id == kept.current, kept.renaming == one.id};
  };
  const auto is_current = [&](const session_info& one) { return one.id == kept.current; };
  return {all | std::views::filter(is_current) | std::views::transform(key_of) | std::ranges::to<std::vector>(),
          all | std::views::filter(std::not_fn(is_current)) | std::views::transform(key_of) | std::ranges::to<std::vector>()};
}
// A list of them under its title, where it has any.
inline auto session_rows(const palette& colours, std::string title, const std::vector<session_key>& rows) {
  namespace c = skiff::compose;
  return c::visible(!rows.empty(), c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .margin = {10.0f, 0.0f, 0.0f, 0.0f}}),
      section_title(colours, std::move(title)),
      c::many(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
              rows | std::views::transform([&](const session_key& one) { return session_row(colours, one); }) | std::ranges::to<std::vector>())));
}
inline auto sessions_list(const palette& colours, const sessions_key& key) {
  namespace c = skiff::compose;
  return c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                   session_rows(colours, "CURRENT SESSION", key.current),
                   session_rows(colours, "OTHER SESSIONS", key.others));
}
using sessions_list_t = decltype(sessions_list(std::declval<const palette&>(), sessions_key{}));

// Element's Security, under Device verification: whether this session has
// the cross-signing keys, and whether room keys are backed up.
[[nodiscard]] inline std::string security_said(const facts_t& now) {
  constexpr std::string_view what =
      "To verify device identity and grant access to encrypted messages: cross-signing. Set it up here, or, where another "
      "session of yours has it, bring it back with your recovery key.";
  const auto& known = kept_of(now).security;
  if (!known)
    return std::string(what);
  return std::format("Cross-signing: {}  ·  Key backup: {}\n{}",
                     known->cross_signing ? "✓ ready on this session" : "⚠ not on this session",
                     known->backup ? "✓ on" : "⚠ off", what);
}
[[nodiscard]] inline bool cross_signing(const facts_t& now) {
  const auto& known = kept_of(now).security;
  return known && known->cross_signing;
}
[[nodiscard]] inline bool backed_up(const facts_t& now) {
  const auto& known = kept_of(now).security;
  return known && known->backup;
}
// What the page says under its title: loading, none, or signing out; what
// the server refused is said apart, in the error's colour.
[[nodiscard]] inline std::string sessions_said(const facts_t& now) {
  const account_state& kept = kept_of(now);
  if (kept.signing_out)
    return "Signing out…";
  if (!kept.sessions)
    return "Loading the sessions…";
  return kept.sessions->empty() ? std::string("No sessions.") : std::string();
}
struct password_draft {
  std::string password;
  friend bool operator==(const password_draft&, const password_draft&) = default;
};
// The password the server asked for to sign them out, and the sign-out
// asked again with it.
inline auto password_row(const palette& colours) {
  namespace c = skiff::compose;
  return c::local<password_draft>(c::column(c::vbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      nodes::Text("Your password, to sign sessions out:", 13.0f, colours.dim),
      c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
             c::bound<skiff::model::Field<&password_draft::password>>(
                 c::styled({.height = 34.0f, .grow = scene::axes::kX}, widgets::TextField<std::string>(colours.widgets, "Password", true))),
             c::asking_for<password_draft>(
                 [](const password_draft& now) { return request::change_account{account_change::sign_out_again{now.password}}; },
                 c::styled({.width = 96.0f, .height = 34.0f}, widgets::Button<scene::NoAction>(colours.widgets, "Sign out", {}))))));
}

// Device verification, as Cinny puts it over the sessions: cross-signing set
// up here, or brought back with the recovery key -- the Privacy page's own
// rows, the same buttons. Without it, a session verified by emoji is
// trusted only by the client that verified it, and nothing is signed.
// Element's last resort under them: new cross-signing keys, the dialog
// saying what it undoes; and its Secure Backup, made anew or deleted.
inline auto account_sessions(const palette& colours, const account_page_facts&) {
  namespace c = skiff::compose;
  return c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      section_title(colours, "DEVICE VERIFICATION"),
      c::text_for<facts_t>(security_said, note(colours, "")),
      row_if<sends<request::setup_cross_signing>>(colours, "Set up cross-signing…", std::not_fn(cross_signing)),
      row_if<sends<request::restore_cross_signing>>(colours, "Restore with the recovery key…", std::not_fn(cross_signing)),
      row_item(colours, "Reset your identity…", sends<request::reset_identity>{}),
      row_if<sends<request::reset_backup>>(colours, "Reset the key backup", cross_signing),
      row_if<sends<request::delete_backup>>(colours, "Delete the key backup", backed_up),
      c::styled({.margin = {14.0f, 0.0f, 0.0f, 0.0f}}, section_title(colours, "SESSIONS")),
      c::shown_for<facts_t>([](const facts_t& now) { return !kept_of(now).refused.empty(); },
          c::text_for<facts_t>([](const facts_t& now) { return kept_of(now).refused; },
                               c::styled({.fillX = true}, wrapped(nodes::Text("", 13.0f, colours.error))))),
      c::shown_for<facts_t>([](const facts_t& now) { return kept_of(now).refused.empty() && !sessions_said(now).empty(); },
          c::text_for<facts_t>(sessions_said, note(colours, ""))),
      c::projected<facts_t>([](const facts_t& now) { return now ? std::optional(sessions_of(now)) : std::nullopt; },
          c::mount<sessions_list_t, sessions_key>([at = &colours](const sessions_key& key) { return std::tuple{sessions_list(*at, key)}; },
                                                  {.fillX = true, .autoSize = scene::axes::kY})),
      // Element's: every session of one's own not verified, signed out.
      row_item(colours, "Sign out unverified sessions…", sends<request::sign_out_unverified>{}),
      c::shown_for<facts_t>([](const facts_t& now) { return kept_of(now).needs_password; }, password_row(colours)),
      c::shown_for<facts_t>([](const facts_t& now) { return sessions_of(now).others.size() > 1; },
          c::styled({.width = 260.0f, .height = 34.0f, .margin = {8.0f, 0.0f, 0.0f, 0.0f}},
                    widgets::Button<asks<account_change::sign_out_others>>(colours.widgets, "Sign out of all other sessions",
                                                                          ask(account_change::sign_out_others{})))),
      c::styled({.width = 100.0f, .height = 30.0f},
                button_for<sends<request::refresh_sessions>>(colours.widgets, "Refresh", {})));
}
using account_sessions_t = decltype(account_sessions(std::declval<const palette&>(), std::declval<const account_page_facts&>()));

// ---- the account's Encryption page ----------------------------------------------
// As Element's Cryptography: room keys to verified sessions only; this
// session, its ID and its key in fours, to be compared with what another
// session shows; its room keys exported or imported, as Element does them;
// cross-signing set up, or restored.
// This session, as its account's protocol state says it, once known.
[[nodiscard]] inline std::string session_line(const facts_t& now) {
  if (!now)
    return {};
  return spl::visit(spl::overloaded{[](const state& known) {
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
inline auto encryption_page(const palette& colours, const account_page_facts&) {
  namespace c = skiff::compose;
  using flip = sends<::mux::ui::request::flip_only_verified>;
  using toggle_t = toggle_for<flip>;
  return c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      section_title(colours, "ENCRYPTION"),
      c::row(c::hbox(16.0f, {.fillX = true, .height = kRowHeight, .padding = {0.0f, 20.0f, 0.0f, 20.0f}}),
             c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                       elided(nodes::Text("Never send encrypted messages to unverified sessions", 15.0f, colours.text))),
             c::applied<facts_t>([](const facts_t& now) { return now && config::only_verified_of(now->account); },
                                 [](toggle_t& toggle, bool on) { toggle.setOn(on); },
                                 c::styled({.alignSelf = scene::align::kMiddle}, toggle_t(colours.widgets, flip{})))),
      c::shown_for<facts_t>([](const facts_t& now) { return !session_line(now).empty(); },
                            c::text_for<facts_t>(session_line, nodes::Text("", 13.0f, colours.dim))),
      row_item(colours, "Export room keys…", sends<request::export_room_keys>{}),
      row_item(colours, "Import room keys…", sends<request::import_room_keys>{}),
      row_item(colours, "Set up cross-signing…", sends<request::setup_cross_signing>{}),
      row_item(colours, "Restore with the recovery key…", sends<request::restore_cross_signing>{}));
}
using encryption_page_t = decltype(encryption_page(std::declval<const palette&>(), std::declval<const account_page_facts&>()));

}  // namespace mux::proto::matrix::settings_detail

// Its pages: their titles, icons, and what each is, made over the facts;
// and what each asks of the program as it opens.
export namespace mux::proto::matrix::settings {
struct profile_policy {
  static constexpr bool banners = true;
  static auto presences() { return std::vector<std::pair<std::string, availability_t>>{{"Online", availability::online{}}, {"Away", availability::away{}}, {"Offline", availability::offline{}}}; }
};
constexpr std::string_view page_title(profile) { return "Profile"; }
inline ui::icon_t page_icon(profile) { return ui::icon::info{}; }
inline auto page_view(profile, const ui::palette& colours, const ui::account_page_facts& first) {
  return ui::profile_page<profile_policy>(colours, first);
}
inline ui::request::refresh_profile asked_as_it_opens(profile, const account_id& by) { return {by}; }

constexpr std::string_view page_title(encryption) { return "Encryption"; }
inline ui::icon_t page_icon(encryption) { return ui::icon::check{}; }
inline auto page_view(encryption, const ui::palette& colours, const ui::account_page_facts& first) {
  return settings_detail::encryption_page(colours, first);
}

constexpr std::string_view page_title(sessions) { return "Sessions"; }
inline ui::icon_t page_icon(sessions) { return ui::icon::info{}; }
inline auto page_view(sessions, const ui::palette& colours, const ui::account_page_facts& first) {
  return settings_detail::account_sessions(colours, first);
}
inline request::refresh_sessions asked_as_it_opens(sessions, const account_id&) { return {}; }
}  // namespace mux::proto::matrix::settings
