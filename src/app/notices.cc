// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.notices: the notification settings, kept in step with what each
// account's server keeps of them; and the Notifications page of Settings.
// What is notified, and UnifiedPush, are the notifications plugin's. A part
// of the program: it reaches the rest through the services it is given.
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
import mux.app.kept;
import mux.app.services;
import mux.app.requests;
import splice.bytes;

export namespace mux::app {

class notices_part {
 public:
  explicit notices_part(services& shared) : s_(&shared) {}
  notices_part(const notices_part&) = delete;
  notices_part& operator=(const notices_part&) = delete;

  void receive(const mux::change::notifications_changed& now) {
    namespace setting = mux::config::notify_setting;
    // What the server says is what is in effect there: kept as a choice of
    // the chat's or the account's own only where it differs from what the
    // levels above give. Kept whole, a Matrix account always had a choice
    // of its own -- the server's default rules notify on every message --
    // and every account's "Only mentions and keywords" never won: plain
    // messages notified (a tester's).
    auto p = now.choices;
    const auto own_only = [&](std::optional<bool>& said, auto which) {
      if (said && *said == s_->kept->inherited_notify(now.by, now.room, which))
        said.reset();
    };
    own_only(p.on, setting::on{});
    own_only(p.mentions, setting::mentions{});
    // An account's, as the server has it by default -- notifications on,
    // every message -- says no choice: the server's default rules are not
    // the user's. Taken for one, every account's "Only mentions and
    // keywords" lost to it at the first sync, before mux had told the
    // server anything (a tester's).
    if (!now.room) {
      if (p.on == true)
        p.on.reset();
      if (p.mentions == false)
        p.mentions.reset();
    }
    if (now.sound_shared)
      own_only(p.sound, setting::sound{});
    if (now.room) {
      const mux::conversation_id in{now.by, *now.room};
      auto choices = s_->kept->own_of<&mux::config::chat_choices::notify>(in);
      choices.on = p.on; choices.mentions = p.mentions;
      if (now.sound_shared) choices.sound = p.sound;
      s_->kept->choose<&mux::config::chat_choices::notify>(in, choices);
      s_->kept->choose<&mux::config::chat_choices::muted>(in, p.on == false);
    } else s_->kept->change_account(now.by.address, [&](auto& account) {
      account.shared.notify = p.on;
      account.shared.notify_mentions = p.mentions;
      if (now.sound_shared) account.shared.notify_sound = p.sound;
    });
    remembered_.insert_or_assign({now.by, now.room}, current(now.by, now.room));
  }
  // Compare saved preferences, rather than the selected account. Incoming
  // updates establish the baseline before this runs, so they are not echoed.
  void sync_choices() {
    for (const auto& account : s_->kept->accounts().values()) {
      const auto by = kept_settings::id_of(account);
      sync_one(by, std::nullopt);
    }
    for (const auto& account : s_->model->accounts().values())
      for (const auto& chat : account.conversations.values()) sync_one(chat.id.account, chat.id.id);
    for (const auto& in : s_->kept->state.root().chats.keys()) sync_one(in.account, in.id);
    for (auto& [key, previous] : remembered_)
      if (key.second && !s_->kept->state.root().chats.find(mux::conversation_id{key.first, *key.second}))
        sync_one(key.first, key.second);
    seeded_ = true;
  }


 private:
  struct preference_pair {
    mux::notification_choices own, effective;
    friend bool operator==(const preference_pair&, const preference_pair&) = default;
  };
  preference_pair current(const mux::account_id& by, const std::optional<std::string>& room) const {
    const auto* account = s_->kept->settings_of(by.address);
    mux::notification_choices own;
    if (account) {
      const auto& p = account->shared;
      own = {p.notify, p.notify_mentions, p.notify_sound};
    }
    const auto& all = s_->kept->notifications();
    mux::notification_choices effective{own.on.value_or(all.desktop), own.mentions.value_or(all.mentions_only.value_or(false)),
        own.sound.value_or(all.sound)};
    if (room) {
      const mux::conversation_id in{by, *room};
      const auto p = s_->kept->own_of<&mux::config::chat_choices::notify>(in);
      own = {p.on, p.mentions, p.sound};
      const auto muted = s_->kept->own_of<&mux::config::chat_choices::muted>(in);
      if (muted) own.on = false;
      effective = {s_->kept->notify_value(in, mux::config::notify_setting::on{}),
          s_->kept->notify_value(in, mux::config::notify_setting::mentions{}),
          s_->kept->notify_value(in, mux::config::notify_setting::sound{})};
    }
    return {own, effective};
  }
  void sync_one(const mux::account_id& by, const std::optional<std::string>& room) {
    const auto now = current(by, room);
    const auto [at, first] = remembered_.try_emplace(std::pair{by, room}, now);
    if ((!first && at->second != now) || (first && seeded_ && now.own != mux::notification_choices{})) {
      at->second = now;
      s_->net->set_notifications(by, room, now.own, now.effective);
    }
  }
  bool seeded_ = false;
  std::map<std::pair<mux::account_id, std::optional<std::string>>, preference_pair> remembered_;


  services* s_;
};

}  // namespace mux::app
