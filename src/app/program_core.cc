// SPDX-License-Identifier: AGPL-3.0-only
// The program: waking on changes, keeping what changed, frames, drafts, the screens shown.
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

void app::apply_limits() {
  mux::ui::avatar_images().budget = static_cast<std::size_t>(limits.pictures_in_memory_mb) << 20;
  store.budget = static_cast<std::uintmax_t>(limits.messages_on_disk_mb) << 20;
}

auto app::window() -> skiff::scene::Scene<window_type>& { return scene; }

void app::woken() {
  auto changes = box->take();
  if (changes.empty())
    return;
  for (const auto& one : changes) {
    if (const auto* given = std::get_if<mux::change::session_given>(&one))
      this->keep_session(*given);
    if (const auto* picture = std::get_if<mux::change::avatar_loaded>(&one))
      this->take_avatar(*picture, true);
    model->apply(one);
    this->keep_on_disk(one);
  }
  // Messages held to a number in all, least recently read out first.
  model->trim(static_cast<std::size_t>(limits.messages_in_memory), root().main().chosen);
  this->refresh();
  // What comes into the chat being read, at its end, is read.
  if (const auto& chosen = root().main().chosen; chosen && root().main().timeline.atEnd(40.0f))
    this->mark_read(*chosen);
  // A room joined from a link: opened once it is here.
  if (pending_link)
    if (const auto found = this->chat_of(*pending_link)) {
      const auto where = std::exchange(pending_link, std::nullopt);
      this->open_chat(*found, where->event);
    }
}

void app::keep_on_disk(const mux::change_t& one) {
  if (ask.demo)
    return;
  const auto as_now = [&](const mux::conversation_id& in, const std::string& id) {
    if (const mux::conversation* chat = model->find(in))
      if (const auto found = std::ranges::find(chat->timeline, id, &mux::message::id); found != chat->timeline.end())
        store.record(*found);
  };
  std::visit(mux::overloaded{[&](const mux::change::message_added& c) { as_now(c.message.in, c.message.id); },
                             [&](const mux::change::message_edited& c) { as_now(c.in, c.id); },
                             [&](const mux::change::message_redacted& c) { as_now(c.in, c.id); },
                             [&](const mux::change::reaction_changed& c) { as_now(c.in, c.id); },
                             [&](const mux::change::receipts_changed& c) {
                               if (const mux::conversation* chat = model->find(c.in))
                                 store.keep_reads(c.in, *chat);
                             },
                             [&](const mux::change::message_acknowledged& c) {
                               store.forget(c.in, c.local_id);
                               as_now(c.in, c.id);
                             },
                             [](const auto&) {}},
             one);
}

void app::keep_session(const mux::change::session_given& given) {
  const auto found = this->find(given.account.address);
  if (found == saved.end())
    return;
  std::visit(mux::overloaded{[&](mux::config::matrix_account& one) {
                               one.access_token = given.access_token;
                               one.device_id = given.device_id;
                             },
                             [](mux::config::xmpp_account&) {}},
             *found);
  (void)this->write();
}

void app::before_frame() {
  root().drop_closed();
  auto pending = std::exchange(ask.requests, {});
  for (const request_t& one : pending)
    std::visit([this](const auto& each) { this->apply(each); }, one);
  if (drawer_waits && !root().pages_moving()) {
    root().close_drawer_now();
    drawer_waits = false;
  }
}

void app::closing() {
  if (const auto& chosen = root().main().chosen)
    this->keep_draft(*chosen, root().main().line.text());
  net->shutdown();
}

void app::keep_draft(const mux::conversation_id& in, const std::string& text) {
  auto& drafts = root().main().drafts;
  const bool blank = std::ranges::all_of(text, [](unsigned char c) { return std::isspace(c) != 0; });
  const auto found = drafts.find(in);
  if (blank ? found == drafts.end() : (found != drafts.end() && found->second == text))
    return;
  if (blank)
    drafts.erase(in);
  else
    drafts.insert_or_assign(in, text);
  if (ask.demo)
    return;
  knot::value::object all;
  for (const auto& [id, draft] : drafts)
    all.emplace(id.account.address + "\n" + id.id, knot::value(draft));
  const auto where = mux::config::state_path("drafts.json");
  std::error_code failed;
  std::filesystem::create_directories(where.parent_path(), failed);
  std::ofstream(where, std::ios::binary | std::ios::trunc) << knot::to_json_string(knot::value(std::move(all)));
}

void app::load_drafts() {
  std::ifstream file(mux::config::state_path("drafts.json"), std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  auto parsed = knot::try_read<knot::value>(std::string_view(text));
  if (!parsed || !parsed->is<knot::value::object>())
    return;
  for (const auto& [key, draft] : parsed->as<knot::value::object>()) {
    const auto cut = key.find('\n');
    if (cut == std::string::npos || !draft.is<std::string>())
      continue;
    const std::string address = key.substr(0, cut);
    root().main().drafts.insert_or_assign(
        mux::conversation_id{mux::account_id{mux::ui::protocol_of(address), address}, key.substr(cut + 1)},
        draft.as<std::string>());
  }
}

auto app::root() -> window_type& { return scene.root(); }

void app::show_conversations() {
  root().close_drawer();
  pending_login.reset();
  root().close();
  this->refresh();
}

auto app::show_accounts() -> accounts& {
  // From the drawer, the page comes in over it, and the drawer goes once
  // the page is in: not two things moving at once.
  if (root().drawer_open())
    drawer_waits = true;
  root().close_settings();
  pending_login.reset();
  auto& panel = root().open<accounts>();
  if (config_error)
    panel.say(*config_error);
  this->refresh();
  return panel;
}

auto app::show_account(const std::string& address) -> accounts& {
  auto& panel = this->show_accounts();
  if (const auto found = this->find(address); found != saved.end()) {
    panel.select(*found, *model);
    panel.show(saved, *model);
  }
  return panel;
}

void app::show_adding() {
  auto& panel = this->show_accounts();
  panel.proxies = proxies;
  panel.show_adding();
  this->refresh();
}

auto app::find(std::string_view address) -> std::vector<mux::config::account_t>::iterator {
  return std::ranges::find(saved, address, [](const auto& one) -> std::string_view {
    return mux::config::address_of(one);
  });
}

auto app::xmpp_form_up() -> mux::ui::xmpp_form<actions>* {
  auto* up = root().open_panel();
  if (!up)
    return nullptr;
  return std::visit([](auto& panel) { return panel.xmpp(); }, *up);
}

void app::refresh() {
  this->ask_avatars();
  root().main().muted = muted;
  root().show(saved, *model);
  root().main().show(*model);
  if (auto* up = root().open_panel())
    std::visit([this](auto& panel) { this->bring_up_to_date(panel); }, *up);
}

void app::bring_up_to_date(accounts& panel) {
  panel.proxies = proxies;
  panel.show(saved, *model);
  if (auto* pane = panel.adding())
    std::visit([this](auto& form) { this->watch_login(form); }, pane->form);
}

}  // namespace mux::app
