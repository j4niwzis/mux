// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui.proto.xmpp -- What XMPP shows of its own in the client's UI: its
// account form, found by ADL where the panels are made.
export module mux.ui.proto.xmpp;

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

namespace mux::proto::xmpp::form_detail {
using namespace ::mux::ui;

// What "Advanced" folds out on an XMPP form: the resource, where to connect,
// and PLAIN without TLS. Its height is what its last layout took.
template <class Actions>
struct xmpp_advanced : nodes::Stack {
  using plain_toggle = widgets::Toggle<ask<Actions, &Actions::toggle_plain>>;
  // The switch and what it says, side by side.
  struct plain_row : nodes::Stack {
    struct parts_t {
      plain_toggle plain;
      nodes::Text label;
    } parts;
    plain_row(Actions* a, const palette& colours)
        : parts{.plain = plain_toggle(colours.widgets, {a}),
                .label = nodes::Text("Allow PLAIN without TLS. Only for a test server on this machine: never over a network.",
                                     13.0f, colours.error)} {
      this->setHorizontal();
      this->setGap(10.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      parts.label.setWrapped(true);
      parts.label.apply({.grow = scene::axes::kX});
    }
  };
  struct parts_t {
    field resource{"Device name (resource)", "mux", "mux"};
    field host{"Host", "from the domain's SRV records"};
    field port{"Port", "5222"};
    plain_row row;
  } parts;

  xmpp_advanced(Actions* a, const palette& colours) : parts{.row = plain_row(a, colours)} {
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 0.0f, 12.0f, 0.0f}});
    this->setGap(8.0f);
  }
  [[nodiscard]] plain_toggle& plain() { return parts.row.parts.plain; }
  [[nodiscard]] const plain_toggle& plain() const { return parts.row.parts.plain; }
};

// An XMPP account's settings: its JID and password, and "Advanced" folds out
// the rest. It fills the column it is given.
template <class Actions>
struct xmpp_form : nodes::Stack {
  // What the add-account pane says of the protocol.
  static constexpr std::string_view note = "An address like user@example.com, on a server such as Prosody or ejabberd.";
  Actions* actions = nullptr;
  // The address the account was saved under, when this is an edit of one.
  std::optional<std::string> editing;
  bool advanced = false;

  using advanced_button_t = widgets::Button<ask<Actions, &Actions::toggle_advanced>>;
  struct parts_t {
    field address{"Address (JID)", "user@example.com"};
    field password{"Password", "Password"};
    advanced_button_t advanced_button;
    widgets::Collapsible<xmpp_advanced<Actions>> more;
    form_end<Actions> end;
  } parts;

  xmpp_form(Actions* a, const palette& colours, const std::optional<::mux::proto::xmpp::kept>& from)
      : actions(a),
        parts{.advanced_button = advanced_button_t(colours.widgets, "Advanced", {a}),
              .more = widgets::Collapsible<xmpp_advanced<Actions>>(a, colours),
              .end = form_end<Actions>(colours, a, from.has_value())} {
    auto& [address, password, advanced_button, more, end] = parts;
    auto& folded = more.child().parts;
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    this->setGap(12.0f);
    password.parts.box.setMasked(true);
    advanced_button.apply({.width = 120.0f, .height = 36.0f});
    if (from) {
      editing = from->address;
      address.parts.box.setText(from->address);
      password.parts.box.setText(from->password);
      folded.resource.parts.box.setText(from->resource);
      if (from->host)
        folded.host.parts.box.setText(*from->host);
      if (from->port)
        folded.port.parts.box.setText(std::to_string(*from->port));
      more.child().plain().setOn(from->plain_without_tls);
      advanced = from->resource != "mux" || from->host || from->port || from->plain_without_tls;
    }
    more.setOpenNow(advanced);
  }

  // Folded out or away: smoothly, where things move.
  void show_advanced(bool shown) {
    advanced = shown;
    parts.more.setOpen(shown);
  }

  void flip_plain() { parts.more.child().plain().setOn(!parts.more.child().plain().on()); }

  // The account as typed, or what is wrong with it. What is folded away is
  // kept as it is: folding is not clearing.
  [[nodiscard]] std::expected<::mux::proto::xmpp::kept, std::string> account() const {
    const auto& folded = parts.more.child().parts;
    ::mux::proto::xmpp::kept out{.address = parts.address.text(),
                             .password = parts.password.text(),
                             .resource = folded.resource.text(),
                             .host = typed_or_nothing(folded.host.text()),
                             .plain_without_tls = parts.more.child().plain().on()};
    if (const std::string& text = folded.port.text(); !text.empty()) {
      std::int64_t number = 0;
      const auto [last, failed] = std::from_chars(text.data(), text.data() + text.size(), number);
      if (failed != std::errc{} || last != text.data() + text.size())
        return std::unexpected("A port is a number from 1 to 65535");
      out.port = number;
    }
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


}  // namespace mux::proto::xmpp::form_detail

export namespace mux::proto::xmpp {

// Its account form, for the panels: by the protocol's tag, and by what an
// account of it keeps.
template <class Actions>
using form = form_detail::xmpp_form<Actions>;
template <class Actions>
constexpr type_tag<form<Actions>> form_type(const state&, type_tag<Actions>) {
  return {};
}
template <class Actions>
constexpr type_tag<form<Actions>> form_type_for(const kept&, type_tag<Actions>) {
  return {};
}

}  // namespace mux::proto::xmpp
