// SPDX-License-Identifier: AGPL-3.0-only
// The program: files sent, pictures opened and saved, downloads.
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
import mux.logic.links;

namespace mux::app {

void app::apply(const request::attach_files&) {
  if (root().main().chosen)
    mux::host::choose_files();
}

void app::apply(const request::close_send_box&) {
  to_send.clear();
  root().close_send_box();
}

void app::files_given(std::vector<std::string> paths, bool dropped) {
  if (!root().main().chosen)
    return;
  for (const std::string& path : paths) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
      continue;
    prepared_file one;
    one.bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    one.name = std::filesystem::path(path).filename().string();
    one.mimetype = "application/octet-stream";
    if (const auto type = mux::media::picture_of(one.bytes)) {
      if (auto image = skia::decodeImage(one.bytes.data(), one.bytes.size())) {
        one.image = true;
        one.width = image->width();
        one.height = image->height();
        one.mimetype = std::string(mux::media::mimetype_of(*type));
        // Dropped: its metadata cut out of the file, the picture's bytes
        // as they were, and named image.<type> -- as Settings says.
        if (dropped && sending.strip_metadata)
          one.bytes = mux::media::without_metadata(one.bytes);
        if (dropped && sending.rename)
          one.name = std::format("image.{}", mux::media::extension_of(*type));
        // Its local id, under which its thumbnail is shown while it goes.
        one.key = std::format("mux-file-{}-{}", std::chrono::system_clock::now().time_since_epoch().count(),
                              ++files_made);
        mux::ui::thumbnails().put(one.key, std::move(image));
      }
    }
    to_send.push_back(std::move(one));
  }
  if (to_send.empty())
    return;
  std::vector<mux::ui::pending_file> shown;
  for (const prepared_file& one : to_send)
    shown.push_back({one.name, one.key, static_cast<std::int64_t>(one.bytes.size()), one.image});
  root().open_send_box(shown);
}

void app::apply(const request::send_files&) {
  const auto& chosen = root().main().chosen;
  auto* box = root().send_box_up();
  if (!chosen || !box || to_send.empty())
    return;
  std::string caption = box->caption.text();
  for (prepared_file& one : to_send) {
    // Its local id is its picture's, so the window shows it while it goes.
    std::string local = one.key.empty() ? std::format("mux-file-{}-{}", std::chrono::system_clock::now().time_since_epoch().count(),
                                                      ++files_made)
                                        : one.key;
    net->send_file(*chosen, std::move(local), std::move(one.bytes), one.name, one.mimetype, one.image, one.width,
                   one.height, std::exchange(caption, std::string()));
  }
  to_send.clear();
  root().close_send_box();
}

void app::apply(const request::reply_to& one) {
  menu_target.id = one.id;
  menu_target.text = one.text;
  this->apply(request::menu_reply{});
}

void app::apply(const request::menu_reply&) {
  root().close_menu();
  composing = compose::reply{menu_target.id};
  // As tdesktop's: "Reply to <name>" over a line of the message -- its
  // text, or what it carries where it has none.
  std::string title = "Reply";
  std::string line = menu_target.text;
  if (const auto& chosen = root().main().chosen)
    if (const mux::conversation* chat = model->find(*chosen)) {
      const auto said = std::ranges::find(chat->timeline, menu_target.id, &mux::message::id);
      if (said != chat->timeline.end()) {
        title = "Reply to " + mux::ui::sender_name(*chat, said->sender);
        if (line.empty() && said->attachment)
          line = mux::is_picture(said->attachment->kind) ? std::string("Photo") : said->attachment->name;
      }
    }
  std::ranges::replace(line, '\n', ' ');
  root().main().line.show_context(mux::ui::compose_context{mux::ui::icon::reply{}, std::move(title), std::move(line)});
}

void app::apply(const request::menu_edit&) {
  root().close_menu();
  composing = compose::edit{menu_target.id};
  std::string line = menu_target.text;
  std::ranges::replace(line, '\n', ' ');
  root().main().line.show_context(mux::ui::compose_context{mux::ui::icon::pencil{}, "Edit message", std::move(line)});
  root().main().line.set_text(menu_target.text);
}

void app::apply(const request::menu_copy&) {
  root().close_menu();
  skiff::scene::setClipboardText(menu_target.copied.empty() ? menu_target.text : menu_target.copied);
}

void app::apply(const request::react& one) {
  const auto& chosen = root().main().chosen;
  const mux::conversation* chat = chosen ? model->find(*chosen) : nullptr;
  if (!chat)
    return;
  const auto said = std::ranges::find(chat->timeline, one.id, &mux::message::id);
  if (said == chat->timeline.end())
    return;
  const std::string& me = chosen->account.address;
  const auto who = said->reactions.find(one.key);
  const bool on = who == said->reactions.end() || !who->second.contains(me);
  model->apply(mux::change_t{mux::change::reaction_changed{*chosen, one.id, one.key, me, on}});
  if (!ask.demo)
    net->react(*chosen, one.id, one.key, on);
  this->refresh();
}

void app::apply(const request::menu_react& one) {
  root().close_menu();
  this->apply(request::react{menu_target.id, one.key});
}

void app::apply(const request::menu_copy_link&) {
  root().close_menu();
  skiff::scene::setClipboardText(menu_target.link);
}

void app::apply(const request::menu_save&) {
  root().close_menu();
  if (menu_target.media)
    pictures.save(*menu_target.media, menu_target.media_name.empty() ? std::string("image") : menu_target.media_name);
}

void app::apply(const request::menu_delete&) {
  root().close_menu();
  const auto& chosen = root().main().chosen;
  if (!chosen)
    return;
  if (ask.demo)
    box->push(mux::change_t{mux::change::message_redacted{*chosen, menu_target.id}});
  else
    net->remove_message(*chosen, menu_target.id);
}

void app::apply(const request::open_url& one) {
  if (const auto where = mux::logic::link_of(one.url)) {
    this->follow(*where);
    return;
  }
  mux::host::open_url(one.url);
}

}  // namespace mux::app
