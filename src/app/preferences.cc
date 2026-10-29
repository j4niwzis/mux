// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.preferences: what the user sets for an account and for a chat --
// muted, its strip and colour, its notifications, its room events, where a
// chat is placed in other accounts' lists, the spaces' bars, receipts,
// typing, link previews, an account's proxy -- kept, and shown where it is
// set. A part of the program: it reaches the settings it is given and the
// rest through the services.
export module mux.app.preferences;

import std;
import splice;
import mux.core;
import mux.config;
import mux.protocols;
import mux.logic.room_events;
import mux.ui;
import mux.ui.proto;
import mux.app.network;
import mux.app.kept;
import mux.app.services;
import mux.app.requests;

export namespace mux::app {

class preferences_part {
 public:
  using accounts = mux::ui::accounts_panel;
  preferences_part(services& shared, kept_settings& kept) : s_(&shared), k_(&kept) {}
  preferences_part(const preferences_part&) = delete;
  preferences_part& operator=(const preferences_part&) = delete;

  void apply(const request::toggle_mute&) {
    auto& screen = s_->root();
    if (!s_->chosen())
      return;
    k_->flip<&mux::app::chat_choices::muted>(*s_->chosen());
    (void)k_->write();
    s_->refresh_due = true;
  }
  void apply(const request::toggle_mute_of& one) {
    k_->flip<&mux::app::chat_choices::muted>(one.which);
    (void)k_->write();
    s_->refresh_due = true;
  }
  void apply(const request::flip_only_verified&) {
    s_->with_chosen_account([&](mux::config::account_t& account) {
      auto* kept = mux::config::only_verified_in(account);
      if (kept == nullptr)
        return;
      *kept = !kept->value_or(false);
      // Shown on its page, as the account its facts hold.
      mux::ui::change_shown<std::optional<mux::ui::account_page_facts>>(*s_->showing, [&](auto& open) {
        if (open && open->by.address == mux::config::address_of(account))
          open->account = account;
      });
      // Told to that account, running, where its client can: its sessions'
      // keys go so from now.
      s_->net->set_only_verified(kept_settings::id_of(account), **kept);
      (void)k_->write();
    });
  }
  // Its read mentions shared with its other sessions, or sealed there: the
  // one switched, kept, shown on its page, told to it running.
  void flip_mentions(mux::config::account_t& account, std::optional<bool>& kept) {
    kept = !kept.value_or(false);
    const auto now = mux::config::mentions_choice_of(account);
    if (!now)
      return;
    s_->net->set_mentions_sharing(kept_settings::id_of(account), now->shared, now->sealed);
    (void)k_->write();
  }
  void apply(const request::flip_account_mentions_shared&) {
    s_->with_chosen_account([&](mux::config::account_t& account) {
      if (std::optional<bool>* kept = mux::config::mentions_shared_in(account))
        this->flip_mentions(account, *kept);
    });
  }
  void apply(const request::flip_account_mentions_sealed&) {
    s_->with_chosen_account([&](mux::config::account_t& account) {
      if (std::optional<bool>* kept = mux::config::mentions_sealed_in(account))
        this->flip_mentions(account, *kept);
    });
  }
  void apply(const request::flip_account_receipts&) {
    s_->with_chosen_account([&](mux::config::account_t& account) {
      auto& kept = mux::config::read_receipts_in(account);
      kept = !kept.value_or(true);
      (void)k_->write();
    });
  }
  // An account's colour chosen, and its strip on its chats in other lists:
  // kept, and the lists shown again.
  void apply(const request::set_account_colour& one) {
    s_->with_chosen_account([&](mux::config::account_t& account) {
      mux::config::colour_in(account) = mux::config::said_of<mux::config::accent_said_t>(one.colour);
      (void)k_->write();
    });
    s_->refresh_due = true;
  }
  void apply(const request::flip_account_strip&) {
    s_->with_chosen_account([&](mux::config::account_t& account) {
      auto& kept = mux::config::strip_in(account);
      kept = !kept.value_or(true);
      (void)k_->write();
    });
    s_->refresh_due = true;
  }
  void apply(const request::place_chat& one) {
    if (one.to == one.chat.account)
      return;
    // Moved: out of every other list it was moved to, into this one.
    k_->change_part<std::vector<mux::config::chat_placement>>([&](auto& all) {
      if (one.moved)
        std::erase_if(all, [&](const mux::config::chat_placement& each) {
          return each.account == one.chat.account.address && each.conversation == one.chat.id && each.moved;
        });
      if (auto* kept = placement_in(all, one.chat, one.to))
        kept->moved = one.moved;
      else
        all.push_back({.account = one.chat.account.address, .conversation = one.chat.id, .listed_in = one.to.address,
                       .moved = one.moved});
    });
    (void)k_->write();
    s_->refresh_due = true;
  }
  void apply(const request::unplace_chat& one) {
    k_->change_part<std::vector<mux::config::chat_placement>>([&](auto& all) {
      std::erase_if(all, [&](const mux::config::chat_placement& each) {
        return each.account == one.chat.account.address && each.conversation == one.chat.id && each.listed_in == one.from.address;
      });
    });
    (void)k_->write();
    s_->refresh_due = true;
  }
  void apply(const request::flip_chat_strip& one) {
    const auto* own = k_->settings_of(one.chat.account.address);
    k_->change_part<std::vector<mux::config::chat_placement>>([&](auto& all) {
      if (auto* kept = placement_in(all, one.chat, one.in))
        kept->strip = !kept->strip.value_or(own == nullptr || mux::config::strip_of(*own));
    });
    s_->refresh_due = true;
  }
  void apply(const request::set_chat_strip_colour& one) {
    k_->change_part<std::vector<mux::config::chat_placement>>([&](auto& all) {
      if (auto* kept = placement_in(all, one.chat, one.in)) {
        kept->strip_colour = mux::config::said_of<mux::config::accent_said_t>(one.colour);
        kept->strip = true;
      }
    });
    s_->refresh_due = true;
  }

  // Room events, for the chosen account's chats: shown or not from now on,
  // whatever every account's is.
  void apply(const request::flip_account_room_events&) {
    s_->with_chosen_account([&](mux::config::account_t& account) {
      auto& kept = mux::config::room_events_in(account);
      kept = !kept.value_or(k_->history().show_room_events);

      (void)k_->write();
      s_->refresh_due = true;
    });
  }
  // Room events, for the chat being read, whatever its account's are.
  void apply(const request::flip_chat_room_events&) {
    const auto chosen = s_->managed();
    if (!chosen)
      return;
    const bool now = k_->room_events_shown(*chosen);
    k_->choose<&mux::app::chat_choices::room_events>(*chosen, !now);
    (void)k_->write();
    s_->notice("Room events", !now ? "Joins, renames and other room events are shown in this chat."
                                            : "Room events are hidden in this chat.");
    s_->refresh_due = true;
  }

 private:
  // Chats in other accounts' lists: placed, taken out, their strips.
  static mux::config::chat_placement* placement_in(std::vector<mux::config::chat_placement>& all, const mux::conversation_id& chat, const mux::account_id& in) {
    const auto found = std::ranges::find_if(all, [&](const mux::config::chat_placement& one) {
      return one.account == chat.account.address && one.conversation == chat.id && one.listed_in == in.address;
    });
    return found == all.end() ? nullptr : &*found;
  }

  services* s_;
  kept_settings* k_;
};

}  // namespace mux::app
