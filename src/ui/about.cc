// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:about -- Build identity, licensing and resolved dependencies.
export module mux.ui:about;

import std;
import skiff.compose;
import skiff.nodes.text;
import mux.platform.video;
import :base;
import :icons;
import :controls;
import :proxies;

export namespace mux::ui {
struct library_info {
  std::string_view id, name, version, revision, license, repository, license_text;
  std::vector<std::string_view> dependencies;
};
#include "about.inc"

template <class Node> auto about_visible(bool visible, Node node) { node.setVisible(visible); return node; }
inline auto about_words(const palette& colours, std::string text) {
  return wrapped(skiff::compose::styled(
      {.fillX = true, .margin = {8.0f, 20.0f, 8.0f, 20.0f}},
      nodes::Text(std::move(text), 14.0f, colours.text)));
}
inline library_info library_details(const library_info& info) {
  auto result = info;
  if (info.id == "ffmpeg" && platform::video::kPlays) {
    result.version = platform::video::ffmpeg_version();
    const auto license = platform::video::ffmpeg_license();
    result.license = license;
    const std::array<std::pair<std::string_view, std::string_view>, 4> names{{
        {"LGPL version 2.1 or later", "LGPL-2.1-or-later"},
        {"LGPL version 3 or later", "LGPL-3.0-or-later"},
        {"GPL version 2 or later", "GPL-2.0-or-later"},
        {"GPL version 3 or later", "GPL-3.0-or-later"}}};
    for (const auto& [description, spdx] : names) {
      if (license != description) continue;
      result.license = spdx;
      const auto text = std::ranges::find(about_data::ffmpeg_licenses, spdx,
          [](const auto& entry) { return entry.first; });
      if (text != about_data::ffmpeg_licenses.end()) result.license_text = text->second;
      break;
    }
  }
  return result;
}
inline auto library_link(const palette& colours, const library_info& info) {
  const auto library = library_details(info);
  return settings_link(colours, std::format("{} · {} · {}", library.name, library.version, library.license),
                       icon::gear{}, request::settings_library{std::string(library.id)});
}
inline const library_info* library_of(std::string_view id) {
  const auto found = std::ranges::find(about_data::libraries, id, &library_info::id);
  return found == about_data::libraries.end() ? nullptr : &*found;
}
inline auto about_page(const palette& colours) {
  auto libraries = std::ranges::to<std::vector>(std::views::transform(
      about_data::primary_libraries, [&](std::string_view id) {
        return library_link(colours, *library_of(id));
      }));
  return skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      page_header<sends<request::settings_home>, sends<request::close_settings>>(colours, "About mux", {}, {}, true, true),
      about_words(colours, std::format("mux {}\n{}\nCommit: {}\nLicense: {}", about_data::version,
                                     about_data::branch, about_data::commit, about_data::license)),
      settings_link(colours, "GitHub repository", icon::gear{}, request::open_url{std::string(about_data::repository)}),
      about_words(colours, "Libraries"), skiff::compose::many(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(libraries)),
      about_words(colours, std::string(about_data::license_text)));
}
using about_page_t = decltype(about_page(std::declval<const palette&>()));
inline auto library_page(const palette& colours, std::string_view id) {
  const library_info missing{id, id, "Unknown", {}, "Unknown", {}, {}, {}};
  const auto* found = library_of(id);
  const auto library = library_details(found ? *found : missing);
  auto dependencies = library.dependencies | std::views::transform([&](std::string_view dep) {
    const auto* info = library_of(dep);
    return library_link(colours, info ? *info : missing);
  }) | std::ranges::to<std::vector>();
  return skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      page_header<sends<request::settings_about>, sends<request::close_settings>>(colours, std::string(library.name), {}, {}, true, true),
      about_words(colours, std::format("Version: {}\nRevision: {}\nLicense: {}", library.version,
                                     library.revision.empty() ? "Not reported" : library.revision, library.license)),
      about_visible(!library.repository.empty(), settings_link(colours, "Repository", icon::gear{}, request::open_url{std::string(library.repository)})),
      about_visible(!dependencies.empty(), about_words(colours, "Dependencies")),
      skiff::compose::many(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(dependencies)),
      about_words(colours, library.license_text.empty() ? std::format("License: {}\nLicense text was not supplied by the installed package.", library.license)
                                                     : std::string(library.license_text)));
}
using library_page_t = decltype(library_page(std::declval<const palette&>(), std::string_view{}));
} // namespace mux::ui
