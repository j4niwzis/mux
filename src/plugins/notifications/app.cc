// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.notifications.app -- The notifications' part of the program:
// what is told of outside the chat in view -- a message come, an invite --
// as the settings say: by the desktop's notifications or mux's own, with
// the chime; a notification's actions, its chat opened, read or answered;
// and UnifiedPush, which wakes the program for them. Run as changes arrive
// and as the window is woken, not with the frames: off screen there are
// none, and notifications are given all the same.
export module mux.plugin.notifications.app;

import std;
import splice;
import splice.bytes;
import mux.composition;
import mux.core;
import mux.config;
import mux.logic.notifications;
import mux.ui;
import mux.platform.audio;
import mux.platform.notifications;
import mux.platform.push;
import mux.app.network;
import mux.app.kept;
import mux.app.services;
import mux.plugin.notifications.types;

export namespace mux::plugins::notifications {

struct part {
  app::services* s_ = nullptr;

  // What arrived, as a batch: each live message notified as the settings
  // say -- a mention known by the marks the same batch made -- and each
  // invite.
  void took(const std::vector<change_t>& changes) {
    if (s_->demo())
      return;
    std::set<std::string> mentioning;
    for (const change_t& one : changes)
      spl::visit(spl::overloaded{[&](const change::mentioned& m) { mentioning.insert(m.event); }, [](const auto&) {}}, one);
    for (const change_t& one : changes)
      spl::visit(spl::overloaded{[&](const change::message_added& added) {
                                   const bool live = spl::visit(spl::overloaded{[](placement::at_end) { return true; }, [](const auto&) { return false; }},
                                                                added.where);
                                   if (live)
                                     this->message_came(added.message, mentioning.contains(added.message.id),
                                                        s_->on_screen && s_->chosen() == added.message.in);
                                 },
                                 [&](const change::conversation_updated& updated) {
                                   if (updated.invite)
                                     this->invite_came(updated.id, *updated.invite, updated.name);
                                 },
                                 [](const auto&) {}},
                 one);
  }
  // The window woken: UnifiedPush started or stopped as the settings say,
  // what its connector said taken -- a push a sync now -- and what a
  // notification asked: pressed, its chat opened; its actions, the chat
  // read, or answered and read, and taken away.
  template <class Asked>
  void woken(const Asked& asked) {
    this->follow_push();
    this->take_push();
    while (const auto said = this->take_asked())
      spl::visit(spl::overloaded{[&](const opened_chat& one) { asked(ui::request::choose{one.chat}); },
                                 [&](const read_chat& one) {
                                   asked(ui::request::mark_chat_read{one.chat});
                                   dismiss(one.chat);
                                 },
                                 [&](const replied& one) {
                                   if (!one.text.empty())
                                     s_->net->send(one.chat, one.text);
                                   asked(ui::request::mark_chat_read{one.chat});
                                   dismiss(one.chat);
                                 }},
                 *said);
  }
  // Between frames too: UnifiedPush turned on or off in Settings.
  // Its page of Settings, from Settings' home.
  void apply(const open_settings&) {
    if (s_->settings_open())
      s_->settings_page(settings_page{s_->kept->notifications()});
  }
  void tick() { this->follow_push(); }
  void follow_push() {
    const bool wanted = this->own().unified_push.value_or(false) && !s_->demo();
    if (wanted == pushing_)
      return;
    pushing_ = wanted;
    if (wanted)
      this->start_push();
    else
      this->stop_push();
  }
  // The notifications mux shows itself, for the host to put up.
  [[nodiscard]] std::vector<app::toast_due> take_toasts() { return std::exchange(toasts_due_, {}); }


  // A message come as it happened, notified as the settings say -- where it
  // was said since mux started (a minute's grace for clocks): not a chat's
  // last messages, which the first sync puts at its end too. An account
  // recreated after a proxy change also replays its saved timeline: each
  // message is handled only once per run. `in_view`: its chat is on screen.
  void message_came(const mux::message& said, bool mentions_me, bool in_view) {
    if (!messages_.first(said))
      return;
    if (in_view && s_->window_focused)
      return;
    const auto decision = s_->kept->notify_for(said.in, mentions_me);
    if (!decision.popup) {
      this->sound_only(decision.sound);
      return;
    }
    const mux::conversation* chat = s_->model->find(said.in);
    std::string title = "mux";
    if (chat) {
      const std::string who = mux::ui::sender_name(*chat, said.sender);
      title = mux::ui::is_group(*chat) ? std::format("{} ({})", who, mux::ui::display_name(*chat)) : who;
    }
    std::string text = "New message";
    {
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

  // Its own settings, as kept; and one of them changed.
  [[nodiscard]] const kept& own() const { return s_->kept->plugin_settings<kept>(); }
  template <class Change>
  void keep(Change change) {
    auto now = this->own();
    change(now);
    s_->kept->set_plugin_settings(std::move(now));
  }
  void start_push() {
    const kept settings = this->own();
    if (push_thread_.joinable() || s_->kept->keeps_nothing)
      return;
    if (!settings.push_token) {
      this->keep([](kept& now) { now.push_token = mux::platform::push::new_token(); });
      (void)s_->kept->write();
    }
    const auto token = this->own().push_token.value_or("");
    push_forget_ = std::make_shared<std::atomic<bool>>(false);
    push_thread_ = std::jthread([inbox = push_box_, wake = *s_->wake, forget = push_forget_,
                                 token](std::stop_token stop) {
      mux::platform::push::run(stop, std::string(mux::platform::push::kAppId), token, "Messages from your accounts",
                               forget, push_sink{inbox, wake});
    });
    if (settings.push_endpoint)
      s_->net->set_push_endpoint(settings.push_endpoint);
  }
  void stop_push() {
    s_->net->set_push_endpoint(std::nullopt);
    this->keep([](kept& now) { now.push_endpoint.reset(); });
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
  // What a notification asked of mux -- pressed, its chat opened; its
  // actions, the chat read or answered -- read once into what it is: its
  // word, the chat's account's address and its id, and a reply's text, a
  // line each.
  struct opened_chat {
    mux::conversation_id chat;
  };
  struct read_chat {
    mux::conversation_id chat;
  };
  struct replied {
    mux::conversation_id chat;
    std::string text;
  };
  using asked_t = spl::variant<opened_chat, read_chat, replied>;
  [[nodiscard]] std::optional<asked_t> take_asked() {
    const auto said = mux::platform::notifications::take_opened();
    if (!said)
      return std::nullopt;
    const auto word_end = said->find('\n');
    const auto address_end = word_end == std::string::npos ? std::string::npos : said->find('\n', word_end + 1);
    if (address_end == std::string::npos)
      return std::nullopt;
    const std::string word = said->substr(0, word_end);
    const std::string address = said->substr(word_end + 1, address_end - word_end - 1);
    const auto room_end = said->find('\n', address_end + 1);
    const std::string room = said->substr(address_end + 1, room_end == std::string::npos ? std::string::npos : room_end - address_end - 1);
    const mux::conversation_id chat{{mux::ui::protocol_of(address), address}, room};
    // The words a request is said by, each what it asks.
    static constexpr std::array kWords{std::string_view("open"), std::string_view("read"), std::string_view("reply")};
    const auto which = std::ranges::find(kWords, word);
    switch (which - kWords.begin()) {
      case 0: return asked_t{opened_chat{chat}};
      case 1: return asked_t{read_chat{chat}};
      case 2:
        if (room_end == std::string::npos)
          return std::nullopt;
        return asked_t{replied{chat, said->substr(room_end + 1)}};
      default: return std::nullopt;
    }
  }
  // A chat's notification taken away: read, or answered, from it.
  static void dismiss(const mux::conversation_id& chat) {
    mux::platform::notifications::dismiss_chat(std::format("{}\n{}", chat.account.address, chat.id));
  }
  void take_push() {
    std::vector<mux::platform::push::event> said;
    {
      std::lock_guard held(push_box_->lock);
      said = std::exchange(push_box_->pending, {});
    }
    const kept settings = this->own();
    for (const auto& one : said)
      spl::visit(spl::overloaded{[&](const mux::platform::push::endpoint& given) {
                                         std::println(std::cerr, "[push] endpoint {}", given.url);
                                         if (settings.push_endpoint != given.url) {
                                           this->keep([&](kept& now) { now.push_endpoint = given.url; });
                                           (void)s_->kept->write();
                                         }
                                         s_->net->set_push_endpoint(given.url);
                                       },
                                       [&](const mux::platform::push::message&) { s_->net->sync_now(); },
                                       [&](const mux::platform::push::unregistered&) {
                                         std::println(std::cerr, "[push] the distributor dropped the registration");
                                         this->keep([](kept& now) { now.push_endpoint.reset(); });
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

  // Shown by the backend chosen: the system's service, asked off the UI's
  // thread, with the system's own sound -- the user's, by default (#16873);
  // or mux's own window, with the chime.
  void show(const mux::conversation_id& in, std::string title, std::string text, bool sound) {
    const auto& chosen = this->own().sound_file;
    const bool custom = sound && chosen && !chosen->empty();
    if (custom) { this->sound_only(true); sound = false; }
    spl::visit(spl::overloaded{[&](mux::config::notify_backend::native) {
                                       // Pressed, its chat opened (take_opened).
                                       std::thread([title, text, sound, chat = std::format("{}\n{}", in.account.address, in.id)] {
                                         if (!mux::platform::notifications::notify_chat(title, text, sound, chat))
                                           std::println(std::cerr, "[notify] no desktop notification service; {}: {}",
                                                        title, text);
                                       }).detach();
                                     },
                                     [&](mux::config::notify_backend::built_in) {
                                       this->sound_only(sound);
                                       toasts_due_.push_back(app::toast_due{in, in.id, std::move(title), std::move(text)});
                                     }},
                  s_->kept->notifications().backend);
  }
  // A sound with nothing shown, or with mux's own window: the chime.
  void sound_only(bool sound) {
    if (!sound) return;
    const auto& chosen = this->own().sound_file;
    if (chosen && !chosen->empty()) {
      if (sound_path_ != *chosen) {
        sound_path_ = *chosen;
        const auto bytes = spl::bytes::file_text(sound_path_);
        custom_sound_ = bytes ? mux::platform::audio::decode(*bytes) : std::nullopt;
        if (!custom_sound_) s_->notice("Notification sound", "The selected Ogg sound could not be loaded. Using the default chime.");
      }
      if (custom_sound_) { sound_player_->play("notification", *custom_sound_); return; }
    }
    chime_->play();
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
    app::wake_window wake;
    void operator()(mux::platform::push::event one) const {
      {
        std::lock_guard held(inbox->lock);
        inbox->pending.push_back(std::move(one));
      }
      wake();
    }
  };

  bool pushing_ = false;
  std::vector<app::toast_due> toasts_due_;
  logic::notification_history messages_;
  std::set<conversation_id> invites_told_;
  // Held apart: neither moves, and the part is moved into the program's.
  std::unique_ptr<platform::audio::chime> chime_ = std::make_unique<platform::audio::chime>();
  std::unique_ptr<platform::audio::speaker> sound_player_ = std::make_unique<platform::audio::speaker>();
  std::string sound_path_;
  std::optional<platform::audio::pcm> custom_sound_;
  std::shared_ptr<push_inbox> push_box_ = std::make_shared<push_inbox>();
  std::shared_ptr<std::atomic<bool>> push_forget_;
  std::jthread push_thread_;
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::notifications
