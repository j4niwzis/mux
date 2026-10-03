// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui.proto.matrix:form -- Matrix's account form.
export module mux.ui.proto.matrix:form;

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

  matrix_form(Actions* a, const std::optional<::mux::proto::matrix::kept>& from)
      : actions(a), parts{.end = form_end<Actions>(legacy_palette(), a, from.has_value())} {
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

  [[nodiscard]] std::expected<::mux::proto::matrix::kept, std::string> account() const {
    ::mux::proto::matrix::kept out{.user_id = parts.user_id.text(),
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
constexpr type_tag<form<Actions>> form_type(const state&, type_tag<Actions>) {
  return {};
}
template <class Actions>
constexpr type_tag<form<Actions>> form_type_for(const kept&, type_tag<Actions>) {
  return {};
}

}  // namespace mux::proto::matrix
