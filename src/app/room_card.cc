// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.room_card: a room not joined, from a link -- its card, filled when
// its server answers, and joined, knocked on, its invite declined, or
// closed from there. A part of the program: it owns the card's state, and
// leaves what the program does next -- the room opened, once joined -- in
// the services.
export module mux.app.room_card;

import std;
import mux.core;
import mux.ui;
import mux.logic.links;
import mux.app.network;
import mux.app.services;
import mux.app.requests;

export namespace mux::app {

class room_card_part {
 public:
  explicit room_card_part(services& shared) : s_(&shared) {}
  room_card_part(const room_card_part&) = delete;
  room_card_part& operator=(const room_card_part&) = delete;

  // A link to a room not joined: its card, at once, looking it up -- filled
  // when its server answers (previewed), and joined from there.
  void look_up(const mux::logic::link_step::join& step, const mux::logic::link_t& link) {
    looked_ = looked_up{step, link};
    s_->root().open_room_card(step.room, mux::room_preview{.note = "Looking it up\u2026"});
    s_->net->preview_room(step.by, step.room, step.via);
  }
  // An invite: its card -- who asked, Accept, Decline -- not a chat.
  void invited(const mux::conversation& chat) {
    const std::string who = chat.invite->from_name.empty() ? chat.invite->from : chat.invite->from_name;
    looked_ = looked_up{mux::logic::link_step::join{chat.id.account, chat.id.id, {}}, std::nullopt};
    s_->root().open_room_card(chat.id.id, mux::room_preview{.id = chat.id.id,
                                                            .name = mux::ui::display_name(chat),
                                                            .alias = chat.alias.value_or(""),
                                                            .topic = chat.topic.value_or(""),
                                                            .avatar = chat.avatar,
                                                            .note = std::format("Invited by {}", who),
                                                            .invite = true});
  }
  // Whether the card is up for the room asked under that address.
  [[nodiscard]] bool looking_at(std::string_view asked) const { return looked_ && looked_->step.room == asked; }
  // What the server said of the room: the card filled, while it is up for
  // that room still -- or, the room joined after all, by an address not
  // known here, opened and not offered to join.
  void previewed(const mux::change::room_previewed& shown) {
    if (!looked_ || !s_->root().room_card_up() || looked_->step.by != shown.by || looked_->step.room != shown.asked)
      return;
    for (const auto& [id, account] : s_->model->accounts())
      for (const auto& [key, chat] : account.conversations)
        if (!shown.preview.id.empty() && chat.id.id == shown.preview.id) {
          s_->root().close_room_card();
          s_->chat_due = chat.id;
          return;
        }
    if (shown.preview.avatar && !shown.preview.id.empty())
      s_->net->fetch_avatar(shown.by, *shown.preview.avatar, shown.preview.id);
    s_->root().open_room_card(shown.asked, shown.preview);
  }

  // The room of the card joined: opened when it comes, in woken().
  void apply(const request::knock_room_card&) {
    if (!looked_)
      return;
    const auto looked = *std::exchange(looked_, std::nullopt);
    s_->net->knock(looked.step.by, looked.step.room, looked.step.via, std::string());
    s_->root().close_room_card();
    s_->root().show_notice("Asked to join. You'll be let in once someone in the room accepts.");
  }
  void apply(const request::join_room_card&) {
    if (!looked_)
      return;
    const auto looked = *std::exchange(looked_, std::nullopt);
    s_->joining = looked.link;
    s_->net->join(looked.step.by, looked.step.room, looked.step.via);
    s_->root().close_room_card();
  }
  // The invite of the card declined: the room left, the card closed.
  void apply(const request::decline_room_card&) {
    if (!looked_)
      return;
    const auto looked = *std::exchange(looked_, std::nullopt);
    s_->net->leave(mux::conversation_id{looked.step.by, looked.step.room});
    s_->root().close_room_card();
  }
  void apply(const request::close_room_card&) {
    looked_.reset();
    s_->root().close_room_card();
  }

 private:
  // The card's room: what the link said, and the link.
  struct looked_up {
    mux::logic::link_step::join step;
    std::optional<mux::logic::link_t> link;
  };
  services* s_;
  std::optional<looked_up> looked_;
};

}  // namespace mux::app
