// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.notices: what the program tells the user of, outside the chat in
// view -- a message come, an invite -- as the settings say: by the desktop's
// notifications or mux's own, with the chime; and UnifiedPush, which wakes
// the program for them. A part of the program: it owns its state, and
// reaches the rest through the services it is given.
export module mux.app.notices;

import std;
import splice;
import mux.core;
import mux.config;
import mux.logic.notifications;
import mux.ui;
import mux.platform.audio;
import mux.platform.notifications;
import mux.platform.push;
import mux.app.network;
import mux.app.services;
import mux.app.requests;

export namespace mux::app {

class notices_part {
 public:
  explicit notices_part(services& shared) : s_(&shared) {}
  notices_part(const notices_part&) = delete;
  notices_part& operator=(const notices_part&) = delete;

  // A notification mux shows itself, in a window of its own: for the host.
  struct toast_due {
    mux::conversation_id chat;
    std::string key;
    std::string title;
    std::string text;
  };
  [[nodiscard]] std::vector<toast_due> take_toasts() { return std::exchange(toasts_due_, {}); }

  // Whether the window has the keyboard's focus: a message to the chat
  // being read then notifies nothing.
  void focus_changed(bool on) { window_focused_ = on; }

  // A message come as it happened, notified as the settings say -- where it
  // was said since mux started (a minute's grace for clocks): not a chat's
  // last messages, which the first sync puts at its end too. An account
  // recreated after a proxy change also replays its saved timeline: each
  // message is handled only once per run. `in_view`: its chat is on screen.
  void message_came(const mux::message& said, bool mentions_me, bool in_view) {
    if (!messages_.first(said))
      return;
    if (in_view && window_focused_)
      return;
    const auto decision = s_->kept->notify_for(said.in, mentions_me);
    if (!decision.popup) {
      this->sound_only(decision.sound);
      return;
    }
    const mux::conversation* chat = s_->model->find(said.in);
    std::string title = "mux";
    if (decision.show_name && chat) {
      const std::string who = mux::ui::sender_name(*chat, said.sender);
      title = mux::ui::is_group(*chat) ? std::format("{} ({})", who, mux::ui::display_name(*chat)) : who;
    }
    std::string text = "New message";
    if (decision.show_text) {
      text = said.body.plain.empty() && said.attachment ? std::string("Picture or file") : said.body.plain;
      if (text.size() > 300) {
        // Cut where a character starts: half of one is not UTF-8, and the bus
        // drops a connection that sends it.
        std::size_t cut = 300;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
          --cut;
        text = text.substr(0, cut) + "…";
      }
    }
    this->show(said.in, std::move(title), std::move(text), decision.sound);
  }

  // An invite come: said once a run -- who asked, and to what -- by the
  // backend chosen, with the chime, as the settings say for its account.
  void invite_came(const mux::conversation_id& in, const mux::invite_info& invite, const std::string& name) {
    if (!invites_told_.insert(in).second)
      return;
    const auto decision = s_->kept->notify_for(in, true);
    if (!decision.popup) {
      this->sound_only(decision.sound);
      return;
    }
    const std::string who = invite.from_name.empty() ? invite.from : invite.from_name;
    this->show(in, invite.direct ? std::format("{} invites you to chat", who) : std::format("Invite to {}", name),
               invite.direct ? std::string("A direct chat") : std::format("from {}", who), decision.sound);
  }

  // Notifications in Settings: the page, its switches, what shows them.
  void apply(const request::settings_notifications&) { this->show_page(); }
  void apply(const request::flip_notify& one) {
    bool& flag = mux::config::flag_in(s_->kept->notifications, one.flag);
    flag = !flag;
    (void)s_->kept->write();
    this->show_page();
  }
  void apply(const request::set_notify_backend& one) {
    s_->kept->notifications.backend = mux::config::word_of(one.backend);
    (void)s_->kept->write();
    this->show_page();
  }
  // Woken by UnifiedPush: on, the connector started and the servers given a
  // pusher as it hands an endpoint; off, the registration dropped with the
  // distributor, and the endpoint forgotten.
  void apply(const request::flip_unified_push&) {
    auto& settings = s_->kept->notifications;
    const bool on = !settings.unified_push.value_or(false);
    settings.unified_push = on;
    if (on)
      this->start_push();
    else
      this->stop_push();
    (void)s_->kept->write();
    this->show_page();
  }

  void start_push() {
    auto& settings = s_->kept->notifications;
    if (push_thread_.joinable() || s_->kept->keeps_nothing)
      return;
    if (!settings.push_token) {
      settings.push_token = mux::platform::push::new_token();
      (void)s_->kept->write();
    }
    push_forget_ = std::make_shared<std::atomic<bool>>(false);
    push_thread_ = std::jthread([inbox = push_box_, wake = *s_->wake, forget = push_forget_,
                                 token = *settings.push_token](std::stop_token stop) {
      mux::platform::push::run(stop, std::string(mux::platform::push::kAppId), token, "Messages from your accounts",
                               forget, push_sink{inbox, wake});
    });
    if (settings.push_endpoint)
      s_->net->set_push_endpoint(settings.push_endpoint);
  }
  void stop_push() {
    s_->net->set_push_endpoint(std::nullopt);
    s_->kept->notifications.push_endpoint.reset();
    if (!push_thread_.joinable())
      return;
    // Left to finish on its own -- it waits a second for the bus at a time,
    // and the distributor's answer to Unregister after: not on this thread.
    push_forget_->store(true);
    push_thread_.request_stop();
    push_thread_.detach();
  }
  // What the connector said since: the endpoint given to the accounts, a
  // push a sync now, the rest said in the log.
  void take_push() {
    std::vector<mux::platform::push::event> said;
    {
      std::lock_guard held(push_box_->lock);
      said = std::exchange(push_box_->pending, {});
    }
    auto& settings = s_->kept->notifications;
    for (const auto& one : said)
      splice::visit(splice::overloaded{[&](const mux::platform::push::endpoint& given) {
                                         std::println(std::cerr, "[push] endpoint {}", given.url);
                                         if (settings.push_endpoint != given.url) {
                                           settings.push_endpoint = given.url;
                                           (void)s_->kept->write();
                                         }
                                         s_->net->set_push_endpoint(given.url);
                                       },
                                       [&](const mux::platform::push::message&) { s_->net->sync_now(); },
                                       [&](const mux::platform::push::unregistered&) {
                                         std::println(std::cerr, "[push] the distributor dropped the registration");
                                         settings.push_endpoint.reset();
                                         s_->net->set_push_endpoint(std::nullopt);
                                         (void)s_->kept->write();
                                       },
                                       [&](const mux::platform::push::registered& with) {
                                         std::println(std::cerr, "[push] registered with {}", with.distributor);
                                       },
                                       [&](const mux::platform::push::refused& why) {
                                         std::println(std::cerr, "[push] the distributor refused: {}", why.reason);
                                       },
                                       [&](const mux::platform::push::no_distributor&) {
                                         std::println(std::cerr, "[push] no UnifiedPush distributor on the session bus");
                                       },
                                       [&](const mux::platform::push::no_bus&) {
                                         std::println(std::cerr, "[push] no session bus");
                                       }},
                    one);
  }

 private:
  // Shown by the backend chosen: the system's service, asked off the UI's
  // thread, with the system's own sound -- the user's, by default (#16873);
  // or mux's own window, with the chime.
  void show(const mux::conversation_id& in, std::string title, std::string text, bool sound) {
    splice::visit(splice::overloaded{[&](mux::config::notify_backend::native) {
                                       std::thread([title, text, sound] {
                                         if (!mux::platform::notifications::notify(title, text, sound))
                                           std::println(std::cerr, "[notify] no desktop notification service; {}: {}",
                                                        title, text);
                                       }).detach();
                                     },
                                     [&](mux::config::notify_backend::built_in) {
                                       this->sound_only(sound);
                                       toasts_due_.push_back({in, in.id, std::move(title), std::move(text)});
                                     }},
                  mux::config::notify_backend_of(s_->kept->notifications.backend));
  }
  // A sound with nothing shown, or with mux's own window: the chime.
  void sound_only(bool sound) {
    if (sound)
      chime_.play();
  }
  void show_page() {
    if (auto* up = s_->root().settings_up())
      up->show_notifications(s_->kept->notifications);
  }

  // UnifiedPush's connector, on a thread of its own: what it says put in a
  // box drained by take_push() -- data the program reads, the window woken
  // for it.
  struct push_inbox {
    std::mutex lock;
    std::vector<mux::platform::push::event> pending;
  };
  struct push_sink {
    std::shared_ptr<push_inbox> inbox;
    wake_window wake;
    void operator()(mux::platform::push::event one) const {
      {
        std::lock_guard held(inbox->lock);
        inbox->pending.push_back(std::move(one));
      }
      wake();
    }
  };

  services* s_;
  bool window_focused_ = true;
  std::vector<toast_due> toasts_due_;
  mux::logic::notification_history messages_;
  std::set<mux::conversation_id> invites_told_;
  mux::platform::audio::chime chime_;
  std::shared_ptr<push_inbox> push_box_ = std::make_shared<push_inbox>();
  std::shared_ptr<std::atomic<bool>> push_forget_;
  std::jthread push_thread_;
};

}  // namespace mux::app
