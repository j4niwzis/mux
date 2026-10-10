// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:storage -- Settings: storage and files.
export module mux.ui:storage;

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
import :base;
import :icons;
import :controls;
import :accounts;
import :proxies;

export namespace mux::ui {

// Settings' Notifications page, as Telegram Desktop's: a notification on
// the desktop or not, a sound
// or not; what shows it -- the desktop's own service, or mux's window; and
// whether UnifiedPush wakes mux. Made of the model's widgets, each bound to
// its setting by its member pointer: what is chosen is an edit of the
// model, and what the model holds is what shows.
template <auto Setting, class T = bool>
auto setting_switch(const palette& colours, std::string text) {
  return skiff::compose::row(
      skiff::compose::hbox(16.0f, {.fillX = true,
                                   .height = row_item<nothing>::kHeight,
                                   .padding = {0.0f, 20.0f, 0.0f, 20.0f}}),
      skiff::compose::styled(
          {.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
          elided(nodes::Text(std::move(text), 15.0f, colours.text))),
      skiff::compose::bound<skiff::model::Field<Setting>>(
          skiff::compose::styled({.alignSelf = scene::align::kMiddle},
                                 widgets::ToggleField<T>(colours.widgets))));
}
inline nodes::Text spaced_title(const palette& colours, std::string text) {
  return skiff::compose::styled({.margin = {10.0f, 0.0f, 4.0f, 20.0f}},
                                section_title(colours, std::move(text)));
}
inline nodes::Text spaced_note(const palette& colours, std::string text) {
  return wrapped(skiff::compose::styled(
      {.fillX = true, .margin = {10.0f, 20.0f, 0.0f, 20.0f}},
      note_text(colours, std::move(text))));
}
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
  return skiff::compose::row(
      skiff::compose::hbox(6.0f, {.fillX = true, .height = 50.0f, .padding = {0.0f, 12.0f, 0.0f, 20.0f}}),
      skiff::compose::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                             elided(nodes::Text(std::move(what), 15.0f, colours.text))),
      skiff::compose::text_for<config::cache_limits>(limit_words<Which>{unit},
          skiff::compose::styled({.alignSelf = scene::align::kMiddle}, nodes::Text("", 14.0f, colours.accent, true))),
      skiff::compose::bound<config::cache_limits>(skiff::compose::styled({.alignSelf = scene::align::kMiddle},
          icon_button<limit_press<Which>>(colours, icon::minus{}, {false}))),
      skiff::compose::bound<config::cache_limits>(skiff::compose::styled({.alignSelf = scene::align::kMiddle},
          icon_button<limit_press<Which>>(colours, icon::plus{}, {true}))));
}

// Settings' Storage page: whether what mux keeps on disk is sealed; how
// much is kept in memory and on disk, each a line with its number and a
// step down and up; a way to clear what is kept on disk; whether deleted
// messages are shown, and how much of them is kept; and every chat's room
// events. The limits and the history's switch are the model's widgets; the
// seal, the clearing and every chat's choices are asked as before.
struct local_seal { bool on = false; };
inline auto seal_settings_view(const palette& colours) {
  using field = skiff::model::Field<&local_seal::on>;
  return skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      skiff::compose::row(skiff::compose::hbox(16.0f, {.fillX = true, .height = row_item<nothing>::kHeight,
          .padding = {0.0f, 20.0f, 0.0f, 20.0f}}),
          skiff::compose::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
              nodes::Text("Encrypt local data", 15.0f, colours.text)),
          skiff::compose::bound<field>(skiff::compose::onClick(request::flip_local_encryption{},
              skiff::compose::styled({.alignSelf = scene::align::kMiddle}, widgets::ToggleField<bool>(colours.widgets)),
              "Encrypt local data"))),
      skiff::compose::shown_if<field>([](bool on) { return on; },
          settings_link(colours, "Change the passphrase", icon::pencil{}, request::change_passphrase{})));
}
inline auto storage_settings_view(const palette& colours) {
    namespace limit = config::limit;
    using skiff::compose::bound;
    auto clear = settings_link(colours, "Clear stored messages and pictures", icon::close{}, request::clear_stored{});
    return skiff::compose::column(
        skiff::compose::vbox(0.0f, {.fillX = true,
                                    .autoSize = scene::axes::kY,
                                    .padding = {0.0f, 0.0f, 12.0f, 0.0f}}),
        spaced_title(colours, "ENCRYPTION"), seal_settings_view(colours),
        spaced_note(colours, "Off by default. On, everything mux keeps on disk "
                             "is sealed under a passphrase asked for at "
                             "every start: settings with passwords and tokens, "
                             "chats, drafts, encryption keys. Pictures "
                             "are not kept on disk then."),
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
        bound<skiff::model::Field<&config::history_settings::show_receipts>>(
            show_hide_field<receipts_setting, bool, skiff::model::Field<&config::history_settings::show_receipts>>(
                colours, choice_level::everywhere{})),
        bound<skiff::model::Field<&config::history_settings::link_previews>>(
            show_hide_field<link_previews_setting, bool, skiff::model::Field<&config::history_settings::link_previews>>(
                colours, choice_level::everywhere{})),
        bound<skiff::model::Field<&config::history_settings::previews_direct>>(
            show_hide_field<previews_direct_setting, std::optional<bool>, skiff::model::Field<&config::history_settings::previews_direct>>(
                colours, choice_level::everywhere{})),
        bound<skiff::model::Field<&config::history_settings::jump_search>>(
            jump_search_field<std::int64_t, skiff::model::Field<&config::history_settings::jump_search>>(colours,
                                            choice_level::everywhere{})),
        bound<skiff::model::Field<&config::history_settings::send_typing>>(
            show_hide_field<typing_setting, std::optional<bool>, skiff::model::Field<&config::history_settings::send_typing>>(
                colours, choice_level::everywhere{})),
        spaced_note(colours, "Deleted messages are kept on disk, apart from "
                             "the rest and up to their own size, the "
                             "oldest going first past it. Shown, one stays "
                             "where it was, with all it said and its "
                             "time, marked removed."));
  }
inline auto storage_page(const palette& colours) {
  return skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      page_header<sends<request::settings_home>, sends<request::close_settings>>(colours, "Storage", {}, {}, true, true),
      storage_settings_view(colours));
}
using storage_page_t = decltype(storage_page(std::declval<const palette&>()));

inline auto notification_settings_view(const palette& colours) {
  using every = config::notification_settings;
  auto backend = skiff::compose::bound<skiff::model::Field<&every::backend>>(
      skiff::compose::styled({.margin = {4.0f, 20.0f, 4.0f, 20.0f}}, widgets::ChoiceTabs<config::notify_backend_t>(
          {{"System", config::notify_backend::native{}}, {"Built in", config::notify_backend::built_in{}}})));
  return skiff::compose::column(
      skiff::compose::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}), spaced_title(colours, "NOTIFICATIONS"),
      setting_switch<&every::desktop>(colours, "Notifications"),
      setting_switch<&every::mentions_only, std::optional<bool>>(colours, "Only mentions and keywords"),
      setting_switch<&every::sound>(colours, "Sound"),
      spaced_title(colours, "CUSTOM SOUND"),
      skiff::compose::bound<skiff::model::Field<&every::sound_file>>(skiff::compose::styled(
          {.fillX = true, .height = 36.0f, .margin = {4.0f, 20.0f, 4.0f, 20.0f}},
          widgets::TextField<std::optional<std::string>>(colours.widgets, "Ogg Opus or Vorbis file; empty uses the default"))),
      spaced_title(colours, "SHOWN BY"), std::move(backend),
      spaced_note(colours, "System asks the desktop's own notification service (org.freedesktop.Notifications); Built in "
                           "shows mux's own, in a corner of the screen, as Telegram Desktop does."),
      spaced_title(colours, "WAKE"),
      setting_switch<&every::unified_push, std::optional<bool>>(colours, "Wake by UnifiedPush"),
      spaced_note(colours, "Your Matrix servers push to the UnifiedPush distributor on this device (ntfy, NextPush, "
                           "KDE's), which wakes mux at once. Off, nothing is given to the servers, and mux only learns "
                           "of messages while it runs."));
}
inline auto notifications_page(const palette& colours) {
  using header = page_header_t<sends<request::settings_home>, sends<request::close_settings>>;
  return skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      skiff::compose::styled({.depth = 1.0f, .background = colours.sidebar}, page_header<sends<request::settings_home>, sends<request::close_settings>>(colours, "Notifications", {}, {}, true, true)),
      notification_settings_view(colours));
}
using notifications_page_t = decltype(notifications_page(std::declval<const palette&>()));

// Settings' Files page: what is done to a picture dropped on the window
// before it is sent.
inline auto files_settings_view(const palette& colours) {
  using sending = config::sending_settings;
  return skiff::compose::column(
      skiff::compose::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      spaced_title(colours, "PICTURES DROPPED ON THE WINDOW"),
      setting_switch<&sending::strip_metadata>(colours, "Remove metadata"),
      setting_switch<&sending::rename>(colours, "Name them image.<type>"),
      spaced_note(colours, "Metadata is where and when a picture was taken, with what, by whom: EXIF, XMP and the like. "
                           "It is cut out of the file; the picture itself is sent as it is, not compressed again."));
}
inline auto files_page(const palette& colours) {
  using header = page_header_t<sends<request::settings_home>, sends<request::close_settings>>;
  return skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      skiff::compose::styled({.depth = 1.0f, .background = colours.sidebar}, page_header<sends<request::settings_home>, sends<request::close_settings>>(colours, "Files", {}, {}, true, true)),
      files_settings_view(colours));
}
using files_page_t = decltype(files_page(std::declval<const palette&>()));

}  // namespace mux::ui
