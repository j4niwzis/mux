// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.room_card.app -- The room card's part of the program: a room
// not joined, from a link -- its card, filled when its server answers, and
// joined, knocked on, or closed from there -- or an invite, chosen: its
// card, Accept and Decline, not a chat.
export module mux.plugin.room_card.app;

import std;
import mux.composition;
import mux.core;
import mux.logic.links;
import mux.ui;
import mux.app.network;
import mux.app.services;
import mux.plugin.room_card.types;
import mux.plugin.room_card.ui_top;

export namespace mux::plugins::room_card {

struct part {
  app::services* s_ = nullptr;
  // The card's room: what the link said, and the link.
  struct looked_up {
    logic::link_step::join step;
    std::optional<logic::link_t> link;
  };
  std::optional<looked_up> looked_;

  void open(looked_up looked, shown card) {
    s_->card_room = looked.step.room;
    looked_ = std::move(looked);
    ui::show(*s_->showing, std::optional(std::move(card)));
  }
  void close() {
    looked_.reset();
    s_->card_room.reset();
    ui::show<shown>(*s_->showing, std::nullopt);
  }

  // A link to a room not joined: its card, at once, looking it up.
  void apply(const ui::request::look_up_room& one) {
    this->open(looked_up{one.step, logic::on_account(one.link, one.step.by)}, shown{one.step.room, room_preview{.note = "Looking it up…"}});
    s_->net->preview_room(one.step.by, one.step.room, one.step.via);
  }
  // An invite: its card -- who asked, Accept, Decline -- not a chat.
  void apply(const ui::request::show_invite& one) {
    const conversation* chat = s_->model->find(one.chat);
    if (!chat || !chat->invite)
      return;
    const std::string who = chat->invite->from_name.empty() ? chat->invite->from : chat->invite->from_name;
    this->open(looked_up{logic::link_step::join{chat->id.account, chat->id.id, {}}, std::nullopt},
               shown{chat->id.id, room_preview{.id = chat->id.id, .name = ui::display_name(*chat), .alias = chat->alias.value_or(""),
                                               .topic = chat->topic.value_or(""), .avatar = chat->avatar,
                                               .note = std::format("Invited by {}", who), .invite = true}});
  }
  // What the server said of the room: the card filled, while it is up for
  // that room still -- or, the room joined after all, by an address not
  // known here, opened and not offered to join.
  void take(const change::room_previewed& one) {
    if (!looked_ || !s_->showing->look<std::optional<shown>>()->has_value() || looked_->step.by != one.by || looked_->step.room != one.asked)
      return;
    for (const auto& [id, owner] : s_->model->accounts())
      for (const auto& [key, chat] : owner.conversations)
        if (id == one.by && !one.preview.id.empty() && chat.id.id == one.preview.id) {
          this->close();
          s_->chat_due = chat.id;
          return;
        }
    if (one.preview.avatar && !one.preview.id.empty())
      s_->net->fetch_avatar(one.by, *one.preview.avatar, one.preview.id);
    ui::show(*s_->showing, std::optional(shown{one.asked, one.preview}));
  }
  // The room of the card joined: opened when it comes.
  void apply(const knock&) {
    if (!looked_)
      return;
    const auto looked = *looked_;
    this->close();
    s_->net->knock(looked.step.by, looked.step.room, looked.step.via, std::string());
    s_->not_implemented("Asked to join. You'll be let in once someone in the room accepts.");
  }
  void apply(const join&) {
    if (!looked_)
      return;
    const auto looked = *looked_;
    this->close();
    s_->joining = looked.link;
    s_->net->join(looked.step.by, looked.step.room, looked.step.via);
  }
  // The invite declined: the room left, the card closed.
  void apply(const decline&) {
    if (!looked_)
      return;
    const auto looked = *looked_;
    this->close();
    s_->net->leave(conversation_id{looked.step.by, looked.step.room});
  }
  void apply(const ui::request::close_shown<shown>&) { this->close(); }
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::room_card
