// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.marks.app -- The marks' part of the program: the mentions of
// the user and the reactions to their messages not yet seen -- the model's,
// as the protocols say them -- kept on disk across runs, each marked message
// kept whole with them, listed, and gone to; the mentions seen shared with
// the account's other sessions where its protocol does (Matrix's account
// data).
export module mux.plugin.marks.app;

import std;
import splice;
import knot;
import mux.composition;
import mux.core;
import mux.config;
import mux.ui;
import mux.app.network;
import mux.app.store;
import mux.app.services;
import mux.plugin.marks.types;
import mux.plugin.marks.ui;

export namespace mux::plugins::marks {

// The mentions seen in a room, told to its account to share with its other
// sessions -- where its account does.
struct share_seen {
  std::string room;
  std::vector<std::string> seen;
  template <class Account>
    requires requires(Account& account, std::string room, std::vector<std::string> seen) { account.share_marks_seen(room, seen); }
  void operator()(Account& account) const {
    account.share_marks_seen(room, seen);
  }
};

struct part {
  app::services* s_ = nullptr;
  bool loaded_ = false;
  // The list of marks open, by its kind: shown again as what it waits for
  // comes; and each message it fetched, fetched once.
  std::optional<mark_kind_t> listed_;
  bool relist_ = false;
  std::set<std::string> fetched_;
  // Marked messages kept whole as their marks are made; those to keep as
  // the model has them; those not here yet, fetched and kept when they come.
  std::set<std::string> kept_;
  std::vector<std::pair<conversation_id, std::string>> to_keep_;
  std::map<conversation_id, std::set<std::string>> on_disk_;
  std::map<std::string, conversation_id> wanted_;
  // The marks read back of chats not here yet: put in as they come, and
  // written again as they were until then, not dropped by a save before.
  std::vector<std::pair<conversation_id, change_t>> pending_;
  std::map<conversation_id, config::chat_marks> not_here_;
  // What the last save wrote of the chats the model had: carried where one
  // has gone since.
  std::map<conversation_id, config::chat_marks> saved_;
  // Each chat's mentions seen, as last given to its account to share.
  std::map<conversation_id, std::optional<std::vector<std::string>>> shared_;
  // How many marks and seen each chat had at the last look: written again
  // where that moved -- by a change, or by the program seeing them shown.
  std::vector<std::tuple<conversation_id, std::size_t, std::size_t, std::size_t>> counted_;
  bool dirty_ = false;

  // ---- what came --------------------------------------------------------------
  // A mark made: its message kept whole, as it is now -- the list shows it
  // from there, never "Loading…"; written. A message fetched aside, or the
  // word that one is not there: the list open shown again.
  void take(const change::mentioned& one) {
    to_keep_.emplace_back(one.in, one.event);
    dirty_ = true;
  }
  void take(const change::reacted_to_mine& one) {
    to_keep_.emplace_back(one.in, one.target);
    dirty_ = true;
  }
  void take(const change::reaction_changed& one) { dirty_ = dirty_ || one.live; }
  void take(const change::message_added& one) {
    relist_ = relist_ || spl::visit(spl::overloaded{[](placement::aside) { return true; }, [](const auto&) { return false; }}, one.where);
  }
  void take(const change::event_missing&) { relist_ = true; }

  // Between events: the marks read back put in where their chats came,
  // marked messages kept, the list shown again where what it waited for
  // came, the marks written where they moved, and the buttons' counts.
  void tick() {
    if (!std::exchange(loaded_, true))
      this->load();
    std::erase_if(pending_, [&](const auto& waiting) {
      if (s_->model->find(waiting.first) == nullptr)
        return false;
      s_->model->apply(waiting.second);
      not_here_.erase(waiting.first);
      dirty_ = true;
      return true;
    });
    if (!s_->demo())
      for (const auto& [in, id] : std::exchange(to_keep_, {}))
        this->keep_marked(in, id);
    this->fetched();
    if (std::exchange(dirty_, false) || this->counts_moved())
      this->save();
    const conversation* chat = this->chosen();
    const shown now{chat ? chat->unread_mentions.size() : 0, chat ? chat->unread_reactions.size() : 0};
    if (*s_->showing->look<shown>() != now)
      ui::show(*s_->showing, now);
  }

  // ---- asked ------------------------------------------------------------------
  // The oldest mention or reaction not yet seen, gone to; let go once seen.
  void apply(const jump& one) {
    const conversation* chat = this->chosen();
    if (chat == nullptr)
      return;
    const auto& marks = marks_of(*chat, one.kind);
    if (marks.empty())
      return;
    // The oldest first, as Telegram goes through them: by when each came, not
    // by the order they were learned in -- the ones caught up after a restart
    // come in after newer ones.
    const auto oldest = std::ranges::min_element(marks, {}, &unread_mark::at);
    const std::string target = oldest->target, event = oldest->event;
    this->go(*chat, one.kind, event, target);
  }
  // All of them, listed as the chat's bubbles: each mention's message, each
  // reaction's with who and what -- one not here yet, fetched on its own.
  void apply(const list& one) {
    const conversation* chat = this->chosen();
    if (chat == nullptr)
      return;
    const conversation_id& in = chat->id;
    // What was kept with the marks, for those not held now.
    const auto kept = s_->demo() ? std::map<std::string, message>{} : s_->store->marked(in);
    const auto entry_of = [&](const unread_mark& mark) {
      entry made{.event = mark.event};
      const message* said = ui::held_message(*chat, mark.target);
      if (said == nullptr)
        if (const auto found = kept.find(mark.target); found != kept.end())
          said = &found->second;
      if (said == nullptr) {
        made.said = message{.in = in, .id = mark.target, .at = mark.at, .body = {"Loading…", std::nullopt}};
        if (!s_->demo() && fetched_.insert(mark.target).second)
          s_->net->fetch_quoted(in, mark.target);
        return made;
      }
      made.said = *said;
      if (const auto reacted = std::ranges::find(said->reaction_events, mark.event, &message::reaction_event::event);
          reacted != said->reaction_events.end()) {
        made.who = reacted->who;
        made.key = reacted->key;
      }
      return made;
    };
    const auto entries = marks_of(*chat, one.kind) | std::views::transform(entry_of) | std::ranges::to<std::vector>();
    ui::show(*s_->showing, std::optional(listed{one.kind, in, entries, s_->model}));
    listed_ = one.kind;
  }
  // One of the list, gone to, and let go.
  void apply(const go_to& one) {
    const conversation* chat = this->chosen();
    if (chat == nullptr)
      return;
    const auto& marks = marks_of(*chat, one.kind);
    const auto found = std::ranges::find(marks, one.event, &unread_mark::event);
    if (found == marks.end())
      return;
    const std::string target = found->target;
    this->go(*chat, one.kind, one.event, target);
  }
  void apply(const ui::request::close_shown<listed>&) {
    listed_.reset();
    ui::show<listed>(*s_->showing, std::nullopt);
  }

  // ---- kept on disk -------------------------------------------------------------
  // The marks kept, read back at the start: each put in once its chat is here.
  void load() {
    const auto opened = s_->vault->read_file(config::state_path("marks.json"));
    if (!opened)
      return;
    const auto read = knot::try_read<config::marks_file>(std::string_view(*opened));
    if (!read)
      return;
    for (const config::chat_marks& chat : read->chats)
      this->hold({{ui::protocol_of(chat.account), chat.account}, chat.conversation}, chat);
  }
  // A chat's marks held until the chat is here, and put in then: read back
  // at the start, or kept from the last save while the chat was gone.
  void hold(const conversation_id& id, const config::chat_marks& chat) {
    const auto at = [](std::int64_t ms) { return std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::milliseconds(ms)); };
    not_here_.insert_or_assign(id, chat);
    // What was seen first, so that nothing seen comes back as unread.
    if (chat.seen)
      pending_.emplace_back(id, change_t{change::marks_seen{id, *chat.seen}});
    for (const auto& mark : chat.mentions)
      pending_.emplace_back(id, change_t{change::mentioned{id, mark.event, at(mark.at)}});
    for (const auto& mark : chat.reactions)
      pending_.emplace_back(id, change_t{change::reacted_to_mine{id, mark.event, mark.target, at(mark.at)}});
  }
  // Whether any chat's marks or seen moved since the last look.
  [[nodiscard]] bool counts_moved() {
    std::vector<std::tuple<conversation_id, std::size_t, std::size_t, std::size_t>> now;
    for (const auto& [id, owner] : s_->model->accounts())
      for (const auto& [key, one] : owner.conversations)
        if (!one.unread_mentions.empty() || !one.unread_reactions.empty() || !one.seen_marks.empty())
          now.emplace_back(one.id, one.unread_mentions.size(), one.unread_reactions.size(), one.seen_marks.size());
    if (now == counted_)
      return false;
    counted_ = std::move(now);
    return true;
  }
  // The marks written, as the model has them and as they were read of the
  // chats not here yet.
  void save() {
    if (s_->demo())
      return;
    config::marks_file out;
    std::map<conversation_id, config::chat_marks> now;
    const auto kept = [](const unread_mark& mark) {
      return config::kept_mark{mark.event, mark.target, static_cast<std::int64_t>(mark.at.time_since_epoch().count())};
    };
    for (const auto& [id, owner] : s_->model->accounts())
      for (const auto& [key, one] : owner.conversations) {
        if (one.unread_mentions.empty() && one.unread_reactions.empty() && one.seen_marks.empty())
          continue;
        config::chat_marks chat{.account = id.address, .conversation = one.id.id};
        if (!one.seen_marks.empty())
          chat.seen = one.seen_marks;
        chat.mentions = one.unread_mentions | std::views::transform(kept) | std::ranges::to<std::vector>();
        chat.reactions = one.unread_reactions | std::views::transform(kept) | std::ranges::to<std::vector>();
        now.insert_or_assign(one.id, std::move(chat));
      }
    // A chat gone from the model since the last save -- its account made
    // again (a proxy changed), a room out of a sliding sync's window, an
    // account taken away and added again -- keeps its marks: held as those
    // read back are, and put back as it comes.
    for (const auto& [id, chat] : saved_)
      if (!now.contains(id) && !not_here_.contains(id))
        this->hold(id, chat);
    // Each chat's mentions seen, to its account: shared with its other
    // sessions where it shares them (Matrix's, as set), where they changed.
    for (const auto& [id, chat] : now)
      if (chat.seen && chat.seen != shared_[id]) {
        shared_[id] = chat.seen;
        s_->net->ask(id.account, share_seen{id.id, *chat.seen});
      }
    saved_ = now;
    out.chats = now | std::views::values | std::ranges::to<std::vector>();
    out.chats.append_range(not_here_ | std::views::values);
    (void)s_->vault->write_file(config::state_path("marks.json"), knot::to_json_string(out));
  }

  // What was fetched for them: marked messages kept with their marks now;
  // and the list open shown again where what it waited for came -- a
  // message fetched for it, or the word that it is not there (its mark then
  // gone): it said "Loading…" until it was opened again.
  void fetched() {
    std::erase_if(wanted_, [&](const auto& one) {
      const auto& [id, in] = one;
      const conversation* chat = s_->model->find(in);
      const message* said = chat ? ui::held_message(*chat, id) : nullptr;
      if (said == nullptr)
        return false;
      kept_.insert(id);
      s_->store->keep_marked(in, *said);
      return true;
    });
    if (!listed_ || !s_->showing->look<std::optional<listed>>()->has_value()) {
      listed_.reset();
      relist_ = false;
      return;
    }
    if (std::exchange(relist_, false))
      this->apply(list{*listed_});
  }

  [[nodiscard]] const conversation* chosen() const {
    const auto& chosen = s_->chosen();
    return chosen ? s_->model->find(*chosen) : nullptr;
  }
  [[nodiscard]] static const std::vector<unread_mark>& marks_of(const conversation& chat, mark_kind_t kind) {
    return spl::visit(spl::overloaded{[&](mark_kind::mention) -> const std::vector<unread_mark>& { return chat.unread_mentions; },
                                      [&](mark_kind::reaction) -> const std::vector<unread_mark>& { return chat.unread_reactions; }},
                      kind);
  }
  // A marked message gone to: in a thread, where it is an answer in one --
  // the thread opened beside the chat, and the mark let go, the message
  // being shown there -- else in the timeline, straight to what is around
  // it, as a reply's quote goes. There the mark is let go only once the
  // message is on the screen (marks_shown): a jump cancelled, or still on
  // its way, leaves it.
  void go(const conversation& chat, mark_kind_t kind, const std::string& event, const std::string& target) {
    if (const message* said = ui::held_message(chat, target); said && said->thread) {
      s_->open_thread(*said->thread);
      if (!s_->demo())
        s_->net->load_thread(chat.id, *said->thread);
      s_->model->apply(change_t{change::mark_taken{chat.id, kind, event}});
      dirty_ = true;
      s_->refresh_due = true;
      return;
    }
    s_->show_message_due = app::services::message_asked{target};
    s_->refresh_due = true;
  }
  // A marked message kept whole: from what is held, else from what an
  // earlier run kept, else fetched and kept once it comes (fetched()).
  void keep_marked(const conversation_id& in, const std::string& id) {
    if (!kept_.insert(id).second)
      return;
    if (const conversation* chat = s_->model->find(in))
      if (const message* said = ui::held_message(*chat, id)) {
        s_->store->keep_marked(in, *said);
        return;
      }
    auto on_disk = on_disk_.find(in);
    if (on_disk == on_disk_.end())
      on_disk = on_disk_.emplace(in, s_->store->marked(in) | std::views::keys | std::ranges::to<std::set<std::string>>()).first;
    if (on_disk->second.contains(id))
      return;
    kept_.erase(id);
    wanted_.insert_or_assign(id, in);
    s_->net->fetch_quoted(in, id);
  }
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::marks
