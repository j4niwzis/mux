// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.outbox: what the user sends -- the field's text as a message, an
// answer or an edit, and files chosen with the paperclip or dropped on the
// window, shown in the send box before they go.
export module mux.app.outbox;

import std;
import skia;
import mux.core;
import mux.config;
import mux.host;
import mux.ui;
import mux.app.network;
import mux.app.requests;
import mux.app.services;
import mux.app.drafts;
import mux.logic.sending;
import mux.logic.messages;

export namespace mux::app {

class outbox_part {
 public:
  outbox_part(services& shared, drafts_part& drafts, const mux::config::sending_settings& settings)
      : s_(&shared), drafts_(&drafts), settings_(&settings) {}

  // What is written answers a message, or edits one: said over the field.
  void answer(std::string id, std::string title, std::string line) {
    composing_ = compose::reply{std::move(id)};
    s_->root().main().line.show_context(mux::ui::compose_context{mux::ui::icon::reply{}, std::move(title), std::move(line)});
  }
  void edit(std::string id, const std::string& text) {
    composing_ = compose::edit{std::move(id)};
    std::string line = text;
    std::ranges::replace(line, '\n', ' ');
    s_->root().main().line.show_context(mux::ui::compose_context{mux::ui::icon::pencil{}, "Edit message", std::move(line)});
    s_->root().main().line.set_text(text);
  }
  // Up in an empty input: the last message sent here edited.
  void apply(const request::edit_last&) {
    const auto& chosen = s_->root().main().chosen;
    const conversation* chat = chosen ? s_->model->find(*chosen) : nullptr;
    if (!chat)
      return;
    const auto last = std::ranges::find_if(chat->timeline.rbegin(), chat->timeline.rend(), [](const message& one) {
      return one.outgoing && !one.redacted && !one.id.empty() && !one.body.plain.empty();
    });
    if (last != chat->timeline.rend())
      this->edit(last->id, last->body.plain);
  }
  // Ctrl+Up: the last message answered -- and, answering one, the one above
  // it; Ctrl+Down the one below, and past the newest the answer let go. The
  // one answered is scrolled to and flashed, as tdesktop does.
  void apply(const request::reply_step& one) {
    auto& screen = s_->root().main();
    const conversation* chat = screen.chosen ? s_->model->find(*screen.chosen) : nullptr;
    if (!chat)
      return;
    std::vector<const message*> answerable;
    for (const message& each : chat->timeline)
      if (!each.id.empty() && !each.redacted)
        answerable.push_back(&each);
    if (answerable.empty())
      return;
    const std::optional<std::string> now = std::visit(
        overloaded{[](const compose::reply& r) { return std::optional<std::string>(r.id); },
                   [](const auto&) { return std::optional<std::string>(); }},
        composing_);
    const auto at = now ? std::ranges::find(answerable, *now, &message::id) : answerable.end();
    const message* next = nullptr;
    if (at == answerable.end())
      next = one.older ? answerable.back() : nullptr;
    else if (one.older)
      next = at == answerable.begin() ? *at : *(at - 1);
    else if (at + 1 != answerable.end())
      next = *(at + 1);
    if (!next) {
      this->apply(request::cancel_compose{});
      return;
    }
    const std::string title = "Reply to " + (next->outgoing ? std::string("You") : mux::ui::sender_name(*chat, next->sender));
    this->answer(next->id, title, logic::reply_line(next, next->body.plain));
    screen.jump_to(next->id);
  }
  void apply(const request::cancel_compose&) {
    composing_ = compose::plain{};
    s_->root().main().line.show_context(std::nullopt);
  }
  void apply(const request::submit_message& one) { this->send(one.text); }
  void apply(const request::send_typed&) { this->send(s_->root().main().line.text()); }

  // Files: chosen with the paperclip, or dropped; the send box closed, or
  // what is in it sent -- the caption with the first.
  void apply(const request::attach_files&) {
    if (s_->root().main().chosen)
      mux::host::choose_files();
  }
  void apply(const request::close_send_box&) {
    to_send_.clear();
    s_->root().close_send_box();
  }
  void apply(const request::send_files&) {
    const auto& chosen = s_->root().main().chosen;
    auto* box = s_->root().send_box_up();
    if (!chosen || !box || to_send_.empty())
      return;
    std::string caption = box->caption.text();
    for (file& one : to_send_)
      s_->net->send_file(*chosen, one.local, std::move(one.as.bytes), one.as.name, one.as.mimetype,
                         one.as.picture.has_value(), one.width, one.height, std::exchange(caption, std::string()));
    to_send_.clear();
    s_->root().close_send_box();
  }
  // Files given: read and prepared as the logic of sending says; a
  // picture's thumbnail shown under its local id while it goes. Then the
  // send box, with what was waiting in it before.
  void files_given(std::vector<std::string> paths, bool dropped) {
    if (!s_->root().main().chosen)
      return;
    for (const std::string& path : paths) {
      std::ifstream in(path, std::ios::binary);
      if (!in)
        continue;
      std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      file one{logic::prepared_of(std::move(bytes), std::filesystem::path(path).filename().string(), dropped, *settings_),
               std::format("mux-file-{}-{}", std::chrono::system_clock::now().time_since_epoch().count(), ++made_)};
      if (one.as.picture)
        if (auto image = skia::decodeImage(one.as.bytes.data(), one.as.bytes.size())) {
          one.width = image->width();
          one.height = image->height();
          mux::ui::thumbnails().put(one.local, std::move(image));
        }
      to_send_.push_back(std::move(one));
    }
    if (to_send_.empty())
      return;
    std::vector<mux::ui::pending_file> shown;
    for (const file& one : to_send_)
      shown.push_back({one.as.name, one.as.picture ? one.local : std::string(),
                       static_cast<std::int64_t>(one.as.bytes.size()), one.as.picture.has_value()});
    s_->root().open_send_box(shown);
  }

 private:
  // The field's text sent: as a message, an answer, or an edit -- as what is
  // written says -- and the field and its draft emptied.
  void send(std::string text) {
    auto& screen = s_->root().main();
    if (!screen.chosen || !logic::sendable(text))
      return;
    const conversation_id to = *screen.chosen;
    // What is sent goes at the chat's end: the chat back to its newest
    // first, where it is a window elsewhere, or it would not be shown.
    s_->go_live(to);
    screen.jump_to_end();
    std::visit(overloaded{[&](const compose::plain&) { s_->ask->send(to, std::move(text)); },
                          [&](const compose::reply& one) {
                            if (s_->demo())
                              s_->ask->send(to, std::move(text));
                            else
                              s_->net->send(to, std::move(text), one.id);
                          },
                          [&](const compose::edit& one) {
                            if (s_->demo())
                              s_->box->push(change_t{change::message_edited{to, one.id, body{std::move(text), std::nullopt}}});
                            else
                              s_->net->edit(to, one.id, std::move(text));
                          }},
               composing_);
    composing_ = compose::plain{};
    screen.line.show_context(std::nullopt);
    screen.line.clear();
    drafts_->keep(to, std::string());
  }

  struct file {
    logic::prepared as;
    std::string local;  // its id until the server gives one; its thumbnail's
    int width = 0, height = 0;
  };

  services* s_;
  drafts_part* drafts_;
  const mux::config::sending_settings* settings_;
  compose_t composing_ = compose::plain{};
  std::vector<file> to_send_;
  std::uint64_t made_ = 0;
};

}  // namespace mux::app
