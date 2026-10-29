// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui.base:pages -- What pages are built of, under the window's settings
// and dialogs: headings, a switch's row, a chat's choices and notifications,
// a profile's editor; the looks plugins' sections. Below the protocols' UI
// modules and the plugins'. (A room's settings are the room_settings
// plugin's.)
module;
#include "skiff/named.h"
export module mux.ui.base:pages;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.icon;
import skiff.nodes.image;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.button;
import skiff.widgets.model;
import skiff.widgets.sliderbar;
import skiff.widgets.textarea;
import skiff.model;
import skiff.compose;
import skiff.widgets.pill;
import mux.core;
import mux.config;
import mux.composition;
import mux.plugins.types;
import mux.protocols;
import skiff.widgets.textbox;
import :base;
import :icons;
import :avatars;
import :controls;
import :themes;
import :names;
import :forms;

export namespace mux::ui {


// An account's pages, as the program shows them: the account as saved and
// its protocol's state; the page up; what its protocol keeps while they are
// open; and its own profile and presence as the model has them -- with what
// the Profile page needs of a save: whether one is on its way, what came of
// it, and a round that moves where its drafts are to be made again (the
// profile first heard, a save gone through).
// What the model says of a saved account, in a few words, and whether that
// is a failure.
[[nodiscard]] inline std::pair<std::string, bool> connection_state_of(const config::account_t& one, const model& now) {
  const std::string& address = config::address_of(one);
  if (!config::enabled_of(one))
    return {"off", false};
  const account* found = now.accounts().find(account_id{protocol_of(address), address});
  if (found == nullptr)
    return {"offline", false};
  bool failed = false;
  std::string said = spl::visit(spl::overloaded{[](const connection::offline&) { return std::string("offline"); },
                                           [](const connection::connecting&) { return std::string("connecting…"); },
                                           [](const connection::online&) { return std::string("online"); },
                                           [&failed](const connection::failed& why) {
                                             failed = true;
                                             return "failed: " + why.error;
                                           }},
                                found->state);
  return {std::move(said), failed};
}
struct account_page_facts {
  account_id by;
  config::account_t account;
  protocol_state_t state;
  account_page_t page;
  account_part_t kept;
  std::optional<user_profile> profile;
  std::optional<mux::presence> presence;
  std::uint64_t profile_result = 0;  // the model's count of answers, as last read
  std::uint32_t profile_round = 0;
  bool profile_saving = false;
  std::optional<std::string> profile_said;
  // How it is, as the model says -- its protocol and state, failed said
  // apart; and the proxy profiles there are, for its Proxy page.
  std::string how;
  bool failed = false;
  std::vector<config::proxy_settings> proxies;
};
// The facts read again from the model: its profile and presence; an answer
// come in says what came of a save, and one that went through makes the
// drafts again, as does the profile first heard.
inline void read_account_model(account_page_facts& facts, const model& now) {
  const auto [how, failed] = connection_state_of(facts.account, now);
  facts.how = std::format("{} · {}", config::protocol_name(facts.account), how);
  facts.failed = failed;
  const account* known = now.accounts().find(facts.by);
  if (known == nullptr)
    return;
  const auto profile = known->profiles.find(facts.by.address);
  std::optional<user_profile> heard = profile == known->profiles.end() ? std::nullopt : std::optional(profile->second);
  if (heard && !facts.profile)
    ++facts.profile_round;
  if (known->profile_result != facts.profile_result) {
    facts.profile_result = known->profile_result;
    facts.profile_said = known->profile_error.value_or("Profile saved.");
    if (std::exchange(facts.profile_saving, false) && !known->profile_error)
      ++facts.profile_round;
  }
  facts.profile = std::move(heard);
  const auto presence = known->presences.find(facts.by.address);
  facts.presence = presence == known->presences.end() ? std::nullopt : std::optional(presence->second);
}
// What a protocol keeps of an account while its pages are open, fresh: its
// first part, or none.
template <class List>
struct first_account_part {
  using type = proto::no_account_part;
};
template <class First, class... Rest>
struct first_account_part<proto::account_part_list<First, Rest...>> {
  using type = First;
};
template <class Tag>
using first_account_part_t = typename first_account_part<decltype(proto::account_parts_of(state_of<Tag>{}))>::type;
// The facts of an account's page as it opens: what its protocol keeps of it
// fresh, the model's answers so far already read.
[[nodiscard]] inline account_page_facts account_page_opened(const config::account_t& one, protocol_state_t state,
                                                             account_page_t page, const model& now) {
  const std::string& address = config::address_of(one);
  account_page_facts facts{.by = account_id{protocol_of(address), address}, .account = one, .state = std::move(state), .page = std::move(page)};
  spl::visit([&](auto tag) { facts.kept = account_part_t(std::in_place_type<first_account_part_t<decltype(tag)>>); }, facts.by.speaks);
  if (const account* known = now.accounts().find(facts.by))
    facts.profile_result = known->profile_result;
  read_account_model(facts, now);
  return facts;
}
// A protocol's own account page, made over these facts: page_view(page,
// colours, facts), as its UI module overloads it.
template <class Page>
using account_page_view_t = decltype(page_view(Page{}, std::declval<const palette&>(), std::declval<const account_page_facts&>()));


// Element's names for levels: 100 Admin, 50 Moderator, the default Default.
[[nodiscard]] inline std::string level_name(std::int64_t level, std::int64_t fallback) {
  if (level >= 100)
    return "Admin";
  if (level >= 50)
    return "Moderator";
  if (level == fallback)
    return "Default";
  return std::format("Custom level ({})", level);
}

template <class Rule, class Variant>
[[nodiscard]] bool is_rule(const Variant& now) {
  return spl::visit(spl::overloaded{[](const Rule&) { return true; }, [](const auto&) { return false; }}, now);
}


// A heading over a tab, and over a part of one, as Element's.
inline nodes::Text tab_heading(const palette& colours, std::string text) {
  nodes::Text out(std::move(text), 20.0f, colours.text, true);
  out.apply({.margin = {0.0f, 0.0f, 12.0f, 0.0f}});
  return out;
}
inline nodes::Text part_heading(const palette& colours, std::string text) {
  nodes::Text out(std::move(text), 15.0f, colours.text, true);
  out.apply({.margin = {18.0f, 0.0f, 4.0f, 0.0f}});
  return out;
}
inline nodes::Text explained(const palette& colours, std::string text) {
  nodes::Text out(std::move(text), 13.0f, colours.dim);
  out.setWrapped(true);
  out.apply({.fillX = true, .margin = {2.0f, 0.0f, 6.0f, 0.0f}});
  return out;
}

// A room by its ID and name.
struct named_room {
  std::string id;
  std::string name;
};

// A chat's own choices, bound to them in the model: receipts, previews,
// where previews come from, typing sent -- Default as the level above.
inline auto chat_settings_view(const palette& colours, const conversation_id& chat) {
  using choices = config::chat_choices;
  const choice_level_t level = choice_level::chat{};
  return SKIFF_NAMED(chat_settings_view, skiff::compose::scoped<choices>(
      skiff::compose::handlers(),
      skiff::compose::column(
          skiff::compose::vbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          event_kinds_field<choices>(colours, level, proto::room_event_kinds_of(state_before(chat.account.speaks))),
          skiff::compose::bound<skiff::model::Field<&choices::receipts>>(show_hide_field<receipts_setting, std::optional<bool>, skiff::model::Field<&choices::receipts>>(colours, level)),
          skiff::compose::bound<skiff::model::Field<&choices::previews>>(
              show_hide_field<link_previews_setting, std::optional<bool>, skiff::model::Field<&choices::previews>>(colours, level)),
          skiff::compose::visible(proto::offers(state_before(chat.account.speaks), proto::feature::server_previews{}),
              skiff::compose::bound<skiff::model::Field<&choices::previews_direct>>(
                  show_hide_field<previews_direct_setting, std::optional<bool>, skiff::model::Field<&choices::previews_direct>>(colours, level))),
          skiff::compose::bound<skiff::model::Field<&choices::typing>>(show_hide_field<typing_setting, std::optional<bool>, skiff::model::Field<&choices::typing>>(colours, level)),
          skiff::compose::bound<skiff::model::Field<&choices::jump_search>>(jump_search_field<std::optional<std::int64_t>, skiff::model::Field<&choices::jump_search>>(colours, level))),
      chat));
}

// A chat's notifications on or off: off is muted -- the chat list's mute,
// the same -- and on, said apart from it; Default, neither. So it reads
// and sets the chat's choices whole.
struct chat_notify_change {
  std::optional<bool> on;
  config::chat_choices operator()(const config::chat_choices& now) const {
    auto next = now;
    next.muted = on == false;
    next.notify.on = on == true ? on : std::nullopt;
    return next;
  }
};
struct set_chat_notify {
  std::optional<bool> on;
  using Answer = skiff::bind::Own<chat_notify_change>;
  Answer operator()() const { return skiff::bind::own(chat_notify_change{on}); }
};
inline auto chat_on_field(const palette& colours) {
  namespace c = skiff::compose;
  using choices = config::chat_choices;
  const auto shown = [](const choices& now) { return now.muted ? std::optional<bool>(false) : now.notify.on; };
  return SKIFF_NAMED(chat_on_field, c::scoped<choices>(c::handlers(), c::row(setting_row(),
      setting_label(nodes::Text(std::string(notify_on_setting::label), 14.0f, colours.text)),
      setting_button<choices>(colours, "Default", 70.0f,
          [shown](const choices& now) { return !shown(now); }, set_chat_notify{std::nullopt}),
      setting_button<choices>(colours, std::string(notify_on_setting::yes), 70.0f,
          [shown](const choices& now) { return shown(now) == true; }, set_chat_notify{true}),
      setting_button<choices>(colours, std::string(notify_on_setting::no), 70.0f,
          [shown](const choices& now) { return shown(now) == false; }, set_chat_notify{false}))));
}
// A chat's notification rows, bound to its own choices in the model.
inline auto chat_notify_view(const palette& colours, const conversation_id& chat) {
  using notify = config::notify_choices;
  using skiff::compose::bound;
  const choice_level_t level = choice_level::chat{};
  return SKIFF_NAMED(chat_notify_view, skiff::compose::scoped<config::chat_choices>(
      skiff::compose::handlers(),
      skiff::compose::column(
          skiff::compose::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          bound<config::chat_choices>(chat_on_field(colours)),
          bound<skiff::model::Field<&notify::mentions>>(show_hide_field<notify_mentions_setting, std::optional<bool>, skiff::model::Field<&notify::mentions>>(colours, level)),
          bound<skiff::model::Field<&notify::sound>>(show_hide_field<notify_sound_setting, std::optional<bool>, skiff::model::Field<&notify::sound>>(colours, level))),
      chat));
}

// Stable model-bound switches: keep the widget while its saved value changes.
template <auto Member>
inline auto space_choice_row(const palette& colours, std::string label, bool allowed = true) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(space_choice_row, c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY,
                              .padding = {6.0f, 0.0f, 6.0f, 0.0f}, .disabled = !allowed}),
      c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                wrapped(nodes::Text(std::move(label), 14.0f, colours.text))),
      c::bound<skiff::model::Field<Member>>(c::styled({.alignSelf = scene::align::kMiddle},
                                                    widgets::ToggleField<bool>(colours.widgets)))));
}



// A setting's row with a switch at its end: the text taking the room; the
// switch flips what `act` answers.
template <class Act>
auto switch_row(const palette& colours, std::string text, Act act) {
  namespace c = skiff::compose;
  std::string label = text;
  return SKIFF_NAMED(switch_row, c::row(c::hbox(16.0f, {.fillX = true, .height = kRowHeight, .padding = {0.0f, 20.0f, 0.0f, 20.0f}}),
                c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(nodes::Text(std::move(text), 15.0f, colours.text))),
                c::named(std::move(label), {}, c::styled({.alignSelf = scene::align::kMiddle}, toggle_for<Act>(colours.widgets, std::move(act))))));
}


using account_page_read = std::optional<account_page_facts>;
// What a protocol's profile holds beside the name, pronouns and biography
// (profile_policy_of(state), by ADL): a banner, and the presences it has.
struct profile_policy {
  bool banners = false;
  std::vector<std::pair<std::string, availability_t>> presences;
};

// A section's title on a settings page, as Gajim sets them: small, bold, dim.
inline nodes::Text section_title(const palette& colours, std::string text) {
  return nodes::Text(std::move(text), 13.0f, colours.dim, true);
}
// A note under a section, as the settings' pages have them.
inline nodes::Text note_text(const palette& colours, std::string text) { return nodes::Text(std::move(text), 13.0f, colours.dim); }

// An account's Privacy page: whether it sends read receipts; whether it
// tells others one is typing, as every account's until chosen here, and a
// chat or space of it may choose again.
// An account's own choices in the model, bound under a scope at it: typing
// sent, on its Privacy page; receipts as faces, link previews and where
// they come from, on its Chats page.

// Reactions are local compositions: inline pictures retain their own frame
// deadlines, and a click sends the original (unmodified) reaction key.
struct reaction_pictures {
  static std::optional<scene::PillPicture> pill(std::string_view) { return std::nullopt; }
  static const skia::Sp<skia::SkImage>* picture(std::string_view target) {
    if (const auto* moving = emoji_animations().at(target, animation_clock())) return moving;
    return emoji_images().find(target);
  }
  static bool animated(std::string_view target) { return emoji_animations().has(target); }
  static double wakeAt(std::string_view target) { return emoji_animations().next_frame_at(target, animation_clock()); }
};


struct trailer_facts {
  const palette& colours;
  const looks_shown& looks;
  const message& said;
  const conversation& in;
  std::string message_id;
};
// What a message's cards are made from (contract::message_cards): the
// message, its first link, the program's model (the previews fetched), and
// whether the chat shows previews.
// What a message's footers are made from (contract::message_footers): the
// chat, and who has read up to the message where the chat shows receipts.
struct footer_facts {
  const palette& colours;
  const conversation& in;
  const std::vector<std::string>& readers;
};
struct card_facts {
  const palette& colours;
  const message& said;
  std::optional<std::string> link;
  const model* now = nullptr;
  bool previews_shown = true;
};


[[nodiscard]] inline skiff::compose::Look list_dialog(float height) {
  return skiff::compose::vbox(
      0.0f,
      {.fillX = true, .height = height, .padding = {0.0f, 0.0f, 12.0f, 0.0f}});
}
template <class Rows> [[nodiscard]] auto dialog_list(Rows rows) {
  return skiff::compose::styled(
      {.fillX = true, .grow = scene::axes::kY},
      nodes::ScrollContainer<Rows>(skiff::compose::styled(
          {.fillX = true, .autoSize = scene::axes::kY}, std::move(rows))));
}

// A chat as the chats hold it now: none, where it is gone.
[[nodiscard]] inline const conversation& chat_or_none(const model* now, const conversation_id& in) {
  static const conversation none{};
  const conversation* found = now == nullptr ? nullptr : now->find(in);
  return found == nullptr ? none : *found;
}

// A member as a list shows them: their avatar, their name over how they
// are, their role; pressed, their info.
inline auto member_row(const palette& colours, const member& one, std::string how) {
  const std::string name = one.name.empty() ? one.id : one.name;
  return skiff::compose::recorded(skiff::compose::onClick(request::open_member_info{one.id}, skiff::compose::row(
      skiff::compose::hbox(12.0f, {.fillX = true, .height = 54.0f, .padding = {0.0f, 16.0f, 0.0f, 16.0f},
          .hoverBackground = colours.chosen}),
      avatar_mark(avatar_of::person{}, one.id, name, 40.0f), two_lines(colours, name, std::move(how), 14.0f, 4.0f),
      skiff::compose::visible(one.role.has_value(), skiff::compose::styled(
          {.alignSelf = scene::align::kStart, .margin = {10.0f, 0.0f, 0.0f, 0.0f}},
          widgets::Pill(one.role.value_or(""), {.plate = skia::colorSetARGB(255, 62, 52, 96),
              .text = skia::colorSetARGB(255, 190, 170, 250), .size = 12.0f, .height = 20.0f, .padX = 8.0f})))), name));
}
using member_row_t = decltype(member_row(std::declval<const palette&>(), std::declval<const member&>(), ""));
// The plugins' sections on the pages a chat's looks are chosen on
// (contract::level_sections), for the page's level, in their order -- the
// background and bubbles pickers, for one.
template <class List>
struct level_section_list;
template <class... Features>
struct level_section_list<plugin_list<Features...>> {
  template <class Feature>
  static auto section_of(const palette& colours, choice_level_t level) -> decltype(extend(contract::level_sections{}, type_tag<Feature>{}, colours, level)) {
    static_assert(requires { extend(contract::level_sections{}, type_tag<Feature>{}, colours, level); },
                  composition::broken<contract::level_sections, Feature>());
    return extend(contract::level_sections{}, type_tag<Feature>{}, colours, level);
  }
  static auto made(const palette& colours, choice_level_t level) {
    return skiff::compose::column(skiff::compose::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}), section_of<Features>(colours, level)...);
  }
};
using level_section_plugins = level_section_list<composition::providers<contract::level_sections, enabled_plugins>>;

}  // namespace mux::ui
