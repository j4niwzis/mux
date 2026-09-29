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

void app::take_avatar(const mux::change::avatar_loaded& picture, bool fresh) {
  // A file fetched to be saved: into Downloads, a number added where the
  // name is taken, and opened.
  if (picture.key.starts_with("file:")) {
    this->save_download(picture.bytes, picture.key.substr(5), true);
    return;
  }
  if (picture.key.starts_with("save:")) {
    // The name after save:, where the menu gave one; a picture's else.
    const std::string name = picture.key.substr(5);
    this->save_download(picture.bytes, name.starts_with("mxc://") ? std::string("image") : name, false);
    return;
  }
  if (auto image = skia::decodeImage(picture.bytes.data(), picture.bytes.size())) {
    mux::ui::avatar_images().put(picture.key, std::move(image));
    scene.state().markDamaged();
  }
  if (fresh) {
    // A thumbnail or a whole picture under its key; an avatar under its
    // source, whoever it is of.
    const auto where = avatar_file(picture.key.starts_with("thumb:") || picture.key.starts_with("full:")
                                       ? picture.key
                                       : picture.source);
    std::error_code failed;
    std::filesystem::create_directories(where.parent_path(), failed);
    std::ofstream(where, std::ios::binary) << picture.bytes;
    avatars_fetched.erase(picture.source);
    avatars_fetched.erase(picture.key);
    if (++avatars_written % 50 == 1)
      prune_avatar_files();
  }
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
  const std::string key = "thumb:" + source;
  if (source.empty() || mux::ui::avatar_images().has(key) || avatars_fetched.contains(key))
    return;
  const auto where = avatar_file(key);
  if (std::ifstream file{where, std::ios::binary}) {
    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::error_code failed;
    std::filesystem::last_write_time(where, std::filesystem::file_time_type::clock::now(), failed);
    this->take_avatar(mux::change::avatar_loaded{key, source, std::move(bytes)}, false);
    return;
  }
  avatars_fetched.insert(key);
  net->fetch_media(of, source, key, 860);
}

void app::ask_avatars() {
  if (ask.demo)
    return;
  const auto want = [&](const mux::account_id& of, const std::optional<std::string>& source, const std::string& key) {
    // Shown already, or on its way: nothing to do.
    if (!source || source->empty() || mux::ui::avatar_images().has(key) || avatars_fetched.contains(*source))
      return;
    const auto where = avatar_file(*source);
    if (std::ifstream file{where, std::ios::binary}) {
      std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
      std::error_code failed;
      std::filesystem::last_write_time(where, std::filesystem::file_time_type::clock::now(), failed);  // used now
      this->take_avatar(mux::change::avatar_loaded{key, *source, std::move(bytes)}, false);
      return;
    }
    avatars_fetched.insert(*source);
    net->fetch_avatar(of, *source, key);
  };
  for (const auto& [id, account] : model->accounts())
    for (const auto& [key, one] : account.conversations) {
      want(id, one.avatar, one.id.id);
      // The people of the chat being read: its members' pictures.
      if (root().main().chosen == one.id) {
        for (const mux::member& each : one.members)
          want(id, each.avatar, each.id);
        // Its pictures' thumbnails, at twice the size they are drawn at.
        for (const mux::message& said : one.timeline)
          if (said.attachment && mux::is_picture(said.attachment->kind))
            want_picture(id, said.attachment->source);
      }
    }
}

}  // namespace mux::app
