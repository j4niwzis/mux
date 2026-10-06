// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui.proto.matrix:sessions -- The account's Sessions and Encryption pages.
export module mux.ui.proto.matrix:sessions;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.flow;
import skiff.nodes.text;
import skiff.nodes.scroll;
import skiff.widgets.button;
import skiff.widgets.motion;
import skiff.widgets.sliderbar;
import skiff.widgets.textarea;
import skiff.widgets.textbox;
import mux.core;
import mux.config;
import mux.proto.kept;
import mux.proto.matrix;
import mux.proto.matrix.requests;
import mux.ui;

// ---- the account's Sessions page -----------------------------------------------
namespace mux::proto::matrix::settings_detail {
using namespace ::mux::ui;

// An account's Sessions page, as Element's: this session first, then the
// others -- each its name, its ID, where and when it was last seen -- each to
// rename or sign out, and all the others at once. Where the server asks for
// the password to sign one out, a field for it.
// A session's ID, when it was last seen, from where: as Element says them.
// Its time told by mux.ui's seen_at: clang 23 crashed on that format
// string made first in this module (see app/network.cc).
[[nodiscard]] inline std::string facts_of(const proto::matrix::session_info& one, bool current) {
  std::string out = one.id;
  if (current)
    out += " · this session";
  if (one.last_seen)
    out += " · last seen " + seen_at(*one.last_seen);
  if (one.ip)
    out += " · " + *one.ip;
  return out;
}

template <class Actions>
struct account_sessions : nodes::Stack {
  Actions* actions = nullptr;
  // The colours it is made in, for the rows it makes later.
  const palette* colours_ = nullptr;
  struct sign_out_one {
    account_sessions* page;
    std::string device;
    void operator()() const { page->sign_out({device}); }
  };
  struct sign_out_rest {
    account_sessions* page;
    void operator()() const { page->sign_out(page->others); }
  };
  struct verify_one {
    account_sessions* page;
    std::string device;
    void operator()() const { page->actions->ask_for(request::verify_session{device}); }
  };
  struct start_rename {
    account_sessions* page;
    std::size_t row;
    void operator()() const { page->renaming(row); }
  };
  struct save_rename {
    account_sessions* page;
    std::size_t row;
    void operator()() const { page->rename(row); }
  };
  struct reload {
    Actions* actions;
    void operator()() const { actions->ask_for(request::refresh_sessions{}); }
  };
  // One session: its name over its ID, when and where it was last seen;
  // Rename, and Sign out where it is not this one.
  struct session_row : nodes::Stack {
    std::string device;
    std::string name;
    struct lines_t : two_lines {
      lines_t(const palette& colours, std::string shown, std::string facts)
          : two_lines(colours, std::move(shown), std::move(facts), 15.0f, 2.0f, 12.0f) {}
    };
    struct parts_t {
      lines_t lines;
      widgets::TextBox<> field;
      widgets::Button<save_rename> save;
      widgets::Button<start_rename> rename;
      std::optional<widgets::Button<verify_one>> verify;
      std::optional<widgets::Button<sign_out_one>> sign_out;
    } parts;
    session_row(account_sessions* page, std::size_t index, const proto::matrix::session_info& one, bool current)
        : device(one.id), name(one.name),
          parts{.lines = lines_t(*page->colours_, one.name.empty() ? std::string("Unnamed session") : one.name, facts_of(one, current)),
                .field = widgets::TextBox<>((*page->colours_).widgets, "Session name"),
                .save = widgets::Button<save_rename>((*page->colours_).widgets, "Save", {page, index}),
                .rename = widgets::Button<start_rename>((*page->colours_).widgets, "Rename", {page, index})} {
      this->setHorizontal();
      this->setGap(8.0f);
      fState.apply({.fillX = true, .height = 60.0f, .padding = {0.0f, 12.0f, 0.0f, 12.0f}, .cornerRadius = 8.0f,
                    .background = page->colours_->tile});
      parts.field.setText(one.name);
      parts.field.apply({.height = 32.0f, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      parts.field.setVisible(false);
      parts.save.apply({.width = 70.0f, .height = 30.0f, .alignSelf = scene::align::kMiddle});
      parts.save.setVisible(false);
      parts.rename.apply({.width = 80.0f, .height = 30.0f, .alignSelf = scene::align::kMiddle});
      if (!current) {
        parts.verify.emplace((*page->colours_).widgets, "Verify", verify_one{page, one.id});
        parts.verify->apply({.width = 70.0f, .height = 30.0f, .alignSelf = scene::align::kMiddle});
        parts.sign_out.emplace((*page->colours_).widgets, "Sign out", sign_out_one{page, one.id});
        parts.sign_out->apply({.width = 86.0f, .height = 30.0f, .alignSelf = scene::align::kMiddle});
      }
    }
    void show_field(bool on) {
      parts.lines.setVisible(!on);
      parts.field.setVisible(on);
      parts.save.setVisible(on);
      parts.rename.setVisible(!on);
      this->invalidateLayout();
    }
  };
  struct password_row : nodes::Stack {
    struct parts_t {
      nodes::Text label;
      widgets::TextBox<> field;
    } parts;
    explicit password_row(const palette& colours)
        : parts{.label = nodes::Text("Your password, to sign sessions out:", 13.0f, colours.dim),
                .field = widgets::TextBox<>(colours.widgets, "Password")} {
      this->setGap(6.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      parts.field.setMasked(true);
      parts.field.apply({.fillX = true, .height = 34.0f});
    }
  };
  // Device verification, as Cinny puts it over the sessions: cross-signing
  // set up here, or brought back with the recovery key -- the Privacy page's
  // own rows, the same buttons. Without it, a session verified by emoji is
  // trusted only by the client that verified it, and nothing is signed.
  using set_up_row = row_item<asks<Actions, request::setup_cross_signing>>;
  using restore_row = row_item<asks<Actions, request::restore_cross_signing>>;
  using reset_row = row_item<asks<Actions, request::reset_identity>>;
  using reset_backup_row = row_item<asks<Actions, request::reset_backup>>;
  using delete_backup_row = row_item<asks<Actions, request::delete_backup>>;
  using sign_out_unverified_row = row_item<asks<Actions, request::sign_out_unverified>>;
  struct parts_t {
    nodes::Text verification_title;
    nodes::Text verification_note;
    set_up_row set_up;
    restore_row restore;
    // Element's last resort: new cross-signing keys, the dialog saying what
    // it undoes.
    reset_row reset;
    // Element's Secure Backup: made anew, or deleted.
    reset_backup_row reset_backup;
    delete_backup_row delete_backup;
    nodes::Text title;
    nodes::Text note;
    nodes::Text current_title;
    std::vector<session_row> current;
    nodes::Text others_title;
    std::vector<session_row> rows;
    // Element's: every session of one's own not verified, signed out.
    sign_out_unverified_row sign_out_unverified;
    password_row password;
    widgets::Button<sign_out_rest> rest;
    widgets::Button<reload> refresh;
  } parts;
  std::vector<std::string> others;

  // Its sessions asked of the server as it opens.
  account_sessions(Actions* a, const palette& colours, const ui_shared&, const config::account_t&, const model&)
      : actions(a), colours_(&colours), parts{.verification_title = section_title(colours, "DEVICE VERIFICATION"),
              .verification_note = nodes::Text("To verify device identity and grant access to encrypted messages: cross-signing. " "Set it up here, or, where another session of yours has it, bring it back with " "your recovery key.", 13.0f, colours.dim),
              .set_up = set_up_row(colours, "Set up cross-signing\u2026", {a}),
              .restore = restore_row(colours, "Restore with the recovery key\u2026", {a}),
              .reset = reset_row(colours, "Reset your identity\u2026", {a}),
              .reset_backup = reset_backup_row(colours, "Reset the key backup", {a}),
              .delete_backup = delete_backup_row(colours, "Delete the key backup", {a}),
              .title = section_title(colours, "SESSIONS"),
              .note = nodes::Text("Loading the sessions…", 13.0f, colours.dim),
              .current_title = section_title(colours, "CURRENT SESSION"),
              .others_title = section_title(colours, "OTHER SESSIONS"),
              .sign_out_unverified = sign_out_unverified_row(colours, "Sign out unverified sessions\u2026", {a}),
              .password = password_row(colours),
              .rest = widgets::Button<sign_out_rest>(colours.widgets, "Sign out of all other sessions", {this}),
              .refresh = widgets::Button<reload>(colours.widgets, "Refresh", {a})} {
    this->setGap(8.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    parts.verification_note.setWrapped(true);
    parts.verification_note.apply({.fillX = true});
    parts.title.apply({.margin = {14.0f, 0.0f, 0.0f, 0.0f}});
    parts.note.setWrapped(true);
    parts.note.apply({.fillX = true});
    for (nodes::Text* each : {&parts.current_title, &parts.others_title})
      each->apply({.margin = {10.0f, 0.0f, 0.0f, 0.0f}});
    parts.password.setVisible(false);
    parts.rest.apply({.width = 260.0f, .height = 34.0f, .margin = {8.0f, 0.0f, 0.0f, 0.0f}});
    parts.rest.setVisible(false);
    parts.refresh.apply({.width = 100.0f, .height = 30.0f});
    for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.current_title, &parts.others_title})
      each->setVisible(false);
    a->ask_for(request::refresh_sessions{});
  }
  // Element's Security, under Device verification: whether this session has
  // the cross-signing keys, and whether room keys are backed up.
  void show_security(bool cross_signing, bool backup) {
    parts.verification_note.setText(std::format(
        "Cross-signing: {}  \u00b7  Key backup: {}\nTo verify device identity and grant access to encrypted messages: "
        "set it up here, or, where another session of yours has it, bring it back with your recovery key.",
        cross_signing ? "\u2713 ready on this session" : "\u26A0 not on this session",
        backup ? "\u2713 on" : "\u26A0 off"));
    parts.set_up.setVisible(!cross_signing);
    parts.restore.setVisible(!cross_signing);
    parts.reset_backup.setVisible(cross_signing);
    parts.delete_backup.setVisible(backup);
    this->invalidateLayout();
  }
  // The sessions, as the server listed them: this one first, the others by
  // when they were last seen, the latest first.
  void show(const std::string& current, std::vector<proto::matrix::session_info> all) {
    std::ranges::sort(all, std::ranges::greater{}, [](const proto::matrix::session_info& one) {
      return one.last_seen.value_or(std::chrono::sys_time<std::chrono::milliseconds>{});
    });
    parts.current.clear();
    parts.rows.clear();
    others.clear();
    for (const proto::matrix::session_info& one : std::views::filter(all, [&](const proto::matrix::session_info& s) { return s.id == current; }))
      parts.current.emplace_back(this, 0, one, true);
    std::size_t index = 0;
    for (const proto::matrix::session_info& one : std::views::filter(all, [&](const proto::matrix::session_info& s) { return s.id != current; })) {
      parts.rows.emplace_back(this, ++index, one, false);
      others.push_back(one.id);
    }
    parts.note.setText(all.empty() ? std::string("No sessions.") : std::string());
    parts.note.setVisible(all.empty());
    parts.current_title.setVisible(!parts.current.empty());
    parts.others_title.setVisible(!parts.rows.empty());
    parts.rest.setVisible(parts.rows.size() > 1);
    this->invalidateLayout();
  }
  // The server said no: why; and the password field, where that is it.
  void refused(const std::string& why, bool needs_password) {
    parts.note.setText(why);
    parts.note.setColour(colours_->error);
    parts.note.setVisible(true);
    if (needs_password)
      parts.password.setVisible(true);
    this->invalidateLayout();
  }
  void sign_out(std::vector<std::string> devices) {
    if (devices.empty())
      return;
    parts.note.setText("Signing out…");
    parts.note.setColour(colours_->dim);
    parts.note.setVisible(true);
    actions->ask_for(request::sign_out_sessions{std::move(devices), parts.password.parts.field.text()});
    this->invalidateLayout();
  }
  [[nodiscard]] session_row* row_at(std::size_t index) {
    if (index == 0)
      return parts.current.empty() ? nullptr : &parts.current.front();
    return index <= parts.rows.size() ? &parts.rows[index - 1] : nullptr;
  }
  void renaming(std::size_t index) {
    if (session_row* row = this->row_at(index))
      row->show_field(true);
  }
  void rename(std::size_t index) {
    if (session_row* row = this->row_at(index)) {
      row->show_field(false);
      actions->ask_for(request::rename_session{row->device, row->parts.field.text()});
    }
  }
  void say(std::string, bool) {}
};

// The account's Encryption page, as Element's Cryptography: room keys to
// verified sessions only; this session, its ID and its key in fours, to be
// compared with what another session shows; its room keys exported or
// imported, as Element does them; cross-signing set up, or restored.
template <class Actions>
struct encryption_page : nodes::Stack {
  using only_verified_row = switch_row<ask<Actions, &Actions::flip_only_verified>>;
  using export_row = row_item<asks<Actions, request::export_room_keys>>;
  using import_row = row_item<asks<Actions, request::import_room_keys>>;
  using cross_signing_row = row_item<asks<Actions, request::setup_cross_signing>>;
  using recovery_row = row_item<asks<Actions, request::restore_cross_signing>>;
  struct parts_t {
    nodes::Text title;
    only_verified_row only_verified;
    nodes::Text session_line;
    export_row export_keys;
    import_row import_keys;
    cross_signing_row cross_signing;
    recovery_row recovery;
  } parts;

  encryption_page(Actions* a, const palette& colours, const ui_shared& shared, const config::account_t& one, const model& now)
      : parts{.title = section_title(colours, "ENCRYPTION"),
              .only_verified = only_verified_row(colours, "Never send encrypted messages to unverified sessions", {a}),
              .session_line = nodes::Text("", 13.0f, colours.dim),
              .export_keys = export_row(colours, "Export room keys\u2026", {a}),
              .import_keys = import_row(colours, "Import room keys\u2026", {a}),
              .cross_signing = cross_signing_row(colours, "Set up cross-signing\u2026", {a}),
              .recovery = recovery_row(colours, "Restore with the recovery key\u2026", {a})} {
    this->setGap(8.0f);
    fState.apply({.fill = true});
    parts.only_verified.parts.toggle.setOnNow(config::only_verified_of(one));
    const std::string& address = config::address_of(one);
    this->show_session(protocol_state_of(shared, account_id{protocol_of(address), address}));
  }
  void show_only_verified(bool on) { parts.only_verified.parts.toggle.setOn(on); }
  // This session, as its account's protocol state says it, once known.
  void show_session(const protocol_state_t& known) {
    const auto own = spl::visit(
        spl::overloaded{[](const state& now) {
                             return now.device_id.empty() ? std::optional<std::pair<std::string, std::string>>()
                                                          : std::optional(std::pair{now.device_id, now.ed25519});
                           },
                           [](const auto&) { return std::optional<std::pair<std::string, std::string>>(); }},
        known);
    parts.session_line.setVisible(own.has_value());
    if (!own)
      return;
    const std::string grouped = std::ranges::to<std::string>(std::views::join(std::views::transform(std::views::enumerate(own->second), [](const auto& at) {
                                  const auto [index, letter] = at;
                                  return index > 0 && index % 4 == 0 ? std::string{' ', letter} : std::string(1, letter);
                                })));
    parts.session_line.setText(std::format("Session ID: {}\nSession key: {}", own->first, grouped));
  }
  void say(std::string, bool) {}
};

}  // namespace mux::proto::matrix::settings_detail

export namespace mux::proto::matrix {
// For the program, which tells it what the client says of the sessions.
template <class Actions>
using sessions_page = settings_detail::account_sessions<Actions>;
}  // namespace mux::proto::matrix

export namespace mux::proto::matrix::settings {
constexpr std::string_view page_title(encryption) { return "Encryption"; }
inline ui::icon_t page_icon(encryption) { return ui::icon::check{}; }
template <class Actions>
constexpr type_tag<settings_detail::encryption_page<Actions>> page_type(encryption, type_tag<Actions>) {
  return {};
}
constexpr std::string_view page_title(sessions) { return "Sessions"; }
inline ui::icon_t page_icon(sessions) { return ui::icon::info{}; }
template <class Actions>
constexpr type_tag<settings_detail::account_sessions<Actions>> page_type(sessions, type_tag<Actions>) {
  return {};
}
}  // namespace mux::proto::matrix::settings
