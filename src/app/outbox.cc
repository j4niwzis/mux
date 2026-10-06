// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.outbox: what the user sends -- the field's text as a message, an
// answer or an edit, and files chosen with the paperclip or dropped on the
// window, shown in the send box before they go.
export module mux.app.outbox;

import std;
import mux.platform.files;
import splice.bytes;
import splice;
import skia;
import mux.core;
import mux.config;
import mux.platform.dialogs;
import mux.platform.video;
import mux.ui;
import mux.app.network;
import mux.app.requests;
import mux.app.services;
import mux.app.drafts;
import mux.logic.sending;
import mux.logic.messages;
import mux.protocols;

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
    // What was being written kept, to be put back when the edit is let go --
    // from before the first edit, where one edit steps to another.
    std::string before = spl::visit(
        spl::overloaded{[](const compose::edit& e) { return e.before; },
                           [&](const auto&) { return s_->root().main().line.plain(); }},
        composing_);
    const std::string edited = id;
    composing_ = compose::edit{std::move(id), std::move(before)};
    std::string line = text;
    std::ranges::replace(line, '\n', ' ');
    s_->root().main().line.show_context(mux::ui::compose_context{mux::ui::icon::pencil{}, "Edit message", std::move(line)});
    s_->root().main().line.set_text(text);
    // Its formatting back, where its HTML has only runs.
    if (const auto& chosen = s_->root().main().chosen; chosen && !text.empty())
      if (const message* said = this->message_of(*chosen, edited); said && said->body.html)
        if (auto runs = mux::ui::editable_runs(*said->body.html))
          s_->root().main().line.set_formatted(std::move(runs->first), runs->second);
  }
  // A picture whose caption may be edited (not a video's, whose own is its
  // file): as Element edits one.
  [[nodiscard]] static bool captioned(const message& one) {
    return one.attachment && is_picture(one.attachment->kind) && !one.attachment->video;
  }
  // What of one's own messages may be edited: its text, or its picture's
  // caption -- not a file, a video or a sound.
  // As the chat's protocol's rule for edits allows: any of one's own, or
  // only the last (XMPP's).
  [[nodiscard]] bool editable(const conversation& chat, const message& one) const {
    return proto::may_edit(mux::ui::protocol_state_of(s_->ui, chat.id.account), chat, one) && !one.id.empty() &&
           (one.attachment ? captioned(one) : !one.body.plain.empty());
  }
  // What the field is given to edit: the text; a picture's caption, nothing
  // where it has none -- its body then its file's name.
  [[nodiscard]] static std::string edited_text(const message& one) {
    return one.attachment && one.body.plain == one.attachment->name ? std::string() : one.body.plain;
  }
  [[nodiscard]] const message* message_of(const conversation_id& in, const std::string& id) const {
    if (const conversation* chat = s_->model->find(in))
      if (const auto found = std::ranges::find(chat->timeline, id, &message::id); found != chat->timeline.end())
        return &*found;
    return nullptr;
  }
  // Ctrl+K: the link box, with what is selected and the link on it.
  void apply(const request::ask_link&) {
    auto [text, url] = s_->root().main().line.link_asked();
    s_->root().open_link(std::move(text), std::move(url));
  }
  void apply(const request::set_link& one) {
    s_->root().close_link();
    s_->root().main().line.put_link(one.text, one.url);
  }
  void apply(const request::close_link&) { s_->root().close_link(); }
  // Up in an empty input: the last message sent here edited.
  void apply(const request::edit_last&) {
    const auto& chosen = s_->root().main().chosen;
    const conversation* chat = chosen ? s_->model->find(*chosen) : nullptr;
    if (!chat)
      return;
    const auto last = std::ranges::find_if(chat->timeline.rbegin(), chat->timeline.rend(),
                                           [&](const message& one) { return editable(*chat, one); });
    if (last != chat->timeline.rend())
      this->edit(last->id, edited_text(*last));
  }
  // Ctrl+Up: the last message answered -- and, answering one, the one above
  // it; Ctrl+Down the one below, and past the newest the answer let go. The
  // one answered is scrolled to and flashed, as tdesktop does.
  void apply(const request::reply_step& one) {
    auto& screen = s_->root().main();
    const conversation* chat = screen.chosen ? s_->model->find(*screen.chosen) : nullptr;
    if (!chat)
      return;
    // Editing: Ctrl+Up and Down step through the user's own messages, the
    // one above or below edited in its place -- past the newest, the edit
    // let go.
    const std::optional<std::string> editing = spl::visit(
        spl::overloaded{[](const compose::edit& e) { return std::optional<std::string>(e.id); },
                   [](const auto&) { return std::optional<std::string>(); }},
        composing_);
    if (editing) {
      std::vector<const message*> own;
      for (const message& each : chat->timeline)
        if (editable(*chat, each))
          own.push_back(&each);
      const auto at = std::ranges::find(own, *editing, &message::id);
      const message* next = nullptr;
      if (at != own.end()) {
        if (one.older)
          next = at == own.begin() ? *at : *(at - 1);
        else if (at + 1 != own.end())
          next = *(at + 1);
      }
      if (!next) {
        this->apply(request::cancel_compose{});
        return;
      }
      this->edit(next->id, edited_text(*next));
      screen.jump_to(next->id);
      return;
    }
    // What can be answered: what the view shows -- not what it hides, a room
    // event its filter leaves out.
    std::vector<const message*> answerable;
    for (const message& each : chat->timeline)
      if (!each.id.empty() && !each.redacted && screen.shown_in(chat->id, each))
        answerable.push_back(&each);
    if (answerable.empty())
      return;
    const std::optional<std::string> now = spl::visit(
        spl::overloaded{[](const compose::reply& r) { return std::optional<std::string>(r.id); },
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
    this->answer(next->id, title,
                 next->body.plain.empty() ? logic::reply_line(next, next->body.plain)
                                          : mux::ui::quote_line_of(*next, *chat, s_->model));
    screen.jump_to(next->id);
  }
  // Element's unsent bar: what the server did not take here -- sent again,
  // each anew at the end and the one not sent let go; or let go.
  [[nodiscard]] std::vector<mux::message> unsent_here() const {
    std::vector<mux::message> out;
    const auto& chosen = s_->root().main().chosen;
    if (const mux::conversation* chat = chosen ? s_->model->find(*chosen) : nullptr)
      for (const mux::message& one : chat->timeline)
        if (one.outgoing &&
            spl::visit(spl::overloaded{[](const mux::delivery::failed&) { return true; }, [](const auto&) { return false; }},
                          one.delivery))
          out.push_back(one);
    return out;
  }
  void apply(const request::retry_unsent&) {
    for (const mux::message& one : this->unsent_here()) {
      s_->box->push(change_t{change::message_discarded{one.in, one.id}});
      if (s_->demo())
        s_->ask->send(one.in, one.body.plain);
      else
        s_->net->send(one.in, one.body.plain, one.replies_to, {});
    }
  }
  void apply(const request::discard_unsent&) {
    for (const mux::message& one : this->unsent_here())
      s_->box->push(change_t{change::message_discarded{one.in, one.id}});
  }
  // An edit let go: the field back to what was written before it, not the
  // edited message's text left in it to be sent as a new one.
  void apply(const request::cancel_compose&) {
    spl::visit(spl::overloaded{[&](const compose::edit& e) { s_->root().main().line.set_text(e.before); },
                                     [](const auto&) {}},
                  composing_);
    composing_ = compose::plain{};
    s_->root().main().line.show_context(std::nullopt);
  }
  // Enter in the field: sent while the field still holds it. Two asked for
  // before the first was handled -- the field emptied by it -- send once.
  void apply(const request::submit_message& one) {
    if (s_->root().main().line.plain().empty())
      return;
    this->send(one.text);
  }
  void apply(const request::stop_jump&) { s_->root().main().stop_jump(); }
  void apply(const request::send_typed&) { this->send(s_->root().main().line.plain()); }

  // Files: chosen with the paperclip, or dropped; the send box closed, or
  // what is in it sent -- the caption with the first.
  void apply(const request::attach_files&) {
    if (!s_->root().main().chosen || !mux::ui::may_send_files(s_->ui, s_->root().main().chosen->account))
      return;
    if (to_send_.empty())
      files_thread_.reset();
    s_->system_dialogs->choose_files();
  }
  // The thread panel's paperclip: what is chosen goes into the thread open.
  void apply(const request::attach_in_thread&) {
    if (!s_->root().main().chosen || !mux::ui::may_send_files(s_->ui, s_->root().main().chosen->account))
      return;
    if (to_send_.empty())
      files_thread_ = s_->root().main().thread_open();
    s_->system_dialogs->choose_files();
  }
  void apply(const request::close_send_box&) {
    to_send_.clear();
    files_thread_.reset();
    s_->root().close_send_box();
  }
  void apply(const request::send_files&) {
    const auto& chosen = s_->root().main().chosen;
    auto* box = s_->root().send_box_up();
    if (!chosen || !box || to_send_.empty())
      return;
    std::string caption = box->parts.caption.text();
    auto& screen = s_->root().main();
    // Into a thread: its root and its latest, and what is answered in it.
    std::optional<mux::thread_place> thread;
    std::optional<std::string> reply_to;
    if (files_thread_) {
      std::string latest = *files_thread_;
      if (const mux::conversation* chat = s_->model->find(*chosen))
        if (const auto found = chat->threads.find(*files_thread_); found != chat->threads.end() && !found->second.empty())
          latest = found->second.back().id;
      thread = mux::thread_place{*files_thread_, std::move(latest)};
      if (screen.thread_open() == files_thread_)
        reply_to = screen.parts.threads.answering;
    } else {
      // Sent while answering: the first of them the answer, as Element sends.
      reply_to = spl::visit(spl::overloaded{[](const compose::reply& r) { return std::optional<std::string>(r.id); },
                                                  [](const auto&) { return std::optional<std::string>(); }},
                               composing_);
    }
    for (file& one : to_send_)
      s_->net->send_file(*chosen, one.local, std::move(one.as.bytes), one.as.name, one.as.mimetype,
                         one.as.picture.has_value(), one.width, one.height, std::exchange(caption, std::string()),
                         std::exchange(reply_to, std::nullopt), thread, std::move(one.video));
    if (thread) {
      if (screen.thread_open() == files_thread_)
        screen.parts.threads.stop_answering();
    } else if (spl::visit(spl::overloaded{[](const compose::reply&) { return true; }, [](const auto&) { return false; }},
                             composing_)) {
      composing_ = compose::plain{};
      screen.line.show_context(std::nullopt);
    }
    to_send_.clear();
    files_thread_.reset();
    s_->root().close_send_box();
  }
  // A saved GIF sent into the chat, as a picture that moves -- as a file
  // dropped is, without the send box: its thumbnail shown under its local id
  // while it goes.
  void apply(const request::send_gif& one) {
    const auto& chosen = s_->root().main().chosen;
    if (!chosen || s_->demo())
      return;
    auto bytes_read = spl::bytes::file_text(one.path);
    if (!bytes_read)
      return;
    std::string bytes = std::move(*bytes_read);
    std::string name = std::filesystem::path(one.path).filename().string();
    if (!name.contains('.'))
      name += ".gif";
    file sent{logic::prepared_of(std::move(bytes), std::move(name), false, *settings_),
              std::format("mux-file-{}-{}", std::chrono::system_clock::now().time_since_epoch().count(), ++made_)};
    if (auto image = skia::decodeImage(sent.as.bytes.data(), sent.as.bytes.size())) {
      sent.width = image->width();
      sent.height = image->height();
      mux::ui::thumbnails().put("local:" + sent.local, std::move(image));
    }
    s_->root().close_emoji();
    s_->go_live(*chosen);
    s_->root().main().jump_to_end();
    // Sent while answering: the answer, as a sticker or a text would be.
    const std::optional<std::string> reply_to = this->answering();
    s_->net->send_file(*chosen, sent.local, std::move(sent.as.bytes), sent.as.name, sent.as.mimetype,
                       sent.as.picture.has_value(), sent.width, sent.height, std::string(), reply_to);
    if (reply_to)
      this->stop_answering();
  }
  // The message being answered, where one is: what a GIF or a sticker sent
  // now answers.
  [[nodiscard]] std::optional<std::string> answering() const {
    return spl::visit(spl::overloaded{[](const compose::reply& r) { return std::optional<std::string>(r.id); },
                                      [](const auto&) { return std::optional<std::string>(); }},
                      composing_);
  }
  // The answer sent: the field writes plainly again, the bar over it gone.
  void stop_answering() {
    composing_ = compose::plain{};
    s_->root().main().line.show_context(std::nullopt);
  }

  // A sticker, sent into the chat being read; the popup closed.
  void apply(const request::send_sticker& one) {
    // Kept among the recent, as tdesktop's.
    s_->emoji.remember_sticker(one.sticker);
    const auto& chosen = s_->root().main().chosen;
    if (!chosen || s_->demo())
      return;
    s_->root().close_emoji();
    s_->go_live(*chosen);
    s_->root().main().jump_to_end();
    // Sent while answering: the answer, as a text would be.
    const std::optional<std::string> reply_to = this->answering();
    s_->net->send_sticker(*chosen, one.sticker, reply_to);
    if (reply_to)
      this->stop_answering();
  }

  // Files given: read and prepared as the logic of sending says; a
  // picture's thumbnail shown under its local id while it goes. Then the
  // send box, with what was waiting in it before.
  void files_given(std::vector<std::string> paths, bool dropped) {
    if (!s_->root().main().chosen)
      return;
    // Dropped or pasted where files are not sent: said, not lost silently.
    if (!mux::ui::may_send_files(s_->ui, s_->root().main().chosen->account)) {
      s_->root().show_message("Files", "Files cannot be sent in this chat.");
      return;
    }
    // Dropped while the thread's field has the keys: into the thread, as
    // its paperclip sends.
    if (dropped && to_send_.empty())
      files_thread_ = s_->root().main().writing_in_thread() ? s_->root().main().thread_open() : std::nullopt;
    for (const std::string& path : paths) {
      auto bytes_read = mux::platform::files::read(path);
      if (!bytes_read)
        continue;
      std::string bytes = std::move(*bytes_read);
      file one{logic::prepared_of(std::move(bytes), mux::platform::files::name(path), dropped, *settings_),
               std::format("mux-file-{}-{}", std::chrono::system_clock::now().time_since_epoch().count(), ++made_)};
      if (one.as.picture)
        if (auto image = skia::decodeImage(one.as.bytes.data(), one.as.bytes.size())) {
          one.width = image->width();
          one.height = image->height();
          mux::ui::thumbnails().put("local:" + one.local, std::move(image));
        }
      // A video: its size, length and first picture, sent with it as m.video
      // says them; the picture shown under its local id while it goes.
      if (!one.as.picture && one.as.mimetype.starts_with("video/"))
        if (auto seen = mux::platform::video::player::look(path)) {
          one.width = seen->width;
          one.height = seen->height;
          one.video = mux::video_look{.duration_ms = static_cast<std::int64_t>(seen->seconds * 1000.0),
                                      .thumbnail = skia::encodeImage(*seen->first, false),
                                      .thumbnail_width = seen->width,
                                      .thumbnail_height = seen->height};
          mux::ui::thumbnails().put("local:" + one.local, std::move(seen->first));
        }
      to_send_.push_back(std::move(one));
    }
    if (to_send_.empty())
      return;
    std::vector<mux::ui::pending_file> shown;
    for (const file& one : to_send_)
      shown.push_back({one.as.name, one.as.picture ? "local:" + one.local : std::string(),
                       static_cast<std::int64_t>(one.as.bytes.size()), one.as.picture.has_value()});
    s_->root().open_send_box(shown);
  }

 private:
  // The thread what is in the send box goes into, where it is one.
  std::optional<std::string> files_thread_;
  // The field's text sent: as a message, an answer, or an edit -- as what is
  // written says -- and the field and its draft emptied.
  void send(std::string text) {
    // A text sent: the ways back from jumps let go, as tdesktop's
    // sendTextWithTags clears its reply returns.
    if (const auto& chosen = s_->root().main().chosen)
      s_->root().main().returns.erase(*chosen);
    auto& screen = s_->root().main();
    if (!screen.chosen || !logic::sendable(text))
      return;
    const conversation_id to = *screen.chosen;
    // A command of its protocol's own: asked, not sent.
    if (const auto asked = mux::proto::command_of(mux::ui::protocol_state_of(s_->ui, to.account), to, text)) {
      spl::visit(spl::overloaded{[](mux::proto::part::no_request) {}, [&](const auto& one) { s_->ask->ask_for(one); }},
                    *asked);
      screen.line.set_text({});
      return;
    }
    // What is sent goes at the chat's end: the chat back to its newest
    // first, where it is a window elsewhere, or it would not be shown.
    s_->go_live(to);
    screen.jump_to_end();
    // Who was picked from the @ list for it, its pills: sent as mentions.
    auto mentions = screen.line.mentions();
    // How it is formatted: its runs, as the field has them.
    auto styles = screen.line.styles();
    spl::visit(spl::overloaded{[&](const compose::plain&) {
                            if (s_->demo())
                              s_->ask->send(to, std::move(text));
                            else
                              s_->net->send(to, std::move(text), std::nullopt, std::move(mentions), std::move(styles));
                          },
                          [&](const compose::reply& one) {
                            if (s_->demo())
                              s_->ask->send(to, std::move(text));
                            else
                              s_->net->send(to, std::move(text), one.id, std::move(mentions), std::move(styles));
                          },
                          [&](const compose::edit& one) {
                            // A picture's: its caption, the picture kept.
                            const message* said = this->message_of(to, one.id);
                            const bool picture = said != nullptr && captioned(*said);
                            if (s_->demo())
                              s_->box->push(change_t{change::message_edited{to, one.id, body{std::move(text), std::nullopt}}});
                            else if (picture)
                              s_->net->edit_caption(to, one.id, std::move(text), *said->attachment);
                            else
                              s_->net->edit(to, one.id, std::move(text), std::move(styles));
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
    std::optional<mux::video_look> video;
  };

  services* s_;
  drafts_part* drafts_;
  const mux::config::sending_settings* settings_;
  compose_t composing_ = compose::plain{};
  std::vector<file> to_send_;
  std::uint64_t made_ = 0;
};

}  // namespace mux::app
