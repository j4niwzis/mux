// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.jumps: where the chat's list goes, as the program decides it --
// a message jumped to (a quote, a link, a search's find, the first unread
// as a chat opens), brought into view and flashed: aimed at where it is
// held, fetched with what is around it or paged back to where it is not;
// the ways back from jumps ("↓" goes back to each, the last first, before
// the newest, as Telegram's; and the chats a jump left, to go back to);
// the history asked for at the list's edges; and what the list says the
// reader sees. The list is told all of it through the shown model
// (chat_told, message_marks, chat_ways) and answers what it did
// (ListAimed, ListEdge, ListSeen): it keeps none of it, and the program
// measures nothing on the screen. A part of the program: it reaches the
// rest through the services.
export module mux.app.jumps;

import std;
import splice;
import skiff.compose;
import mux.core;
import mux.config;
import mux.ui;
import mux.ui.proto;
import mux.proto;
import mux.protocols;
import mux.kept_root;
import mux.app.network;
import mux.app.services;
import mux.app.requests;

export namespace mux::app {

using chat_aimed = skiff::compose::ListAimed<std::string, mux::ui::chat_list_tag>;
using chat_edge = skiff::compose::ListEdge<mux::ui::chat_list_tag>;
using chat_seen = skiff::compose::ListSeen<std::string, mux::ui::chat_list_tag>;
using thread_aimed = skiff::compose::ListAimed<std::string, mux::ui::thread_list_tag>;
using thread_edge = skiff::compose::ListEdge<mux::ui::thread_list_tag>;
using thread_seen = skiff::compose::ListSeen<std::string, mux::ui::thread_list_tag>;
using chat_span = skiff::compose::ListSpan<std::string, mux::ui::chat_list_tag>;
using thread_span = skiff::compose::ListSpan<std::string, mux::ui::thread_list_tag>;

// The first unread message among those held: the one after the message
// read up to that the reader did not send. Where that marker is not known
// yet -- a chat opened before its read marker is loaded, which the server
// gives only as a count of unread -- that many messages back from the
// newest, as the server counts them: said, not done, and not the reader's.
[[nodiscard]] inline std::optional<std::string> first_unread_here(const mux::conversation& one) {
  const auto& all = one.timeline;
  if (one.unread_here() <= 0)
    return std::nullopt;
  if (one.read_up_to) {
    const auto read = std::ranges::find(all, *one.read_up_to, &mux::message::id);
    if (read == all.end())
      return std::nullopt;
    const auto after = std::ranges::subrange(std::next(read), all.end());
    const auto theirs = std::ranges::find_if(after, std::not_fn(&mux::message::outgoing));
    return theirs == after.end() ? std::nullopt : std::optional(theirs->id);
  }
  auto counted = all | std::views::reverse | std::views::filter([](const mux::message& said) { return !said.outgoing && !said.service; }) | std::views::take(one.unread_here());
  std::optional<std::string> found;
  for (const mux::message& said : counted) { found = said.id; }
  return found;
}
// Where a chat with unread opens: its first unread held, or else the
// message read up to, to be fetched with what is around it.
[[nodiscard]] inline std::optional<std::string> first_unread(const mux::conversation& one) {
  if (auto here = first_unread_here(one))
    return here;
  if (one.unread_here() > 0 && one.read_up_to)
    return one.read_up_to;
  return std::nullopt;
}

// Each chat's room events as it shows them -- in an encrypted room, who
// joins and who is invited always: each is given the room's key, and the
// server could put anyone there.
[[nodiscard]] inline mux::room_event_filter chat_filter_of(const services& shared, const mux::conversation& chat) {
  mux::room_event_filter filter = shared.kept->reads().room_event_filter_of(chat.id);
  if (chat.encrypted)
    for (const mux::room_event_t& kind : std::array{mux::room_event_t{mux::room_event::joins{}}, mux::room_event_t{mux::room_event::invites{}}}) { filter.shown[kind.index()] = true; }
  return filter;
}
// Whether the chat shows a message: not a room event it hides.
[[nodiscard]] inline bool chat_shows(const services& shared, const mux::conversation& chat, const mux::message& said) {
  return !said.service || chat_filter_of(shared, chat).shows(said.event_kind);
}

class jumps_part {
 public:
  explicit jumps_part(services& shared) : s_(&shared) {}
  jumps_part(const jumps_part&) = delete;
  jumps_part& operator=(const jumps_part&) = delete;

  // What the list's rows are made with: the program's, given once.
  void made_with(const mux::ui::bubble_needs& needs) { needs_ = needs; }

  // -- where the list goes

  // A message to go to in the chat open: brought into view in the middle
  // and flashed, the part of it a quote marked; or, quiet -- a chat opened
  // at what it was read up to -- near the top, its unread below it.
  template <class Asked>
  void go(const mux::conversation_id& in, std::string to, std::optional<std::string> fragment, bool quiet, const Asked& asked) {
    going_ = going_t{.in = in, .to = std::move(to), .fragment = std::move(fragment), .quiet = quiet, .since = now_ms(), .began = now_ms()};
    this->go_on(asked);
  }
  // A way back taken, or the newest: "↓".
  template <class Asked>
  void to_end(const Asked& asked) {
    const auto in = this->chosen();
    if (!in)
      return;
    if (auto& stack = returns_[*in]; !stack.empty()) {
      std::string back = std::move(stack.back());
      stack.pop_back();
      this->go(*in, std::move(back), std::nullopt, false, asked);
      return;
    }
    going_.reset();
    aim_ = {.end = true, .number = ++number_};
    this->tell();
  }
  // A page of the messages up or down, most of what is in view: PageUp and
  // PageDown.
  void apply(const request::page_messages& one) { this->page(one.up); }
  void page(bool up) {
    if (!this->chosen())
      return;
    going_.reset();
    aim_ = {.number = ++number_, .page = up ? -1 : 1};
    this->tell();
  }
  // A jump in this chat about to go: the message it went from, else the
  // newest the reader sees, to come back to. As tdesktop's doneJumpFrom:
  // a way back to where it goes is done with first (skipReplyReturn).
  void note_return(const std::string& to, std::optional<std::string> from) {
    const auto in = this->chosen();
    if (!in)
      return;
    auto& stack = returns_[*in];
    while (!stack.empty() && stack.back() == to)
      stack.pop_back();
    if (auto here = std::move(from).or_else([&] { return seen_.bottom; }))
      stack.push_back(std::move(*here));
    this->show_ways();
  }
  // A jump to another chat about to go: this one, to come back to.
  void note_chat_return() {
    if (const auto in = this->chosen())
      chat_returns_.push_back(*in);
    this->show_ways();
  }
  [[nodiscard]] std::optional<mux::conversation_id> take_chat_return() {
    if (chat_returns_.empty())
      return std::nullopt;
    mux::conversation_id back = std::move(chat_returns_.back());
    chat_returns_.pop_back();
    this->show_ways();
    return back;
  }
  // Another chat shown: the ways back from jumps in the one left gone, as
  // tdesktop clears its reply returns (showHistory); a jump asked with its
  // opening -- a link to a message in it -- kept; else, with unread, opened
  // at the first, quietly.
  template <class Asked>
  void opened(const Asked& asked) {
    const auto in = this->chosen();
    returns_.clear();
    seen_ = {};
    unread_from_.reset();
    // Come back to the chat a jump left, by hand: that way back is done.
    while (in && !chat_returns_.empty() && chat_returns_.back() == *in)
      chat_returns_.pop_back();
    if (going_ && going_->in != in)
      going_.reset();
    const mux::conversation* chat = in ? s_->model->find(*in) : nullptr;
    // Its bubbles as its levels say: set before its rows are made.
    s_->looks.bubbles = in ? s_->kept->reads().bubbles_of(*in) : mux::config::bubble_look{};
    ++looks_;
    if (chat && !going_) {
      unread_from_ = first_unread_here(*chat);
      if (auto first = first_unread(*chat)) {
        this->go(*in, std::move(*first), std::nullopt, true, asked);
        return;
      }
    }
    this->tell();
  }

  // -- the thread open beside the chat

  // A message of the thread open -- its root or an answer -- brought into
  // view in its panel and flashed: a quote of it pressed. False where it
  // is not one of its.
  bool aim_in_thread(const std::string& id) {
    const auto& shown = *s_->showing->look<mux::ui::chat_shown>();
    const mux::conversation* chat = shown.chosen ? s_->model->find(*shown.chosen) : nullptr;
    if (!chat || !shown.thread)
      return false;
    const auto answers = chat->threads.find(*shown.thread);
    if (id != *shown.thread && (answers == chat->threads.end() || !std::ranges::contains(answers->second, id, &mux::message::id)))
      return false;
    thread_aim_ = {.key = id, .number = ++number_};
    this->tell_thread();
    return true;
  }
  void apply(const thread_aimed& one) {
    mux::ui::change_shown<mux::ui::message_marks>(*s_->showing, [&](mux::ui::message_marks& marks) {
      marks.flashed = one.key;
      marks.fragment.reset();
      ++marks.flash;
    });
    flash_until_ = now_ms() + kFlashMs;
  }
  // At its top, the page of answers before those held, as a chat's history
  // is asked for at its top: a thread opens at its newest page only.
  void apply(const thread_edge& one) {
    const auto& shown = *s_->showing->look<mux::ui::chat_shown>();
    if (one.older && shown.chosen && shown.thread)
      s_->net->load_thread_older(*shown.chosen, *shown.thread);
  }
  // What the thread's panel shows: its pictures asked for as it changes,
  // those around it only.
  void apply(const thread_seen& one) {
    if (const services::thread_list_now now{one.top, one.bottom}; now != s_->thread_list) {
      s_->thread_list = now;
      // Asked again only near the ends of what was last asked for: at every
      // row scrolled past, the messages around were all parsed again for
      // their emoji, and a long thread lagged.
      const auto& shown = *s_->showing->look<mux::ui::chat_shown>();
      const mux::conversation* chat = shown.chosen ? s_->model->find(*shown.chosen) : nullptr;
      const auto answers = chat && shown.thread ? chat->threads.find(*shown.thread) : decltype(chat->threads.end()){};
      if (!chat || !shown.thread || answers == chat->threads.end() ||
          services::nears_end(answers->second, s_->thread_pictures_span, one.top, one.bottom))
        s_->ui.pictures_due = true;
    }
  }
  // What the thread's list is told: the thread open, if one is.
  void tell_thread() {
    const auto& shown = *s_->showing->look<mux::ui::chat_shown>();
    const mux::conversation* chat = shown.chosen ? s_->model->find(*shown.chosen) : nullptr;
    if (thread_of_ != shown.thread) {
      thread_of_ = shown.thread;
      thread_aim_ = {};
    }
    mux::ui::thread_told now;
    if (chat && shown.threads_open && shown.thread) {
      now.of = chat->id;
      now.thread = shown.thread;
      now.aim = thread_aim_;
      needs_.now = s_->model;
      now.needs = &needs_;
      now.frost = s_->looks.window.frost;
      now.looks = looks_;
      now.fetched = this->fetched();
    }
    if (*s_->showing->look<mux::ui::thread_told>() != now)
      mux::ui::show(*s_->showing, std::move(now));
  }

  // -- rows selected by a press, or a drag across them

  // As the drag goes, the rows it would select (or let go) washed as
  // selected; let go, the span selected -- or let go, where its first was
  // selected -- a press that did not move, the one row.
  template <class Asked>
  void apply(const chat_span& one, const Asked& asked) {
    this->spanned(one.first, one.last, one.done, false, asked);
  }
  template <class Asked>
  void apply(const thread_span& one, const Asked& asked) {
    this->spanned(one.first, one.last, one.done, true, asked);
  }

  // -- what the list answers

  // Brought into view: the jump done, and flashed where it is not quiet.
  void apply(const chat_aimed& one) {
    if (!going_ || going_->to != one.key)
      return;
    if (!going_->quiet)
      mux::ui::change_shown<mux::ui::message_marks>(*s_->showing, [&](mux::ui::message_marks& marks) {
        marks.flashed = one.key;
        marks.fragment = going_->fragment;
        ++marks.flash;
      });
    if (!going_->quiet)
      flash_until_ = now_ms() + kFlashMs;
    going_.reset();
    this->show_ways();
  }
  // At an edge of what is held: older history asked at the top -- not
  // again for the same page while it comes, but again after a while it
  // did not -- and, a window away from the newest, the next page at the
  // bottom.
  template <class Asked>
  void apply(const chat_edge& one, const Asked& asked) {
    const auto in = this->chosen();
    const mux::conversation* chat = in ? s_->model->find(*in) : nullptr;
    if (!chat)
      return;
    if (one.older)
      this->ask_older(*chat, asked);
    else if (chat->detached && chat->future_from && newer_asked_ != chat->future_from) {
      newer_asked_ = chat->future_from;
      asked(request::load_newer{chat->id, *chat->future_from});
    }
  }
  // What the reader sees: each way back come down to let go -- once the
  // view's middle is at it or past it, or the view is at the end -- as
  // tdesktop's checkReplyReturns.
  void apply(const chat_seen& one) {
    // Other messages in view: their pictures asked for -- the people's, the
    // thumbnails, the previews -- as they come into view. Asked only as
    // the window was refreshed, one scrolled or jumped to kept its plate,
    // grey, until something else refreshed it.
    const auto in = this->chosen();
    if (const mux::conversation* shown = in ? s_->model->find(*in) : nullptr;
        seen_ != one && shown && services::nears_end(shown->timeline, s_->chat_pictures_span, one.top, one.bottom))
      s_->ui.pictures_due = true;
    seen_ = one;
    const mux::conversation* chat = in ? s_->model->find(*in) : nullptr;
    if (!chat)
      return;
    const auto place = [&](const std::string& id) -> std::optional<std::size_t> {
      const auto at = std::ranges::find(chat->timeline, id, &mux::message::id);
      return at == chat->timeline.end() ? std::nullopt : std::optional(static_cast<std::size_t>(at - chat->timeline.begin()));
    };
    auto& stack = returns_[*in];
    const auto middle = one.middle ? place(*one.middle) : std::nullopt;
    const auto passed = [&](const std::string& id) {
      const auto at = place(id);
      return one.end || (at && middle && *at <= *middle);
    };
    while (!stack.empty() && passed(stack.back()))
      stack.pop_back();
    this->show_ways();
  }
  // The newest message the reader has seen whole, while no jump is on its
  // way: what passes by on the way is not read.
  [[nodiscard]] std::optional<std::string> last_seen() const { return going_ ? std::nullopt : seen_.bottom; }
  [[nodiscard]] const chat_seen& seen() const { return seen_; }

  // -- going on

  // Each frame while a jump is on its way, and as the chat changes: where
  // the message is held, aimed at; held as an answer in a thread, its
  // thread opened; else fetched with what is around it, and where that
  // does not bring it, paged back to -- as far as the chat's limit says.
  template <class Asked>
  void go_on(const Asked& asked) {
    // Its flash over: what is flashed let go, so that a row made again for
    // it later -- scrolled back to -- is not flashed again as it is made.
    if (flash_until_ > 0.0 && now_ms() >= flash_until_) {
      flash_until_ = 0.0;
      mux::ui::change_shown<mux::ui::message_marks>(*s_->showing, [](mux::ui::message_marks& marks) {
        marks.flashed.reset();
        marks.fragment.reset();
      });
    }
    this->tell_thread();
    this->follow_looks();
    if (this->chosen() && s_->showing->look<mux::ui::chat_told>()->fetched != this->fetched())
      this->tell();
    this->show_ways();
    if (!going_)
      return;
    auto& jump = *going_;
    const auto in = this->chosen();
    const mux::conversation* chat = in ? s_->model->find(*in) : nullptr;
    if (!chat || *in != jump.in) {
      going_.reset();
      this->tell();
      return;
    }
    const double now = now_ms();
    const auto& all = chat->timeline;
    // More of the chat come: the jump got somewhere.
    if (all.size() != jump.held) {
      jump.held = all.size();
      jump.since = now;
    }
    if (const auto found = std::ranges::find(all, jump.to, &mux::message::id); found != all.end()) {
      // A line the chat's settings hide -- a join, a rename, replied to:
      // landed on the nearest shown after it, else before it.
      const mux::room_event_filter filter = this->filter_of(*chat);
      const auto shows = [&](const mux::message& said) { return !said.service || filter.shows(said.event_kind); };
      if (!shows(*found)) {
        const auto after = std::ranges::find_if(std::ranges::subrange(found, all.end()), shows);
        const auto before = std::ranges::find_if(std::ranges::subrange(all.begin(), found) | std::views::reverse, shows);
        if (after != all.end())
          jump.to = after->id;
        else if (before != std::ranges::end(std::ranges::subrange(all.begin(), found) | std::views::reverse))
          jump.to = before->id;
        else {
          going_.reset();
          this->tell();
          return;
        }
      }
      if (aim_.key != jump.to || aim_.end) {
        aim_ = {.key = jump.to,
                .number = ++number_,
                .top = jump.quiet ? std::optional(unread_from_ && *unread_from_ == jump.to ? 0.0f : 60.0f) : std::nullopt,
                .line = jump.fragment};
        this->tell();
      }
      return;
    }
    if (const mux::message* held = mux::ui::held_message(*chat, jump.to); held && held->thread) {
      // Come as an answer in a thread: kept with its thread, never in the
      // timeline. Its thread opened.
      asked(request::open_thread{*held->thread});
      going_.reset();
      this->tell();
      return;
    }
    if (mux::proto::offers(mux::ui::protocol_state_of(s_->ui, jump.in.account), mux::proto::feature::history_context{}) && !jump.paging) {
      // Not here: a window of the history around it, from the server --
      // not all of it from here to there. Where that does not bring it in
      // a while, paged back to.
      if (!jump.context_asked) {
        jump.context_asked = true;
        jump.since = now;
        asked(request::load_context{jump.in, jump.to});
      } else if (now - jump.since > kContextPatienceMs) {
        jump.paging = true;
      }
      return;
    }
    if (chat->history_from) {
      if (!jump.base)
        jump.base = all.size();
      const std::int64_t most = s_->kept->reads().jump_search_of(jump.in);
      if (most > 0 && static_cast<std::int64_t>(all.size() - std::min(*jump.base, all.size())) >= most) {
        going_.reset();  // as far as the chat's limit: given up
        this->tell();
        return;
      }
      if (this->ask_older(*chat, asked))
        jump.since = now;
    } else if (now - jump.since > 2000.0) {
      going_.reset();  // the beginning, and it was not there
      this->tell();
    }
  }
  [[nodiscard]] bool going() const { return going_.has_value(); }
  // When the loader is due: a jump that lands in a frame or two -- Ctrl+Up
  // to a message in view -- never shows it, as tdesktop's does not; one
  // still on its way after a while does. The window woken for it then.
  [[nodiscard]] double loader_wake_in() const {
    if (!going_ || this->waited())
      return std::numeric_limits<double>::infinity();
    return std::max(1.0, going_->began + kLoaderAfterMs - now_ms());
  }
  [[nodiscard]] std::optional<std::string> going_to() const { return going_ ? std::optional(going_->to) : std::nullopt; }
  // Let go -- by hand, at the loader; or the message is not there.
  void stop() {
    going_.reset();
    this->show_ways();
  }
  void apply(const request::stop_jump&) { this->stop(); }
  // A text sent: the ways back let go.
  void clear_returns() {
    if (const auto in = this->chosen())
      returns_.erase(*in);
    this->show_ways();
  }
  // To the newest, the ways back let go: what was sent is there.
  void to_newest() {
    going_.reset();
    if (const auto in = this->chosen())
      returns_.erase(*in);
    aim_ = {.end = true, .number = ++number_};
    this->tell();
  }
  // Every message in view now, while no jump is on its way: what the
  // reader can be said to see.
  [[nodiscard]] std::vector<std::string> shown_now() const {
    const auto in = this->chosen();
    const mux::conversation* chat = in ? s_->model->find(*in) : nullptr;
    if (!chat || going_ || !seen_.top || !seen_.bottom)
      return {};
    const auto& all = chat->timeline;
    const auto top = std::ranges::find(all, *seen_.top, &mux::message::id);
    const auto bottom = std::ranges::find(all, *seen_.bottom, &mux::message::id);
    if (top == all.end() || bottom == all.end() || bottom < top)
      return {};
    return std::ranges::subrange(top, std::next(bottom)) | std::views::transform(&mux::message::id) | std::ranges::to<std::vector>();
  }
  // The chat's bubbles as its levels say now: where a setting moved them,
  // set and the rows made again.
  void follow_looks() {
    const auto in = this->chosen();
    if (!in)
      return;
    if (auto now = s_->kept->reads().bubbles_of(*in); now != s_->looks.bubbles) {
      s_->looks.bubbles = std::move(now);
      this->looks_changed();
    }
  }
  // The looks moved -- a theme, a chat's bubbles chosen: the rows made
  // again.
  void looks_changed() {
    ++looks_;
    this->tell();
  }

  // What the list is told: which chat, how it shows it, and where to aim.
  void tell() {
    const auto in = this->chosen();
    const mux::conversation* chat = in ? s_->model->find(*in) : nullptr;
    mux::ui::chat_told now;
    if (chat) {
      const mux::chat_settings reads = s_->kept->reads();
      now.of = *in;
      now.aim = aim_;
      now.filter = this->filter_of(*chat);
      now.receipts = reads.receipts_shown(*in);
      now.previews = reads.previews_shown(*in) && !chat->encrypted;
      // The program's model, as it is now: rooms found, previews fetched.
      needs_.now = s_->model;
      now.needs = &needs_;
      now.unread_from = unread_from_;
      now.frost = s_->looks.window.frost;
      now.looks = looks_;
      now.fetched = this->fetched();
    }
    if (*s_->showing->look<mux::ui::chat_told>() != now)
      mux::ui::show(*s_->showing, std::move(now));
    this->show_ways();
  }

 private:
  // How long a jump waits on a window around its message before paging
  // back to it; and on the older asked, before asking again.
  static constexpr double kContextPatienceMs = 4000.0;
  static constexpr double kHistoryPatienceMs = 10000.0;
  static constexpr double kLoaderAfterMs = 300.0;
  // How long a message jumped to stays flashed: its rows made meanwhile --
  // a far jump's window made again as what is around it comes -- flashed too.
  static constexpr double kFlashMs = 1500.0;
  double flash_until_ = 0.0;
  [[nodiscard]] bool waited() const { return going_ && now_ms() - going_->began >= kLoaderAfterMs; }
  struct going_t {
    mux::conversation_id in;
    std::string to;
    std::optional<std::string> fragment;
    bool quiet = false;
    // When it last got anywhere, and how much of the chat was held then:
    // it waits, and is given up, by that.
    double since = 0.0;
    // When it was asked: the loader shown only once it has waited a while.
    double began = 0.0;
    std::size_t held = 0;
    bool context_asked = false, paging = false;
    std::optional<std::size_t> base;
  };
  // What the program has fetched for rows, counted: more come, rows read.
  [[nodiscard]] std::size_t fetched() const { return s_->model->previews.size() + s_->model->rooms_found.size(); }
  static double now_ms() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
  }
  [[nodiscard]] std::optional<mux::conversation_id> chosen() const { return s_->showing->look<mux::ui::chat_shown>()->chosen; }
  // Each chat's room events as it shows them -- in an encrypted room, who
  // joins and who is invited always: each is given the room's key.
  [[nodiscard]] mux::room_event_filter filter_of(const mux::conversation& chat) const { return chat_filter_of(*s_, chat); }
  // The older, asked: once for the page from where it is held, again
  // after a while where nothing came.
  template <class Asked>
  bool ask_older(const mux::conversation& chat, const Asked& asked) {
    // No token, and nothing held to page back from: nothing to ask. No token
    // but messages held: asked all the same -- the history part pages back
    // from before the oldest of them, unless it knows the room begins there.
    if (!chat.history_from && chat.timeline.empty())
      return false;
    const auto page = std::pair{chat.history_from.value_or(std::string()), chat.timeline.empty() ? std::string() : chat.timeline.front().id};
    const double now = now_ms();
    if (older_asked_ == page && now - older_asked_ms_ < kHistoryPatienceMs)
      return false;
    older_asked_ = page;
    older_asked_ms_ = now;
    asked(request::load_older{chat.id, chat.history_from});
    return true;
  }
  template <class Asked>
  void spanned(const std::string& first, const std::string& last, bool done, bool in_thread, const Asked& asked) {
    const auto& shown = *s_->showing->look<mux::ui::chat_shown>();
    const mux::conversation* chat = shown.chosen ? s_->model->find(*shown.chosen) : nullptr;
    if (!chat)
      return;
    // The messages of the list it is in: the thread's root and answers, or
    // the chat's timeline.
    std::vector<const mux::message*> all;
    if (in_thread && shown.thread) {
      if (const mux::message* root = mux::ui::held_message(*chat, *shown.thread))
        all.push_back(root);
      if (const auto answers = chat->threads.find(*shown.thread); answers != chat->threads.end())
        all.append_range(answers->second | std::views::transform([](const mux::message& one) { return &one; }));
    } else {
      all = chat->timeline | std::views::transform([](const mux::message& one) { return &one; }) | std::ranges::to<std::vector>();
    }
    const auto place = [&](const std::string& id) { return std::ranges::find(all, id, &mux::message::id); };
    const auto from = place(first), to = place(last);
    if (from == all.end() || to == all.end())
      return;
    const auto [begin, end] = std::minmax(from, to);
    auto ids = std::ranges::subrange(begin, std::next(end)) | std::views::filter([](const mux::message* one) { return !one->redacted && !one->id.empty(); }) | std::views::transform([](const mux::message* one) { return one->id; }) | std::ranges::to<std::vector>();
    const auto& marks = *s_->showing->look<mux::ui::message_marks>();
    const bool selecting = !marks.selected.contains(first);
    if (done) {
      mux::ui::change_shown<mux::ui::message_marks>(*s_->showing, [](mux::ui::message_marks& now) { now.previewed.reset(); });
      asked(request::selection_span{std::move(ids), selecting});
      return;
    }
    // As the drag goes: at most a hundred, as a selection is.
    std::set<std::string> preview = marks.selected;
    for (const std::string& id : ids)
      if (!selecting)
        preview.erase(id);
      else if (preview.size() < 100)
        preview.insert(id);
    mux::ui::change_shown<mux::ui::message_marks>(*s_->showing, [&](mux::ui::message_marks& now) { now.previewed = std::move(preview); });
  }
  // What the corner over the list shows: a way back in this chat, one to
  // the chat a jump left, and the loader while a jump is on its way.
  void show_ways() {
    const auto in = this->chosen();
    const auto stack = in ? returns_.find(*in) : returns_.end();
    const mux::conversation* chat = in ? s_->model->find(*in) : nullptr;
    mux::ui::chat_ways now{.back_here = stack != returns_.end() && !stack->second.empty(),
                           .back_to_chat = !chat_returns_.empty(),
                           .loading = this->waited(),
                           .detached = chat && chat->detached,
                           .mentions = chat ? chat->unread_mentions.size() : 0,
                           .reactions = chat ? chat->unread_reactions.size() : 0};
    if (*s_->showing->look<mux::ui::chat_ways>() != now)
      mux::ui::show(*s_->showing, now);
    s_->list = {.jump_target = this->going_to(), .top = seen_.top, .bottom = seen_.bottom};
  }

  services* s_;
  mux::ui::bubble_needs needs_;
  std::optional<going_t> going_;
  skiff::compose::WindowAim<std::string> aim_;
  std::uint64_t number_ = 0, looks_ = 0;
  std::optional<std::string> unread_from_;
  chat_seen seen_;
  std::map<mux::conversation_id, std::vector<std::string>> returns_;
  std::vector<mux::conversation_id> chat_returns_;
  std::optional<std::pair<std::string, std::string>> older_asked_;
  double older_asked_ms_ = 0.0;
  std::optional<std::string> newer_asked_;
  skiff::compose::WindowAim<std::string> thread_aim_;
  std::optional<std::string> thread_of_;
};

}  // namespace mux::app
