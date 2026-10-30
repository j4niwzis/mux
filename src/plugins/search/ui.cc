// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.search.ui -- Finding in a chat, as tdesktop's search in a chat:
// in place of the head, a field with the magnifier, how many are found and
// which is shown ("3 of 12"), the arrows to the newer and the older, and
// close. Enter goes to the older one, Shift+Enter to the newer, Esc closes;
// Up and Down step. And the magnifier in the chat's head that opens it.
module;
#include "../../inline_in_release.h"
#include "skiff/named.h"
export module mux.plugin.search.ui;

import std;
import skiff.scene;
import skiff.compose;
import skiff.nodes.box;
import skiff.nodes.text;
import skiff.widgets.textbox;
import mux.composition;
import mux.core;
import mux.ui.base;
import mux.plugin.search.types;

export namespace mux::plugins::search {
using namespace ::mux::ui;

// What the bar says of it: a callable type, made as it is shown.
struct found_said_t {
  std::string operator()(const shown& now) const {
    return !now.asked ? std::string() : now.of == 0 ? std::string("No results") : std::format("{} of {}", now.at.value_or(0) + 1, now.of);
  }
};
struct typed_act {
  using Answer = std::optional<request::search_typed>;
  std::optional<request::search_typed> operator()(std::string_view text) { return request::search_typed{std::string(text)}; }
};
struct step_act {
  using Answer = request::search_step;
  bool older = true;
  request::search_step operator()() { return request::search_step{older}; }
};
MUX_INLINE_IN_RELEASE auto search_bar(const palette& colours) {
  namespace c = skiff::compose;
  const scene::Spec middle{.alignSelf = scene::align::kMiddle};
  auto field = widgets::TextBox<typed_act>(colours.widgets, "Search", {});
  field.setSearchIcon(true);
  const auto keys = [](const scene::key::down& press) -> std::optional<std::variant<request::search_step, request::close_search>> {
    namespace keys = scene::keys;
    if (press.key == keys::kEnter)
      return request::search_step{!press.modifiers.template has<scene::modifier::shift>()};
    if (press.key == keys::kEscape)
      return request::close_search{};
    if (press.key == keys::kUp || press.key == keys::kDown)
      return request::search_step{press.key == keys::kDown};
    return std::nullopt;
  };
  return SKIFF_NAMED(search_bar, c::shown_for<shown>([](const shown& now) { return now.open; },
      c::onKeys(keys,
          c::row(c::hbox(4.0f, {.fillX = true, .height = kChatHeadHeight, .padding = {0.0f, 16.0f, 1.0f, 22.0f},
                                .background = colours.sidebar}),
                 c::focused_by<keys_asked>(keys_on<keys::search>{},
                     c::applied<shown>([](const shown& now) { return now.open; },
                         [](widgets::TextBox<typed_act>& typed, bool open) {
                           if (!open && !typed.text().empty())
                             typed.setText({});
                         },
                         c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, std::move(field)))),
                 c::text_for<shown>(found_said_t{}, c::styled(middle, nodes::Text("", 13.0f, colours.dim))),
                 c::styled(middle, icon_button(colours, icon::up{}, step_act{false})),
                 c::styled(middle, icon_button(colours, icon::down{}, step_act{true})),
                 c::styled(middle, icon_button(colours, icon::close{}, sends<request::close_search>{})),
                 c::styled({.place = scene::anchor::kBottomLeft, .fillX = true, .height = 1.0f}, nodes::Box<>(colours.band))))));
}

// Its bar in the chat's column (mux.ui:conversations_screen), under the head
// it stands in place of while it is open.
MUX_INLINE_IN_RELEASE auto extend(contract::chat_column, type_tag<feature>, const ui_needs& needs) { return search_bar(*needs.colours); }
// The magnifier in the chat's head (mux.ui:header).
MUX_INLINE_IN_RELEASE auto extend(contract::chat_header, type_tag<feature>, const palette& colours) {
  return SKIFF_NAMED(search_button, skiff::compose::styled({.alignSelf = scene::align::kMiddle},
                                                           icon_button(colours, icon::search{}, sends<request::open_search>{})));
}

}  // namespace mux::plugins::search
