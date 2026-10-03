// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui.proto.matrix -- What Matrix shows of its own in the client's UI:
// its overloads on its tag, found by ADL where the UI is made (the program,
// which imports mux.ui.proto). Its account form; and Manage room's tabs of
// Matrix's: a room's name, topic and addresses; its join rule, history and
// encryption; its power levels; its version.
export module mux.ui.proto.matrix;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.flow;
import skiff.nodes.text;
import skiff.widgets.button;
import skiff.widgets.motion;
import skiff.widgets.sliderbar;
import skiff.widgets.textarea;
import mux.core;
import mux.config;
import mux.proto.kept;
import mux.ui;

export namespace mux::proto::matrix {

constexpr ui::manage_tab_list<ui::settings_tab::general, ui::settings_tab::security, ui::settings_tab::roles,
                              ui::settings_tab::advanced>
manage_tabs(tag) {
  return {};
}

}  // namespace mux::proto::matrix

namespace mux::proto::matrix::form_detail {
using namespace ::mux::ui;

// A Matrix account's settings: its user ID and password, the homeserver
// (found through the server's .well-known when left empty) and what this
// device is called.
template <class Actions>
struct matrix_form : nodes::Stack {
  // What the add-account pane says of the protocol.
  static constexpr std::string_view note = "A user ID like @user:example.org, on a homeserver such as Synapse.";
  Actions* actions = nullptr;
  std::optional<std::string> editing;

  struct parts_t {
    field user_id{"User ID", "@user:example.org"};
    field password{"Password", "Password"};
    field homeserver{"Homeserver", "found through the server's .well-known"};
    field device_name{"Device name", "mux", "mux"};
    form_end<Actions> end;
  } parts;

  matrix_form(Actions* a, const std::optional<config::matrix_account>& from)
      : actions(a), parts{.end = form_end<Actions>(a, from.has_value())} {
    auto& [user_id, password, homeserver, device_name, end] = parts;
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    this->setGap(12.0f);
    password.parts.box.setMasked(true);
    if (from) {
      editing = from->user_id;
      user_id.parts.box.setText(from->user_id);
      password.parts.box.setText(from->password);
      if (from->homeserver)
        homeserver.parts.box.setText(*from->homeserver);
      device_name.parts.box.setText(from->device_name);
    }
  }

  [[nodiscard]] std::expected<config::matrix_account, std::string> account() const {
    config::matrix_account out{.user_id = parts.user_id.text(),
                               .password = parts.password.text(),
                               .homeserver = typed_or_nothing(parts.homeserver.text()),
                               .device_name = parts.device_name.text()};
    if (auto wrong = check(out))  // the protocol's own check, by ADL
      return std::unexpected(*wrong);
    return out;
  }

  void say(std::string text, bool error) { parts.end.say(std::move(text), error); }

  using Node::onKey;
  void onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply) {
    if (press.key == scene::keys::kEnter) {
      actions->submit_login();
      reply.handle();
    }
  }

};


}  // namespace mux::proto::matrix::form_detail

export namespace mux::proto::matrix {

// Its account form, for the panels: by the protocol's tag, and by what an
// account of it keeps.
template <class Actions>
using form = form_detail::matrix_form<Actions>;
template <class Actions>
constexpr std::type_identity<form<Actions>> form_type(tag, std::type_identity<Actions>) {
  return {};
}
template <class Actions>
constexpr std::type_identity<form<Actions>> form_type_for(const kept&, std::type_identity<Actions>) {
  return {};
}

}  // namespace mux::proto::matrix
