// SPDX-License-Identifier: AGPL-3.0-only
// The program: links followed to rooms, people and messages.
module mux.app.program;

import std;
import splice;
import knot;
import skia;
import mux.core;
import mux.config;
import mux.net;
import mux.media;
import mux.ui;
import mux.protocols;
import skiff.paint;
import skiff.scene;
import mux.app.network;
import mux.app.demo;
import mux.app.store;
import mux.app.requests;
import mux.app.words;
import mux.logic.links;

namespace mux::app {

void app::open_chat(const mux::conversation_id& which, const std::optional<std::string>& event) {
  if (event) {
    this->go_to_message(which, *event, std::nullopt);
    return;
  }
  auto& screen = root().main();
  screen.current = which.account;
  screen.wanted.reset();
  if (this->last_account != which.account.address) {
    this->last_account = which.account.address;
    (void)this->write();
  }
  this->apply(request::choose{which});
}

// A message gone to -- from a reply's quote, a link, a room's own link in
// its messages -- one way for all of them: a chat not open is opened first,
// the one open is not chosen again; a reaction's own line goes to what it
// reacted to; and the view jumps, to the part a quote marks where it has one.
void app::go_to_message(const mux::conversation_id& in, std::string id, std::optional<std::string> fragment) {
  auto& screen = root().main();
  // Where it goes from, to come back to: this chat, where it goes to
  // another; else the place in this one.
  const bool here = screen.chosen && *screen.chosen == in;
  if (!here) {
    screen.note_chat_return();
    this->open_chat(in, std::nullopt);
  }
  if (const mux::conversation* chat = model->find(in))
    if (const auto aside = chat->quoted.find(id);
        aside != chat->quoted.end() && aside->second.reaction && aside->second.replies_to) {
      id = *aside->second.replies_to;
      fragment.reset();
    }
  // As tdesktop's doneJumpFrom: a way back to where it goes is done with,
  // then where it went from is one.
  if (here) {
    screen.skip_return(id);
    screen.note_return();
  }
  screen.jump_to(std::move(id), std::move(fragment));
}

void app::follow(const mux::logic::link_t& where) {
  auto& screen = root().main();
  spl::visit(spl::overloaded{[&](const mux::logic::link_step::open_chat& step) {
                               if (step.event)
                                 this->go_to_linked(step.chat, *step.event);
                               else
                                 this->open_chat(step.chat, std::nullopt);
                             },
                             [&](const mux::logic::link_step::member_page& step) {
                               this->apply(request::open_member_info{step.user});
                             },
                             [&](const mux::logic::link_step::say& step) { root().show_message(step.title, step.text); },
                             [&](const mux::logic::link_step::join& step) {
                               // Its card first, as a person's: filled when
                               // its server answers, in woken(), and joined
                               // from there.
                               room_card.look_up(step, where);
                             }},
             mux::logic::where_to(*model, where, screen.current));
}

// A link's message gone to. An answer in a thread -- or a thread's root --
// in the thread's panel: the timeline shows no answers, and a jump to one
// there never landed, the message seeming never to load. One not here yet
// is fetched on its own, and gone to again once it is (woken); meanwhile
// the timeline jumps to it, as to any message.
void app::go_to_linked(const mux::conversation_id& in, const std::string& event) {
  const mux::conversation* chat = model->find(in);
  if (chat && mux::ui::held_message(*chat, event)) {
    auto& screen = root().main();
    if (!(screen.chosen && *screen.chosen == in)) {
      screen.note_chat_return();
      this->open_chat(in, std::nullopt);
    }
    if (const mux::conversation* now = model->find(in); now && this->open_in_thread(*now, event))
      return;
  } else if (chat && !shared.demo()) {
    linked_ = {in, event};
    net->fetch_quoted(in, event);
  }
  this->go_to_message(in, event, std::nullopt);
}
bool app::open_in_thread(const mux::conversation& chat, const std::string& id) {
  const mux::message* said = mux::ui::held_message(chat, id);
  if (said == nullptr)
    return false;
  const std::optional<std::string> thread = said->thread                                    ? said->thread
                                            : said->threaded && said->threaded->count > 0 ? std::optional<std::string>(said->id)
                                                                                          : std::nullopt;
  if (!thread)
    return false;
  auto& screen = root().main();
  screen.stop_jump();
  if (screen.thread_open() != thread) {
    screen.open_thread(*thread);
    if (!shared.demo())
      net->load_thread(chat.id, *thread);
  }
  this->refresh();
  // Its answers still on their way: scrolled to once they are (woken).
  if (!screen.parts.threads.scroll_to(id))
    thread_target_ = id;
  return true;
}

void app::apply(const request::resize_sidebar& one) { root().main().resize_sidebar(one.x); }

void app::apply(const request::message_person& one) {
  if (model->find(one.who) != nullptr) {
    this->apply(request::choose{one.who});
    return;
  }
  // No chat with them yet: one started, as Start chat starts it -- their
  // card closed, the chat opened once it is made.
  this->apply(request::close_person_info{});
  rooms.apply(request::start_direct{one.who.id});
}

// A quote pressed: to what it quotes -- or, a reaction's, to the message it
// reacted to, as any quote's.
void app::apply(const request::jump_to_message& one) {
  // An answer in a thread, or the root of the thread open: there, in the
  // thread's panel -- its quote pressed in the thread went nowhere.
  if (const auto& chosen = root().main().chosen)
    if (const mux::conversation* chat = model->find(*chosen))
      if (const mux::message* said = mux::ui::held_message(*chat, one.id)) {
        auto& screen = root().main();
        const std::optional<std::string> thread = said->thread ? said->thread
                                                  : screen.thread_open() == said->id ? std::optional<std::string>(said->id)
                                                                                      : std::nullopt;
        if (thread) {
          if (screen.thread_open() != thread) {
            screen.open_thread(*thread);
            if (!shared.demo())
              net->load_thread(chat->id, *thread);
            this->refresh();
          }
          if (screen.parts.threads.scroll_to(one.id))
            return;
        }
      }
  if (const auto& chosen = root().main().chosen) {
    root().main().return_from = one.from;
    this->go_to_message(*chosen, one.id, one.fragment);
    root().main().return_from.reset();
  }
}

}  // namespace mux::app
