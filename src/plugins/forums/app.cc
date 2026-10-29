// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.forums.app -- The forums' part of the program, and what they
// say to the chat list (contract::chat_scopes): a space the user shows as a
// forum -- not one that holds spaces -- or a forum of its protocol's own
// (proto::native_forum) opens as a list of chats, its rooms as topics; its
// row, as tdesktop's, the newest of its topics, their unread summed.
export module mux.plugin.forums.app;

import std;
import splice;
import mux.composition;
import mux.core;
import mux.protocols;
import mux.ui;
import mux.app.kept;
import mux.app.services;
import mux.plugin.forums.types;

export namespace mux::plugins::forums {

[[nodiscard]] inline bool chosen_as_forum(const app::services& shared, const conversation& one) {
  const auto& chosen = shared.kept->plugin_settings<kept>().chats;
  return std::ranges::contains(chosen, forum_chat{one.id.account.address, one.id.id});
}
inline bool opens_as_scope(contract::chat_scopes, type_tag<feature>, const app::services& shared, const conversation& one) {
  return one.space && (chosen_as_forum(shared, one) || proto::native_forum(ui::protocol_state_of(shared.ui, one.id.account), one));
}
template <class Events>
std::optional<conversation> scope_row(contract::chat_scopes, type_tag<feature>, const app::services& shared, const conversation& one,
                                      const account& in, const Events& events_of) {
  if (!opens_as_scope(contract::chat_scopes{}, type_tag<feature>{}, shared, one))
    return std::nullopt;
  mux::conversation made = one;
  made.timeline.clear();
  made.read_up_to.reset();
  made.detached = false;
  made.unread = 0;
  made.highlights = 0;
  auto topics = one.children | std::views::transform([&](const std::string& child) { return in.conversations.find(child); }) | std::views::filter([](const mux::conversation* topic) { return topic != nullptr && !topic->space; });
  made.unread = std::ranges::fold_left(topics | std::views::transform([&](const mux::conversation* topic) { return topic->unread_here(events_of(*topic)); }),
                                       std::int64_t{0}, std::plus{});
  made.highlights = std::ranges::fold_left(topics | std::views::transform(&mux::conversation::highlights), decltype(made.highlights){0}, std::plus{});
  const auto newest_in = [&](const mux::conversation* topic) { return mux::newest(*topic, events_of(*topic)); };
  auto with_one = topics | std::views::filter([&](const mux::conversation* topic) { return newest_in(topic) != nullptr; });
  if (const auto best = std::ranges::max_element(with_one, {}, [&](const mux::conversation* topic) { return newest_in(topic)->at; });
      best != std::ranges::end(with_one)) {
    made.timeline.push_back(*newest_in(*best));
    made.row_topic = ui::display_name(**best);
    made.members = (*best)->members;
  }
  return made;
}

struct part {
  app::services* s_ = nullptr;

  // A space shown as a forum, or as a space again -- not one that holds
  // spaces: it is a space.
  void apply(const flip_forum& one) {
    const conversation* space = s_->model->find(one.chat);
    if (!space || !space->space)
      return;
    const bool holds_spaces = std::ranges::any_of(space->children, [&](const std::string& child) {
      const conversation* in = s_->model->find(conversation_id{one.chat.account, child});
      return in && in->space;
    });
    auto now = s_->kept->plugin_settings<kept>();
    const forum_chat chat{one.chat.account.address, one.chat.id};
    if (const auto at = std::ranges::find(now.chats, chat); at != now.chats.end())
      now.chats.erase(at);
    else if (!holds_spaces)
      now.chats.push_back(chat);
    s_->kept->set_plugin_settings(std::move(now));
    (void)s_->kept->write();
    s_->refresh_due = true;
  }
  // Each frame: what the window shows of them, as they are kept.
  void tick() {
    shown now{s_->kept->plugin_settings<kept>().chats | std::views::transform([](const forum_chat& one) {
                return conversation_id{account_id{protocol_of(one.account), one.account}, one.room};
              }) | std::ranges::to<std::set>()};
    if (*s_->showing->look<shown>() != now)
      ui::show(*s_->showing, std::move(now));
  }
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::forums
