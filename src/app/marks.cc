// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.marks: Telegram's @ and heart -- the mentions of the user and the
// reactions to their messages not yet seen: kept on disk across runs, each
// marked message kept whole with them, listed, and gone to. A part of the
// program: it owns its state, and reaches the rest through the services it
// is given.
export module mux.app.marks;

import std;
import splice;
import knot;
import mux.core;
import mux.config;
import mux.ui;
import mux.app.network;
import mux.app.store;
import mux.app.services;
import mux.app.requests;

export namespace mux::app {

class marks_part {
 public:
  explicit marks_part(services& shared) : s_(&shared) {}

  // The marks kept, read back at the start: each put in once its chat is here.
  void load() {
    const auto opened = s_->vault->read_file(mux::config::state_path("marks.json"));
    if (!opened)
      return;
    const auto read = knot::try_read<mux::config::marks_file>(std::string_view(*opened));
    if (!read)
      return;
    for (const mux::config::chat_marks& chat : read->chats)
      this->hold({{mux::ui::protocol_of(chat.account), chat.account}, chat.conversation}, chat);
  }
  // A chat's marks held until the chat is here, and put in then: read back
  // at the start, or kept from the last save while the chat was gone.
  void hold(const mux::conversation_id& id, const mux::config::chat_marks& chat) {
    const auto at = [](std::int64_t ms) {
      return std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::milliseconds(ms));
    };
    not_here_.insert_or_assign(id, chat);
    // What was seen first, so that nothing seen comes back as unread.
    if (chat.seen)
      pending_.emplace_back(id, mux::change_t{mux::change::marks_seen{id, *chat.seen}});
    for (const auto& mark : chat.mentions)
      pending_.emplace_back(id, mux::change_t{mux::change::mentioned{id, mark.event, at(mark.at)}});
    for (const auto& mark : chat.reactions)
      pending_.emplace_back(id, mux::change_t{mux::change::reacted_to_mine{id, mark.event, mark.target, at(mark.at)}});
  }

  // The marks written, as the model has them and as they were read of the
  // chats not here yet.
  void save() {
    if (s_->demo())
      return;
    mux::config::marks_file out;
    std::map<mux::conversation_id, mux::config::chat_marks> now;
    const auto kept = [](const mux::unread_mark& mark) {
      return mux::config::kept_mark{mark.event, mark.target, static_cast<std::int64_t>(mark.at.time_since_epoch().count())};
    };
    for (const auto& [id, account] : s_->model->accounts())
      for (const auto& [key, one] : account.conversations) {
        if (one.unread_mentions.empty() && one.unread_reactions.empty() && one.seen_marks.empty())
          continue;
        mux::config::chat_marks chat{.account = id.address, .conversation = one.id.id};
        if (!one.seen_marks.empty())
          chat.seen = one.seen_marks;
        std::ranges::transform(one.unread_mentions, std::back_inserter(chat.mentions), kept);
        std::ranges::transform(one.unread_reactions, std::back_inserter(chat.reactions), kept);
        now.insert_or_assign(one.id, std::move(chat));
      }
    // A chat gone from the model since the last save -- its account made
    // again (a proxy changed), a room out of a sliding sync's window, an
    // account taken away and added again -- keeps its marks: held as those
    // read back are, and put back as it comes. Written from the model alone,
    // its seen list was lost, and its mentions came back unread.
    for (const auto& [id, chat] : saved_)
      if (!now.contains(id) && !not_here_.contains(id))
        this->hold(id, chat);
    // Each chat's mentions seen, to its account: shared with its other
    // sessions where it shares them (Matrix's, as set), where they changed.
    for (const auto& [id, chat] : now)
      if (chat.seen && chat.seen != shared_[id]) {
        shared_[id] = chat.seen;
        s_->net->on_account_of(id, [room = id.id, seen = *chat.seen](auto& account)
                                       -> decltype(void(account.share_marks_seen(room, seen))) { account.share_marks_seen(room, seen); });
      }
    saved_ = now;
    std::ranges::copy(std::views::values(now), std::back_inserter(out.chats));
    std::ranges::copy(std::views::values(not_here_), std::back_inserter(out.chats));
    (void)s_->vault->write_file(mux::config::state_path("marks.json"), knot::to_json_string(out));
  }

  // The changes the model was given: the marks read back put in where their
  // chats came, written where they changed, and a mark made kept with its
  // message, whole, as it is now -- the list shows it from there, never
  // "Loading…".
  void took(const std::vector<mux::change_t>& changes) {
    std::erase_if(pending_, [&](const auto& waiting) {
      if (s_->model->find(waiting.first) == nullptr)
        return false;
      s_->model->apply(waiting.second);
      not_here_.erase(waiting.first);
      return true;
    });
    const bool changed = std::ranges::any_of(changes, [](const mux::change_t& one) {
      return spl::visit(spl::overloaded{[](const mux::change::mentioned&) { return true; },
                                              [](const mux::change::reacted_to_mine&) { return true; },
                                              [](const mux::change::reaction_changed& c) { return c.live; },
                                              [](const auto&) { return false; }},
                           one);
    });
    if (changed)
      this->save();
    if (s_->demo())
      return;
    for (const mux::change_t& one : changes)
      spl::visit(spl::overloaded{[&](const mux::change::mentioned& m) { this->keep_marked(m.in, m.event); },
                                       [&](const mux::change::reacted_to_mine& r) { this->keep_marked(r.in, r.target); },
                                       [](const auto&) {}},
                    one);
  }

  // What was fetched for them: marked messages kept with their marks now;
  // and the list open shown again where what it waited for came -- a
  // message fetched for it, or the word that it is not there (its mark then
  // gone): it said "Loading…" until it was opened again.
  void fetched(const std::vector<mux::change_t>& changes) {
    std::erase_if(wanted_, [&](const auto& one) {
      const auto& [id, in] = one;
      const mux::conversation* chat = s_->model->find(in);
      const mux::message* said = chat ? mux::ui::held_message(*chat, id) : nullptr;
      if (said == nullptr)
        return false;
      kept_.insert(id);
      s_->store->keep_marked(in, *said);
      return true;
    });
    if (!listed_ || !s_->root().marks_up()) {
      listed_.reset();
      return;
    }
    const bool waited = std::ranges::any_of(changes, [](const mux::change_t& one) {
      return spl::visit(spl::overloaded{[](const mux::change::message_added& c) {
                                                return spl::visit(spl::overloaded{[](mux::placement::aside) { return true; },
                                                                                        [](const auto&) { return false; }},
                                                                     c.where);
                                              },
                                              [](const mux::change::event_missing&) { return true; },
                                              [](const auto&) { return false; }},
                           one);
    });
    if (waited)
      this->apply(request::list_marks{*listed_});
  }

  // The oldest mention or reaction not yet seen, gone to; let go once seen.
  void apply(const request::jump_to_mark& one) {
    const mux::conversation* chat = this->chosen();
    if (chat == nullptr)
      return;
    const auto& marks = marks_of(*chat, one.kind);
    if (marks.empty())
      return;
    // The oldest first, as Telegram goes through them: by when each came, not
    // by the order they were learned in -- the ones caught up after a restart
    // come in after newer ones.
    const auto oldest = std::ranges::min_element(marks, {}, &mux::unread_mark::at);
    const std::string target = oldest->target, event = oldest->event;
    this->go_to(*chat, one.kind, event, target);
  }
  // All of them, listed as the chat's bubbles: each mention's message, each
  // reaction's with who and what -- one not here yet, fetched on its own.
  void apply(const request::list_marks& one) {
    const mux::conversation* chat = this->chosen();
    if (chat == nullptr)
      return;
    const mux::conversation_id& in = chat->id;
    // What was kept with the marks, for those not held now.
    const auto kept = s_->demo() ? std::map<std::string, mux::message>{} : s_->store->marked(in);
    const auto entry_of = [&](const mux::unread_mark& mark) {
      mux::ui::mark_entry entry{.event = mark.event};
      const mux::message* said = mux::ui::held_message(*chat, mark.target);
      if (said == nullptr)
        if (const auto found = kept.find(mark.target); found != kept.end())
          said = &found->second;
      if (said == nullptr) {
        entry.said = mux::message{.in = in, .id = mark.target, .at = mark.at, .body = {"Loading…", std::nullopt}};
        if (!s_->demo() && fetched_.insert(mark.target).second)
          s_->net->fetch_quoted(in, mark.target);
        return entry;
      }
      entry.said = *said;
      if (const auto reacted = std::ranges::find(said->reaction_events, mark.event, &mux::message::reaction_event::event);
          reacted != said->reaction_events.end()) {
        entry.who = reacted->who;
        entry.key = reacted->key;
      }
      return entry;
    };
    const auto entries = std::ranges::to<std::vector>(std::views::transform(marks_of(*chat, one.kind), entry_of));
    s_->root().open_marks(one.kind, *chat, entries, s_->model);
    listed_ = one.kind;
  }
  // One of the list, gone to, and let go.
  void apply(const request::go_to_mark& one) {
    const mux::conversation* chat = this->chosen();
    if (chat == nullptr)
      return;
    const auto& marks = marks_of(*chat, one.kind);
    const auto found = std::ranges::find(marks, one.event, &mux::unread_mark::event);
    if (found == marks.end())
      return;
    const std::string target = found->target;
    this->go_to(*chat, one.kind, one.event, target);
  }
  void apply(const request::close_marks&) {
    listed_.reset();
    s_->root().close_marks();
  }

 private:
  [[nodiscard]] const mux::conversation* chosen() const {
    const auto& chosen = s_->root().main().chosen;
    return chosen ? s_->model->find(*chosen) : nullptr;
  }
  [[nodiscard]] static const std::vector<mux::unread_mark>& marks_of(const mux::conversation& chat, mux::mark_kind_t kind) {
    return spl::visit(
        spl::overloaded{[&](mux::mark_kind::mention) -> const std::vector<mux::unread_mark>& { return chat.unread_mentions; },
                           [&](mux::mark_kind::reaction) -> const std::vector<mux::unread_mark>& { return chat.unread_reactions; }},
        kind);
  }

  // A marked message gone to: in a thread, where it is an answer in one --
  // the thread opened beside the chat, and the mark let go, the message
  // being shown there -- else in the timeline, straight to what is around
  // it, as a reply's quote goes. There the mark is let go only once the
  // message is on the screen (marks_shown): a jump cancelled, or still on
  // its way, leaves it. Paged back to in the timeline, an answer in a thread
  // was looked for there for ever.
  void go_to(const mux::conversation& chat, mux::mark_kind_t kind, const std::string& event, const std::string& target) {
    if (const mux::message* said = mux::ui::held_message(chat, target); said && said->thread) {
      s_->root().main().open_thread(*said->thread);
      if (!s_->demo())
        s_->net->load_thread(chat.id, *said->thread);
      s_->model->apply(mux::change_t{mux::change::mark_taken{chat.id, kind, event}});
      this->save();
      s_->refresh_due = true;
      return;
    }
    s_->root().main().jump_to(target);
    s_->refresh_due = true;
  }

  // A marked message kept whole: from what is held, else from what an
  // earlier run kept, else fetched and kept once it comes (fetched()).
  void keep_marked(const mux::conversation_id& in, const std::string& id) {
    if (!kept_.insert(id).second)
      return;
    if (const mux::conversation* chat = s_->model->find(in))
      if (const mux::message* said = mux::ui::held_message(*chat, id)) {
        s_->store->keep_marked(in, *said);
        return;
      }
    auto on_disk = on_disk_.find(in);
    if (on_disk == on_disk_.end())
      on_disk = on_disk_.emplace(in, std::ranges::to<std::set<std::string>>(std::views::keys(s_->store->marked(in)))).first;
    if (on_disk->second.contains(id))
      return;
    kept_.erase(id);
    wanted_.insert_or_assign(id, in);
    s_->net->fetch_quoted(in, id);
  }

  services* s_;
  // The list of marks open, by its kind: shown again as what it waits for
  // comes; and each message it fetched, fetched once.
  std::optional<mux::mark_kind_t> listed_;
  std::set<std::string> fetched_;
  // Marked messages kept whole as their marks are made; those not here yet,
  // fetched and kept when they come.
  std::set<std::string> kept_;
  std::map<mux::conversation_id, std::set<std::string>> on_disk_;
  std::map<std::string, mux::conversation_id> wanted_;
  // The marks read back of chats not here yet: put in as they come, and
  // written again as they were until then, not dropped by a save before.
  std::vector<std::pair<mux::conversation_id, mux::change_t>> pending_;
  std::map<mux::conversation_id, mux::config::chat_marks> not_here_;
  // What the last save wrote of the chats the model had: carried where one
  // has gone since.
  std::map<mux::conversation_id, mux::config::chat_marks> saved_;
  // Each chat's mentions seen, as last given to its account to share.
  std::map<mux::conversation_id, std::optional<std::vector<std::string>>> shared_;
};

}  // namespace mux::app
