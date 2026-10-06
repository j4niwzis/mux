// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.history: a chat's history -- kept on disk as it comes, with where
// what is on disk has gaps (message_store::gap_mark), and paged back from
// the disk where it has it, from the server past that. A part of the
// program: it owns its state, and reaches the rest through the services it
// is given.
export module mux.app.history;

import std;
import splice;
import mux.core;
import mux.proto;
import mux.protocols;
import mux.ui;
import mux.app.network;
import mux.app.workers;
import mux.app.store;
import mux.app.services;
import mux.app.requests;

export namespace mux::app {

class history_part {
 public:
  explicit history_part(services& shared) : s_(&shared) {}

  // A change the model was given, kept on disk as it says.
  void keep(const mux::change_t& one) {
    if (s_->demo())
      return;
    // Where the history on disk has gaps, noted as it comes (see
    // message_store::gap_mark). A page from the server: the message it was
    // paged from now follows it, and before its oldest is the token it ended
    // at -- or the room's beginning. A first sync: before its first message
    // is its prev_batch. A window loaded around a message: its oldest, not
    // known to follow anything on disk. Too many marks cost only a request.
    spl::visit(
        spl::overloaded{
            [&](const mux::change::history_position& c) {
              const mux::conversation* chat = s_->model->find(c.in);
              if (const auto paged = paging_from_.find(c.in); paged != paging_from_.end()) {
                auto& gaps = gaps_of(c.in);
                gaps.erase(paged->second);
                // The page reached what the disk had before it was asked for
                // (on_disk_, read then): the gap is closed, and the disk goes
                // on from there -- else every run's first page back left a
                // gap past which everything was asked of the server again.
                const auto known = on_disk_.find(c.in);
                const bool overlapped = chat && !chat->timeline.empty() && known != on_disk_.end() &&
                                        known->second.contains(chat->timeline.front().id);
                if (chat && !chat->timeline.empty() && !overlapped)
                  gaps.insert_or_assign(chat->timeline.front().id,
                                        message_store::gap_mark{.token = c.from, .start = !c.from.has_value()});
                paging_from_.erase(paged);
                this->changed(c.in);
              } else {
                sync_gap_.insert_or_assign(c.in, c.from);
              }
            },
            [&](const mux::change::message_added& c) {
              spl::visit(spl::overloaded{
                                [&](mux::placement::at_end) {
                                  if (const auto pending = sync_gap_.find(c.message.in); pending != sync_gap_.end()) {
                                    gaps_of(c.message.in)
                                        .insert_or_assign(c.message.id, message_store::gap_mark{.token = pending->second,
                                                                                                .start = !pending->second});
                                    sync_gap_.erase(pending);
                                    this->changed(c.message.in);
                                  }
                                },
                                [&](mux::placement::in_window) {
                                  // A window over history already on disk -- a jump back
                                  // to what was read before -- leaves no gap: what is
                                  // before it is there already, as it was. Only one
                                  // over what the disk did not have does.
                                  auto known = on_disk_.find(c.message.in);
                                  if (known == on_disk_.end())
                                    known = on_disk_
                                                .emplace(c.message.in, std::ranges::to<std::set<std::string>>(std::views::keys(s_->store->everything(c.message.in))))
                                                .first;
                                  const bool was_kept = !known->second.insert(c.message.id).second;
                                  const mux::conversation* chat = s_->model->find(c.message.in);
                                  if (!was_kept && chat && !chat->timeline.empty() && chat->timeline.front().id == c.message.id) {
                                    gaps_of(c.message.in).insert_or_assign(c.message.id, message_store::gap_mark{});
                                    this->changed(c.message.in);
                                  }
                                },
                                [](const auto&) {}},
                            c.where);
            },
            [](const auto&) {}},
        one);
    const auto as_now = [&](const mux::conversation_id& in, const std::string& id) {
      if (const mux::conversation* chat = s_->model->find(in))
        if (const auto found = std::ranges::find(chat->timeline, id, &mux::message::id); found != chat->timeline.end())
          s_->store->record(*found);
    };
    // A message added is kept as it is now in the timeline -- or, where it is
    // not in it (it came live while the chat is a window elsewhere), as it
    // came: kept either way, or it would be lost on going back to the newest.
    const auto added = [&](const mux::change::message_added& c) {
      // A message fetched for a quote is not history read in order: kept on
      // disk, it would be read back as though it were next to the rest.
      if (spl::visit(spl::overloaded{[](mux::placement::aside) { return true; }, [](const auto&) { return false; }},
                     c.where))
        return;
      const mux::conversation* chat = s_->model->find(c.message.in);
      const bool in_timeline =
          chat && std::ranges::find(chat->timeline, c.message.id, &mux::message::id) != chat->timeline.end();
      if (in_timeline)
        as_now(c.message.in, c.message.id);
      else if (!c.message.id.empty())
        s_->store->record(c.message);
    };
    spl::visit(spl::overloaded{[&](const mux::change::message_added& c) { added(c); },
                               [&](const mux::change::message_edited& c) { as_now(c.in, c.id); },
                               [&](const mux::change::message_encrypted& c) { as_now(c.in, c.id); },
                               [&](const mux::change::message_discarded& c) { s_->store->forget(c.in, c.id); },
                               [&](const mux::change::reaction_changed& c) { as_now(c.in, c.id); },
                               [&](const mux::change::receipts_changed& c) {
                                 if (const mux::conversation* chat = s_->model->find(c.in))
                                   s_->store->keep_reads(c.in, *chat);
                               },
                               [&](const mux::change::message_acknowledged& c) {
                                 s_->store->forget(c.in, c.local_id);
                                 as_now(c.in, c.id);
                               },
                               [](const auto&) {}},
               one);
  }

  // Older messages of a chat: from the disk while it has some from before
  // the oldest in memory, from the server past that.
  void apply(const request::load_older& one) {
    if (s_->demo())
      return;
    const mux::conversation* chat = s_->model->find(one.in);
    const auto before = chat && !chat->timeline.empty() ? chat->timeline.front().at
                                                        : message_store::time_point::max();
    const std::optional<std::string> front = chat && !chat->timeline.empty() ? std::optional(chat->timeline.front().id) : std::nullopt;
    // From the server: from the token of the gap before the message paged
    // from, where there is one -- else the one the model has.
    const auto from_server = [this](const mux::conversation_id& in, const std::optional<std::string>& paged_from,
                                    std::string from) {
      if (paged_from) {
        paging_from_.insert_or_assign(in, *paged_from);
        // What the disk has, before the page comes and is kept: whether it
        // reaches it is told by this.
        if (!on_disk_.contains(in))
          on_disk_.emplace(in, std::ranges::to<std::set<std::string>>(std::views::keys(s_->store->everything(in))));
        if (const auto gap = gaps_of(in).find(*paged_from); gap != gaps_of(in).end()) {
          if (gap->second.start)
            return;  // the room's beginning: nothing older anywhere
          if (gap->second.token)
            from = *gap->second.token;
        }
      }
      // Where its protocol pages back at all: an XMPP server with no
      // archive is not asked, to time out.
      if (!mux::proto::can_page_back(mux::ui::protocol_state_of(s_->ui, in.account)))
        return;
      s_->net->load_older(in, std::move(from));
    };
    // A window of the history, where gaps were not kept for this chat: the
    // disk may not have what is next to it. From the server.
    this->gaps_of(one.in);  // read from its file, where it has one
    const bool gaps_known = gaps_kept_before_.contains(one.in);
    if (chat && chat->detached && !gaps_known) {
      from_server(one.in, front, one.from);
      return;
    }
    // The message paged from has a gap before it: nothing on disk follows.
    if (front && gaps_of(one.in).contains(*front)) {
      from_server(one.in, front, one.from);
      return;
    }
    s_->work->run([this, in = one.in, from = one.from, before, front, from_server]() -> workers::done_t {
      auto kept = s_->store->older(in, before, 100);
      return [this, in, from, front, from_server, kept = std::move(kept)]() mutable {
        // Up to the first gap from the newest: what is before it is not
        // known to follow.
        const auto& gaps = gaps_of(in);
        const auto cut = std::ranges::find_if(std::views::reverse(kept),
                                              [&](const mux::message& said) { return gaps.contains(said.id); });
        if (cut != (std::views::reverse(kept)).end())
          kept.erase(kept.begin(), std::prev(cut.base()));
        if (kept.empty()) {
          from_server(in, front, from);
          return;
        }
        for (auto it = kept.rbegin(); it != kept.rend(); ++it)
          s_->model->apply(mux::change_t{mux::change::message_added{.message = std::move(*it), .where = mux::placement::at_start{}}});
        // The window may ask again: there may be more on the disk.
        s_->root().main().history_asked.reset();
        s_->refresh_due = true;
      };
    });
  }
  // A window around a message jumped to, and a window paged forward.
  void apply(const request::load_context& one) {
    if (!s_->demo())
      s_->net->load_context(one.in, one.target);
  }
  void apply(const request::load_newer& one) {
    if (!s_->demo())
      s_->net->load_newer(one.in, one.from);
  }

 private:
  // A chat's gaps, read from its file the first time.
  message_store::gaps_t& gaps_of(const mux::conversation_id& in) {
    auto found = gaps_.find(in);
    if (found == gaps_.end()) {
      auto read = s_->store->gaps(in);
      if (read)
        gaps_kept_before_.insert(in);
      found = gaps_.emplace(in, read.value_or(message_store::gaps_t{})).first;
    }
    return found->second;
  }
  void changed(const mux::conversation_id& in) {
    gaps_kept_before_.insert(in);
    s_->store->keep_gaps(in, gaps_of(in));
  }

  services* s_;
  // The gaps of each chat's history on disk, as read and as changed; the
  // message each chat was paged back from, on the server, while it is; and
  // the token of a first sync, for the first message it brings.
  std::map<mux::conversation_id, message_store::gaps_t> gaps_;
  std::set<mux::conversation_id> gaps_kept_before_;
  std::map<mux::conversation_id, std::string> paging_from_;
  std::map<mux::conversation_id, std::optional<std::string>> sync_gap_;
  // The ids of each chat's messages on disk, read once when a window is
  // loaded in it: a window over what is there leaves no gap.
  std::map<mux::conversation_id, std::set<std::string>> on_disk_;
};

}  // namespace mux::app
