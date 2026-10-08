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
// the desktop or not, the sender's name and the text in it or not, a sound
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
template <class Which> struct limit_stepper : skiff::compose::Stacked {
  using step_button =
      skiff::bind::Bound<config::cache_limits, icon_button<limit_press<Which>>>;
  using value_t = decltype(skiff::compose::text_for<config::cache_limits>(
      limit_words<Which>{}, std::declval<nodes::Text>()));
  struct parts_t {
    nodes::Text label;
    value_t value;
    step_button less;
    step_button more;
  } parts;
  limit_stepper(const palette &colours, std::string what, std::string_view unit)
      : Stacked(skiff::compose::hbox(6.0f,
                                     {.fillX = true,
                                      .height = 50.0f,
                                      .padding = {0.0f, 12.0f, 0.0f, 20.0f}})),
        parts{.label = skiff::compose::styled(
                  {.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                  elided(nodes::Text(std::move(what), 15.0f, colours.text))),
              .value = skiff::compose::text_for<config::cache_limits>(
                  limit_words<Which>{unit},
                  skiff::compose::styled(
                      {.alignSelf = scene::align::kMiddle},
                      nodes::Text("", 14.0f, colours.accent, true))),
              .less = skiff::compose::bound<config::cache_limits>(
                  skiff::compose::styled({.alignSelf = scene::align::kMiddle},
                                         icon_button<limit_press<Which>>(
                                             colours, icon::minus{}, {false}))),
              .more = skiff::compose::bound<config::cache_limits>(
                  skiff::compose::styled({.alignSelf = scene::align::kMiddle},
                                         icon_button<limit_press<Which>>(
                                             colours, icon::plus{}, {true})))} {
  }
};

// Settings' Storage page: whether what mux keeps on disk is sealed; how
// much is kept in memory and on disk, each a line with its number and a
// step down and up; a way to clear what is kept on disk; whether deleted
// messages are shown, and how much of them is kept; and every chat's room
// events. The limits and the history's switch are the model's widgets; the
// seal, the clearing and every chat's choices are asked as before.
template <class Actions> struct storage_page : skiff::compose::Stacked {
  using header_t = page_header<sends<::mux::ui::request::settings_home>, sends<::mux::ui::request::close_settings>>;
  using clear_row = row_item<sends<::mux::ui::request::clear_stored>>;
  using seal_row = switch_row<sends<::mux::ui::request::flip_local_encryption>>;
  using change_row = row_item<sends<::mux::ui::request::change_passphrase>>;
  // The seal: its switch, and the passphrase to change where it is on.
  struct seal_rows : skiff::compose::Stacked {
    struct parts_t {
      seal_row seal;
      change_row change;
    } parts;
    seal_rows(const palette &colours, bool sealed)
        : Stacked(skiff::compose::vbox(
              0.0f, {.fillX = true, .autoSize = scene::axes::kY})),
          parts{.seal = seal_row(colours, "Encrypt local data", {}),
                .change = change_row(colours, "Change the passphrase", {})} {
      this->show_sealed(sealed, true);
    }
    void show_sealed(bool sealed, bool at_once = false) {
      if (at_once)
        parts.seal.parts.toggle.setOnNow(sealed);
      else
        parts.seal.parts.toggle.setOn(sealed);
      parts.change.setVisible(sealed);
    }
  };
  static auto settings_of(const palette& colours, const config::history_settings& history, bool sealed) {
    namespace limit = config::limit;
    using skiff::compose::bound;
    auto clear = clear_row(colours, "Clear stored messages and pictures", {}, icon::close{});
    return skiff::compose::column(
        skiff::compose::vbox(0.0f, {.fillX = true,
                                    .autoSize = scene::axes::kY,
                                    .padding = {0.0f, 0.0f, 12.0f, 0.0f}}),
        spaced_title(colours, "ENCRYPTION"), seal_rows(colours, sealed),
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
        bound<config::history_settings>(
            event_kinds_field<config::history_settings>(
                colours, choice_level::everywhere{})),
        bound<skiff::model::Field<&config::history_settings::show_receipts>>(
            show_hide_field<receipts_setting, bool>(
                colours, choice_level::everywhere{})),
        bound<skiff::model::Field<&config::history_settings::link_previews>>(
            show_hide_field<link_previews_setting, bool>(
                colours, choice_level::everywhere{})),
        bound<skiff::model::Field<&config::history_settings::previews_direct>>(
            show_hide_field<previews_direct_setting, std::optional<bool>>(
                colours, choice_level::everywhere{})),
        bound<skiff::model::Field<&config::history_settings::jump_search>>(
            jump_search_field<std::int64_t>(colours,
                                            choice_level::everywhere{})),
        bound<skiff::model::Field<&config::history_settings::send_typing>>(
            show_hide_field<typing_setting, std::optional<bool>>(
                colours, choice_level::everywhere{})),
        spaced_note(colours, "Deleted messages are kept on disk, apart from "
                             "the rest and up to their own size, the "
                             "oldest going first past it. Shown, one stays "
                             "where it was, with all it said and its "
                             "time, marked removed."));
  }
  using settings_t = decltype(settings_of(std::declval<const palette&>(), std::declval<const config::history_settings&>(), false));
  struct parts_t {
    header_t header;
    settings_t settings;
  } parts;

  storage_page(const ui_needs<Actions>& n, const config::cache_limits&, const config::history_settings& history, bool sealed)
      : storage_page(*n.colours, history, sealed) {}
  storage_page(const palette &colours, const config::history_settings &history,
               bool sealed)
      : Stacked(skiff::compose::vbox(0.0f, {.fill = true})),
        parts{.header = header_t(colours, "Storage", {}, {}, true, true),
              .settings = settings_of(colours, history, sealed)} {}
  void show_receipts(bool) {}
  void show_sealed(bool sealed) { std::get<1>(parts.settings.fParts).show_sealed(sealed); }
};

inline auto notification_settings_view(const palette& colours) {
  using every = config::notification_settings;
  auto backend = skiff::compose::bound<skiff::model::Field<&every::backend>>(widgets::ChoiceTabs<config::notify_backend_t>(
      {{"System", config::notify_backend::native{}}, {"Built in", config::notify_backend::built_in{}}}));
  backend.apply({.margin = {4.0f, 20.0f, 4.0f, 20.0f}});
  return skiff::compose::column(
      skiff::compose::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}), spaced_title(colours, "NOTIFICATIONS"),
      setting_switch<&every::desktop>(colours, "Notifications"),
      setting_switch<&every::mentions_only, std::optional<bool>>(colours, "Only mentions and keywords"),
      setting_switch<&every::show_name>(colours, "The sender's name"),
      setting_switch<&every::show_text>(colours, "The message's text"), setting_switch<&every::sound>(colours, "Sound"),
      spaced_title(colours, "SHOWN BY"), std::move(backend),
      spaced_note(colours, "System asks the desktop's own notification service (org.freedesktop.Notifications); Built in "
                           "shows mux's own, in a corner of the screen, as Telegram Desktop does."),
      spaced_title(colours, "WAKE"),
      setting_switch<&every::unified_push, std::optional<bool>>(colours, "Wake by UnifiedPush"),
      spaced_note(colours, "Your Matrix servers push to the UnifiedPush distributor on this device (ntfy, NextPush, "
                           "KDE's), which wakes mux at once. Off, nothing is given to the servers, and mux only learns "
                           "of messages while it runs."));
}
template <class Actions> struct notifications_page : skiff::compose::Stacked {
  using header_t = page_header<sends<::mux::ui::request::settings_home>, sends<::mux::ui::request::close_settings>>;
  using settings_t = decltype(notification_settings_view(std::declval<const palette&>()));
  struct parts_t {
    header_t header;
    settings_t settings;
  } parts;
  notifications_page(const ui_needs<Actions>& n, const config::notification_settings&)
      : notifications_page(*n.colours) {}
  notifications_page(const palette &colours)
      : Stacked(skiff::compose::vbox(0.0f, {.fill = true})),
        parts{.header = header_t(colours, "Notifications", {}, {}, true, true),
              .settings = notification_settings_view(colours)} {}
  void show_receipts(bool) {}
};

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
template <class Actions> struct files_page : skiff::compose::Stacked {
  using header_t = page_header<sends<::mux::ui::request::settings_home>, sends<::mux::ui::request::close_settings>>;
  using settings_t = decltype(files_settings_view(std::declval<const palette&>()));
  struct parts_t {
    header_t header;
    settings_t settings;
  } parts;
  files_page(const ui_needs<Actions>& n, const config::sending_settings&) : files_page(*n.colours) {}
  files_page(const palette &colours)
      : Stacked(skiff::compose::vbox(0.0f, {.fill = true})),
        parts{.header = header_t(colours, "Files", {}, {}, true, true),
              .settings = files_settings_view(colours)} {}
  void show_receipts(bool) {}
};

}  // namespace mux::ui
