// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.notifications.ui -- Settings' Notifications page, as Telegram
// Desktop's: a notification on the desktop or not, a sound or not; what
// shows it -- the desktop's own service, or mux's window; and whether
// UnifiedPush wakes mux (contract::settings_pages), linked from Settings'
// home.
module;
#include "skiff/named.h"
export module mux.plugin.notifications.ui;

import std;
import skiff.scene;
import skiff.compose;
import skiff.model;
import skiff.widgets.model;
import skiff.widgets.textarea;
import skiff.nodes.text;
import mux.core;
import mux.config;
import mux.composition;
import mux.ui.base;
import mux.plugin.notifications.types;

export namespace mux::plugins::notifications {

using namespace ::mux::ui;

// UnifiedPush's distributors found on the bus, each a row -- the one in use
// marked -- pressed, chosen; as Element's "Choose distributor".
inline auto distributor_rows(const palette& colours, const push_shown& now) {
  constexpr std::string_view kPrefix = "org.unifiedpush.Distributor.";
  auto rows = now.distributors | std::views::enumerate | std::views::transform([&](const auto& each) {
                const auto& [at, name] = each;
                const std::string_view shown = std::string_view(name).starts_with(kPrefix) ? std::string_view(name).substr(kPrefix.size())
                                                                                           : std::string_view(name);
                return settings_link(colours, std::format("{}{}", name == now.chosen ? "✓ " : "", shown), icon::bell{},
                                     choose_distributor{static_cast<std::size_t>(at)});
              }) | std::ranges::to<std::vector>();
  return skiff::compose::many(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(rows));
}
using distributor_rows_t = decltype(distributor_rows(std::declval<const palette&>(), push_shown{}));
inline auto distributors_view(const palette& colours) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(distributors_view, c::column(c::vbox(2.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::text_for<push_shown>([](const push_shown& now) {
        return now.status.empty() ? std::string("Not registered: turn on Wake by UnifiedPush.")
                                  : now.endpoint ? std::format("{}\nEndpoint: {}", now.status, *now.endpoint) : now.status;
      }, c::styled({.fillX = true, .margin = {6.0f, 20.0f, 4.0f, 20.0f}}, note_text(colours, ""))),
      c::projected<push_shown>([](const push_shown& now) { return std::optional(now); },
          c::mount<distributor_rows_t, push_shown>([at = &colours](const push_shown& now) { return std::tuple{distributor_rows(*at, now)}; },
                                                   {.fillX = true, .autoSize = scene::axes::kY})),
      settings_link(colours, "Register again", icon::send{}, register_again{})));
}
inline auto notification_settings_view(const palette& colours) {
  using every = config::notification_settings;
  auto backend = skiff::compose::bound<skiff::model::Field<&every::backend>>(
      skiff::compose::styled({.margin = {4.0f, 20.0f, 4.0f, 20.0f}}, themed_tabs<config::notify_backend_t>(colours, 
          {{"System", config::notify_backend::native{}}, {"Built in", config::notify_backend::built_in{}}})));
  return SKIFF_NAMED(notification_settings_view, skiff::compose::column(
      skiff::compose::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}), spaced_title(colours, "NOTIFICATIONS"),
      setting_switch<&every::desktop>(colours, "Notifications"),
      setting_switch<&every::mentions_only, std::optional<bool>>(colours, "Only mentions and keywords"),
      setting_switch<&every::sound>(colours, "Sound"),
      spaced_title(colours, "CUSTOM SOUND"),
      skiff::compose::bound<skiff::model::Field<&kept::sound_file>>(skiff::compose::styled(
          {.fillX = true, .height = 36.0f, .margin = {4.0f, 20.0f, 4.0f, 20.0f}},
          widgets::TextField<std::optional<std::string>>(colours.widgets, "Ogg Opus or Vorbis file; empty uses the default"))),
      spaced_title(colours, "SHOWN BY"), std::move(backend),
      spaced_note(colours, "System asks the desktop's own notification service (org.freedesktop.Notifications); Built in "
                           "shows mux's own, in a corner of the screen, as Telegram Desktop does."),
      spaced_title(colours, "WAKE"),
      setting_switch<&kept::unified_push, std::optional<bool>>(colours, "Wake by UnifiedPush"),
      spaced_note(colours, "Your Matrix servers push to the UnifiedPush distributor on this device (ntfy, NextPush, "
                           "KDE's), which wakes mux at once. Off, nothing is given to the servers, and mux only learns "
                           "of messages while it runs."),
      spaced_title(colours, "DISTRIBUTOR"), distributors_view(colours)));
}
inline auto notifications_view(const palette& colours) {
  using header = page_header_t<sends<request::settings_home>, sends<request::close_settings>>;
  return SKIFF_NAMED(notifications_view, skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      skiff::compose::styled({.depth = 1.0f, .background = colours.sidebar}, page_header<sends<request::settings_home>, sends<request::close_settings>>(colours, "Notifications", {}, {}, true, true)),
      notification_settings_view(colours)));
}

// Settings' link to it (mux.ui:settings_pages), and the page.
inline auto extend(contract::settings_links, type_tag<feature>, const palette& colours) {
  return settings_link(colours, "Notifications", icon::bell{}, open_settings{});
}
inline auto extend(contract::settings_pages, const ui_needs& n, const settings_page&) { return notifications_view(*n.colours); }

}  // namespace mux::plugins::notifications
