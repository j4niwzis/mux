// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.storage.ui -- Settings' Storage page -- whether what mux keeps
// on disk is sealed, how much is kept in memory and on disk, clearing it,
// deleted messages, every chat's room events and what they show -- linked
// from Settings' home (contract::settings_links,
// settings_pages).
module;
#include "../../inline_in_release.h"
#include "skiff/named.h"
export module mux.plugin.storage.ui;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.flow;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.model;
import skiff.compose;
import skiff.widgets.model;
import skiff.bind;
import mux.core;
import mux.config;
import mux.composition;
import mux.ui.base;
import mux.plugin.storage.types;
import mux.levels;

export namespace mux::plugins::storage {

using namespace ::mux::ui;

// A limit, as the model holds it, shown with a step down and up: halved or
// doubled within its bounds, the whole of the limits set again.
template <class Which> struct limit_change {
  bool more = false;
  config::cache_limits operator()(config::cache_limits now) const {
    const auto [low, high] = config::bounds_of(config::limit_t{Which{}});
    auto &value = config::value_of(now, Which{});
    value = std::clamp(more ? value * 2 : value / 2, low, high);
    return now;
  }
};
template <class Which> struct limit_press {
  using Answer = skiff::bind::Own<limit_change<Which>>;
  bool more = false;
  Answer operator()() const {
    return skiff::bind::own(limit_change<Which>{more});
  }
};
template <class Which> struct limit_words {
  std::string_view unit;
  std::string operator()(config::cache_limits now) const {
    return std::format("{} {}", config::value_of(now, Which{}), unit);
  }
};
template <class Which>
auto limit_stepper(const palette& colours, std::string what, std::string_view unit) {
  return SKIFF_NAMED(limit_stepper, skiff::compose::row(
      skiff::compose::hbox(6.0f, {.fillX = true, .height = 50.0f, .padding = {0.0f, 12.0f, 0.0f, 20.0f}}),
      skiff::compose::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                             elided(nodes::Text(std::move(what), 15.0f, colours.text))),
      skiff::compose::text_for<config::cache_limits>(limit_words<Which>{unit},
          skiff::compose::styled({.alignSelf = scene::align::kMiddle}, nodes::Text("", 14.0f, colours.accent, true))),
      skiff::compose::bound<config::cache_limits>(skiff::compose::styled({.alignSelf = scene::align::kMiddle},
          icon_button(colours, icon::minus{}, limit_press<Which>{false}))),
      skiff::compose::bound<config::cache_limits>(skiff::compose::styled({.alignSelf = scene::align::kMiddle},
          icon_button(colours, icon::plus{}, limit_press<Which>{true})))));
}

// Settings' Storage page: whether what mux keeps on disk is sealed; how
// much is kept in memory and on disk, each a line with its number and a
// step down and up; a way to clear what is kept on disk; whether deleted
// messages are shown, and how much of them is kept; and every chat's room
// events. The limits and the history's switch are the model's widgets; the
// seal, the clearing and every chat's choices are asked as before.
MUX_INLINE_IN_RELEASE auto seal_settings_view(const palette& colours) {
  using field = skiff::model::Field<&local_seal::on>;
  return SKIFF_NAMED(seal_settings_view, skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      skiff::compose::row(skiff::compose::hbox(16.0f, {.fillX = true, .height = kRowHeight,
          .padding = {0.0f, 20.0f, 0.0f, 20.0f}}),
          skiff::compose::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
              nodes::Text("Encrypt local data", 15.0f, colours.text)),
          skiff::compose::bound<field>(skiff::compose::onClick(request::flip_local_encryption{},
              skiff::compose::styled({.alignSelf = scene::align::kMiddle}, widgets::ToggleField<bool>(colours.widgets)),
              "Encrypt local data"))),
      skiff::compose::shown_if<field>([](bool on) { return on; },
          settings_link(colours, "Change the passphrase", icon::pencil{}, request::change_passphrase{}))));
}
MUX_INLINE_IN_RELEASE auto storage_settings_view(const auto& colours) {
    namespace limit = config::limit;
    using skiff::compose::bound;
    auto clear = settings_link(colours, "Clear stored messages and pictures", icon::close{}, request::clear_stored{});
    return SKIFF_NAMED(storage_settings_view, skiff::compose::column(
        skiff::compose::vbox(0.0f, {.fillX = true,
                                    .autoSize = scene::axes::kY,
                                    .padding = {0.0f, 0.0f, 12.0f, 0.0f}}),
#ifdef MUX_LOCAL_ENCRYPTION
        spaced_title(colours, "ENCRYPTION"), seal_settings_view(colours),
        spaced_note(colours, "Off by default. On, everything mux keeps on disk "
                             "is sealed under a passphrase asked for at "
                             "every start: settings with passwords and tokens, "
                             "chats, drafts, encryption keys. Pictures "
                             "are not kept on disk then."),
#endif
        spaced_title(colours, "IN MEMORY"),
        limit_stepper<limit::messages_in_memory>(colours, "Messages",
                                                 "messages"),
        limit_stepper<limit::pictures_in_memory>(colours, "Pictures", "MB"),
        spaced_title(colours, "ON DISK"),
        limit_stepper<limit::messages_on_disk>(colours, "Messages", "MB"),
        limit_stepper<limit::pictures_on_disk>(colours, "Pictures", "MB"),
        std::move(clear),
        spaced_note(colours,
                    "Memory holds the newest of the chats read lately; the "
                    "disk holds the rest, and what is scrolled "
                    "back to comes from there before the server. Past a limit, "
                    "what was used longest ago goes first."),
        spaced_title(colours, "DELETED MESSAGES"),
        setting_switch<&config::history_settings::show_deleted>(
            colours, "Show deleted messages"),
        limit_stepper<limit::deleted_on_disk>(colours, "On disk", "MB"),
        spaced_title(colours, "ROOM EVENTS"),
        event_kinds_field<config::history_settings>(colours, choice_level::everywhere{}),
        bound<skiff::model::Field<&config::history_settings::jump_search>>(
            jump_search_field<std::int64_t, skiff::model::Field<&config::history_settings::jump_search>>(colours,
                                            choice_level::everywhere{})),
        choice_row_plugins::made(colours, level_place::every{}),
        spaced_note(colours, "Deleted messages are kept on disk, apart from "
                             "the rest and up to their own size, the "
                             "oldest going first past it. Shown, one stays "
                             "where it was, with all it said and its "
                             "time, marked removed.")));
  }
MUX_INLINE_IN_RELEASE auto storage_view(const auto& colours) {
  return SKIFF_NAMED(storage_view, skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      page_header<sends<request::settings_home>, sends<request::close_settings>>(colours, "Storage", {}, {}, true, true),
      storage_settings_view(colours)));
}


// Settings' links to them, and the pages.
MUX_INLINE_IN_RELEASE auto extend(contract::settings_links, type_tag<feature>, const palette& colours) {
  return skiff::compose::column(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                                settings_link(colours, "Storage", icon::clip{}, open_storage{}));
}
MUX_INLINE_IN_RELEASE auto extend(contract::settings_pages, const auto& n, const storage_page&) { return storage_view(*n.colours); }

}  // namespace mux::plugins::storage
