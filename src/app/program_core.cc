// SPDX-License-Identifier: AGPL-3.0-only
// The program: waking on changes, keeping what changed, frames, drafts, the screens shown.
module mux.app.program;

import mux.vault;
import std;
import splice;
import knot;
import skia;
import mux.core;
import mux.platform.audio;
import mux.platform.notifications;
import mux.config;
import mux.net;
import mux.media;
import mux.ui;
import skiff.paint;
import skiff.scene;
import skiff.nodes.text;
import mux.app.network;
import mux.app.kept;
import skiff.bind;
import mux.app.demo;
import mux.app.store;
import mux.app.requests;
import mux.app.words;
import mux.logic.links;

namespace mux::app {

auto app::window() -> skiff::scene::Scene<window_type>& { return scene; }

void app::woken() {
  // What UnifiedPush's connector said, first: a push is a sync now.
  notices.take_push();
  // What a notification asked: pressed, its chat opened -- it only brought
  // mux up; its actions, the chat read, or answered and read, and taken away.
  while (const auto asked = notices.take_asked())
    spl::visit(spl::overloaded{[&](const notices_part::opened_chat& one) { this->open_chat(one.chat, std::nullopt); },
                               [&](const notices_part::read_chat& one) {
                                 reading.mark_read(one.chat);
                                 notices_part::dismiss(one.chat);
                               },
                               [&](const notices_part::replied& one) {
                                 if (!one.text.empty())
                                   net->send(one.chat, one.text);
                                 reading.mark_read(one.chat);
                                 notices_part::dismiss(one.chat);
                               }},
               *asked);
  // What the workers made, put where it goes: on this, the UI's thread.
  work.finish();
  auto taken = box->take();
  surge_.came += taken.size();
  // A wake bringing many at once said, with how many wait already.
  if (taken.size() >= 200)
    mux::log_line("app", std::format("arriving: {} changes at once, {} waiting before them", taken.size(), arriving_.size()));
  std::ranges::move(taken, std::back_inserter(arriving_));
  surge_.deepest = std::max(surge_.deepest, arriving_.size());
  // Off screen, no frame takes it a slice at a time: all of it now, the
  // model kept up and notifications given.
  if (!on_screen)
    this->take_arriving(std::chrono::hours(24));  // all of it: not max(), which overflows as nanoseconds
}

// What arrived taken, oldest first, until `most` has gone by -- one change
// at least -- and what was taken then seen to as one batch.
void app::take_arriving(std::chrono::milliseconds most) {
  if (arriving_.empty())
    return;
  const auto began = std::chrono::steady_clock::now();
  const auto due = [&] { return std::chrono::steady_clock::now() - began >= most; };
  std::vector<mux::change_t> changes;
  // Deletions of what is not held in memory, by chat: looked for on disk on
  // a worker, each chat's file read once for all of its (mark_deleted_on_disk).
  std::map<mux::conversation_id, std::vector<std::string>> deleted_on_disk;
  bool person_presence_changed = false;
  // One batch of edits for all that came: what binds to the chats told
  // once, after.
  model->chats().beginBatch();
  // One change slow to apply said, by its type's name, as the compiler
  // spells it in a function's name (no table of names kept by hand).
  const auto said_slow = [](const mux::change_t& one, double ms) {
    if (ms >= 30.0)
      spl::visit([&]<class Change>(const Change&) {
                   mux::log_line("app", std::format("arriving: one change took {:.0f} ms ({})", ms,
                                                    std::source_location::current().function_name()));
                 }, one);
  };
  std::optional<std::chrono::steady_clock::time_point> one_began;
  while (!arriving_.empty() && (changes.empty() || !due())) {
    if (one_began)
      said_slow(changes.back(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - *one_began).count());
    one_began = std::chrono::steady_clock::now();
    changes.push_back(std::move(arriving_.front()));
    arriving_.pop_front();
    const mux::change_t& one = changes.back();
    // What the plugins' parts take of it: a call's signalling.
    this->plugins_take(one);
    // What the program itself does with a change, besides the model: a
    // session kept, a picture shown.
    spl::visit(spl::overloaded{[&](const mux::change::avatar_loaded& picture) {
                                 pictures.take(picture, true);
                                 mux::ui::download_progress().erase(picture.source);
                               },
                               // A room the user made: shown, once the model has it.
                               [&](const mux::change::room_created& made) { made_room_ = made.id; },
                               // A directory searched: its rooms, in Explore.
                               [&](const mux::change::directory_listed& listed) {
                                 // The own server's, for what the chat list searched.
                                 if (listed.server.empty() && !listed.space)
                                   chat_list.found_rooms(listed.query, listed.rooms);
                               },
                               // Something the server refused: a notice saying why.
                               [&](const mux::change::refused& said) { shared.notice("Not done", said.what); },
                               [&](const mux::change::notice& said) { shared.notice(said.heading, said.what); },
                               // People found: in Start chat, while it asks for them.
                               // A person's profile: their picture asked for, where they have one.
                               [&](const mux::change::profile_found& found) {
                                 pictures.avatar_changed(found.user, found.avatar);
                                 pictures.profile_avatars.insert_or_assign(found.user, found.avatar);
                               },
                               [&](const mux::change::members_changed& changed) {
                                 for (const auto& member : changed.members)
                                   pictures.avatar_changed(member.id, member.avatar);
                               },
                               // An invite come: said once a run.
                               [&](const mux::change::conversation_updated& updated) {
                                 pictures.avatar_changed(updated.id.id, updated.avatar);
                                 if (updated.invite)
                                   notices.invite_came(updated.id, *updated.invite, updated.name);
                               },
                               [&](const mux::change::devices_listed& listed) {
                                 if (person_open_ && person_open_->first.account == listed.by && person_open_->second == listed.user) {
                                   const auto [chat, user] = *person_open_;
                                   mux::ui::show(showing, std::optional(mux::ui::person_shown{chat.account, user,
                                                      mux::ui::person_of(shared.ui, model->find(chat), *model, chat.account, user)}));
                                 }
                               },
                               [&](const mux::change::trust_changed& told) {
                                 if (person_open_ && person_open_->first.account == told.by && person_open_->second == told.user) {
                                   const auto [chat, user] = *person_open_;
                                   mux::ui::show(showing, std::optional(mux::ui::person_shown{chat.account, user,
                                                      mux::ui::person_of(shared.ui, model->find(chat), *model, chat.account, user)}));
                                 }
                               },
                               [&](const mux::change::people_found& found) {
                                 mux::ui::change_shown<std::optional<mux::ui::new_chat_facts>>(showing, [&](auto& now) {
                                   if (now && now->by == std::optional(found.by) && now->query == found.query) {
                                     now->found = found.people;
                                     now->rows = mux::ui::people_rows(*now);
                                     for (const auto& person : found.people) {
                                       if (person.avatar && !person.avatar->empty())
                                         mux::ui::listed_avatars().emplace_back(person.id, *person.avatar);
                                     }
                                   }
                                 });
                                 chat_list.found_people(found.query, found.people);
                               },
                               // A room looked up: its card filled, while it
                               // is up for that room still.
                               [&](const mux::change::room_previewed& shown) {
                                 // Asked for a pill: the room there or not, and
                                 // its picture, under the address it was named by.
                                 if (pictures.pill_rooms.contains(shown.asked) &&
                                     !room_card.looking_at(shown.asked)) {
                                   if (!shown.preview.id.empty()) {
                                     model->rooms_found.insert_or_assign(shown.asked, shown.preview.name);
                                     if (shown.preview.avatar)
                                       net->fetch_avatar(shown.by, *shown.preview.avatar, shown.asked);
                                   }
                                   return;
                                 }
                                 room_card.previewed(shown);
                               },
                               [&](const mux::change::media_progress& how) {
                                 mux::ui::download_progress().insert_or_assign(how.source, how.done);
                                 scene.state().markDamaged();
                               },
                                [&](const mux::change::notifications_changed& now) { notices.receive(now); },
                               // What an account's protocol is now: what is offered of it.
                               [&](const mux::change::protocol_state_changed& now) {
                                 shared.ui.protocol_states.insert_or_assign(now.account, now.now);
                                 this->refresh();
                               },
                               [&](const mux::change::presence_changed& now) {
                                 if (!person_open_ || person_open_->first.account != now.account || person_open_->second != now.contact) return;
                                 const auto* account = model->accounts().find(now.account);
                                 if (!account) { person_presence_changed = true; return; }
                                 const auto previous = account->presences.find(now.contact);
                                 person_presence_changed = person_presence_changed || previous == account->presences.end() || previous->second != now.now;
                               },
                               // A protocol's own: what the program does with it, as the
                               // protocol says (mux.app.proto); nothing by default.
                               [&](const auto& theirs) {
                                 using mux::app::defaults::program_told;
                                 program_told(*this, theirs);
                               }},
               one);
    // A message deleted: marked where it is kept, and kept whole apart --
    // as it was, before the model takes it out of view.
    // One that comes again already deleted -- a page back, a window, the
    // sync kept at every start -- is the server's copy, emptied: the same,
    // for what is kept of it, unless it is marked so already.
    const auto deleted = [&](const mux::conversation_id& in, const std::string& id, bool again) {
      const mux::conversation* chat = model->find(in);
      const auto found = chat ? std::ranges::find(chat->timeline, id, &mux::message::id)
                              : std::vector<mux::message>::const_iterator{};
      if (chat && found != chat->timeline.end()) {
        if (!(again && found->redacted))
          store.mark_deleted(in, id, *found);
      } else {
        deleted_on_disk[in].push_back(id);
      }
    };
    if (!ask.demo)
      spl::visit(spl::overloaded{[&](const mux::change::message_redacted& c) { deleted(c.in, c.id, false); },
                                 [&](const mux::change::message_added& c) {
                                   if (c.message.redacted && !c.message.id.empty())
                                     deleted(c.message.in, c.message.id, true);
                                 },
                                 [](const auto&) {}},
                 one);
    paging.before(one);
    model->apply(one);
    paging.keep(one);
  }
  if (one_began)
    said_slow(changes.back(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - *one_began).count());
  const auto batch_began = std::chrono::steady_clock::now();
  model->chats().endBatch();
  if (const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - batch_began).count(); ms >= 30.0)
    mux::log_line("app", std::format("arriving: the model's end of batch took {:.0f} ms for {} changes", ms, changes.size()));
  // Refresh profile cards after their facts have reached the model.
  const auto refresh_person = [&](const account_id& by, const std::string& user) {
    if (person_open_ && person_open_->first.account == by && person_open_->second == user) {
      const auto [chat, who] = *person_open_;
      mux::ui::show(showing, std::optional(mux::ui::person_shown{chat.account, who,
          mux::ui::person_of(shared.ui, model->find(chat), *model, chat.account, who)}));
    }
  };
  for (const auto& changed : changes) {
    spl::visit(spl::overloaded{
        [&](const mux::change::profile_found& profile) {
          // Fetch only after the model contains the new banner URL.
          shared.ui.pictures_due = true;
          refresh_person(profile.by, profile.user);
        },
        [&](const mux::change::presence_changed& presence) {
          // An unchanged presence snapshot must not reset the card's scroll.
          if (person_presence_changed) refresh_person(presence.account, presence.contact);
        },
        [](const auto&) {}}, changed);
  }

  for (auto& [in, ids] : deleted_on_disk)
    work.run([this, in, ids = std::move(ids)]() -> workers::done_t {
      store.mark_deleted_on_disk(in, ids);
      return {};
    });
  // What came as it happened, notified: mentions known by the marks the same
  // changes made.
  if (!ask.demo) {
    std::set<std::string> mentioning;
    for (const mux::change_t& one : changes)
      spl::visit(spl::overloaded{[&](const mux::change::mentioned& m) { mentioning.insert(m.event); },
                                 [](const auto&) {}},
                 one);
    for (const mux::change_t& one : changes)
      spl::visit(spl::overloaded{[&](const mux::change::message_added& added) {
                                   const bool live = spl::visit(
                                       spl::overloaded{[](mux::placement::at_end) { return true; },
                                                       [](const auto&) { return false; }},
                                       added.where);
                                   if (live)
                                     notices.message_came(added.message, mentioning.contains(added.message.id),
                                                          on_screen && shared.chosen() == added.message.in);
                                 },
                                 [](const auto&) {}},
                 one);
  }
  // Messages held to a number in all, least recently read out first.
  model->trim(static_cast<std::size_t>(this->limits().messages_in_memory), shared.chosen());
  const auto refresh_began = std::chrono::steady_clock::now();
  this->refresh();
  if (const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - refresh_began).count(); ms >= 30.0)
    mux::log_line("app", std::format("arriving: the refresh after {} changes took {:.0f} ms", changes.size(), ms));
  // A room the user made: opened once the model has it.
  if (made_room_ && model->find(*made_room_))
    this->open_chat(*std::exchange(made_room_, std::nullopt), std::nullopt);
  // A room joined from a link: opened once it is here.
  if (shared.joining)
    if (const auto found = mux::logic::chat_of(*model, *shared.joining)) {
      const auto link = *std::exchange(shared.joining, std::nullopt);
      // A message in it, where the link is to one.
      const auto event = spl::visit(
          spl::overloaded{[](const mux::logic::mention::place& one) { return one.event; },
                             [](const auto&) { return std::optional<std::string>(); }},
          mux::logic::mention_in(link));
      if (event)
        this->go_to_linked(*found, *event);
      else
        this->open_chat(*found, std::nullopt);
    }
  // A message jumped to that the server says is not there: the jump
  // stopped, and said why -- it paged the whole history back for it.
  for (const mux::change_t& one : changes)
    spl::visit(spl::overloaded{[&](const mux::change::event_missing& gone) {
                                       if (jumps.going_to() == gone.id) {
                                         // An edit: to the message it edits, which is where it shows.
                                         if (const auto& chosen = shared.chosen(); gone.instead && chosen) {
                                           jumps.go(*chosen, *gone.instead, std::nullopt, false, taker{this});
                                         } else {
                                           jumps.stop();
                                           shared.not_implemented("That message isn't there any more, or can't be seen from this account.");
                                         }
                                       }
                                     },
                                     [](const auto&) {}},
                  one);
  // A link's message fetched: in its thread, where it is in one.
  if (linked_)
    if (const mux::conversation* chat = model->find(linked_->first); chat && mux::ui::held_message(*chat, linked_->second)) {
      const auto [in, event] = *std::exchange(linked_, std::nullopt);
      if (shared.chosen() == in)
        (void)this->open_in_thread(*chat, event);
    }
  // An answer gone to, scrolled to once its thread's panel has it.
  if (thread_target_) {
    auto& screen = root();
    if (!shared.thread_open() || jumps.aim_in_thread(*thread_target_))
      thread_target_.reset();
  }
  // The slice, and the surge it is part of: said whole once the queue is
  // empty again, where it took more than one slice.
  ++surge_.slices;
  surge_.ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
  if (arriving_.empty()) {
    if (surge_.slices > 1)
      mux::log_line("app", std::format("arriving: {} changes taken in {} slices, {:.0f} ms in all; {} waited at most", surge_.came,
                                       surge_.slices, surge_.ms, surge_.deepest));
    surge_ = {};
  }
}









void app::wire() {
  // What the parts reach, set in place: services holds the program's own
  // state too (the looks, the paint, what the window's parts share), which
  // the window was made pointing at -- made anew, it was all lost.
  shared.model = model;
  shared.showing = &showing;
  shared.showing_binding = &showing_binding;
  shared.net = net;
  shared.store = &store;
  shared.box = box;
  shared.ask = &ask;
  shared.scene = &scene;
  shared.kept = this;
  shared.vault = vault;
  shared.work = &work;
  work.wake_with(wake.kind);
  shared.system_dialogs = &system_dialogs;
  shared.wake = &wake;
  shared.speaker = &speaker;
  store.vault = vault;
}

// The window, bound to the model whole: one binding, its change log read
// once a frame. Where something made a subtree in place -- a dialog opened,
// a panel slid in, a settings page shown (skiff's work::subtreeMade) -- it
// is walked whole once, for what was made to read the model; else only
// what changed.
void app::take_page_input() {
  if (std::exchange(skiff::scene::work::subtreeMade(), false)) {
    window_binding.invalidate();
    window_binding.refresh(root(), this->state);
    showing_binding.invalidate();
    this->refresh_shown();
    chats_binding.invalidate();
    chats_binding.refresh(root(), model->chats());
  }
  this->settle_model();
}
// How the window looks, as the model holds it, put where the window's
// parts read it.
void app::show_looks() {
  const auto& now = this->appearance();
  shared.looks.developer_tools = now.developer_tools;
  auto& window = shared.looks.window;
  window.home_hides = now.home_hides_spaced;
  window.home_direct = now.home_hides_direct;
  window.spaces = now.spaces;
  window.top_bar = now.top_bar;
  window.interface_scale = now.interface_scale;
  window.live_blur = now.live_blur;
  window.behind = now.wallpaper_behind;
  window.frost = now.frost_blur;
  window.chosen = now.window_opacity;
  shared.looks.bubbles_everywhere = now.bubbles.value_or(mux::config::bubble_look{});
  shared.looks.panels_everywhere = now.panels.value_or(mux::config::bubble_look{});
  shared.refresh_due = true;
}
// What the model's reactions asked for: the file written, UnifiedPush's
// connector started or stopped; and the page up shown the model again.
void app::settle_model() {
  notices.sync_choices();
  const auto due = this->take_due();
  if (due.write)
    (void)this->write();
  if (due.looks)
    this->show_looks();
  if (due.motion)
    skiff::paint::motionLevel() = motion_of(this->appearance().motion);
  if (due.deleted) {
    model->show_deleted = this->history().show_deleted;
    shared.refresh_due = true;
  }
  if (due.restyle) {
    skiff::scene::forgetStyles();
    shared.rebuild_due = true;
  }
  if (due.opacity) {
    auto& look = shared.looks.window;
    look.chosen = this->appearance().window_opacity;
    if (look.see_through) {
      look.opacity = look.chosen;
      skiff::scene::forgetStyles();
      shared.rebuild_due = true;
    }
  }
  if (due.limits) {
    settings.apply_limits();
    model->trim(static_cast<std::size_t>(this->limits().messages_in_memory), shared.chosen());
  }
  if (due.push) {
    if (*due.push)
      notices.start_push();
    else
      notices.stop_push();
  }
  window_binding.refresh(root(), this->state);
  this->refresh_shown();
  chats_binding.refresh(root(), model->chats());
}

// The chat screen read again by the chats binding: against everything the
// model holds beside the chats, whether or not they moved.
void app::show_chats_now() {
  // The chat chosen, as what is shown has it, read first.
  this->refresh_shown();
  press_target_now() = {&this->state, this};
  // What is kept read first, where it moved since: the screen reads each
  // chat's settings from it as it is shown.
  window_binding.refresh(root(), this->state);
  chats_binding.invalidate();
  chats_binding.refresh(root(), model->chats());
}

// Whether the accounts page is up, and whether it or the settings are, over
// the chats: as what is shown says, not the window's tree.
bool app::accounts_up() { return showing.look<std::optional<mux::ui::panel_facts>>()->has_value(); }
bool app::page_over_chats() {
  return this->accounts_up() || showing.look<std::optional<mux::ui::settings_facts>>()->has_value();
}

void app::refresh_shown() {
  // The drawer's accounts: the saved ones, with what the chats say of each,
  // the one listed marked.
  {
    const auto listed = shared.listed();
    mux::ui::drawer_accounts now{state.root().accounts.values() | std::views::transform([&](const mux::config::account_t& one) {
      const auto [how, failed] = mux::ui::connection_state_of(one, *model);
      return mux::ui::drawer_account{mux::config::address_of(one), std::string(mux::config::protocol_name(one)), how, failed,
                                     listed && listed->address == mux::config::address_of(one)};
    }) | std::ranges::to<std::vector>()};
    if (*showing.look<mux::ui::drawer_accounts>() != now)
      mux::ui::show(showing, std::move(now));
  }
  // The chosen account's page: the account as saved, and what the model
  // says of its profile and presence.
  if (const auto& open = *showing.look<std::optional<mux::ui::account_page_facts>>()) {
    auto now = *open;
    if (const auto* saved = this->settings_of(now.by.address))
      now.account = *saved;
    now.state = mux::ui::protocol_state_of(shared.ui, now.by);
    now.proxies = this->proxies();
    mux::ui::read_account_model(now, *model);
    mux::ui::show(showing, std::optional(std::move(now)));
  }
  // The looks as their choices show them: what the window last read of
  // what is kept, for the chat chosen.
  looks.read();
  const auto shown = showing.look<mux::ui::chat_shown>();
  if (shown->forum && shared.listed() && !chat_list.is_forum({*shared.listed(), *shown->forum}))
    mux::ui::change_shown<mux::ui::chat_shown>(showing, [](auto& now) { now.forum.reset(); });
  shared.read_shown_now();
}

// Right after an event is handled, what it asked for -- taken from the
// window and done at once, the handler that asked having returned: a press
// acts before the next event, not a frame later.
void app::after_event() {
  // The nodes pressed that answer with what they ask for: each delivered
  // along the path the scene routed it on, its event sent at once up the
  // scopes it is in; what nothing in the window takes, to the program.
  // Each in the window's state, else in the chats' model -- the one its
  // node is bound in.
  const auto ways = std::exchange(skiff::scene::hostWork().pressed, {});
  const bool pressed = std::ranges::count_if(ways, [this](const auto& way) {
                         return skiff::bind::press(root(), this->state, way, this) ||
                                skiff::bind::press(root(), model->chats(), way, this) ||
                                skiff::bind::press(root(), showing, way, this);
                       }) > 0;
  // What handlers returned that they ask for, where the routing carried
  // nothing down to send it with (a debug build's erased walks): sent up the
  // scopes their nodes are in, along their paths.
  const auto kept = std::exchange(skiff::scene::hostWork().answers, {});
  const bool answered = std::ranges::count_if(kept, [this](const auto& one) {
                          return skiff::bind::answer(root(), this->state, one, this) ||
                                 skiff::bind::answer(root(), model->chats(), one, this) ||
                                 skiff::bind::answer(root(), showing, one, this);
                        }) > 0;
  // Static routing may already have delivered the request to take(),
  // leaving both queues empty. Its edits still need to reach the window.
  const bool routed = std::exchange(event_requests_taken, false);
  if (!pressed && !answered && !routed)
    return;
  this->take_page_input();
  // What the requests opened or closed, shown.
  this->refresh_shown();
}

// Something arrived and not taken yet: the next frame at once.
double app::wake_in() const {
  return arriving_.empty() ? std::min(model->typing_wake_in(), jumps.loader_wake_in()) : 0.0;
}

void app::before_frame() {
  // What Enter does in a message, as the settings say it now.
  mux::ui::enter_choice() = this->appearance().enter;
  // A slice of what the network brought, before the frame shows it.
  this->take_arriving(kArrivingSlice);
  // What the window's nodes answered as they ticked (a page come to rest
  // scrolled, a timer): delivered as an event's are.
  if (!skiff::scene::hostWork().pressed.empty())
    this->after_event();
  // What the parts asked of the chat's list, and a jump on its way gone on
  // with: aimed at once held, fetched or paged back to while it is not.
  if (auto asked = std::exchange(shared.show_message_due, std::nullopt); asked && shared.chosen())
    jumps.go(*shared.chosen(), std::move(asked->id), std::move(asked->fragment), false, taker{this});
  if (std::exchange(shared.newest_due, false))
    jumps.to_newest();
  if (std::exchange(shared.returns_due, false))
    jumps.clear_returns();
  jumps.go_on(taker{this});
  // The way down's count, as tdesktop's: the chat's unread, as its row in
  // the chats says it -- not what came while the list was away from its
  // end, which a chat opened with its unread above never counted.
  {
    const mux::conversation* chat = shared.chosen() ? model->find(*shared.chosen()) : nullptr;
    const auto unread = chat ? static_cast<std::size_t>(std::max<std::int64_t>(0, chat->unread_here(chat_filter_of(shared, *chat)))) : std::size_t{0};
    if (showing.look<mux::ui::way_down_shown>()->unseen != unread)
      mux::ui::change_shown<mux::ui::way_down_shown>(showing, [&](mux::ui::way_down_shown& now) { now.unseen = unread; });
  }
  chat_view.show();
  chat_list.show();
  manage.show();
  suggestions.show();
  pictures.viewer_tick();
  // Whether Esc lets go of something up: a dialog, a layer, the drawer.
  if (const mux::ui::overlay_shown up{mux::ui::escape_in(showing, mux::ui::escape_order{}).has_value() || showing.look<mux::ui::drawer_shown>()->out};
      *showing.look<mux::ui::overlay_shown>() != up)
    mux::ui::show(showing, up);
  // And the looks, as the chats binding last read them for the chat chosen.
  looks.read();
  // The window's shape, as its size is: and the panel docked under the
  // field on a phone's, the field standing over it.
  // Held to the layout chosen, where one is. As tall as without the
  // on-screen keyboard: up, it took half a portrait phone's height, and the
  // window then shaped as a landscape one -- the list and a chat side by
  // side, squeezed over the keys (a tester's).
  if (const auto box = scene.viewport();
      *showing.look<mux::ui::window_shape>() !=
      mux::ui::window_shape{box.width(), box.height() + skiff::scene::hostWork().keyboardTaken, this->appearance().layout})
    mux::ui::show(showing, mux::ui::window_shape{box.width(), box.height() + skiff::scene::hostWork().keyboardTaken, this->appearance().layout});
  shared.ui.set_docked_panel_height(shared.emoji_open() ? mux::ui::docked_height(*showing.look<mux::ui::window_shape>()) : 0.0f);
  if (showing.look<mux::ui::screen_shown>()->docked != shared.ui.docked_panel_height)
    mux::ui::change_shown<mux::ui::screen_shown>(showing, [&](mux::ui::screen_shown& now) { now.docked = shared.ui.docked_panel_height; });
  if (model->expire_typing())
    chats_binding.refresh(root(), model->chats());
  store.flush_reads();
  ++mux::ui::image_cache::frame();
  // What the model's widgets did: edits of the model, before the frame.
  this->take_page_input();
  // What the screen came to want as it was updated: done.
  // What has been on screen in the chat shown is read, as far as it goes,
  // as in tdesktop: the chat list's counts go down as it is read, not all
  // at once when it is opened or left.
  if (const auto& chosen = shared.chosen(); chosen && !this->page_over_chats())
    if (const auto seen = jumps.last_seen(); seen && reading.mark_seen(*chosen, *seen))
      this->refresh();
  // The mentions and reactions for the user whose messages are on screen,
  // seen -- only those, as tdesktop's.
  if (const auto& chosen = shared.chosen())
    if (const mux::conversation* chat = model->find(*chosen);
        chat && (!chat->unread_mentions.empty() || !chat->unread_reactions.empty()) && !this->accounts_up()) {
      auto shown = jumps.shown_now();
      const auto marked = [&](const mux::unread_mark& mark) { return std::ranges::contains(shown, mark.target); };
      if (std::ranges::any_of(chat->unread_mentions, marked) || std::ranges::any_of(chat->unread_reactions, marked)) {
        model->apply(mux::change_t{mux::change::marks_shown{*chosen, std::move(shown)}});
        this->refresh();
      }
    }
  // The input has the keyboard's focus whenever nothing else does -- at the
  // start, after a panel or a search closes -- so typing always goes to it,
  // as in tdesktop.
  // Not under a finger: there the keys' focus is the text input started,
  // and a phone's keyboard up over the chat until it is put away.
  // Asked once each time the keys are let go, the field taking them as it
  // reads that.
  if (scene.focusedId() != 0)
    keys_asked_free = false;
  else if (!keys_asked_free && !shared.by_touch && shared.chosen() && !this->page_over_chats()) {
    keys_asked_free = true;
    shared.ask_keys(mux::ui::keys::chat{});
  }
  // The emoji panel's pictures in view, asked for as they change -- as it
  // opens, as it scrolls, as its tab or its search changes -- and not only
  // when something else refreshed the window: those not asked yet, not the
  // whole sweep of every picture the window shows (pictures_due), which
  // ran at nearly every frame of a scroll through custom emoji.
  if (auto shown = mux::ui::emoji_pictures_wanted(*showing.look<mux::ui::suggestions_shown>(), *showing.look<mux::ui::chat_draft>(),
                                                *showing.look<mux::ui::thread_draft>()); shown != shared.ui.panel_pictures_shown) {
    pictures.want_shown(shown);
    shared.ui.panel_pictures_shown = std::move(shown);
  }
  const auto style = reads().style_of(shared.listed(), shared.chosen());
  // Text fields save as they are edited; apply when the editor closes or
  // its Apply button is pressed, so rebuilding cannot interrupt typing.
  if (!this->page_over_chats() && !showing.look<std::optional<mux::ui::room_settings_facts>>()->has_value() &&
      (!active_style || *active_style != style)) {
    active_style = style;
    skiff::scene::forgetStyles();
    shared.rebuild_due = true;
  }
  this->plugins_tick();
  keep_selection(menu);
  // A selectable text or a field pressed with the right button -- a long
  // press, on a phone: its menu, the last asked for.
  if (auto asked = std::exchange(skiff::scene::textMenusAsked(), {}); !asked.empty() && !showing.look<std::optional<mux::ui::menu_facts>>()->has_value())
    spl::visit(spl::overloaded{[&](skiff::scene::text_menu::of_text& text) {
                                       shared.show_text_menu(mux::ui::text_menu_items(std::move(text.text), std::move(text.link), shared.emoji));
                                     },
                                     [&](const skiff::scene::text_menu::of_field& field) {
                                       shared.show_text_menu(mux::ui::field_menu_items(field, false), field);
                                     }},
                  asked.back());
  // What the parts left to do: a chat opened, a link followed; the window
  // made again, brought up to date; the emoji picked lately kept.
  if (auto chat = std::exchange(shared.chat_due, std::nullopt))
    this->open_chat(*chat, std::nullopt);
  if (auto link = std::exchange(shared.link_due, std::nullopt))
    this->follow(*link);
  if (std::exchange(shared.rebuild_due, false))
    this->rebuild_in_theme();
  if (std::exchange(shared.refresh_due, false))
    this->refresh();
  if (std::exchange(shared.ui.pictures_due, false))
    pictures.ask();
  if (std::exchange(shared.emoji.emoji_changed, false)) {
    this->change_part<recently_used>([&](recently_used& now) { now.emoji = shared.emoji.recent_emoji; now.favourite_emoji = shared.emoji.favourite_emoji; });
  }
  if (std::exchange(shared.emoji.stickers_changed, false)) {
    this->change_part<recently_used>([&](recently_used& now) {
      now.stickers = shared.emoji.recent_stickers;
      now.favourite_stickers = shared.emoji.favourite_stickers;
    });
  }
}

// Off to the background, or ending: what is written in the chat open kept
// as its draft, and the reads not sent yet written -- on a phone, the
// process may be ended in the background without any other word.
void app::backgrounded() {
  if (const auto& chosen = shared.chosen())
    drafts.keep(*chosen, mux::ui::draft_text(shared.chat_writing()));
  store.flush_reads(true);
}

void app::closing() {
  this->backgrounded();
  net->shutdown();
}

auto app::root() -> window_type& { return scene.root(); }

void app::show_conversations() {
  mux::ui::show(showing, mux::ui::drawer_shown{false});
  accounts_screen.forget_login();
  mux::ui::show<mux::ui::panel_facts>(showing, std::nullopt);
  this->refresh_shown();
  this->refresh();
}





void app::refresh(std::source_location from) {
  static const bool traced = std::getenv("MUX_TRACE_FRAMES") != nullptr;
  if (traced)
    std::println(std::cerr, "[frame] refresh from {}:{} ({})", std::filesystem::path(from.file_name()).filename().string(),
                 from.line(), from.function_name());
  // Off screen: done once, as the window comes back.
  if (!on_screen) {
    refresh_waiting_ = true;
    return;
  }
  this->note_spaces();
  // The chat screen, as the chats binding reads it: against everything the
  // model holds beside the chats, and shown again whether or not they moved.
  this->show_chats_now();
  // The newly made range is now known, including a just-opened chat.
  pictures.ask();
  // The accounts page, where it is up: the account being added shown in,
  // and the chats then.
  if (accounts_screen.bring_up_to_date())
    this->show_conversations();
}


// What is kept, applied: once at the start, or once the vault is opened.
void app::begin(const mux::config::file& saved, std::vector<mux::config::account_t> extra, bool demo,
                std::optional<std::string> error) {
  const auto proxies = saved.proxies.value_or(std::vector<mux::config::proxy_settings>{});
  for (const auto& one : mux::config::accounts_of(saved))
    if (mux::config::enabled_of(one) && !demo)
      net->start(one, proxies);
  for (const auto& one : extra)
    net->start(one, proxies);
  net->thread = std::thread([n = net] {
    try {
      n->loop.run_forever();
    } catch (const std::exception& failed) {
      std::println(std::cerr, "[mux] the network stopped: {}", failed.what());
    }
  });

  this->keeps_nothing = demo;
  // The demo's accounts' protocols as their demos say: no client tells it.
  if (demo)
    for (auto& one : mux::app::fake::states())
      shared.ui.protocol_states.insert_or_assign(one.account, std::move(one.now));
  // What each protocol's account does, for the window to offer.
  mux::app::tell_protocol_ops(shared.ui, mux::protocols{});
  // Load the saved settings through the base; app::take dispatches requests.
  this->kept_settings::take(saved);
  // Loading replaces the model, including its revision. A previous read
  // of the defaults must not suppress the saved settings' first read.
  window_binding.invalidate();
  // And what of them the window holds, put in place there.
  shared.emoji.recent_emoji = this->recent().emoji;
  shared.emoji.recent_stickers = this->recent().stickers;
  shared.emoji.favourite_stickers = this->recent().favourite_stickers;
  shared.emoji.favourite_emoji = this->recent().favourite_emoji;
  if (saved.last_account)
    mux::ui::change_shown<mux::ui::chat_shown>(showing, [&](mux::ui::chat_shown& now) {
      now.current = mux::account_id{mux::ui::protocol_of(*saved.last_account), *saved.last_account};
    });
  // Read the loaded appearance just as a later settings change does.
  // Writing the window's defaults here replaced the saved blur and backdrop.
  this->show_looks();
  this->choose_field<&mux::config::look_settings::interface_scale>(std::clamp(this->appearance().interface_scale, mux::ui::kScaleLeast, mux::ui::kScaleMost));
  shared.looks.window.interface_scale = this->appearance().interface_scale;
  if (!demo)
    this->drafts.load();
  this->model->show_deleted = this->history().show_deleted;
  this->settings.apply_limits();
  // UnifiedPush only where chosen; off by default.
  if (this->notifications().unified_push.value_or(false) && !demo)
    notices.start_push();
  skiff::paint::motionLevel() = motion_of(this->appearance().motion);
  this->config_error = std::move(error);
  this->refresh();
}

void app::lock(std::vector<mux::config::account_t> extra, bool demo) { local_data.lock(std::move(extra), demo); }

// A passphrase given, by what it was asked for: local data's, to its part
// -- opened, what was kept begun -- and a protocol's own, to its program
// glue.
void app::apply(const request::give_passphrase& one) {
  spl::visit(spl::overloaded{[&](mux::config::passphrase_for::unlock) {
                                     if (auto opened = local_data.unlock(one))
                                       this->begin(opened->saved, std::move(opened->extra), opened->demo, std::move(opened->error));
                                   },
                                   [&](mux::config::passphrase_for::encrypt) { local_data.encrypt(one); },
                                   [&](mux::config::passphrase_for::change) { local_data.change(one); },
                                   [&](mux::config::passphrase_for::decrypt) { local_data.decrypt(one); },
                                   [&](const auto& theirs) { passphrase_given(*this, theirs, one); }},
                one.why);
}

// From an account's Privacy page: its room keys, to a file or from one.

}  // namespace mux::app

namespace mux::app {
// Esc on the window: the top-most up let go of, as what is shown says; else
// the drawer pushed back.
void app::apply(const request::escape&) {
  if (auto asked = mux::ui::escape_in(showing, mux::ui::escape_order{}))
    std::visit([&](const auto& one) { this->route(one); }, *asked);
  else if (showing.look<mux::ui::drawer_shown>()->out)
    this->route(request::close_drawer{});
}
// Ctrl+C on the window: what any text shows selected, copied.
void app::apply(const request::copy_selected&) {
  if (const std::string selected = skiff::scene::selectedText(); !selected.empty())
    skiff::scene::setClipboardText(selected);
}
void app::apply(const request::pressed_at& one) { shared.last_press = skia::SkPoint::Make(one.x, one.y); }
}  // namespace mux::app
