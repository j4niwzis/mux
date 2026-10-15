// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.chat_view: what the chat's own bands say, as the program has it --
// its head (who, how they are, who is typing), the pinned bar (the pin above
// what the list shows), the banner its protocol puts over the composer (and
// whether it was dismissed), and the connection notices over the list and
// the chat. Written to the shown model each frame where it changed; the
// bands read it and keep nothing. A part of the program: it reaches the
// rest through the services.
export module mux.app.chat_view;

import std;
import splice;
import mux.core;
import mux.config;
import mux.ui;
import mux.ui.proto;
import mux.proto;
import mux.protocols;
import mux.app.services;
import mux.app.requests;

export namespace mux::app {

class chat_view_part {
 public:
  explicit chat_view_part(services& shared) : s_(&shared) {}
  chat_view_part(const chat_view_part&) = delete;
  chat_view_part& operator=(const chat_view_part&) = delete;

  // What the bands say now: each written where it changed.
  void show() {
    const auto& shown = *s_->showing->look<mux::ui::chat_shown>();
    const auto& screen = s_->root();
    const mux::conversation* chat = shown.chosen ? s_->model->find(*shown.chosen) : nullptr;
    auto head = mux::ui::chat_head_of(s_->ui, chat, *s_->model);
    head.back = s_->single();
    head.hidden = s_->showing->look<mux::ui::search_found>()->open || s_->showing->look<mux::ui::selection_shown>()->count > 0;
    this->put(std::move(head));
    this->put(mux::ui::chats_shown{.none = s_->model->accounts().empty(), .chosen = chat != nullptr});
    this->put(mux::ui::pinned_shown{this->pin_of(chat)});
    this->put(this->banner_of(chat));
    // The account listed's, over the list; the chat's, over the chat --
    // where it is another's, or the two are not side by side.
    const auto notice = [&](const std::optional<mux::account_id>& of) {
      const mux::account* one = of ? s_->model->accounts().find(*of) : nullptr;
      return one ? mux::ui::connection_notice(one->state) : std::string();
    };
    const bool chat_band = chat && (s_->single() || shown.current != chat->id.account);
    this->put(mux::ui::connection_shown{notice(shown.current), chat_band ? notice(chat->id.account) : std::string()});
    // The info, while it is open: nothing of it worked out while it is shut.
    if (shown.info_open && chat)
      this->show_info(*chat, s_->kept->own_of<&mux::app::chat_choices::muted>(chat->id));
  }
  // The info's members near the end of those made: the next few.
  void apply(const request::info_more&) {
    if (made_ < members_total_) {
      made_ += kMembersStep;
      members_seen_.reset();
    }
  }

  // The pinned bar pressed: to the pin it shows, as any jump -- and, the
  // view gone up to it, the bar on to the one pinned before it.
  template <class Asked>
  void apply(const request::pinned_pressed&, const Asked& asked) {
    if (const auto& pin = s_->showing->look<mux::ui::pinned_shown>()->pin)
      asked(request::jump_to_message{pin->id});
  }
  // The banner's button: its protocol's; its close: let go until it says
  // something else.
  template <class Asked>
  void apply(const request::banner_button&, const Asked& asked) {
    if (const auto& chosen = s_->showing->look<mux::ui::chat_shown>()->chosen)
      asked(request::banner_pressed{*chosen});
  }
  void apply(const request::banner_close&) {
    if (const auto& chosen = s_->showing->look<mux::ui::chat_shown>()->chosen)
      dismissed_.insert_or_assign(*chosen, s_->showing->look<mux::ui::banner_shown>()->text);
  }

 private:
  template <class Part>
  void put(Part now) {
    if (*s_->showing->look<Part>() != now)
      mux::ui::show(*s_->showing, std::move(now));
  }
  // The pin the bar shows, as Telegram's: the newest pinned above the view
  // -- before the first message seen -- else the oldest; worked out again
  // as the view moves. A pin not loaded is taken as older than all that is.
  std::optional<mux::ui::pinned_view> pin_of(const mux::conversation* chat) {
    if (!chat || chat->pinned.empty())
      return std::nullopt;
    const auto place = [&](const std::string& id) -> std::ptrdiff_t {
      const auto found = std::ranges::find(chat->timeline, id, &mux::message::id);
      return found == chat->timeline.end() ? -1 : found - chat->timeline.begin();
    };
    const std::ptrdiff_t top = s_->list.top ? place(*s_->list.top) : std::numeric_limits<std::ptrdiff_t>::max();
    // Not const: a filter_view caches its begin, so begin() and end() are not.
    auto above = std::views::filter(std::views::enumerate(chat->pinned), [&](const auto& pin) { return place(std::get<1>(pin)) < top; });
    const auto best = std::ranges::max_element(above, {}, [&](const auto& pin) { return place(std::get<1>(pin)); });
    const std::size_t count = chat->pinned.size();
    const std::size_t at = best == std::ranges::end(above) ? 0 : static_cast<std::size_t>(std::get<0>(*best));
    const std::string& id = chat->pinned[at];
    mux::ui::pinned_view shown{id, count == 1 ? std::string("Pinned message") : std::format("Pinned message #{} of {}", at + 1, count),
                               "A message"};
    if (const mux::message* said = mux::ui::held_message(*chat, id)) {
      shown.line = said->body.plain.empty() && said->attachment ? std::string("Photo") : said->body.plain;
      std::ranges::replace(shown.line, '\n', ' ');
    }
    return shown;
  }
  // What its protocol says over the composer, the first's button; nothing
  // where it was dismissed and says the same.
  mux::ui::banner_shown banner_of(const mux::conversation* chat) {
    if (!chat)
      return {};
    const auto banners = mux::proto::composer_banners(mux::ui::protocol_state_of(s_->ui, chat->id.account), *chat, *s_->model);
    mux::ui::banner_shown now{
        .text = std::ranges::to<std::string>(std::views::join_with(std::views::transform(banners, &mux::proto::part::banner::text), '\n')),
        .button = banners.empty() ? std::string() : banners.front().button,
        .tone = banners.empty() ? mux::proto::part::tone_t{mux::proto::part::tone::plain{}} : banners.front().tone};
    if (now.text.empty())
      dismissed_.erase(chat->id);
    else if (const auto gone = dismissed_.find(chat->id); gone != dismissed_.end() && gone->second == now.text)
      return {};
    return now;
  }

  // The chat's info: its view, its members' head, and its members -- only
  // the first made, and more as the list nears their end, as Qt's
  // fetchMore: a big room has thousands, each with its role, badges and
  // presence worked out. Those made worked out again only where the chat,
  // its members, trust, presence or how many are made moved.
  struct members_seen {
    mux::conversation_id of;
    std::uint64_t members = 0, trust = 0, presence = 0;
    std::size_t made = 0;
    friend bool operator==(const members_seen&, const members_seen&) = default;
  };
  static constexpr std::size_t kMembersStep = 60;
  void show_info(const mux::conversation& chat, bool muted) {
    const mux::model& now = *s_->model;
    const auto& state = mux::ui::protocol_state_of(s_->ui, chat.id.account);
    const auto count = static_cast<std::size_t>(std::max<std::int64_t>(static_cast<std::int64_t>(chat.members.size()), chat.member_count));
    const bool group = mux::ui::is_group(chat);
    mux::ui::info_view head{.key = chat.id.id,
                            .name = mux::ui::display_name(chat),
                            .status = group ? std::format("{} member{}", count, count == 1 ? "" : "s")
                                            : mux::ui::presence_of(s_->ui, now, chat.id.account, mux::ui::contact_of(s_->ui, chat)),
                            .group = group,
                            .muted = muted,
                            .copied = mux::proto::room_link(state, chat).value_or(""),
                            .topic = chat.topic.value_or(""),
                            .addresses = std::vector<std::string>(chat.alias ? 1 : 0, chat.alias.value_or("")),
                            .leavable = mux::proto::can_leave(state, chat)};
    head.addresses.append_range(chat.other_aliases);
    if (!info_of_ || *info_of_ != chat.id) {
      info_of_ = chat.id;
      made_ = kMembersStep;
    }
    const mux::account* owner = now.accounts().find(chat.id.account);
    const members_seen seen{chat.id, chat.members_revision, now.trust_revision(), owner ? owner->presence_revision : 0,
                            std::min(made_, chat.members.size())};
    const bool fresh = members_seen_ != seen;
    if (fresh) {
      members_seen_ = seen;
      members_ = std::ranges::to<std::vector>(std::views::transform(std::views::take(chat.members, seen.made), [&](const mux::member& each) { return this->member_of(chat, each); }));
    }
    members_total_ = chat.members.size();
    // The rest is small: looked at each frame, the members put with it
    // only where something moved -- not copied and compared at every frame.
    info_top top{std::move(head), {count, chat.id, !chat.invite && mux::proto::chat_rights(state, chat).invite}, seen.made < members_total_};
    if (!fresh && top_ == top)
      return;
    top_ = top;
    this->put(mux::ui::info_shown{std::move(top.head), std::move(top.heading), members_, top.more});
  }
  struct info_top {
    mux::ui::info_view head;
    mux::ui::info_members_heading heading;
    bool more = false;
    friend bool operator==(const info_top&, const info_top&) = default;
  };
  // A member as the list shows them: their role, as the chat's protocol
  // says it (Matrix: its power levels, as Element marks its admins and
  // moderators); and what their protocol says of them beside how they are
  // (Matrix: in an encrypted room, their identity -- Element's shield).
  [[nodiscard]] mux::ui::info_member member_of(const mux::conversation& chat, const mux::member& each) const {
    const auto& state = mux::ui::protocol_state_of(s_->ui, chat.id.account);
    mux::member who = each;
    if (!who.role)
      if (std::string role = mux::proto::sender_role(state, chat, each.id); !role.empty())
        who.role = std::move(role);
    std::string how = std::ranges::fold_left(
        mux::proto::person_badges(state, &chat, *s_->model, chat.id.account, each.id),
        mux::ui::presence_of(s_->ui, *s_->model, chat.id.account, each.id), [](std::string so_far, const mux::proto::part::badge& badge) {
          return so_far.empty() ? badge.text : std::format("{} \u00b7 {}", so_far, badge.text);
        });
    return {std::move(who), std::move(how)};
  }

  services* s_;
  std::map<mux::conversation_id, std::string> dismissed_;
  std::optional<mux::conversation_id> info_of_;
  std::size_t made_ = kMembersStep;
  std::size_t members_total_ = 0;
  std::optional<members_seen> members_seen_;
  std::vector<mux::ui::info_member> members_;
  std::optional<info_top> top_;
};

}  // namespace mux::app
