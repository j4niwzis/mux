// SPDX-License-Identifier: AGPL-3.0-only
// The program: avatars and pictures fetched, kept and pruned.
module mux.app.program;

import std;
import knot;
import skia;
import mux.core;
import mux.config;
import mux.net;
import mux.xmpp;
import mux.matrix;
import mux.media;
import mux.host;
import mux.ui;
import skiff.paint;
import skiff.scene;
import mux.app.network;
import mux.app.demo;
import mux.app.store;
import mux.app.requests;
import mux.app.words;

namespace mux::app {

auto app::avatar_file(std::string_view source) -> std::filesystem::path {
  std::string name;
  for (const char c : source)
    name += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
  return mux::config::cache_path("avatars") / name;
}

auto app::kept_file(const mux::media_use_t& use, std::string_view source) -> std::optional<std::filesystem::path> {
  const auto named = [&](std::string_view kind) {
    std::string name(kind);
    for (const char c : source)
      name += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    return std::optional(mux::config::cache_path("avatars") / name);
  };
  return std::visit(mux::overloaded{[&](const mux::media_use::avatar&) { return std::optional(avatar_file(source)); },
                                    [&](const mux::media_use::thumbnail&) { return named("thumb_"); },
                                    [&](const mux::media_use::whole&) { return named("full_"); },
                                    [](const mux::media_use::to_open&) { return std::optional<std::filesystem::path>(); },
                                    [](const mux::media_use::to_save&) { return std::optional<std::filesystem::path>(); }},
                    use);
}

void app::take_avatar(const mux::change::avatar_loaded& picture, bool fresh) {
  const auto shown = [&](mux::ui::image_cache& cache, const std::string& key) {
    if (auto image = skia::decodeImage(picture.bytes.data(), picture.bytes.size())) {
      cache.put(key, std::move(image));
      scene.state().markDamaged();
    }
  };
  std::visit(mux::overloaded{
                 [&](const mux::media_use::avatar& one) {
                   shown(mux::ui::avatar_images(), one.of);
                   if (fresh)
                     avatars_fetched.erase(picture.source);
                 },
                 [&](const mux::media_use::thumbnail&) {
                   shown(mux::ui::thumbnails(), picture.source);
                   if (fresh)
                     thumbnails_fetched.erase(picture.source);
                 },
                 [&](const mux::media_use::whole&) {
                   shown(mux::ui::whole_pictures(), picture.source);
                   if (fresh)
                     wholes_fetched.erase(picture.source);
                 },
                 // A file fetched to be saved: into Downloads, a number added
                 // where the name is taken, and opened; or only saved.
                 [&](const mux::media_use::to_open& one) { this->save_download(picture.bytes, one.name, true); },
                 [&](const mux::media_use::to_save& one) { this->save_download(picture.bytes, one.name, false); }},
             picture.use);
  if (!fresh)
    return;
  if (const auto where = kept_file(picture.use, picture.source)) {
    std::error_code failed;
    std::filesystem::create_directories(where->parent_path(), failed);
    std::ofstream(*where, std::ios::binary) << picture.bytes;
    if (++avatars_written % 50 == 1)
      prune_avatar_files();
  }
}

auto app::read_back(const mux::media_use_t& use, const std::string& source) -> bool {
  const auto where = kept_file(use, source);
  if (!where)
    return false;
  std::ifstream file{*where, std::ios::binary};
  if (!file)
    return false;
  std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  std::error_code failed;
  std::filesystem::last_write_time(*where, std::filesystem::file_time_type::clock::now(), failed);  // used now
  this->take_avatar(mux::change::avatar_loaded{use, source, std::move(bytes)}, false);
  return true;
}

void app::prune_avatar_files() {
  const std::uintmax_t budget = static_cast<std::uintmax_t>(limits.pictures_on_disk_mb) << 20;
  std::error_code failed;
  const auto directory = mux::config::cache_path("avatars");
  std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> files;
  std::uintmax_t total = 0;
  for (const auto& entry : std::filesystem::directory_iterator(directory, failed)) {
    if (!entry.is_regular_file(failed))
      continue;
    total += entry.file_size(failed);
    files.emplace_back(entry.last_write_time(failed), entry.path());
  }
  if (total <= budget)
    return;
  std::ranges::sort(files);
  for (const auto& [when, path] : files) {
    if (total <= budget)
      break;
    const auto size = std::filesystem::file_size(path, failed);
    if (std::filesystem::remove(path, failed))
      total -= size;
  }
}

void app::want_picture(const mux::account_id& of, const std::string& source) {
  if (source.empty() || mux::ui::thumbnails().has(source) || thumbnails_fetched.contains(source))
    return;
  if (this->read_back(mux::media_use::thumbnail{}, source))
    return;
  thumbnails_fetched.insert(source);
  net->fetch_media(of, source, mux::media_use::thumbnail{}, 860);
}

void app::ask_avatars() {
  if (ask.demo)
    return;
  const auto want = [&](const mux::account_id& of, const std::optional<std::string>& source, const std::string& key) {
    // Shown already, or on its way: nothing to do.
    if (!source || source->empty() || mux::ui::avatar_images().has(key) || avatars_fetched.contains(*source))
      return;
    if (this->read_back(mux::media_use::avatar{key}, *source))
      return;
    avatars_fetched.insert(*source);
    net->fetch_avatar(of, *source, key);
  };
  auto& screen = root().main();
  for (const auto& [id, account] : model->accounts())
    for (const auto& [key, one] : account.conversations) {
      want(id, one.avatar, one.id.id);
      // The people of the chat being read: its members' pictures.
      if (screen.chosen == one.id) {
        for (const mux::member& each : one.members)
          want(id, each.avatar, each.id);
        // The thumbnails of the pictures in the bubbles made -- those on
        // screen and near it, at twice the size they are drawn at -- not
        // of every picture in its history, which pushed the rest out.
        const auto [from, to] = screen.made_indices(one.timeline);
        for (std::size_t i = from; i < to && i < one.timeline.size(); ++i)
          if (const mux::message& said = one.timeline[i]; said.attachment && mux::is_picture(said.attachment->kind))
            want_picture(id, said.attachment->source);
      }
    }
}

}  // namespace mux::app
